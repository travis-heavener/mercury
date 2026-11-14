"""Raw HTTP integration tests, called by run.py with regressions.conf.

These ARE Python tests. They supplement the JSON-driven cases because sockets
let us send malformed bytes, pipeline requests, and control connection lifetimes.
These groups run as part of the existing Python suite; no separate executable is needed.
Counts below are test groups, not individual requests or assertions.
"""
from concurrent.futures import ThreadPoolExecutor
import os
import socket
import ssl
import sys


# Assertions implement test failures; optimized Python would silently remove them.
if not __debug__:
    raise RuntimeError("Run the regression suite without Python -O/PYTHONOPTIMIZE")

HAS_SYMLINK_FIXTURE = False


class Connection:
    """Minimal reader for these GET/POST cases, not a general HTTP client.

    A single buffered reader retains bytes belonging to pipelined responses.
    Five-second socket timeouts turn stalls into failures instead of hanging CI.
    """
    def __init__(self, host, port, tls=False, source=None):
        sock = socket.create_connection((host, port), timeout=5, source_address=source)
        if tls:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            # The fixture server uses a locally generated, self-signed certificate.
            context.check_hostname = False
            context.verify_mode = ssl.CERT_NONE
            sock = context.wrap_socket(sock, server_hostname=host)
        self.socket = sock
        self.reader = sock.makefile("rb")

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.reader.close()
        self.socket.close()

    def send(self, data):
        self.socket.sendall(data)

    def response(self):
        line = self.reader.readline()
        if not line:
            raise AssertionError("connection closed before response")
        status = int(line.split()[1])
        headers = {}
        while True:
            line = self.reader.readline()
            if not line:
                raise AssertionError("truncated response headers")
            if line == b"\r\n":
                break
            key, value = line.split(b":", 1)
            headers[key.lower()] = value.strip()
        body = b""
        if headers.get(b"transfer-encoding") == b"chunked":
            while True:
                size = int(self.reader.readline().strip(), 16)
                if size == 0:
                    assert self.reader.readline() == b"\r\n"
                    break
                part = self.reader.read(size)
                assert len(part) == size
                body += part
                assert self.reader.read(2) == b"\r\n"
        elif b"content-length" in headers:
            size = int(headers[b"content-length"])
            body = self.reader.read(size)
            assert len(body) == size, "truncated body"
        elif status not in (100, 204, 304):
            raise AssertionError("unframed persistent response")
        return status, headers, body


def request(path=b"/small.txt", method=b"GET", headers=b"", body=b""):
    """Construct exact bytes without a client library repairing malformed input."""
    return method + b" " + path + b" HTTP/1.1\r\nHost: localhost\r\n" + headers + b"\r\n" + body


def malformed_headers(connect):
    """Reject ambiguous/invalid framing and verify the process still serves requests."""
    for header in (
        b"X: bad\nY: value\r\n", b"Content-Length: 99999999999999999999999999999\r\n",
        b"Content-Length: -1\r\n", b"Content-Length: 1x\r\n",
        b"Content-Length: 1\r\nContent-Length: 2\r\n",
        b"Transfer-Encoding: chunked\r\nContent-Length: 0\r\n",
        b"Transfer-Encoding: chunked\r\n",
    ):
        with connect() as conn:
            conn.send(request(headers=header))
            assert conn.response()[0] == 400
        with connect() as conn:
            conn.send(request())
            assert conn.response()[0] == 200, "server did not survive malformed request"


def malformed_ranges(connect):
    """Ignore invalid ranges safely instead of overflowing or terminating a worker."""
    for value in (b"bytes", b"bytes=0-99999999999999999999999999999", b"bytes=1x-2"):
        with connect() as conn:
            conn.send(request(headers=b"Range: " + value + b"\r\n"))
            assert conn.response()[0] == 200


def pipelines(connect):
    """Keep request bodies and subsequent requests separate on one connection."""
    with connect() as conn:
        conn.send(request() + request())
        for _ in range(2):
            status, _, body = conn.response()
            assert status == 200 and body == b"HELLO WORLD"
    with connect() as conn:
        conn.send(request(b"/body_tests/raw.php", b"POST", b"Content-Length: 3\r\n", b"abc") + request())
        assert conn.response()[2] == b"abc", "next request leaked into CGI body"
        assert conn.response()[2] == b"HELLO WORLD", "next request lost"


def fragmented_body(connect):
    """Preserve all byte values across fragmented sends, CGI, and the next request."""
    payload = bytes(range(256)) * 513
    with connect() as conn:
        data = request(b"/body_tests/raw.php", b"POST", b"Content-Length: " + str(len(payload)).encode() + b"\r\n", payload)
        for start in range(0, len(data), 997):
            conn.send(data[start:start + 997])
        conn.send(request())
        assert conn.response()[2] == payload, "binary request/CGI response changed"
        assert conn.response()[2] == b"HELLO WORLD"


