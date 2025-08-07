#
# Used to send test requests to the server
#   Travis Heavener
#

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
    # Init SSL
    ssl_ctx = init_ssl()

    # Run IPv4 test cases
    num_passing = 0
    num_total = 0
    try:
        for i, test_case in enumerate(cases):
            # Open the socket connection
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.connect((host, port))

                # Send payload & evaluate
                if not test_case.test(s):
                    print(f"Failed IPv4 test #{i+1}")
                else:
                    num_passing += 1
        num_total += len(cases)
    except Exception as e:
        print("[Note]: IPv4 connection failure", e)

    # Run IPv4 SSL test cases
    try:
        for i, test_case in enumerate(cases):
            # Open the socket connection
            with socket.create_connection((host, ssl_port)) as sock:
                with ssl_ctx.wrap_socket(sock, server_hostname=host) as s:
                    # Send payload & evaluate
                    if not test_case.test(s):
                        print(f"Failed IPv4 SSL test #{i+1}")
                    else:
                        num_passing += 1
        num_total += len(cases)
    except Exception as e:
        print("[Note]: IPv4 SSL connection failure", e)

    # Run IPv6 test cases
    try:
        for i, test_case in enumerate(cases):
            # Open the socket connection
            with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
                s.connect((host_v6, port))

                # Send payload & evaluate
                if not test_case.test(s):
                    print(f"Failed IPv6 test #{i+1}")
                else:
                    num_passing += 1
        num_total += len(cases)
    except Exception as e:
        print("[Note]: IPv6 connection failure", e)

    # Run IPv6 SSL test cases
    try:
        for i, test_case in enumerate(cases):
            # Open the socket connection
            with socket.create_connection((host_v6, ssl_port)) as sock:
                with ssl_ctx.wrap_socket(sock, server_hostname=host_v6) as s:
                    # Send payload & evaluate
                    if not test_case.test(s):
                        print(f"Failed IPv6 SSL test #{i+1}")
                    else:
                        num_passing += 1
        num_total += len(cases)
    except Exception as e:
        print("[Note]: IPv6 SSL connection failure", e)

    # Print result
    sys.stdout.reconfigure(encoding="utf-8")
    print("Success ✅" if num_passing == num_total else "Failure ❌")
    print(f"Passing: {num_passing}/{num_total}")