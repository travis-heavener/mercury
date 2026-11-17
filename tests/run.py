"""

Author: Travis Heavener (https://github.com/travis-heavener/)

This file uses the test cases in tests.json to test the output of
  the Mercury HTTP server.

"""

from concurrent.futures import as_completed, ThreadPoolExecutor
from contextlib import nullcontext
import os
import psutil
import socket
import sys
import time

from fastcgi_backend import FastCGIBackend
from harness import HOST, HOST_V6, mercury_process, PORT, run_suite, run_suites, TLS_PORT, tls_context
from regressions import run_wire_regressions, WIRE_TEST_COUNT
import test_backends
from test_case import load_runs, TestCase
import test_limits
import test_observability

TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
ssl_ctx = tls_context()


# Returns True if Mercury process is currently active
def is_mercury_running():
    process_name = None

    # Resolve platform binary name
    if sys.platform in ["win32", "cygwin"]:
        process_name = "mercury.exe"
    elif sys.platform == "linux":
        process_name = "mercury"
    else:
        print(f"Unrecognized sys.platform: {sys.platform}")
        return True

    # Scan running processes for matching server binary
    for proc in psutil.process_iter(["name"]):
        try:
            if proc.info["name"] == process_name:
                return True
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue

    return False


# Returns True if TLS certificates exist
def is_tls_set_up():
    return os.path.exists("../conf/ssl/cert.pem") and os.path.exists("../conf/ssl/key.pem")


# Returns True if single test case passes across specified transport
def run_single_test(case: TestCase, ipv4: bool, tls: bool) -> bool:
    test_type = f"IPv{4 if ipv4 else 6}{' SSL' if tls else ''}"
    try:
        addr = (HOST if ipv4 else HOST_V6, TLS_PORT if tls else PORT)
        family = socket.AF_INET if ipv4 else socket.AF_INET6

        # Dispatch test over TLS or plaintext socket connection
        with socket.socket(family, socket.SOCK_STREAM) as sock:
            sock.connect(addr)
            if tls:
                with ssl_ctx.wrap_socket(sock, server_hostname=HOST if ipv4 else HOST_V6) as s:
                    return case.test(s, f"{test_type} test")
            else:
                return case.test(sock, f"{test_type} test")
    except KeyboardInterrupt as e:
        raise e
    except Exception as e:
        print(f"[Error]: {test_type} failure: {e}")
        return False


# Returns number of passing tests across all transport variants
def run_tests(cases: list[TestCase], max_workers: int) -> int:
    num_passing = 0
    futures = []
    is_tty = sys.stdout.isatty()

    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        try:
            # Schedule test cases across IPv4/IPv6 and plaintext/TLS permutations
            for case in cases:
                futures.append(executor.submit(run_single_test, case, ipv4=True, tls=False))
                futures.append(executor.submit(run_single_test, case, ipv4=True, tls=True))
                futures.append(executor.submit(run_single_test, case, ipv4=False, tls=False))
                futures.append(executor.submit(run_single_test, case, ipv4=False, tls=True))

            total = len(futures)

            # Collect results as they complete
            for count, f in enumerate(as_completed(futures), 1):
                try:
                    if f.result():
                        num_passing += 1
                except Exception as e:
                    print(f"[Error]: Test threw exception: {e}")

                # Emit responsive terminal progress to eliminate visual stalls
                if is_tty and (count % 32 == 0 or count == total):
                    sys.stdout.write(f"\r  Running tests... [{count}/{total}]")
                    sys.stdout.flush()

            if is_tty and total > 0:
                sys.stdout.write("\r\033[K")
                sys.stdout.flush()
        except KeyboardInterrupt as e:
            # Cancel pending futures on interrupt
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

            # Propagate interrupt signal
            raise e

    return num_passing


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")

    # Fail fast if another server instance is active
    if is_mercury_running():
        print("[Error] Mercury is already running somewhere on your system, exiting...")
        sys.exit(1)

    # Change working directory to tests directory
    os.chdir(TESTS_DIR)

    # Verify prerequisite certificates and binaries
    if not is_tls_set_up():
        print("[Error] TLS certs are not configured, run `make cert`, exiting...")
        sys.exit(1)

    if sys.platform in ("win32", "cygwin") and not os.path.exists("../php/php-cgi.exe"):
        print("[Error] PHP for Windows is not configured, run `conf/setup_php.ps1`, exiting...")
        sys.exit(1)

    try:
        # Filter test runs compatible w/ current platform
        runs = [run for run in load_runs() if not run["platforms"] or sys.platform in run["platforms"]]
        num_passing = 0
        num_total = sum(len(run["cases"]) * 4 + (WIRE_TEST_COUNT if run["wire_regressions"] else 0) for run in runs)

        for run in runs:
            print("=" * 80)
            run_total = len(run["cases"]) * 4 + (WIRE_TEST_COUNT if run["wire_regressions"] else 0)
            print(f"Starting run: {run['desc']} ({run_total} tests)")

            # Initialize backend fixture or load static configuration file
            backend = FastCGIBackend(run["fastcgi_transport"]) if run["fastcgi_transport"] else None
            context = backend if backend is not None else nullcontext(f"./tests/conf_files/{run['conf_file']}")

            with context as config_path:
                owned_children = []

                with mercury_process(config_path) as proc:
                    # Discover child worker processes spawned on Windows TCP transport
                    if sys.platform in ("win32", "cygwin") and run["fastcgi_transport"] == "tcp":
                        owned_children = psutil.Process(proc.pid).children(recursive=True)
                        if not owned_children:
                            raise RuntimeError("Mercury did not launch owned PHP backend")

                    # Execute integration test cases
                    run_start = num_passing
                    num_passing += run_tests(run["cases"], max_workers=24)

                    # Execute wire-level regression tests if requested
                    wire_passed = None
                    if run["wire_regressions"]:
                        if sys.stdout.isatty():
                            sys.stdout.write("\r  Running wire regressions...")
                            sys.stdout.flush()
                        wire_passed = run_wire_regressions(HOST, HOST_V6, PORT, TLS_PORT)
                        if sys.stdout.isatty():
                            sys.stdout.write("\r\033[K")
                            sys.stdout.flush()
                        num_passing += wire_passed

                    # Verify managed Windows PHP survives default 500-request recycle boundary
                    if owned_children and run["reuse_cases_from"]:
                        case = TestCase("GET", "/regression_state.php", expectedStatus=200, version="1.1", body_match="unset")
                        for _ in range(510):
                            if not run_single_test(case, ipv4=True, tls=False):
                                raise RuntimeError("Managed PHP failed sustained-listener check")

                    # Report test run outcome
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

        # Run private-port suites together (own ports and temp dirs), then shared-port backend suite alone
        suites_ok = [
            run_suites(("Rate Limiting and Connection Queue Tests", test_limits),
                       ("Structured Logging and Metrics Tests", test_observability)),
            run_suite("CGI and PHP-FPM Process Tests", test_backends),
        ]

        # Print overall test suite summary
        print("=" * 80)
        success = num_passing == num_total and all(suites_ok)
        print("✅ Success" if success else "❌ Failure")
        print(f"Passing: {num_passing}/{num_total}")
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print("Aborting tests...")
        sys.exit(1)
    except Exception as error:
        print(f"[Error] {error}")
        sys.exit(1)
