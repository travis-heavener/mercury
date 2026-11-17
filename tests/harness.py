"""Shared test infrastructure: server lifecycle, private-instance test base, suite reporting."""

from concurrent.futures import as_completed, ThreadPoolExecutor
from contextlib import contextmanager
import itertools
from pathlib import Path
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import unittest

HOST = "127.0.0.1"
HOST_V6 = "::1"
PORT = 8080
TLS_PORT = 8081
IS_WINDOWS = sys.platform in ("win32", "cygwin")
ROOT_DIR = Path(__file__).resolve().parent.parent

_ports = itertools.count(8100) # Loopback ports handed to private test instances (atomic under the GIL)


# Returns path to compiled Mercury server binary
def mercury_bin() -> str:
    return str(ROOT_DIR / "bin" / ("mercury.exe" if IS_WINDOWS else "mercury"))


# Returns client TLS context w/ verification disabled (test certs are self-signed)
def tls_context() -> ssl.SSLContext:
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    return context


# Returns fresh loopback port, dodging kernel TIME_WAIT collisions on rapid rebinds
def claim_port() -> int:
    return next(_ports)


# Returns True if socket connection to target address succeeds
def _ping_sock(addr: str, port: int, timeout: float = 1) -> bool:
    family = socket.AF_INET6 if ":" in addr else socket.AF_INET
    try:
        with socket.socket(family, socket.SOCK_STREAM) as sock:
            sock.settimeout(timeout)
            sock.connect((addr, port))
        return True
    except OSError:
        return False


# Returns True when all listening endpoints accept socket connections
def wait_until_live() -> bool:
    max_timeout = 15 # Max timeout in seconds, per endpoint

    for addr, port in ((HOST, PORT), (HOST_V6, PORT), (HOST, TLS_PORT), (HOST_V6, TLS_PORT)):
        deadline = time.time() + max_timeout
        while not _ping_sock(addr, port):
            if time.time() > deadline:
                return False
            time.sleep(0.25)

    return True