def php_bytes(connect):
    """Preserve binary CGI output and decode query parameters exactly once in PHP."""
    with connect() as conn:
        conn.send(request(b"/regression_bytes.php"))
        assert conn.response()[2] == b"first\nsecond\r\n\x00last\n"
    with connect() as conn:
        conn.send(request(b"/regression_query.php?name=a%26b&encoded=%2520&url=https://example.test/a//b"))
        assert conn.response()[2] == b'{"name":"a&b","encoded":"%20","url":"https://example.test/a//b"}'
    # Verify unverified client Authorization header does not forge REMOTE_USER or AUTH_TYPE
    with connect() as conn:
        conn.send(request(b"/regression_auth.php", headers=b"Authorization: Bearer admin\r\n"))
        status, _, body = conn.response()
        assert status == 200, f"regression_auth.php status {status}"
        assert body == b'{"auth_type":"","remote_user":"","http_authorization":"Bearer admin"}', (
            f"authenticated identity forged: {body!r}"
        )


def authorization(connect):
    """Apply the same denial to equivalent paths before filesystem lookup."""
    paths = [b"/private/file.txt", b"/./private/file.txt", b"/%2e/private/file.txt", b"/%5cprivate/file.txt", b"//private//file.txt"]
    if sys.platform in ("win32", "cygwin"):
        paths.append(b"/PRIVATE/file.txt")
    for path in paths:
        with connect() as conn:
            conn.send(request(path))
            assert conn.response()[0] == 403, f"access rule bypass: {path!r}"

    if sys.platform in ("win32", "cygwin"):
        with connect() as conn:
            conn.send(request(b"/private/FILE~1.TXT"))
            assert conn.response()[0] == 400

    # Verify access denial cannot be bypassed through resource aliases
    with connect() as conn:
        conn.send(request(b"/path_info_denied.php"))
        assert conn.response()[0] == 403, "direct PHP access not denied"
    with connect() as conn:
        conn.send(request(b"/path_info_denied.php/extra"))
        assert conn.response()[0] == 403, "PHP PATH_INFO alias bypassed access denial"

    with connect() as conn:
        conn.send(request(b"/indexed_denied/index.html"))
        assert conn.response()[0] == 403, "direct index.html access not denied"
    with connect() as conn:
        conn.send(request(b"/indexed_denied/"))
        assert conn.response()[0] == 403, "directory index alias bypassed access denial"

    # Verify directory listing restrictions apply to unslashed directory URLs
    with connect() as conn:
        conn.send(request(b"/range_directory"))
        assert conn.response()[0] == 403, "unslashed directory URL bypassed directory index restriction"

    # Verify symlink aliases to denied resources and restricted listings
    if HAS_SYMLINK_FIXTURE:
        with connect() as conn:
            conn.send(request(b"/private_alias/file.txt"))
            assert conn.response()[0] == 403, "symlink alias bypassed access denial"
        with connect() as conn:
            conn.send(request(b"/range_alias/"))
            assert conn.response()[0] == 403, "symlink alias bypassed directory index restriction"


def redirect_injection(connect):
    """Keep encoded CR/LF in Location rather than creating a new response header."""
    query = b"?x=%0d%0aX-Injected:%20yes"
    with connect() as conn:
        conn.send(request(b"/redirect_from/foo.txt" + query))
        status, headers, _ = conn.response()
        assert status == 301
        assert b"x-injected" not in headers
        assert headers[b"location"] == b"/redirect_to/foo.txt" + query


def directory_range(connect):
    """Read an inclusive range from a generated in-memory directory listing."""
    with connect() as conn:
        conn.send(request(b"/range_directory/"))
        status, _, entire = conn.response()
        assert status == 200 and len(entire) > 10
    with connect() as conn:
        conn.send(request(b"/range_directory/", headers=b"Range: bytes=1-3\r\n"))
        status, headers, body = conn.response()
        assert status == 206 and headers[b"content-length"] == b"3"
        assert body == entire[1:4]


def keep_alive_limit(connect):
    """Advertise and enforce closure on the last permitted persistent request."""
    with connect() as conn:
        conn.send(request() * 100)
        for i in range(100):
            status, headers, body = conn.response()
            assert status == 200 and body == b"HELLO WORLD"
            assert headers[b"connection"] == (b"close" if i == 99 else b"keep-alive")
        assert conn.reader.read(1) == b"", "connection not closed at advertised limit"


def header_limit(connect):
    """Bound oversized request headers with 431 instead of unbounded buffering."""
    with connect() as conn:
        conn.send(request(headers=b"X-Large: " + b"a" * 65536 + b"\r\n"))
        assert conn.response()[0] == 431


def concurrent_clients(connect):
    """Exercise simultaneous requests and client-address ownership."""
    def one(_):
        with connect() as conn:
            conn.send(request(b"/regression_ip.php"))
            status, _, body = conn.response()
            assert status == 200 and body in (b"127.0.0.1", b"::1")
    with ThreadPoolExecutor(max_workers=16) as pool:
        list(pool.map(one, range(16)))


