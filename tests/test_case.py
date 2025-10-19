import brotli
from datetime import datetime, timezone
import io
import json
import pathlib
import re
import socket
import zlib
import zstandard as zstd

READ_BUF_SIZE = 1024 * 16 # Read buffer size for recv

gmt_now = lambda: datetime.now(timezone.utc).strftime("%a, %d %b %Y %H:%M:%S GMT")

class TestCase:
    def __init__(self, method: str, path: str, expectedStatus: int, version: str,
                 headers: dict=None, expected_headers: dict=None,
                 body: str="", body_match=None, https_only=False):
        self.method = method
        self.path = path
        self.body = body
        self.version = "HTTP/" + version
        self.expected_status = expectedStatus

        self.headers = {} if headers is None else headers
        self.headers = { k.upper(): v for k, v in self.headers.items() }
        self.headers["HOST"] = "localhost"
        self.headers["USER-AGENT"] = "Mercury Test Agent"

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

# Used to load all test cases from tests.json
def load_cases() -> list[TestCase]:
    cases = []

    # Load JSON
    script_dir = pathlib.Path(__file__).parent.resolve()
    path = script_dir.joinpath("tests.json")
    raw = None
    with open(path, "r") as f:
        raw = json.load(f)

    # Parse JSON
    for group in raw:
        for ver in group["versions"]:
            for case in group["cases"]:
                cases.append(
                    TestCase(
                        case["method"], case["path"], expectedStatus=case["expectedStatus"], version=ver,
                        headers=case["headers"] if "headers" in case else {},
                        expected_headers=case["expectedHeaders"] if "expectedHeaders" in case else {},
                        body=case["body"] if "body" in case else "",
                        https_only=case["httpsOnly"] if "httpsOnly" in case else False
                    )
                )

    return cases