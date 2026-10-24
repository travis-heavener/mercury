"""

Author: Travis Heavener (https://github.com/travis-heavener/)

This file uses the test cases in tests.json to test the output of
  the Mercury HTTP server.

"""

from concurrent.futures import ThreadPoolExecutor, as_completed
from contextlib import contextmanager, nullcontext
import os
import psutil
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import time

from fastcgi_backend import FastCGIBackend
from test_case import TestCase, load_runs
from regressions import WIRE_TEST_COUNT, run_wire_regressions

host = "127.0.0.1"
host_v6 = "::1"
port = 8080
ssl_port = 8081

def is_mercury_running():
    process_name = None

    if sys.platform in ["win32", "cygwin"]:
        process_name = "mercury.exe"
    elif sys.platform == "linux":
        process_name = "mercury"
    else:
        print(f"Unrecognized sys.platform: {sys.platform}")
        return True

    # Check if running
    for proc in psutil.process_iter(["name"]):
        try:
            if proc.info["name"] == process_name:
                return True
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue

    # Base case
    return False

# Returns True if TLS is configured (ie. certs exist)
def is_tls_set_up():
    return os.path.exists("../conf/ssl/cert.pem") and os.path.exists("../conf/ssl/key.pem")

def _ping_sock(addr: str, port: int, ipv4: bool) -> bool:
    try:
        sock = socket.socket(socket.AF_INET if ipv4 else socket.AF_INET6, socket.SOCK_STREAM)
        sock.settimeout(1) # 1 second
        sock.connect((addr, port))
        return True
    except KeyboardInterrupt as e:
        raise e
    except:
        return False
    finally:
        sock.close()

# Sleeps the main thread until pings to the server are successful
def wait_until_live() -> bool:
    MAX_TIMEOUT = 15 # Max timeout in seconds

    # Test IPv4
    start_ts = time.time()
    while not _ping_sock(host, port, ipv4=True):
        if time.time() > start_ts + MAX_TIMEOUT:
            return False
        time.sleep(0.25)

    # Test IPv6
    start_ts = time.time()
    while not _ping_sock(host_v6, port, ipv4=False):
        if time.time() > start_ts + MAX_TIMEOUT:
            return False
        time.sleep(0.25)

    # Test IPv4 w/ SSL
    start_ts = time.time()
    while not _ping_sock(host, ssl_port, ipv4=True):
        if time.time() > start_ts + MAX_TIMEOUT:
            return False
        time.sleep(0.25)

    # Test IPv6 w/ SSL
    start_ts = time.time()
    while not _ping_sock(host_v6, ssl_port, ipv4=False):
        if time.time() > start_ts + MAX_TIMEOUT:
            return False
        time.sleep(0.25)

    return True

# Run tests helper
def run_single_test(case: TestCase, ipv4: bool, tls: bool) -> bool:
    test_type = f"IPv{4 if ipv4 else 6}{' SSL' if tls else ''}"
    try:
        addr = (host if ipv4 else host_v6, ssl_port if tls else port)
        if tls:
            with socket.create_connection(addr) as sock:
                with ssl_ctx.wrap_socket(sock, server_hostname=host if ipv4 else host_v6) as s:
                    return case.test(s, f"{test_type} test")
        else:
            with socket.socket(socket.AF_INET if ipv4 else socket.AF_INET6, socket.SOCK_STREAM) as s:
                s.connect(addr)
                return case.test(s, f"{test_type} test")
    except KeyboardInterrupt as e:
        raise e
    except Exception as e:
        print(f"[Error]: {test_type} failure: {e}")
        return False

# Test run helper
def run_tests(cases: list[TestCase], max_workers: int) -> int:
    num_passing = 0
    futures = []

    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        try:
            # Schedule tests
            for case in cases:
                futures.append(executor.submit(run_single_test, case, ipv4=True, tls=False))
                futures.append(executor.submit(run_single_test, case, ipv4=True, tls=True))
                futures.append(executor.submit(run_single_test, case, ipv4=False, tls=False))
                futures.append(executor.submit(run_single_test, case, ipv4=False, tls=True))

            # Collect results as they complete
            for f in as_completed(futures):
                try:
                    if f.result():
                        num_passing += 1
                except Exception as e:
                    print(f"[Error]: Test threw exception: {e}")
        except KeyboardInterrupt as e:
            # Cancel remaining futures
            for f in futures:
                try:
                    if not f.done():
                        f.cancel()
                except Exception:
                    pass

            try:
                executor.shutdown(wait=False, cancel_futures=True)
            except Exception:
                pass

            # Rethrow
            raise e

    return num_passing

# SSL init helper
def init_ssl():
    context = ssl.create_default_context()
    context.check_hostname = False # Allow self-signed certs
    context.verify_mode = ssl.CERT_NONE
    return context

