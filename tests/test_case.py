import brotli
from datetime import datetime, timezone
import io
import json
import pathlib
import socket
from threading import Lock
import zlib
import zstandard as zstd

READ_BUF_SIZE = 1024 * 16 # Read buffer size for recv

gmt_now = lambda: datetime.now(timezone.utc).strftime("%a, %d %b %Y %H:%M:%S GMT")

print_lock = Lock()
def lprint(*args: list[any]):
    with print_lock:
        print(*args)

class TestCase:
    def __init__(self, method: str, path: str, expectedStatus: int, version: str,
                 headers: dict=None, expected_headers: dict=None,
                 body: str="", body_match: str=None, body_contains_mode: bool=False, https_only=False):
        self.method = method
        self.path = path
        self.body = body
        self.version = "HTTP/" + version
        self.expected_status = expectedStatus

        # Format headers
        self.headers = {}
        for k, v in headers.items():
            ku = k.upper()
            if ku in self.headers:
                if ku == "ACCEPT" or ku == "ACCEPT-ENCODING":
                    self.headers[ku] += ',' + v
                elif ku == "RANGE":
                    self.headers[ku] += ',' + v[v.find('=')+1:]
            else:
                self.headers[ku] = v

        self.headers["HOST"] = "localhost"
        self.headers["USER-AGENT"] = "Mercury Test Agent"

        if len(self.body) > 0:
            self.headers["Content-Length"] = f"{len(self.body)}"

        # Format expected_headers
        expected_headers = {} if expected_headers is None else expected_headers
        self.expected_headers = { k.upper(): v for k, v in expected_headers.items() }

        self.https_only = https_only
        self.body_match = body_match
        self.body_contains_mode = body_contains_mode

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

        if self.version == "HTTP/0.9":
            body = raw.decode("utf-8").replace("\r", "")

            # Verify something is returned
            if len(raw) == 0:
                lprint(f"Failed {test_desc}: Empty response to HTTP/0.9\n", self.inline_desc())
                return False

            # Match body
            does_body_match = (body == self.body_match and not self.body_contains_mode) \
                or (self.body_match is not None and self.body_match in body and self.body_contains_mode)
            if not does_body_match:
                lprint(f"Failed {test_desc}: Body mismatch\n",
                       self.inline_desc() + "\n",
                       "  Body:", body[0:48],
                       "..." if len(body) > 48 else ""
                )
                return False

            # Base case, success
            return True
        else:
            body = raw.replace(b"\r", b"")
            status_code = int(body.split(b" ")[1])

            # Extract headers
            header_lines = body.partition(b"\n\n")[0].split(b"\n")[1:]
            headers = {}
            for line in header_lines:
                line = line.decode("utf-8")
                index = line.find(":")
                key, value = line[:index], line[index+1:]
                headers[ key.strip().upper() ] = value.strip()

            ### Evaluate ###

            # Check status code
            if status_code != self.expected_status:
                lprint(
                    f"Failed {test_desc}: Status mismatch - expected {self.expected_status}, got {status_code}\n",
                    self.inline_desc()
                )
                return False

            # Check headers
            for k, v in self.expected_headers.items():
                if v == False and k in headers:
                    lprint(
                        f"Failed {test_desc}: Has forbidden header \"{k}\"=\"{headers[k]}\"\n",
                        self.inline_desc()
                    )
                    return False
                elif v != False and k not in headers:
                    lprint(
                        f"Failed {test_desc}: Missing header \"{k}\"\n",
                        self.inline_desc()
                    )
                    return False
                elif v is not None and k in headers and headers[k] != v:
                    lprint(
                        f"Failed {test_desc}: Header mismatch - expected \"{k}\"=\"{v}\", got \"{k}\"=\"{headers[k]}\"\n",
                        self.inline_desc()
                    )
                    return False

            # Read all body
            body = ""
            if self.method != "HEAD":
                body, success = read_body(s, headers, raw)
                if not success:
                    lprint(
                        f"Failed {test_desc}: Connection timed out reading body\n",
                        self.inline_desc()
                    )
                    return False

            # Match body
            if self.body_match is not None:
                # Match body
                body_decoded = body.decode("utf-8") if type(body) is bytes else body
                does_body_match = (body_decoded == self.body_match and not self.body_contains_mode) \
                    or (self.body_match in body_decoded and self.body_contains_mode)
                if not does_body_match:
                    lprint(
                        f"Failed {test_desc}: Body mismatch\n",
                        self.inline_desc() + "\n",
                        "  Body:", body[0:48],
                        "..." if len(body) > 48 else ""
                    )
                    return False

            # Check content encoding
            if "CONTENT-ENCODING" in self.expected_headers and self.expected_headers["CONTENT-ENCODING"] is not None and self.expected_headers["CONTENT-ENCODING"] != False:
                script_dir = pathlib.Path(__file__).parent.resolve()
                path = script_dir.joinpath("files").joinpath(self.path[1:])
                if not self._verify_decode( path, body, self.expected_headers["CONTENT-ENCODING"] ):
                    lprint(
                        f"Failed {test_desc}: Content-Encoding decode failed ({self.expected_headers['CONTENT-ENCODING']})\n",
                        self.inline_desc()
                    )
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

