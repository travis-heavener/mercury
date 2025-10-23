"""

Author: Travis Heavener (https://github.com/travis-heavener/)

This file uses the test cases in tests.json to test the output of
  the Mercury HTTP server.

"""

from concurrent.futures import ThreadPoolExecutor, as_completed
import os
import psutil
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import time

from test_case import TestCase, load_runs

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

def _ping_sock(addr: str, port: int, ipv4: bool) -> bool:
    try:
        sock = socket.socket(socket.AF_INET if ipv4 else socket.AF_INET6, socket.SOCK_STREAM)
        sock.settimeout(1) # 1 second
        sock.connect((addr, port))
        return True
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
    except Exception as e:
        print(f"[Error]: {test_type} connection failure: {e}")
        return False

# Test run helper
def run_tests(cases: list[TestCase], ipv4: bool, max_workers: int) -> int:
    num_passing = 0
    futures = []

    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        # Schedule tests
        for case in cases:
            futures.append(executor.submit(run_single_test, case, ipv4, False))

        for case in cases:
            futures.append(executor.submit(run_single_test, case, ipv4, True))

        # Collect results as they complete
        for f in as_completed(futures):
            try:
                if f.result():
                    num_passing += 1
            except Exception as e:
                print(f"[Error]: Test threw exception: {e}")

    return num_passing

# SSL init helper
def init_ssl():
    context = ssl.create_default_context()
    context.check_hostname = False # Allow self-signed certs
    context.verify_mode = ssl.CERT_NONE
    return context

if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")

    # Verify Mercury isn't already running
    if is_mercury_running():
        print("[Error] Mercury is already running somewhere on your system, exiting...")
        exit(1)

    # CD into script directory
    os.chdir( os.path.dirname( os.path.abspath(__file__) ) )

    # Save existing mercury.conf file
    try:
        if os.path.exists("conf_files/mercury.conf.lock"):
            os.remove("conf_files/mercury.conf.lock")
        shutil.copy2("../conf/mercury.conf", "conf_files/mercury.conf.lock")
    except:
        print("[Error] Failed to backup existing mercury.conf file")
        exit(1)

    try:
        # Load cases
        runs = load_runs()
        num_passing = 0
        num_total = sum( [len(run["cases"]) for run in runs] ) * 4

        # Init SSL
        ssl_ctx = init_ssl()

        # Handle each run
        for run in runs:
            print("=" * 80)
            print(f"Starting run: {run['desc']} ({len(run['cases']) * 4} tests)")

            # Update config file
            try:
                os.remove("../conf/mercury.conf")
                shutil.copy2(f"conf_files/{run['conf_file']}", "../conf/mercury.conf")
                time.sleep(1) # Allow time for the file to update
            except:
                print("[Error] Failed to load new config file, aborting remaining tests...")
                break

            # Start Mercury
            proc = None
            timed_out = False
            try:
                if sys.platform in ["win32", "cygwin"]:
                    proc = subprocess.Popen(
                        [ "../bin/mercury.exe" ],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL,
                        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP
                    )
                elif sys.platform == "linux":
                    proc = subprocess.Popen(
                        [ "../bin/mercury" ],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL
                    )
                else:
                    print(f"Unrecognized sys.platform: {sys.platform}")
                    raise Exception()

                # Wait to let the program start
                if not wait_until_live():
                    # Timed out, kill program
                    print(f"[Error] Server timed out when starting")
                    timed_out = True
            except:
                print(f"[Error] Failed to start Mercury")
                continue

            run_start_num_passing = num_passing
            if not timed_out:
                # Run tests
                try:
                    num_passing += run_tests( run["cases"], ipv4=True, max_workers=12 )
                    num_passing += run_tests( run["cases"], ipv4=False, max_workers=12 )
                except:
                    print(f"[Error] Failed running tests")

                if num_passing - run_start_num_passing == len(run["cases"]) * 4:
                    print("✅ Passing")

                # Allow buffer time before terminate
                time.sleep(1)

            # Kill Mercury
            try:
                if proc is not None:
                    if sys.platform in ["win32", "cygwin"]:
                        proc.send_signal(signal.CTRL_BREAK_EVENT)
                    elif sys.platform == "linux":
                        proc.send_signal(signal.SIGINT)
                    proc.wait()
            except:
                print(f"[Error] Failed to kill Mercury, aborting remaining tests...")
                break

        # Print divider
        print("=" * 80)

        # Print result
        print("✅ Success" if num_passing == num_total else "❌ Failure")
        print(f"Passing: {num_passing}/{num_total}")
    finally:
        # Restore original mercury.conf file
        try:
            if os.path.exists("conf_files/mercury.conf.lock"):
                os.remove("../conf/mercury.conf")
                shutil.copy2("conf_files/mercury.conf.lock", "../conf/mercury.conf")
                os.remove("conf_files/mercury.conf.lock")
        except:
            print("[Error] Failed to restore original mercury.conf file")

    # Exit with status
    exit(0 if num_passing == num_total else 1)