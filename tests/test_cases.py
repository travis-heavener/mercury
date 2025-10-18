import brotli
from datetime import datetime, timezone
import io
import pathlib
import re
import socket
import zlib
import zstandard as zstd

"""

Author: Travis Heavener (https://github.com/travis-heavener/)

This file contains the defined test cases (as an array of TestCase class objects).
The test cases are below the class definition at the bottom of this file.

The TestCase class provides structure to specify a desired HTTP status code (or -1 for HTTP/0.9).
This class allows you to specify request headers (the headers field) and specify
  desired response headers (the expected_headers field). For expected response headers, if you
  only wish to check if a header is present without caring about what the value of the header
  is, use None instead of a string value. Additionally, if you want to make sure a header ISN'T
  present, use False.
Use the version field to specify what HTTP version to use for requests WITHOUT the "HTTP/" prefix
  (ex. 0.9, 1.0, 1.1).
Use the body_match field to exactly match the response body.
Use the keep_alive boolean field to control whether the connection is kept alive or is closed.

"""

READ_BUF_SIZE = 1024 * 16 # Read buffer size for recv

gmt_now = lambda: datetime.now(timezone.utc).strftime("%a, %d %b %Y %H:%M:%S GMT")

class TestCase:
    def __init__(self, method: str, path: str, expected: int, body: str="",
                 headers: dict=None, expected_headers: dict=None,
                 version: str="1.1", https_only=False, body_match=None, keep_alive=False):
        self.method = method
        self.path = path
        self.body = body
        self.version = "HTTP/" + version
        self.expected_status = expected

        self.headers = {} if headers is None else headers
        self.headers["Host"] = "localhost"
        self.headers["User-Agent"] = "Mercury Test Agent"
        self.headers["Connection"] = "close" if not keep_alive else "keep-alive"

        if len(self.body) > 0:
            self.headers["Content-Length"] = f"{len(self.body)}"

        # Format expected_headers
        expected_headers = {} if expected_headers is None else expected_headers
        self.expected_headers = { k.upper(): v for k, v in expected_headers.items() }

        self.https_only = https_only
        self.body_match = body_match

    # Generate an inline description of the request
    def inline_desc(self) -> str:
        if self.version == "HTTP/0.9":
            return f"  {self.method} {self.path} {self.version}"

        # Base case, HTTP/1.0+
        s = f"  {self.method} {self.path} {self.version}\n"
        s += f"  Expected Status: {self.expected_status}\n"

        if len(self.headers): s += "  Req. Headers:\n"
        for k, v in self.headers.items():
            s += f"    {k}: {v}\n"

        if len(self.expected_headers): s += "  Expected Res. Headers:\n"
        for k, v in self.expected_headers.items():
            s += f"    {k}: {'<any>' if v is None else '<unset>' if v == False else v}\n"

        # Return everything except extra newline
        return s[:-1]

    # Send the payload to the server socket
    def test(self, s: socket.socket, test_desc: str) -> bool:
        # Auto-pass for HTTPS only requests
        if self.https_only and "SSL" not in test_desc: return True

        # Send payload
        s.sendall(str(self).encode("utf-8"))

        # Read response
        raw = s.recv(READ_BUF_SIZE)
        http_0_9_body = raw

        if self.version == "HTTP/0.9":
            raw = raw.decode("utf-8")
            response = raw.replace("\r", "")

            # Verify something is returned
            if len(response) == 0:
                print(f"Failed {test_desc}: Empty response to HTTP/0.9")
                print(self.inline_desc())
                return False

            # Verify this isn't an HTTP/1.1 fallback
            if re.match(r"^HTTP\/1.1 \d+ [\w ]+$", response.split("\n")[0]):
                print(f"Failed {test_desc}: HTTP/1.1 fallback to HTTP/0.9")
                print(self.inline_desc())
                return False

            # Match body
            if self.body_match is not None and raw != self.body_match:
                print(f"Failed {test_desc}: Body mismatch")
                print(self.inline_desc())
                print("  Body:", http_0_9_body[0:48], end="")
                print("..." if len(http_0_9_body) > 48 else "")
                return False

            # Base case, success
            return True
        else:
            response = raw.replace(b"\r", b"")
            status_code = int(response.split(b" ")[1])

            # Extract headers
            header_lines = response.partition(b"\n\n")[0].split(b"\n")[1:]
            headers = {}
            for line in header_lines:
                line = line.decode("utf-8")
                index = line.find(":")
                key, value = line[:index], line[index+1:]
                headers[ key.strip().upper() ] = value.strip()

            ### Evaluate ###

            # Check status code
            if status_code != self.expected_status:
                print(f"Failed {test_desc}: Status mismatch - expected {self.expected_status}, got {status_code}")
                print(self.inline_desc())
                return False

            # Check headers
            for k, v in self.expected_headers.items():
                if v == False and k in headers:
                    print(f"Failed {test_desc}: Has forbidden header \"{k}\"=\"{headers[k]}\"")
                    print(self.inline_desc())
                    return False
                elif v != False and k not in headers:
                    print(f"Failed {test_desc}: Missing header \"{k}\"")
                    print(self.inline_desc())
                    return False
                elif v is not None and k in headers and headers[k] != v:
                    print(f"Failed {test_desc}: Header mismatch - expected \"{k}\"=\"{v}\", got \"{k}\"=\"{headers[k]}\"")
                    print(self.inline_desc())
                    return False

            # Read all body
            body = ""
            if self.method != "HEAD":
                content_len = int(headers["CONTENT-LENGTH"]) if "CONTENT-LENGTH" in headers else 0
                body = http_0_9_body[ http_0_9_body.find(b"\r\n\r\n")+4: ]

                start = datetime.now().timestamp()
                while len(body) < content_len:
                    body += s.recv(READ_BUF_SIZE)

                    if len(body) < content_len and datetime.now().timestamp() > start + 5:
                        print(f"Failed {test_desc}: Connection timed out reading body")
                        print(self.inline_desc())
                        return False

            # Match body
            if self.body_match is not None:
                if self.body_match is not None and body.decode("utf-8") != self.body_match:
                    print(f"Failed {test_desc}: Body mismatch")
                    print(self.inline_desc())
                    print("  Body:", body[0:48], end="")
                    print("..." if len(body) > 48 else "")
                    return False

            # Check content encoding
            if "CONTENT-ENCODING" in self.expected_headers and self.expected_headers["CONTENT-ENCODING"] is not None and self.expected_headers["CONTENT-ENCODING"] != False:
                script_dir = pathlib.Path(__file__).parent.resolve()
                path = script_dir.joinpath("files").joinpath(self.path[1:])
                if not self._verify_decode( path, body, self.expected_headers["CONTENT-ENCODING"] ):
                    print(f"Failed {test_desc}: Content-Encoding decode failed ({self.expected_headers['CONTENT-ENCODING']})")
                    print(self.inline_desc())
                    return False

            # Base case
            return True

    # Stringify the test case
    def __str__(self) -> str:
        if self.version == "HTTP/0.9":
            return f"{self.method} {self.path}\r\n\r\n"
        else:
            s = f"{self.method} {self.path} {self.version}\r\n"
            s += "\r\n".join(f"{k}: {v}" for k, v in self.headers.items())
            s += "\r\n" * (2 if len(self.headers) > 0 else 1)
            s += self.body
            return s

    # Verify a file is decoded properly
    def _verify_decode(self, path: str, body: str, enc: str) -> bool:
        orig_body = None
        try:
            with open(path, "rb") as f:
                orig_body = f.read()
        except Exception:
            return False

        # Decode incoming body
        if enc == "deflate":
            return zlib.decompress(body, wbits=15) == orig_body
        elif enc == "gzip":
            return zlib.decompress(body, wbits=31) == orig_body
        elif enc == "br":
            return brotli.decompress(body) == orig_body
        elif enc == "zstd":
            dctx = zstd.ZstdDecompressor()
            with dctx.stream_reader(io.BytesIO(body)) as reader:
                return reader.read() == orig_body

        # Base case, uncaught enc type
        return False

