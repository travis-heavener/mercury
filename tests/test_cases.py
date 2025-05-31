from datetime import datetime, timezone
import socket

READ_BUF_SIZE = 1024 * 16 # Read buffer size for recv

gmt_now = lambda: datetime.now(timezone.utc).strftime("%a, %d %b %Y %H:%M:%S GMT")

class TestCase:
    def __init__(self, method: str, path: str, expected_status: int,
                 headers: dict=None, expected_headers: dict=None,
                 http_ver: str="HTTP/1.1"):
        self.method = method
        self.path = path
        self.http_ver = http_ver
        self.expected_status = expected_status

        self.headers = {} if headers is None else headers
        self.headers["Host"] = "Mercury Test Agent"

        # Format expected_headers
        expected_headers = {} if expected_headers is None else expected_headers
        self.expected_headers = { k.upper(): v for k, v in expected_headers.items() }

    # Send the payload to the server socket
    def test(self, s: socket.socket) -> bool:
        # Send payload
        s.sendall(str(self).encode("utf-8"))

        # Read response
        response = s.recv(READ_BUF_SIZE).decode("utf-8").replace("\r", "")
        status_code = int(response.split(" ")[1])

        # Extract headers
        header_lines = response.partition("\n\n")[0].split("\n")[1:]
        headers = {}
        for line in header_lines:
            index = line.find(":")
            key, value = line[:index], line[index+1:]
            headers[ key.strip().upper() ] = value.strip()

        ### Evaluate ###

        # Check status code
        if status_code != self.expected_status:
            return False

        # Check headers
        for k, v in self.expected_headers.items():
            if k not in headers or headers[k] != v:
                return False

        # Base case
        return True

    # Stringify the test case
    def __str__(self) -> str:
        s = f"{self.method} {self.path} {self.http_ver}\r\n"
        s += "\r\n".join(f"{k}: {v}" for k, v in self.headers.items())
        s += "\r\n"
        return s

cases = [
    # Path injection test cases
    TestCase(method="HEAD", path=".",               expected_status=400),
    TestCase(method="HEAD", path="../",             expected_status=400),
    TestCase(method="HEAD", path="..\\",            expected_status=400),
    TestCase(method="HEAD", path="/../",            expected_status=400),
    TestCase(method="HEAD", path="\\..\\",          expected_status=400),

    TestCase(method="HEAD", path="\\..\\",          expected_status=400),
    TestCase(method="HEAD", path="%2e%2e%2f",       expected_status=400),
    TestCase(method="HEAD", path="%2e%2e%5c",       expected_status=400),
    TestCase(method="HEAD", path="%2e%2e/",         expected_status=400),
    TestCase(method="HEAD", path="..%2f",           expected_status=400),

    TestCase(method="HEAD", path="%2e%2e\\",        expected_status=400),
    TestCase(method="HEAD", path="%252e%252e%255c", expected_status=400),
    TestCase(method="HEAD", path="..%255c",         expected_status=400),

    # CRLF injection test cases
    TestCase(method="HEAD", path="/%0D%0A",                 expected_status=404),
    TestCase(method="HEAD", path="/index.html?q=%0D%0A",    expected_status=200),

    # Success cases
    TestCase(method="HEAD", path="/",           expected_status=200),
    TestCase(method="HEAD", path="/?q=12",      expected_status=200),
    TestCase(method="HEAD", path="/index.html", expected_status=200),
    TestCase(method="HEAD", path="/index.html", expected_status=304, headers={"IF-MODIFIED-SINCE": gmt_now()}),

    # Misc. statuses
    TestCase(method="HEAD", path="/foobar",     expected_status=404),
    TestCase(method="POST", path="/",           expected_status=405),
    TestCase(method="HEAD", path="/",           expected_status=406, headers={"Accept": "text/plain"}),
    TestCase(method="HEAD", path="/",           expected_status=505, http_ver="HTTP/2.0"),

    # Invalid query string
    TestCase(method="HEAD", path="/index.html%", expected_status=400)
]