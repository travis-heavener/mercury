"""Exercise CGI/FastCGI worker lifecycle and ownership through real HTTP requests, plus runner diagnostics."""

from concurrent.futures import ThreadPoolExecutor
from contextlib import redirect_stdout
import hashlib
import io
import os
from pathlib import Path
import psutil
import re
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import MagicMock, patch

from fastcgi_backend import FastCGIBackend
from harness import mercury_process, ROOT_DIR
from regressions import Connection, request

IS_ROOT = sys.platform == "linux" and os.geteuid() == 0


# Returns True if fixture process is running in current namespace
def is_process_running(pid):
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


# Keep fixture behavior independent of installed PHP version and INI extensions
WORKER_SOURCE = r'''
import hashlib
import os
from pathlib import Path
import signal
import sys
import time

mode = os.environ.get("QUERY_STRING", "")
output = sys.stdout.buffer

# Emit diagnostics before valid output, including more than one pipe buffer
if mode == "warning":
    sys.stderr.write("worker warning\nX-Injected: rejected\n")
    sys.stderr.flush()
elif mode == "stderr_flood":
    sys.stderr.buffer.write(b"E" * 262144)
    sys.stderr.buffer.flush()

# Keep worker alive until Mercury cancels or times out its request
if mode in ("hang", "closed_hang", "escaped_hang"):
    if mode == "escaped_hang":
        os.setpgid(0, os.getpgid(os.getppid()))
    Path(os.environ["SCRIPT_FILENAME"]).with_suffix(".started").write_text(str(os.getpid()))
    Path(os.environ["SCRIPT_FILENAME"]).parent.joinpath(f"{mode}.started").write_text(str(os.getpid()))
    if mode == "closed_hang":
        os.close(1)
        os.close(2)
    time.sleep(60)

# Terminate worker process abnormally on crash request
if mode == "crash":
    os.kill(os.getpid(), signal.SIGKILL)

# Write response header delimiter
output.write(b"Content-Type: application/octet-stream\r\n\r\n")

# Fill stdout before reading large body to require concurrent pipe transfers
if mode == "duplex":
    output.write(b"P" * 131072)
    output.flush()
elif mode == "flood":
    try:
        while True:
            output.write(b"F" * 65536)
            output.flush()
    except BrokenPipeError:
        pass
    sys.exit(0)
elif mode == "oversized":
    output.write(b"O" * 16384)
    output.flush()
    sys.exit(0)

# Read full request payload from stdin
body = sys.stdin.buffer.read()

# Detect inherited CGI pipes after exec; ignore transient directory descriptors
if mode == "inspect":
    inherited = []
    for name in os.listdir("/proc/self/fd"):
        if int(name) < 3:
            continue
        try:
            target = os.readlink("/proc/self/fd/" + name)
        except FileNotFoundError:
            continue
        if target.startswith("pipe:"):
            inherited.append(name)
    output.write(("leaked:" + ",".join(inherited)).encode() if inherited else b"clean")
elif mode == "duplex":
    output.write(hashlib.sha256(body).hexdigest().encode())
elif mode == "httpoxy":
    output.write(b"has_proxy" if "HTTP_PROXY" in os.environ else b"no_proxy")
else:
    output.write(b"ok")

# Flush buffered response and exit w/ requested status
output.flush()
sys.exit(7 if mode == "nonzero" else 0)
'''