###########################################################################
########                                                           ########
###                            TEST CASES                               ###
########                                                           ########
###########################################################################

cases = [
    # Test PHP parsing (HTTP/1.1 ONLY)
    TestCase("GET", "/index.php",    expected=200, version="1.1"),
    TestCase("HEAD", "/index.php",   expected=200, version="1.1"),
    TestCase("OPTIONS", "/index.php",expected=200, version="1.1"),
    TestCase("POST", "/index.php",   expected=200, version="1.1"),
    TestCase("PUT", "/index.php",    expected=200, version="1.1"),
    TestCase("PATCH", "/index.php",  expected=200, version="1.1"),
    TestCase("DELETE", "/index.php", expected=200, version="1.1"),
    TestCase("POST", "/ASDFGHJKL",   expected=404, version="1.1"),

    # Test PHP path info URIs (HTTP/1.1 ONLY)
    TestCase("GET", "/path_info_test.php/foo/bar", expected=200, version="1.1", body_match="/foo/bar"),
    TestCase("GET", "/path_info_test.php/foo/bar?q=1", expected=200, version="1.1", body_match="/foo/bar"),
    TestCase("GET", "/index.html/foo/bar", expected=404, version="1.1"),

    # Test HTTP body via PHP (HTTP/1.1 checks only, essentially benign on HTTP/1.0)
    TestCase("POST", "/body_tests/raw.php", expected=200, version="1.1", body="field1=value1&field2=value2", body_match="field1=value1&field2=value2"), # x-www-form-urlencoded
    TestCase("POST", "/body_tests/raw.php", expected=200, version="1.1", body="foobar", body_match="foobar"), # Raw body
    TestCase("POST", "/body_tests/form_data.php", expected=200, version="1.1", # Raw body
             headers={"Content-Type": "multipart/form-data;boundary=\"testDelimiter\""},
             body="--testDelimiter\nContent-Disposition: form-data; name=\"field1\"\n\nvalue1\n--testDelimiter\nContent-Disposition: form-data; name=\"field2\"; filename=\"example.txt\"\n\nvalue2\n--testDelimiter--",
             body_match="field1: value1, file contents (example.txt): value2"
    ),

    # Invalid method checking for static files (HTTP/1.1)
    TestCase("GET", "/",            expected=200, version="1.1"),
    TestCase("HEAD", "/",           expected=200, version="1.1"),
    TestCase("HEAD", "*",           expected=400, version="1.1"),
    TestCase("OPTIONS", "/",        expected=204, version="1.1", expected_headers={"Allow": None}),
    TestCase("OPTIONS", "*",        expected=204, version="1.1", expected_headers={"Allow": None}),
    TestCase("POST", "/",           expected=405, version="1.1"),
    TestCase("PUT", "/",            expected=405, version="1.1"),
    TestCase("PATCH", "/",          expected=405, version="1.1"),
    TestCase("DELETE", "/",         expected=405, version="1.1"),
    TestCase("FOO", "/foo-bar",     expected=501, version="1.1"),

    # Invalid method checking for static files (HTTP/1.0)
    TestCase("GET", "/",            expected=200, version="1.0"),
    TestCase("HEAD", "/",           expected=200, version="1.0"),
    TestCase("HEAD", "*",           expected=400, version="1.0"),
    TestCase("OPTIONS", "/",        expected=501, version="1.0"),
    TestCase("OPTIONS", "*",        expected=501, version="1.0"),
    TestCase("POST", "/",           expected=405, version="1.0"),
    TestCase("PUT", "/",            expected=501, version="1.0"),
    TestCase("PATCH", "/",          expected=501, version="1.0"),
    TestCase("DELETE", "/",         expected=501, version="1.0"),
    TestCase("FOO", "/foo-bar",     expected=501, version="1.0"),

    # Invalid method checking for static files (HTTP/0.9)
    # These really just make sure the server doesn't crash,
    # the responses are more loosely checked
    TestCase("GET", "/index.html",  expected=-1, version="0.9"),
    TestCase("HEAD", "/index.html", expected=-1, version="0.9"),

    # Test unsupported HTTP methods
    TestCase("HEAD", "/", expected=505, version="2.0"),
    TestCase("HEAD", "/", expected=505, version="3.0"),
    TestCase("HEAD", "/", expected=505, version="4.0"),

    # Redirect Rule tests
    TestCase("HEAD", "/redirect_from/foo.txt",         expected=301, version="1.1", expected_headers={"Location": "/redirect_to/foo.txt"}),
    TestCase("HEAD", "/redirect_from/bar.txt",         expected=301, version="1.0", expected_headers={"Location": "/redirect_to/bar.txt"}),
    TestCase("HEAD", "/redirect_from/foo.txt",         expected=301, version="1.1", expected_headers={"Location": "/redirect_to/foo.txt"}),
    TestCase("HEAD", "/redirect_from/bar.txt",         expected=301, version="1.0", expected_headers={"Location": "/redirect_to/bar.txt"}),

    # Test Redirect Rule restriction to 300-302 in HTTP/1.0 (falls back to 302 Found)
    TestCase("HEAD", "/redirect_http1.1_only/foo.txt", expected=308, version="1.1", expected_headers={"Location": "/redirect_to/foo.txt"}),
    TestCase("HEAD", "/redirect_http1.1_only/foo.txt", expected=302, version="1.0", expected_headers={"Location": "/redirect_to/foo.txt"})
]

