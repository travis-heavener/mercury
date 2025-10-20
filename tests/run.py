"""

Author: Travis Heavener (https://github.com/travis-heavener/)

This file uses the test cases in tests.json to test the output of
  the Mercury HTTP server.

"""

import os
import shutil
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

# Test run helper
def run_tests(cases: list[TestCase], ipv4: bool) -> int:
    num_passing = 0

    # Run cleartext test cases
    try:
        for case in cases:
            with socket.socket(socket.AF_INET if ipv4 else socket.AF_INET6, socket.SOCK_STREAM) as s:
                s.connect((host if ipv4 else host_v6, port))
                if case.test(s, f"IPv{4 if ipv4 else 6} test"):
                    num_passing += 1
    except:
        print(f"[Warn]: IPv{4 if ipv4 else 6} connection failure")

    # Run SSL test cases
    try:
        for case in cases:
            with socket.create_connection((host if ipv4 else host_v6, ssl_port)) as sock:
                with ssl_ctx.wrap_socket(sock, server_hostname=host if ipv4 else host_v6) as s:
                    # Send payload & evaluate
                    if case.test(s, f"IPv{4 if ipv4 else 6} SSL test"):
                        num_passing += 1
    except:
        print(f"[Warn]: IPv{4 if ipv4 else 6} SSL connection failure")

    return num_passing

# SSL init helper
def init_ssl():
    context = ssl.create_default_context()
    context.check_hostname = False # Allow self-signed certs
    context.verify_mode = ssl.CERT_NONE
    return context

if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")

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
            print(f"Starting run: {run['desc']}")

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
                    proc = subprocess.Popen([ "../bin/mercury.exe" ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, stdin=subprocess.PIPE)
                elif sys.platform == "linux":
                    proc = subprocess.Popen([ "../bin/mercury" ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, stdin=subprocess.PIPE)
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
                    num_passing += run_tests( run["cases"], ipv4=True )
                    num_passing += run_tests( run["cases"], ipv4=False )
                except:
                    print(f"[Error] Failed running tests")

                if num_passing - run_start_num_passing == len(run["cases"]) * 4:
                    print("✅ Passing")

            # Kill Mercury
            try:
                if proc is not None:
                    proc.terminate()
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