@unittest.skipUnless(sys.platform == "linux", "Require Linux CGI process handling")
class CGIProcessTests(unittest.TestCase):
    # Used to create isolated PATH entry, document root, logs, and configuration
    def setUp(self):
        # Prepare isolated test directory structure
        self.directory = tempfile.TemporaryDirectory(prefix="mercury-cgi-test-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()

        # Write mock php-cgi executable fixture
        worker = self.bin / "php-cgi"
        worker.write_text(f"#!{sys.executable}\n" + WORKER_SOURCE)
        worker.chmod(0o755)

        # Prepare dummy script files and error log path
        (self.root / "worker.php").write_text("<?php echo 'fixture';")
        (self.root / "small.txt").write_text("ready")
        self.error_log = self.root / "error.log"

        # Apply fixture PATH only while running this test's Mercury processes
        self.environment = patch.dict(os.environ, {"PATH": str(self.bin) + os.pathsep + os.environ["PATH"]})
        self.environment.start()
        self.addCleanup(self.environment.stop)

    # Used to generate test configuration w/ custom CGI timeout
    def config(self, timeout=5):
        text = (ROOT_DIR / "tests/conf_files/normal.conf").read_text()

        # Rewrite paths and apply custom CGI timeout override
        text = re.sub(r"<DocumentRoot>.*?</DocumentRoot>", f"<DocumentRoot> {self.root} </DocumentRoot>", text)
        text = re.sub(r"<ErrorLogFile>.*?</ErrorLogFile>", f"<ErrorLogFile> {self.error_log} </ErrorLogFile>", text)
        text = text.replace("<Mercury>", f"<Mercury><CGITimeout> {timeout} </CGITimeout>"
                            "<StartupCheckLatestRelease> false </StartupCheckLatestRelease>")

        # Save customized runtime configuration
        path = self.root / "mercury.conf"
        path.write_text(text)
        return str(path)

    # Used to send binary bodies w/ exact framing and read complete responses
    def response(self, mode, body=b"", host="127.0.0.1", tls=False, extra_headers=b""):
        headers = b"Connection: close\r\nContent-Length: " + str(len(body)).encode() + b"\r\n" + extra_headers

        # Connect and dispatch framed POST request
        with Connection(host, 8081 if tls else 8080, tls=tls) as connection:
            connection.send(request(b"/worker.php?" + mode.encode(), method=b"POST", headers=headers, body=body))
            return connection.response()

    # Verify Proxy header is omitted from CGI environment variables (HTTPoxy protection)
    def test_httpoxy_header_omitted_from_environment(self):
        with mercury_process(self.config()):
            status, _, body = self.response("httpoxy", extra_headers=b"Proxy: http://attacker.local:8080\r\n")
            self.assertEqual((status, body), (200, b"no_proxy"))

    # Verify directory links escape question marks and still reach named file
    def test_directory_listing_encodes_question_mark(self):
        (self.root / "odd?name.txt").write_text("question mark fixture")
        with mercury_process(self.config()):
            with Connection("127.0.0.1", 8080) as connection:
                connection.send(request(b"/"))
                status, _, body = connection.response()
                self.assertEqual(status, 200)
                self.assertIn(b'href="/odd%3Fname.txt"', body)
            with Connection("127.0.0.1", 8080) as connection:
                connection.send(request(b"/odd%3Fname.txt"))
                status, _, body = connection.response()
                self.assertEqual((status, body), (200, b"question mark fixture"))

    # Verify unauthenticated stalling TLS connection does not block other worker threads
    def test_stalled_tls_handshake_does_not_block_workers(self):
        with mercury_process(self.config()):
            raw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            try:
                raw_sock.connect(("127.0.0.1", 8081))
                status_tls, _, body_tls = self.response("warning", tls=True)
                self.assertEqual((status_tls, body_tls), (200, b"ok"))
                status_plain, _, body_plain = self.response("warning", tls=False)
                self.assertEqual((status_plain, body_plain), (200, b"ok"))
            finally:
                raw_sock.close()

    # Verify CGI worker stderr warning logs escaped diagnostics without injecting headers
    def test_stderr_warning_keeps_valid_response(self):
        targets = (("127.0.0.1", False), ("127.0.0.1", True), ("::1", False), ("::1", True))
        with mercury_process(self.config()):
            with ThreadPoolExecutor(max_workers=4) as executor:
                results = list(executor.map(lambda t: (t, self.response("warning", host=t[0], tls=t[1])), targets))

            for (host, tls), (status, headers, body) in results:
                with self.subTest(host=host, tls=tls):
                    self.assertEqual((status, body), (200, b"ok"))
                    self.assertNotIn(b"x-injected", headers)

        # Confirm escaped newline diagnostic was written to error log
        self.assertIn("worker warning\\x0A", self.error_log.read_text())

    # Verify excessive stderr diagnostic data is truncated without corrupting stdout
    def test_stderr_flood_drains_without_corrupting_body(self):
        with mercury_process(self.config()):
            status, _, body = self.response("stderr_flood")
            self.assertEqual((status, body), (200, b"ok"))

        # Verify diagnostic truncation marker in error log
        log = self.error_log.read_text()
        self.assertIn("[truncated]", log)
        self.assertLess(len(log), 8192)

    # Verify large concurrent bidirectional pipe transfers preserve binary payload
    def test_large_duplex_transfer_preserves_binary_body(self):
        body = bytes(range(256)) * 4096

        with mercury_process(self.config()):
            status, _, output = self.response("duplex", body)
            self.assertEqual(status, 200)
            self.assertEqual(output, b"P" * 131072 + hashlib.sha256(body).hexdigest().encode())

    # Verify concurrent worker processes do not leak inherited pipe descriptors
    def test_concurrent_workers_do_not_inherit_request_pipes(self):
        with mercury_process(self.config(timeout=10)):
            with ThreadPoolExecutor(max_workers=8) as executor:
                futures = [executor.submit(self.response, "inspect", b"payload" * 16384) for _ in range(16)]
                for future in futures:
                    status, _, body = future.result(timeout=15)
                    self.assertEqual((status, body), (200, b"clean"))

    # Verify nonzero exit code and worker crashes yield 502 Bad Gateway
    def test_failed_worker_exit_rejects_partial_response(self):
        with mercury_process(self.config()):
            for mode in ("nonzero", "crash"):
                with self.subTest(mode=mode):
                    self.assertEqual(self.response(mode)[0], 502)

    # Verify missing CGI executable reports launch error and returns 502 Bad Gateway
    def test_missing_executable_reports_launch_failure(self):
        with patch.dict(os.environ, {"PATH": str(self.root / "missing-bin")}):
            with mercury_process(self.config()):
                self.assertEqual(self.response("normal")[0], 502)

        # Confirm launch failure diagnostic was written to error log
        self.assertIn("Failed to launch PHP CGI worker", self.error_log.read_text())

    # Verify worker timeout returns 504 Gateway Timeout and terminates hung process
    def test_timeout_covers_streams_and_worker_exit(self):
        modes = ("hang", "closed_hang", "escaped_hang")
        with mercury_process(self.config(timeout=1)):
            def exercise(mode):
                start = time.monotonic()
                status = self.response(mode)[0]
                elapsed = time.monotonic() - start
                marker = self.root / f"{mode}.started"
                deadline = time.monotonic() + 2
                while not marker.exists() and time.monotonic() < deadline:
                    time.sleep(0.01)
                worker_pid = int(marker.read_text())
                return mode, status, elapsed, worker_pid

            with ThreadPoolExecutor(max_workers=3) as executor:
                results = list(executor.map(exercise, modes))

            for mode, status, elapsed, worker_pid in results:
                with self.subTest(mode=mode):
                    self.assertEqual(status, 504)
                    self.assertLess(elapsed, 4)

                    # Verify worker process was terminated
                    self.assertFalse(is_process_running(worker_pid))

    # Verify worker timeout covers child closing stdout while request body remains unconsumed
    def test_timeout_covers_closed_stdout_with_unconsumed_body(self):
        with mercury_process(self.config(timeout=1)):
            start = time.monotonic()
            status = self.response("closed_hang", b"X" * 131072)[0]
            elapsed = time.monotonic() - start
            marker = self.root / "closed_hang.started"
            deadline = time.monotonic() + 2
            while not marker.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            worker_pid = int(marker.read_text())

            self.assertEqual(status, 504)
            self.assertLess(elapsed, 4)
            self.assertFalse(is_process_running(worker_pid))

    # Verify server startup rejects invalid zero timeout configuration
    def test_zero_execution_timeout_is_rejected(self):
        result = subprocess.run([str(ROOT_DIR / "bin/mercury"), "-c", self.config(timeout=0)],
                                cwd=ROOT_DIR, capture_output=True, timeout=5)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"CGITimeout", result.stdout + result.stderr)

    # Verify server shutdown cancels active worker process
    def test_shutdown_cancels_active_worker(self):
        worker_pid = None

        with socket.socket() as connection:
            with mercury_process(self.config(timeout=30)):
                connection.connect(("127.0.0.1", 8080))
                connection.sendall(request(b"/worker.php?hang"))

                # Wait for actual worker startup before requesting server shutdown
                marker = self.root / "worker.started"
                deadline = time.monotonic() + 5
                while not marker.exists() and time.monotonic() < deadline:
                    time.sleep(0.01)

                self.assertTrue(marker.exists())
                worker_pid = int(marker.read_text())
                self.assertTrue(is_process_running(worker_pid))
                start = time.monotonic()

            self.assertLess(time.monotonic() - start, 5)

        # Verify worker process was cleaned up after server shutdown
        self.assertFalse(is_process_running(worker_pid))

    # Verify DisableLogs suppresses log file creation and writing
    def test_disable_logs_suppresses_log_output(self):
        text = self.config()
        conf_path = Path(text)
        conf_content = conf_path.read_text().replace(
            "<Mercury>", "<Mercury><DisableLogs> on </DisableLogs>"
        )
        conf_path.write_text(conf_content)
        if self.error_log.exists():
            self.error_log.unlink()

        with mercury_process(str(conf_path)):
            status, _, body = self.response("warning")
            self.assertEqual((status, body), (200, b"ok"))

        # Confirm error log was not created
        self.assertFalse(self.error_log.exists())

    # Verify CGI worker stdout flood terminates at response body ceiling and returns 413
    def test_stdout_flood_terminates_worker_and_returns_payload_too_large(self):
        text = self.config()
        conf_path = Path(text)
        conf_content = conf_path.read_text().replace(
            "<Mercury>", "<Mercury><MaxResponseBody> 1024 </MaxResponseBody>"
        )
        conf_path.write_text(conf_content)

        with mercury_process(str(conf_path)):
            status, _, _ = self.response("flood")
            self.assertEqual(status, 413)

        # Confirm error log recorded body limit failure
        self.assertIn("exceeded maximum response body limit (1024 bytes)", self.error_log.read_text())

    # Verify CGI worker response exceeding MaxResponseBody returns 413
    def test_oversized_cgi_response_returns_payload_too_large(self):
        text = self.config()
        conf_path = Path(text)
        conf_content = conf_path.read_text().replace(
            "<Mercury>", "<Mercury><MaxResponseBody> 1024 </MaxResponseBody>"
        )
        conf_path.write_text(conf_content)

        with mercury_process(str(conf_path)):
            status, _, _ = self.response("oversized")
            self.assertEqual(status, 413)

    # Verify static file exceeding MaxResponseBody returns 413
    def test_static_file_exceeding_max_response_body_returns_payload_too_large(self):
        text = self.config()
        conf_path = Path(text)
        conf_content = conf_path.read_text().replace(
            "<Mercury>", "<Mercury><MaxResponseBody> 1024 </MaxResponseBody>"
        )
        conf_path.write_text(conf_content)
        (self.root / "oversized.txt").write_text("X" * 4096)

        with mercury_process(str(conf_path)):
            with Connection("127.0.0.1", 8080) as connection:
                connection.send(request(b"/oversized.txt"))
                status, _, _ = connection.response()
                self.assertEqual(status, 413)

    # Verify CGI response where headers exceed MaxResponseBody but body is within limit returns 200
    def test_cgi_headers_do_not_count_against_body_limit(self):
        text = self.config()
        conf_path = Path(text)
        conf_content = conf_path.read_text().replace(
            "<Mercury>", "<Mercury><MaxResponseBody> 32 </MaxResponseBody>"
        )
        conf_path.write_text(conf_content)

        with mercury_process(str(conf_path)):
            status, _, body = self.response("normal")
            self.assertEqual((status, body), (200, b"ok"))

    # Verify HTTP/0.9 request exceeding MaxResponseBody closes connection without sending body
    def test_http09_exceeding_max_response_body_closes_connection_without_body(self):
        text = self.config()
        conf_path = Path(text)
        conf_content = conf_path.read_text().replace(
            "<Mercury>", "<Mercury><MaxResponseBody> 32 </MaxResponseBody>"
        )
        conf_path.write_text(conf_content)
        (self.root / "large1024.txt").write_text("A" * 1024)

        with mercury_process(str(conf_path)):
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
                sock.connect(("127.0.0.1", 8080))
                sock.sendall(b"GET /large1024.txt\r\n")
                data = b""
                sock.settimeout(2.0)
                try:
                    while True:
                        chunk = sock.recv(4096)
                        if not chunk:
                            break
                        data += chunk
                except socket.timeout:
                    pass
                self.assertEqual(data, b"")


