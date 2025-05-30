#
# Used to send test requests to the server
#   Travis Heavener
#

import socket
from test_cases import cases

host = "127.0.0.1"
port = 80

if __name__ == "__main__":
    # Read each test case
    num_passing = 0
    for i, test_case in enumerate(cases):
        # Open the socket connection
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((host, port))

            # Send payload & evaluate
            if not test_case.test(s):
                print(f"Failed test #{i+1}")
            else:
                num_passing += 1

    # Print result
    print("✅" if num_passing == len(cases) else "❌", end=" ")
    print(f"Passing: {num_passing}/{len(cases)}")