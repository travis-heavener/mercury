#
# Used to send test requests to the server
#   Travis Heavener
#

import socket
from test_cases import cases

host = "127.0.0.1"
host_v6 = "::1"
port = 80

if __name__ == "__main__":
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
                    print(f"Failed test #{i+1}")
                else:
                    num_passing += 1
        num_total += len(cases)
    except:
        print("[Note]: IPv4 connection failure")

    # Run IPv6 test cases
    try:
        for i, test_case in enumerate(cases):
            # Open the socket connection
            with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
                s.connect((host_v6, port))

                # Send payload & evaluate
                if not test_case.test(s):
                    print(f"Failed test #{i+1}")
                else:
                    num_passing += 1
        num_total += len(cases)
    except:
        print("[Note]: IPv6 connection failure")

    # Print result
    print("✅" if num_passing == num_total else "❌", end=" ")
    print(f"Passing: {num_passing}/{num_total}")