@unittest.skipUnless(sys.platform == "linux", "Runner diagnostics were only exercised on Linux")
class RunnerDiagnosticsTests(unittest.TestCase):
    # Used to emulate hung shutdown without waiting through real CI deadlines
    def exercise_shutdown(self, original_error=False):
        # Configure mock server process to simulate shutdown timeout
        process = MagicMock()
        process.pid = 12345
        process.returncode = None
        process.poll.side_effect = lambda: process.returncode
        process.wait.side_effect = [subprocess.TimeoutExpired("mercury", 15), -9]

        # Simulate kill signal terminating process
        def kill():
            process.returncode = -9

        # Simulate process writing diagnostic output to stderr
        def launch(*args, **kwargs):
            kwargs["stderr"].write(b"retained CGI diagnostic\n")
            kwargs["stderr"].flush()
            return process

        process.kill.side_effect = kill

        # Configure mock connection probe to simulate responsive worker
        probe = MagicMock()
        probe.__enter__.return_value.recv.return_value = b"ready"
        output = io.StringIO()
        expected = ValueError if original_error else RuntimeError

        # Execute shutdown lifecycle under mocked process hooks
        with patch("harness.subprocess.Popen", side_effect=launch), \
             patch("harness.wait_until_live", return_value=True), \
             patch("harness.socket.create_connection", return_value=probe), \
             redirect_stdout(output):
            with self.assertRaises(expected):
                with mercury_process("unused"):
                    if original_error:
                        raise ValueError("original test failure")

        # Verify forced termination occurred and captured diagnostics were printed
        process.kill.assert_called_once()
        self.assertIn("retained CGI diagnostic", output.getvalue())
        self.assertIn("forced shutdown after timeout", output.getvalue())

    # Verify captured stderr diagnostics print on shutdown timeout
    def test_shutdown_timeout_prints_captured_diagnostics(self):
        self.exercise_shutdown()

    # Verify original test failure propagates despite subsequent shutdown timeout
    def test_shutdown_timeout_preserves_original_exception(self):
        self.exercise_shutdown(original_error=True)