# Used to manage Mercury process lifecycle for test run
@contextmanager
def mercury_process(config_path):
    # Capture stderr diagnostics in temporary buffer while discarding stdout
    with tempfile.TemporaryFile() as diagnostics:
        proc = subprocess.Popen(
            [mercury_bin(), "-c", config_path],
            stdout=subprocess.DEVNULL,
            stderr=diagnostics,
            stdin=subprocess.DEVNULL,
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if IS_WINDOWS else 0
        )
        failed = False

        try:
            # Verify server endpoints are responding before running tests
            if not wait_until_live() or proc.poll() is not None:
                diagnostics.seek(0)
                raise RuntimeError("Mercury failed to start:\n" + diagnostics.read().decode(errors="replace"))

            # Wait for HTTP workers after backend startup completes
            with socket.create_connection((HOST, PORT), timeout=10) as probe:
                probe.sendall(b"GET /small.txt HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                if not probe.recv(1024):
                    raise RuntimeError("Mercury closed startup probe")

            # Yield running server process to test runner
            yield proc

            # Verify process did not crash during execution
            if proc.poll() is not None:
                raise RuntimeError("Mercury exited unexpectedly during test run")
        except BaseException:
            failed = True
            raise
        finally:
            # Capture unexpected exit before sending normal shutdown signal
            unexpected_exit = proc.poll()
            shutdown_error = None

            # Terminate server gracefully w/ interrupt signal
            if proc.poll() is None:
                proc.send_signal(signal.CTRL_BREAK_EVENT if IS_WINDOWS else signal.SIGINT)
                try:
                    proc.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    shutdown_error = RuntimeError("Mercury did not shut down gracefully")
                    try:
                        proc.kill()
                        proc.wait(timeout=5)
                    except (OSError, subprocess.TimeoutExpired) as error:
                        print(f"[Diagnostics] Forced shutdown failed: {error}", flush=True)

            # Preserve runtime crash diagnostics normally hidden by redirected stderr
            if failed or unexpected_exit is not None or shutdown_error is not None:
                code = proc.returncode
                formatted_code = f"{code} (0x{code & 0xFFFFFFFF:08X})" if IS_WINDOWS and code is not None else str(code)
                state = "unexpected exit" if unexpected_exit is not None else "after test failure and shutdown"
                if shutdown_error is not None:
                    state = "forced shutdown after timeout"

                print(f"[Diagnostics] Mercury PID {proc.pid}: {state}; exit code {formatted_code}", flush=True)
                diagnostics.seek(0)
                captured = diagnostics.read().decode(errors="replace")
                print("[Diagnostics] Mercury stderr:\n" + (captured or "(empty)"), flush=True)

            # Report cleanup failure after diagnostics without replacing original exception
            if shutdown_error is not None:
                if failed:
                    print(f"[Diagnostics] {shutdown_error}", flush=True)
                else:
                    raise shutdown_error


# Returns exact request bytes, Connection: close by default; None header values drop a default
def build_request(path="/index.html", method="GET", version="HTTP/1.1", headers=None, body=b"") -> bytes:
    merged = {"Host": "localhost", "Connection": "close", **(headers or {})}
    if body:
        merged["Content-Length"] = len(body)
    lines = [f"{method} {path} {version}"] + [f"{k}: {v}" for k, v in merged.items() if v is not None]
    return ("\r\n".join(lines) + "\r\n\r\n").encode("latin1") + body


# Returns body w/ chunked transfer framing removed
def _dechunk(raw: bytes) -> bytes:
    body = b""
    while raw:
        size_line, _, raw = raw.partition(b"\r\n")
        size = int(size_line.split(b";")[0], 16)
        if size == 0:
            break
        body += raw[:size]
        raw = raw[size + 2:]
    return body


class ServerTestCase(unittest.TestCase):
    """Base for tests that spawn private Mercury instances on fresh loopback ports"""

    # Used to allocate ports and scratch space for configs and logs
    def setUp(self):
        self.port, self.tls_port = claim_port(), claim_port()
        self.tmp = Path(tempfile.mkdtemp(prefix="mercury-test-"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.access_log = self.tmp / "access.log"
        self.error_log = self.tmp / "error.log"

    # Returns path to minimal config w/ extra XML merged ahead of the catch-all access rule
    def write_conf(self, extra="", tls=False, sec_mode="Minimal", access="allow all",
                   access_format=None, error_format=None) -> str:
        access_attrs = f' format="{access_format}"' if access_format else ""
        error_attrs = f' format="{error_format}"' if error_format else ""
        lines = [
            "<Mercury>",
            "    <DocumentRoot> ./tests/root/ </DocumentRoot>",
            f"    <BindAddressIPv4> {HOST} </BindAddressIPv4>",
            f"    <Port> {self.port} </Port>",
            f"    <TLSPort> {self.tls_port if tls else 'off'} </TLSPort>",
            "    <TLSCertFile> ./conf/ssl/cert.pem </TLSCertFile>",
            "    <TLSKeyFile> ./conf/ssl/key.pem </TLSKeyFile>",
            "    <EnableLegacyHTTPVersions> on </EnableLegacyHTTPVersions>",
            "    <StartupCheckLatestRelease> false </StartupCheckLatestRelease>",
            f"    <ClientSecurityMode> {sec_mode} </ClientSecurityMode>",
            "    <ClientSecurityIPSalt> fAhGD0OnCe4YhSyx </ClientSecurityIPSalt>",
            f"    <AccessLogFile{access_attrs}> {self.access_log} </AccessLogFile>",
            f"    <ErrorLogFile{error_attrs}> {self.error_log} </ErrorLogFile>",
            f"    {extra}",
            '    <Match pattern=".*">',
            f'        <Access mode="{access}" />',
            "    </Match>",
            "</Mercury>",
        ]
        path = self.tmp / f"mercury-{len(list(self.tmp.glob('*.conf')))}.conf"
        path.write_text("\n".join(lines))
        return str(path)

    # Returns running instance once its listeners accept connections, stopped on test cleanup
    def serve(self, extra="", **conf_opts) -> subprocess.Popen:
        stderr = tempfile.TemporaryFile()
        self.addCleanup(stderr.close)
        proc = subprocess.Popen([mercury_bin(), "-c", self.write_conf(extra, **conf_opts)],
                                stdout=subprocess.DEVNULL, stderr=stderr, stdin=subprocess.DEVNULL,
                                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if IS_WINDOWS else 0)
        self.addCleanup(self.stop_server, proc)

        # Wait for listener readiness, bailing out early if the process died
        ports = (self.port, self.tls_port) if conf_opts.get("tls") else (self.port,)
        deadline = time.monotonic() + 5
        while proc.poll() is None and time.monotonic() < deadline:
            if all(_ping_sock(HOST, port, timeout=0.2) for port in ports):
                return proc
            time.sleep(0.05)

        stderr.seek(0)
        self.fail(f"Mercury failed to start (exit {proc.poll()}):\n{stderr.read().decode(errors='replace')}")

    # Used to terminate instance gracefully, escalating to kill (safe to call repeatedly)
    def stop_server(self, proc):
        if proc.poll() is not None:
            return
        try:
            proc.send_signal(signal.CTRL_BREAK_EVENT if IS_WINDOWS else signal.SIGTERM)
        except OSError:
            pass
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

    # Returns completed process for a launch expected to exit during config validation
    def run_config(self, **conf_opts) -> subprocess.CompletedProcess:
        return subprocess.run([mercury_bin(), "-c", self.write_conf(**conf_opts)],
                              capture_output=True, stdin=subprocess.DEVNULL, timeout=5)

    # Returns open socket to private instance, closed on test cleanup
    def connect(self, port=None, timeout=5.0) -> socket.socket:
        sock = socket.create_connection((HOST, port or self.port), timeout=timeout)
        self.addCleanup(sock.close)
        return sock

    # Returns status, lowercase header dict, and body text for one Connection: close request
    def fetch(self, path="/index.html", port=None, **request_opts) -> tuple[int, dict, str]:
        data = b""
        with socket.create_connection((HOST, port or self.port), timeout=3) as sock:
            sock.sendall(build_request(path, **request_opts))
            try:
                while chunk := sock.recv(4096):
                    data += chunk
            except (socket.timeout, ConnectionError):
                pass

        head, _, body = data.partition(b"\r\n\r\n")
        lines = head.decode("latin1").split("\r\n")
        if len(lines[0].split()) < 2:
            return 0, {}, ""

        headers = {k.strip().lower(): v.strip() for k, _, v in (line.partition(":") for line in lines[1:])}
        if headers.get("transfer-encoding") == "chunked":
            body = _dechunk(body)
        return int(lines[0].split()[1]), headers, body.decode("utf-8", errors="replace")


SUITE_WORKERS = 6 # Tests mostly sleep on timers, so this stays stable even on a single core


# Returns flat list of individual tests in suite
def _flatten(suite) -> list:
    return [leaf for item in suite for leaf in (_flatten(item) if isinstance(item, unittest.TestSuite) else [item])]


# Used to show a live counter on interactive terminals
def _progress(done: int, total: int):
    if sys.stdout.isatty():
        sys.stdout.write(f"\r  Running tests... [{done}/{total}]")
        sys.stdout.flush()


# Returns True if all tests passed, printing tracebacks for failures only
def _report(desc: str, total: int, result: unittest.TestResult) -> bool:
    print("=" * 80)
    print(f"Starting run: {desc} ({total} tests)", flush=True)

    skipped = f", {len(result.skipped)} skipped" if result.skipped else ""
    if result.wasSuccessful():
        print(f"✅ Passing ({result.testsRun - len(result.skipped)} passed{skipped})", flush=True)
    else:
        for label, entries in (("failure", result.failures), ("error", result.errors)):
            for test, details in entries:
                print(f"[{desc} {label}] {test}\n{details}", end="")
        print(f"❌ Failing ({len(result.failures)} failures, {len(result.errors)} errors{skipped})", flush=True)
    return result.wasSuccessful()


# Used to run test modules serially w/ compact progress output, returns True if all tests pass
def run_suite(desc: str, *modules) -> bool:
    suite = unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromModule(m) for m in modules)
    total = suite.countTestCases()

    class ProgressResult(unittest.TestResult):
        def stopTest(self, test):
            super().stopTest(test)
            _progress(self.testsRun, total)

    result = ProgressResult()
    suite.run(result)
    if sys.stdout.isatty():
        sys.stdout.write("\r\033[K")
    return _report(desc, total, result)


# Used to run (desc, module) suites through one shared pool of workers, returns True if all pass
# Tests must be independent: class-level fixtures are not run, so each test builds its own state
def run_suites(*specs, workers: int = SUITE_WORKERS) -> bool:
    groups = [_flatten(unittest.defaultTestLoader.loadTestsFromModule(module)) for _, module in specs]
    total = sum(len(group) for group in groups)
    results = [unittest.TestResult() for _ in specs]

    def run_one(index, test):
        result = unittest.TestResult()
        test.run(result)
        return index, result

    done = 0
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [pool.submit(run_one, i, test) for i, group in enumerate(groups) for test in group]
        for future in as_completed(futures):
            index, partial = future.result()
            merged = results[index]
            merged.testsRun += partial.testsRun
            for attr in ("failures", "errors", "skipped"):
                getattr(merged, attr).extend(getattr(partial, attr))
            done += 1
            _progress(done, total)

    if sys.stdout.isatty():
        sys.stdout.write("\r\033[K")

    # Report in declared order, after everything finishes, so output never interleaves
    outcomes = [_report(desc, len(group), result) for (desc, _), group, result in zip(specs, groups, results)]
    return all(outcomes)