# Used to read the entire body including over transfer encoding
def read_body(s: socket.socket, headers: dict, raw: bytes) -> tuple[bytes, bool]:
    # Check for content length
    if "CONTENT-LENGTH" in headers:
        body = raw[ raw.find(b"\r\n\r\n")+4: ] # Current data from the initial recv
        content_len = int(headers["CONTENT-LENGTH"])

        start = datetime.now().timestamp()
        while len(body) < content_len:
            body += s.recv(READ_BUF_SIZE)
            if len(body) < content_len and datetime.now().timestamp() > start + 5:
                return (body, False) # Handle connection timeout

        return (body, True)
    elif "TRANSFER-ENCODING" in headers:
        body = b""
        buf = raw[ raw.find(b"\r\n\r\n")+4: ] # Current data from the initial recv

        start = datetime.now().timestamp()
        while True:
            # Handle any unparsed buffer data
            # Read octet size
            size_index = buf.find(b"\r\n")
            while size_index == -1:
                buf += s.recv(READ_BUF_SIZE)
                size_index = buf.find(b"\r\n")
            size = int(buf[ :size_index ].decode("utf-8"), 16)

            # Determine end of body
            end_of_chunk = size_index + 2 + size + 2
            while len(buf) < end_of_chunk:
                buf += s.recv(READ_BUF_SIZE)

            # Append body
            body += buf[ size_index+2 : end_of_chunk-2 ]
            buf = buf[ end_of_chunk: ] # Remove end of body

            # Await end chunk
            if size == 0:
                return (body, True)

            # Handle connection timeout
            if datetime.now().timestamp() > start + 5:
                return (body, False)
    else: # No body
        return (b"", True)

# Used to load all test cases from tests.json
def load_runs() -> list[TestCase]:
    runs = []

    # Load JSON
    script_dir = pathlib.Path(__file__).parent.resolve()
    path = script_dir.joinpath("tests.json")
    raw = None
    with open(path, "r") as f:
        raw = json.load(f)

    # Parse JSON
    for run in raw:
        # Load cases for the run
        cases = []
        for group in run["cases"]:
            for ver in group["versions"]:
                for case in group["cases"]:
                    cases.append(
                        TestCase(
                            case["method"], case["path"], expectedStatus=case["expectedStatus"], version=ver,
                            headers=case["headers"] if "headers" in case else {},
                            expected_headers=case["expectedHeaders"] if "expectedHeaders" in case else {},
                            body=case["body"] if "body" in case else "",
                            body_match=case["expectedBody"] if "expectedBody" in case else None,
                            body_contains_mode=case["expectedBodyContainsMode"] if "expectedBodyContainsMode" in case else False,
                            https_only=case["httpsOnly"] if "httpsOnly" in case else False
                        )
                    )

        # Append the run
        runs.append({
            "desc": run["desc"],
            "conf_file": run["confFile"],
            "cases": cases
        })

    return runs