@unittest.skipUnless(sys.platform == "linux", "Require Linux PHP-FPM")
class ManagedBackendTests(unittest.TestCase):
    # Used to prepare readable-by-owner-only document root and test configuration
    def setUp(self):
        self.root = ROOT_DIR
        self.directory = tempfile.TemporaryDirectory(prefix="mercury-owned-test-")
        self.addCleanup(self.directory.cleanup)
        self.document_root = Path(self.directory.name)
        (self.document_root / "index.php").write_text('<?php echo "owned PHP works";')
        (self.document_root / "small.txt").write_text("ready")
        source = (self.root / "tests/conf_files/regressions.conf").read_text()
        self.config = re.sub(r"<DocumentRoot>.*?</DocumentRoot>",
                             f"<DocumentRoot> {self.document_root} </DocumentRoot>", source)
        self.config = self.config.replace("<EnablePHPCGI> on </EnablePHPCGI>",
                                          "<EnablePHPCGI> off </EnablePHPCGI>")

    # Used to write per-test Mercury configuration without editing checked-in files
    def write_config(self, address=None, enabled=True, timeout=2):
        address_node = f"<FastCGIAddress> {address} </FastCGIAddress>" if address is not None else ""
        text = self.config.replace("<Mercury>", f"<Mercury><EnableFastCGI> {'on' if enabled else 'off'} </EnableFastCGI>"
                                   f"{address_node}<FastCGITimeout> {timeout} </FastCGITimeout>")
        path = self.document_root / "mercury.conf"
        path.write_text(text)
        return str(path)

    # Used to assert PHP execution through HTTP rather than socket readiness alone
    def assert_php(self):
        with Connection("127.0.0.1", 8080) as connection:
            connection.send(request(b"/index.php"))
            status, _, body = connection.response()
            self.assertEqual((status, body), (200, b"owned PHP works"))

    # Used to verify owned descendants and runtime files disappear after shutdown
    def assert_cleaned(self, children, directory):
        deadline = time.monotonic() + 5
        while any(child.is_running() for child in children) and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertFalse(any(child.is_running() for child in children))
        self.assertFalse(directory.exists())

    @unittest.skipIf(IS_ROOT, "Run ownership test as development user, not root")
    def test_auto_and_omitted_address_own_private_pool(self):
        for address in (None, "auto"):
            with self.subTest(address=address):
                with mercury_process(self.write_config(address)) as process:
                    self.assert_php()
                    children = psutil.Process(process.pid).children(recursive=True)
                    masters = [child for child in children if "master process (" in " ".join(child.cmdline())]
                    self.assertEqual(len(masters), 1)
                    title = " ".join(masters[0].cmdline())
                    config_path = re.search(r"\((/tmp/mercury-fpm-[^/]+/php-fpm.conf)\)", title)
                    self.assertIsNotNone(config_path, title)
                    directory = Path(config_path.group(1)).parent
                    self.assertEqual(directory.stat().st_mode & 0o777, 0o700)
                    self.assertTrue((directory / "php.sock").is_socket())
                    self.assertEqual((directory / "php.sock").stat().st_mode & 0o777, 0o600)
                    self.assertTrue(all(child.uids().effective == os.geteuid() for child in children))
                self.assert_cleaned(children, directory)

    @unittest.skipIf(IS_ROOT, "Run external fixture as unprivileged user")
    def test_explicit_backend_is_reused_and_survives(self):
        with FastCGIBackend("tcp") as path:
            with mercury_process(path) as process:
                with Connection("127.0.0.1", 8080) as connection:
                    connection.send(request(b"/regression_state.php"))
                    self.assertEqual(connection.response()[2], b"unset")
                self.assertEqual(psutil.Process(process.pid).children(), [])
            # Verify explicit listener still serves PHP after Mercury exits
            with mercury_process(path):
                with Connection("127.0.0.1", 8080) as connection:
                    connection.send(request(b"/regression_state.php"))
                    self.assertEqual(connection.response()[2], b"unset")

    @unittest.skipIf(IS_ROOT, "Run startup failure test as unprivileged user")
    def test_failed_launch_does_not_fall_back_to_system_pool(self):
        # Prefer deliberately failing highest-version binary over installed FPM
        executable = self.document_root / "php-fpm999.999"
        executable.write_text("#!/bin/sh\necho intentional-startup-failure >&2\nexit 9\n")
        executable.chmod(0o700)
        environment = {"PATH": str(self.document_root) + os.pathsep + os.environ.get("PATH", "")}
        with patch.dict(os.environ, environment):
            with mercury_process(self.write_config("auto")) as process:
                self.assertEqual(psutil.Process(process.pid).children(), [])
                with Connection("127.0.0.1", 8080) as connection:
                    connection.send(request(b"/index.php"))
                    self.assertEqual(connection.response()[0], 502)

    def test_disabled_fastcgi_does_not_launch_php(self):
        with mercury_process(self.write_config(enabled=False)) as process:
            self.assertEqual(psutil.Process(process.pid).children(), [])

    def test_explicit_unavailable_address_does_not_start_fpm(self):
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            address = f"127.0.0.1:{reservation.getsockname()[1]}"
            with mercury_process(self.write_config(address)) as process:
                self.assertEqual(psutil.Process(process.pid).children(), [])
                with Connection("127.0.0.1", 8080) as connection:
                    connection.send(request(b"/index.php"))
                    self.assertEqual(connection.response()[0], 502)

    def test_unix_socket_connect_timeout_on_full_backlog(self):
        # Create UDS with backlog of 0 and fill queue
        sock_path = str(self.document_root / "uds_backlog.sock")
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
            listener.bind(sock_path)
            listener.listen(0)
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as filler:
                filler.setblocking(False)
                try:
                    filler.connect(sock_path)
                except BlockingIOError:
                    pass
                t0 = time.monotonic()
                with mercury_process(self.write_config(f"unix:{sock_path}", timeout=1)):
                    t_started = time.monotonic()
                    # Ensure startup probe does not hang past timeout limit
                    self.assertLess(t_started - t0, 2.0)
                    with Connection("127.0.0.1", 8080) as connection:
                        t_req = time.monotonic()
                        connection.send(request(b"/index.php"))
                        status, _, _ = connection.response()
                        t_res = time.monotonic()
                        self.assertEqual(status, 504)
                        self.assertGreaterEqual(t_res - t_req, 0.9)
                        self.assertLess(t_res - t_req, 2.0)

    def test_tcp_socket_connect_timeout_on_full_backlog(self):
        # Create TCP listener with backlog of 1 and fill queue
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(("127.0.0.1", 0))
            port = listener.getsockname()[1]
            listener.listen(1)
            fillers = []
            try:
                for _ in range(10):
                    c = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    c.setblocking(False)
                    try:
                        c.connect(("127.0.0.1", port))
                    except BlockingIOError:
                        pass
                    fillers.append(c)
                t0 = time.monotonic()
                with mercury_process(self.write_config(f"127.0.0.1:{port}", timeout=1)):
                    t_started = time.monotonic()
                    # Ensure startup probe does not hang past timeout limit
                    self.assertLess(t_started - t0, 2.0)
                    with Connection("127.0.0.1", 8080) as connection:
                        t_req = time.monotonic()
                        connection.send(request(b"/index.php"))
                        status, _, _ = connection.response()
                        t_res = time.monotonic()
                        self.assertEqual(status, 504)
                        self.assertGreaterEqual(t_res - t_req, 0.9)
                        self.assertLess(t_res - t_req, 2.0)
            finally:
                for c in fillers:
                    c.close()

    @unittest.skipUnless(IS_ROOT, "Exercise direct-root startup policy only as root")
    def test_direct_root_does_not_start_privileged_pool(self):
        with mercury_process(self.write_config("auto")) as process:
            self.assertEqual(psutil.Process(process.pid).children(), [])
            with Connection("127.0.0.1", 8080) as connection:
                connection.send(request(b"/index.php"))
                self.assertEqual(connection.response()[0], 502)


if __name__ == "__main__":
    unittest.main()