# Tests IDENTICAL for HTTP/1.0 and HTTP/1.1
for ver in ["1.0", "1.1"]:
    cases.extend([
        # Path injection
        TestCase("HEAD", ".",               expected=400, version=ver),
        TestCase("HEAD", "../",             expected=400, version=ver),
        TestCase("HEAD", "..\\",            expected=400, version=ver),
        TestCase("HEAD", "/../",            expected=400, version=ver),
        TestCase("HEAD", "\\..\\",          expected=400, version=ver),
        TestCase("HEAD", "\\..\\",          expected=400, version=ver),
        TestCase("HEAD", "%2e%2e%2f",       expected=400, version=ver),
        TestCase("HEAD", "%2e%2e%5c",       expected=400, version=ver),
        TestCase("HEAD", "%2e%2e/",         expected=400, version=ver),
        TestCase("HEAD", "..%2f",           expected=400, version=ver),
        TestCase("HEAD", "%2e%2e\\",        expected=400, version=ver),
        TestCase("HEAD", "%252e%252e%255c", expected=400, version=ver),
        TestCase("HEAD", "..%255c",         expected=400, version=ver),

        # CRLF injection
        TestCase("HEAD", "/%0D%0A",                 expected=404, version=ver),
        TestCase("HEAD", "/index.html?q=%0D%0A",    expected=200, version=ver),

        # Success test cases
        TestCase("HEAD", "/",           expected=200, version=ver),
        TestCase("HEAD", "/?q=12",      expected=200, version=ver),
        TestCase("HEAD", "/index.html", expected=200, version=ver),

        # Test If-Modified-Since & 304 status
        TestCase("HEAD", "/index.html", expected=304, version=ver, headers={"IF-MODIFIED-SINCE": gmt_now()}),

        # Test byte ranges
        TestCase("HEAD", "/", expected=206, version=ver, headers={"Range": "bytes=0-13"}, expected_headers={"Content-Length": "14"}),
        TestCase("HEAD", "/", expected=416, version=ver, headers={"Range": "bytes=0-13, -19"}),

        # Invalid query string
        TestCase("HEAD", "/index.html%", expected=400, version=ver),

        # Test misc. statuses
        TestCase("HEAD", "/foobar", expected=404, version=ver),
        TestCase("HEAD", "/",       expected=406, version=ver, headers={"Accept": "text/plain"}),
        TestCase("HEAD", "/",       expected=406, version=ver, headers={"Accept": "foobar;bar,;123"}),

        # Test compression (specify full file path with filename)
        TestCase("GET", "/index.html", expected=200, version=ver, headers={"Accept-Encoding": "zstd"},    expected_headers={"Content-Encoding": "zstd"}),
        TestCase("GET", "/index.html", expected=200, version=ver, headers={"Accept-Encoding": "br"},      expected_headers={"Content-Encoding": "br"}, https_only=True),
        TestCase("GET", "/index.html", expected=200, version=ver, headers={"Accept-Encoding": "gzip"},    expected_headers={"Content-Encoding": "gzip"}),
        TestCase("GET", "/index.html", expected=200, version=ver, headers={"Accept-Encoding": "deflate"}, expected_headers={"Content-Encoding": "deflate"}),
        TestCase("GET", "/index.html", expected=200, version=ver, headers={"Accept-Encoding": "foobar"},  expected_headers={"Content-Encoding": False}),

        # Keep-alive tests
        TestCase("HEAD", "/", expected=200, version=ver, keep_alive=True, expected_headers={"Keep-Alive": None}),
        TestCase("HEAD", "/", expected=200, version=ver, keep_alive=False, expected_headers={"Keep-Alive": False})
    ])

# Tests IDENTICAL for all versions
for ver in ["0.9", "1.0", "1.1"]:
    cases.extend([
        # Match node pattern tests
        TestCase("GET", "/index.php", expected=200, version=ver, expected_headers={"X-Match-Test-Header": False}),
        TestCase("GET", "/index.html", expected=200, version=ver, expected_headers={"X-Match-Test-Header": "1"})
    ])