@contextmanager
def mercury_process(config_path):
    windows = sys.platform in ("win32", "cygwin")
    with tempfile.TemporaryFile() as diagnostics:
        proc = subprocess.Popen(
            ["../bin/mercury.exe" if windows else "../bin/mercury", "-c", config_path],
            stdout=subprocess.DEVNULL, stderr=diagnostics, stdin=subprocess.DEVNULL,
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if windows else 0
        )
        failed = False
        try:
            if not wait_until_live() or proc.poll() is not None:
                diagnostics.seek(0)
                raise RuntimeError("Mercury failed to start:\n" + diagnostics.read().decode(errors="replace"))
            # Wait for HTTP workers after backend startup completes
            with socket.create_connection((host, port), timeout=10) as probe:
                probe.sendall(b"GET /small.txt HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                if not probe.recv(1024):
                    raise RuntimeError("Mercury closed startup probe")
            yield proc
            if proc.poll() is not None:
                raise RuntimeError("Mercury exited unexpectedly during test run")
        except BaseException:
            failed = True
            raise
        finally:
            # Capture unexpected exit before sending normal shutdown signal
            unexpected_exit = proc.poll()
            if proc.poll() is None:
                proc.send_signal(signal.CTRL_BREAK_EVENT if windows else signal.SIGINT)
                try:
                    proc.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)
                    raise RuntimeError("Mercury did not shut down gracefully")

            # Preserve runtime crash diagnostics normally hidden by redirected stderr
            if failed or unexpected_exit is not None:
                code = proc.returncode
                formatted_code = f"{code} (0x{code & 0xFFFFFFFF:08X})" if windows and code is not None else str(code)
                state = "unexpected exit" if unexpected_exit is not None else "after test failure and shutdown"
                print(f"[Diagnostics] Mercury PID {proc.pid}: {state}; exit code {formatted_code}", flush=True)
                diagnostics.seek(0)
                captured = diagnostics.read().decode(errors="replace")
                print("[Diagnostics] Mercury stderr:\n" + (captured or "(empty)"), flush=True)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    if is_mercury_running():
        print("[Error] Mercury is already running somewhere on your system, exiting...")
        sys.exit(1)

    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    if not is_tls_set_up():
        print("[Error] TLS certs are not configured, run `make cert`, exiting...")
        sys.exit(1)
    if sys.platform in ("win32", "cygwin") and not os.path.exists("../php/php-cgi.exe"):
        print("[Error] PHP for Windows is not configured, run `conf/setup_php.ps1`, exiting...")
        sys.exit(1)

    try:
        runs = [run for run in load_runs() if not run["platforms"] or sys.platform in run["platforms"]]
        num_passing = 0
        num_total = sum(len(run["cases"]) * 4 + (WIRE_TEST_COUNT if run["wire_regressions"] else 0) for run in runs)
        ssl_ctx = init_ssl()
        for run in runs:
            print("=" * 80)
            run_total = len(run["cases"]) * 4 + (WIRE_TEST_COUNT if run["wire_regressions"] else 0)
            print(f"Starting run: {run['desc']} ({run_total} tests)")
            backend = FastCGIBackend(run["fastcgi_transport"]) if run["fastcgi_transport"] else None
            context = backend if backend is not None else nullcontext(f"./tests/conf_files/{run['conf_file']}")
            with context as config_path:
                owned_children = []
                with mercury_process(config_path) as proc:
                    if sys.platform in ("win32", "cygwin") and run["fastcgi_transport"] == "tcp":
                        owned_children = psutil.Process(proc.pid).children(recursive=True)
                        if not owned_children:
                            raise RuntimeError("Mercury did not launch owned PHP backend")
                    run_start = num_passing
                    num_passing += run_tests(run["cases"], max_workers=12)
                    wire_passed = None
                    if run["wire_regressions"]:
                        wire_passed = run_wire_regressions(host, host_v6, port, ssl_port)
                        num_passing += wire_passed
                    # Verify managed Windows PHP survives default 500-request recycle boundary
                    if owned_children and run["reuse_cases_from"]:
                        case = TestCase("GET", "/regression_state.php", expectedStatus=200, version="1.1", body_match="unset")
                        for _ in range(510):
                            if not run_single_test(case, ipv4=True, tls=False):
                                raise RuntimeError("Managed PHP failed sustained-listener check")
                    suffix = f" (wire regressions: {wire_passed}/{WIRE_TEST_COUNT})" if wire_passed is not None else ""
                    print(("✅ Passing" if num_passing - run_start == run_total else "❌ Failing") + suffix)

                # Verify owned backend exits and external backend survives Mercury shutdown
                deadline = time.monotonic() + 5
                while any(child.is_running() for child in owned_children) and time.monotonic() < deadline:
                    time.sleep(0.05)
                if any(child.is_running() for child in owned_children):
                    raise RuntimeError("Owned PHP backend survived Mercury shutdown")
                if backend is not None and backend.proc is not None and backend.proc.poll() is not None:
                    raise RuntimeError("Mercury stopped externally managed PHP backend")

        # Run Linux ownership checks only after HTTP fixtures release server ports
        # Use same interpreter and propagate failures into overall suite result
        ownership_ok = True
        if sys.platform == "linux":
            print("=" * 80, flush=True)
            ownership_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_fastcgi_backend.py")
            ownership_ok = subprocess.run([sys.executable, ownership_script, "--mercury-output"]).returncode == 0

        print("=" * 80)
        success = num_passing == num_total and ownership_ok
        print("✅ Success" if success else "❌ Failure")
        print(f"Passing: {num_passing}/{num_total}")
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print("Aborting tests...")
        sys.exit(1)
    except Exception as error:
        print(f"[Error] {error}")
        sys.exit(1)