def connection_options(connect):
    """Combine repeated Connection fields and give close precedence over keep-alive."""
    for headers in (
        b"Connection: keep-alive\r\nConnection: close\r\n",
        b"Connection: Keep-Alive, ClOsE\r\n",
    ):
        with connect() as conn:
            conn.send(request(headers=headers))
            status, response_headers, body = conn.response()
            assert status == 200 and body == b"HELLO WORLD"
            assert response_headers[b"connection"] == b"close"
            assert conn.reader.read(1) == b"", "close option was ignored"

    # Unknown options do not disable HTTP/1.1's default persistence.
    with connect() as conn:
        conn.send(request(headers=b"Connection: example-option\r\n") + request())
        assert conn.response()[1][b"connection"] == b"keep-alive"
        assert conn.response()[2] == b"HELLO WORLD"

    # HTTP/1.0 requires explicit keep-alive, which can appear in a token list.
    with connect() as conn:
        first = request(headers=b"Connection: example-option, Keep-Alive\r\n")
        conn.send(first.replace(b"HTTP/1.1", b"HTTP/1.0") + request())
        assert conn.response()[1][b"connection"] == b"keep-alive"
        assert conn.response()[2] == b"HELLO WORLD"


def simple_request(connect):
    """HTTP/0.9 ends after one CRLF and returns only a body before closing."""
    with connect() as conn:
        conn.send(b"GET /small.txt\r\n")
        assert conn.reader.read() == b"HELLO WORLD", "simple request waited for headers"


def expect_100_continue(connect):
    """Send 100 Continue interim response before consuming request body."""
    # Test valid 100-continue expectation
    with connect() as conn:
        conn.send(b"POST /body_tests/raw.php HTTP/1.1\r\nHost: localhost\r\nExpect: 100-continue\r\nContent-Length: 5\r\n\r\n")
        status, _, _ = conn.response()
        assert status == 100, f"Expected 100 Continue, got {status}"
        conn.send(b"hello")
        status, _, body = conn.response()
        assert status == 200 and body == b"hello", f"Expected 200 hello, got {status} {body}"

    # Test unsupported expectation returns 417 Expectation Failed
    with connect() as conn:
        conn.send(b"POST /body_tests/raw.php HTTP/1.1\r\nHost: localhost\r\nExpect: unsupported-expectation\r\nContent-Length: 5\r\n\r\n")
        status, _, _ = conn.response()
        assert status == 417, f"Expected 417 Expectation Failed, got {status}"


WIRE_TESTS = (
    malformed_headers, malformed_ranges, pipelines, fragmented_body, php_bytes,
    authorization, redirect_injection, directory_range, keep_alive_limit,
    header_limit, concurrent_clients, connection_options, simple_request,
    expect_100_continue,
)
# Each group runs in four transport combinations, plus two mixed-source-IP groups.
WIRE_TEST_COUNT = len(WIRE_TESTS) * 4 + 2


def run_wire_regressions(host, host_v6, port, tls_port):
    """Run each group over IPv4/IPv6 and HTTP/HTTPS; return groups passed."""
    global HAS_SYMLINK_FIXTURE
    symlink_private = os.path.join(os.path.dirname(__file__), "root", "private_alias")
    symlink_range = os.path.join(os.path.dirname(__file__), "root", "range_alias")
    created_symlinks = []
    try:
        for alias_path, target in ((symlink_private, "private"), (symlink_range, "range_directory")):
            if os.path.islink(alias_path) or os.path.exists(alias_path):
                os.remove(alias_path)
            os.symlink(target, alias_path)
            created_symlinks.append(alias_path)
        HAS_SYMLINK_FIXTURE = True
    except (OSError, NotImplementedError):
        HAS_SYMLINK_FIXTURE = False

    try:
        def run_transport(item):
            address, tls = item
            passed_in_transport = 0
            connect = lambda source=None: Connection(address, tls_port if tls else port, tls, source)
            for test in WIRE_TESTS:
                try:
                    test(connect)
                    passed_in_transport += 1
                except Exception as error:
                    print(f"[Regression failure] {test.__name__} {address} TLS={tls}: {error}")
            return passed_in_transport

        passed = 0
        combos = [(host, False), (host, True), (host_v6, False), (host_v6, True)]
        with ThreadPoolExecutor(max_workers=4) as pool:
            passed += sum(pool.map(run_transport, combos))

        # Different source addresses expose queued tasks accidentally sharing one IP buffer.
        # 127.0.0.2 is usable without additional interface setup on Linux and Windows.
        def check_ip_ownership(tls):
            def one(index):
                source = "127.0.0.1" if index % 2 == 0 else "127.0.0.2"
                with Connection(host, tls_port if tls else port, tls, (source, 0)) as conn:
                    conn.send(request(b"/regression_ip.php"))
                    status, _, body = conn.response()
                    assert status == 200 and body == source.encode(), (source, status, body)
            with ThreadPoolExecutor(max_workers=8) as pool:
                list(pool.map(one, range(12)))
            return 1

        with ThreadPoolExecutor(max_workers=2) as pool:
            for ok in pool.map(check_ip_ownership, (False, True)):
                passed += ok
        return passed
    finally:
        for alias_path in created_symlinks:
            if os.path.islink(alias_path):
                try:
                    os.remove(alias_path)
                except OSError:
                    pass
        HAS_SYMLINK_FIXTURE = False
