"""

Author: Travis Heavener (https://github.com/travis-heavener/)

This file uses the test cases in test_cases.py to test the output of
  the Mercury HTTP server.

See the note at the top of test_cases.py for more information about
  how to use and make your own test cases.

"""

import pathlib
import re
import socket
import ssl
import sys
from test_cases import cases

host = "127.0.0.1"
host_v6 = "::1"
port = 80
ssl_port = 443

def init_ssl():
    context = ssl.create_default_context()
    context.check_hostname = False # Allow self-signed certs
    context.verify_mode = ssl.CERT_NONE
    return context

if __name__ == "__main__":
    # Verify DocumentRoot
    try:
        script_dir = pathlib.Path(__file__).parent.resolve()
        path = script_dir.joinpath("../conf/mercury.conf")
        with open(path) as f:
            raw = f.read()
            match = re.search(r"\<DocumentRoot\>\s*(.*?)\s*\<\/DocumentRoot\>", raw).groups()[0]
            if script_dir.joinpath("../" + match).resolve() != script_dir.joinpath("files"):
                print("DocumentRoot does not point to ./tests/files, aborting...")
                exit(1)
    except:
        print("Failed to parse mercury.conf")
        exit(1)

    # Init SSL
    ssl_ctx = init_ssl()

    # Run IPv4 test cases
    num_passing = 0
    num_total = len(cases) * 4
    try:
        for i, test_case in enumerate(cases):
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.connect((host, port))

                # Send payload & evaluate
                if test_case.test(s, f"IPv4 test"):
                    num_passing += 1
    except:
        print("[Note]: IPv4 connection failure")

    # Run IPv4 SSL test cases
    try:
        for i, test_case in enumerate(cases):
            with socket.create_connection((host, ssl_port)) as sock:
                with ssl_ctx.wrap_socket(sock, server_hostname=host) as s:
                    # Send payload & evaluate
                    if test_case.test(s, f"IPv4 SSL test"):
                        num_passing += 1
    except:
        print("[Note]: IPv4 SSL connection failure")

    # Run IPv6 test cases
    try:
        for i, test_case in enumerate(cases):
            with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
                s.connect((host_v6, port))

                # Send payload & evaluate
                if test_case.test(s, f"IPv6 test"):
                    num_passing += 1
    except:
        print("[Note]: IPv6 connection failure")

    # Run IPv6 SSL test cases
    try:
        for i, test_case in enumerate(cases):
            with socket.create_connection((host_v6, ssl_port)) as sock:
                with ssl_ctx.wrap_socket(sock, server_hostname=host_v6) as s:
                    # Send payload & evaluate
                    if test_case.test(s, f"IPv6 SSL test"):
                        num_passing += 1
    except:
        print("[Note]: IPv6 SSL connection failure")

    # Print result
    sys.stdout.reconfigure(encoding="utf-8")
    print("Success ✅" if num_passing == num_total else "Failure ❌")
    print(f"Passing: {num_passing}/{num_total}")