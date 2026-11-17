"""Exercise IP rate limiting, accepted connection queue ceiling, and slow-client deadlines."""

import socket
import ssl
import time
import unittest
from unittest.mock import Mock

from harness import ServerTestCase, tls_context

# Server-side closes surface as different errors across Linux and Windows
CLOSED_ERRORS = (ConnectionAbortedError, ConnectionResetError, BrokenPipeError)
TIMEOUTS = (TimeoutError, socket.timeout)

CHUNK = b"x" * 1000
POST_HEAD = "POST /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: {}\r\n\r\n"


# Returns True if server closes sock while chunks trickle in, one per interval
def trickle_until_closed(sock, chunks, interval=0.5, errors=CLOSED_ERRORS) -> bool:
    for chunk in chunks:
        time.sleep(interval)
        try:
            sock.send(chunk)
        except errors:
            return True

    # Nothing failed while sending, so closure must show on read (timeout means still open)
    sock.settimeout(2.0)
    try:
        return sock.recv(1024) == b""
    except errors:
        return True
    except TIMEOUTS:
        return False


class RateLimitTests(ServerTestCase):
    # Used to start instance w/ token bucket limits and optional whitelist CIDRs
    def serve_limited(self, rate, burst, whitelist=(), **conf_opts):
        entries = "".join(f"<Whitelist> {cidr} </Whitelist>" for cidr in whitelist)
        self.serve(f'<RateLimit enabled="on" rate="{rate}" burst="{burst}">{entries}</RateLimit>', **conf_opts)

    # Test burst limits, 429 response, Retry-After header, and recovery
    def test_burst_and_recovery(self):
        self.serve_limited(rate=2.0, burst=2.0)

        self.assertEqual(self.fetch("/small.txt")[0], 200)
        self.assertEqual(self.fetch("/small.txt")[0], 200)

        status, headers, body = self.fetch("/small.txt")
        self.assertEqual(status, 429)
        self.assertGreaterEqual(int(headers["retry-after"]), 1)
        self.assertEqual(headers.get("connection"), "close")
        self.assertIn("Too Many Requests", body)

        # Allow token bucket replenishment
        time.sleep(1.2)
        self.assertEqual(self.fetch("/small.txt")[0], 200)

    # Test whitelist bypass under restrictive burst limit
    def test_whitelist_bypass(self):
        self.serve_limited(rate=1.0, burst=1.0, whitelist=["127.0.0.1/32"])

        for _ in range(5):
            self.assertEqual(self.fetch("/small.txt")[0], 200)

    # Test rate limiting with GPCHashed privacy mode
    def test_gpc_hashed_mode(self):
        self.serve_limited(rate=2.0, burst=2.0, sec_mode="GPCHashed")

        results = [self.fetch("/small.txt", headers={"Sec-GPC": "1"}) for _ in range(3)]
        self.assertEqual([status for status, _, _ in results], [200, 200, 429])
        self.assertIn("retry-after", results[2][1])


class ConnectionQueueTests(ServerTestCase):
    # Used to start instance w/ 1 idle and 2 max workers, so tiny loads saturate the pool
    def serve_queue(self, timeout=2, backlog=10, **conf_opts):
        self.serve(f"<KeepAliveMaxTimeout> {timeout} </KeepAliveMaxTimeout>"
                   "<IdleThreadsPerChild> 1 </IdleThreadsPerChild>"
                   "<MaxThreadsPerChild> 2 </MaxThreadsPerChild>"
                   f"<MaxRequestBacklog> {backlog} </MaxRequestBacklog>", **conf_opts)

    # Used to assert server closed sock (abort/reset counts as closed)
    def assert_closed(self, sock, label, timeout=0.5):
        sock.settimeout(timeout)
        try:
            self.assertEqual(sock.recv(1024), b"", f"{label} should be closed by server")
        except CLOSED_ERRORS:
            pass
        except TIMEOUTS:
            self.fail(f"{label} was still open after {timeout}s")

    # Used to assert sock receives an HTTP response
    def assert_responds(self, sock, label):
        self.assertTrue(sock.recv(1024).startswith(b"HTTP/1.1 "), f"{label} should receive HTTP response")

    # Verify queued connections expire past keep-alive timeout while workers and new requests proceed
    def test_queued_connections_expire_on_timeout(self):
        self.serve_queue(timeout=2, backlog=50)

        # Keep both workers occupied w/ body streaming above minimum rate
        workers = [self.connect() for _ in range(2)]
        for sock in workers:
            sock.send(POST_HEAD.format(len(CHUNK) * 5).encode() + CHUNK)
        queued = [self.connect() for _ in range(10)]

        for _ in range(3):
            time.sleep(0.6)
            for sock in workers:
                sock.send(CHUNK)

        # Await keep-alive deadline expiration for queued tasks
        time.sleep(0.7)

        # Issue requests while expiry pruning and queue draining occur
        live = [self.connect(timeout=3.0) for _ in range(3)]
        for sock in live:
            sock.send(b"GET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")

        # Finish worker requests, then verify expired and live sockets
        for i, sock in enumerate(workers):
            sock.send(CHUNK)
            self.assert_responds(sock, f"Worker socket {i}")
        for i, sock in enumerate(queued):
            self.assert_closed(sock, f"Queued socket {i}")
        for i, sock in enumerate(live):
            self.assert_responds(sock, f"Live socket {i}")

    # Verify connections exceeding backlog ceiling are rejected immediately
    def test_queue_ceiling_rejects_excess_connections(self):
        self.serve_queue(timeout=10, backlog=3)

        # Complete one request first so startup probes are fully drained and the pool state is settled
        self.assertEqual(self.fetch("/small.txt")[0], 200)

        # Busy worker threads w/ partial bodies
        for _ in range(2):
            self.connect().send(b"POST /index.html HTTP/1.1\r\nHost: localhost\r\nContent-Length: 100\r\n\r\npart")
        time.sleep(0.3)

        # Fill queued connection backlog
        for _ in range(3):
            self.connect()
        time.sleep(0.3)

        # Generous window: a healthy server closes instantly, but CI may be CPU-starved
        for i in range(4):
            self.assert_closed(self.connect(), f"Excess connection {i}", timeout=3.0)

    # Returns plaintext or TLS client socket, closed on test cleanup
    def open_client(self, tls):
        if not tls:
            return self.connect()
        sock = tls_context().wrap_socket(self.connect(self.tls_port), server_hostname="localhost")
        self.addCleanup(sock.close)
        return sock

    # Used to verify a slow body is disconnected while a prompt body on a fresh connection completes
    def check_slow_body(self, length, byte, tls=False):
        self.serve_queue(timeout=2, tls=tls)
        errors = CLOSED_ERRORS + (ssl.SSLError,) if tls else CLOSED_ERRORS

        # Trickle body past keep-alive timeout, or far below minimum receive rate
        slow = self.open_client(tls)
        slow.send(POST_HEAD.format(length).encode())
        self.assertTrue(trickle_until_closed(slow, [byte] * 7, errors=errors),
                        "Slow request body should be disconnected by server")

        # Verify prompt request body completes normally
        prompt = self.open_client(tls)
        prompt.send(POST_HEAD.format(8).encode() + b"12345678")
        self.assert_responds(prompt, "Prompt request body")

    def test_slow_body_deadline_disconnects(self):
        self.check_slow_body(8, b"a")

    def test_slow_body_minimum_rate_disconnects(self):
        self.check_slow_body(10000, b"x")

    def test_slow_tls_body_deadline_disconnects(self):
        self.check_slow_body(8, b"a", tls=True)

    # Verify incomplete TLS record streaming cannot monopolize worker past deadline
    def test_slow_tls_incomplete_record_deadline_disconnects(self):
        self.serve_queue(timeout=2, tls=True)

        incoming, outgoing = ssl.MemoryBIO(), ssl.MemoryBIO()
        sslobj = tls_context().wrap_bio(incoming, outgoing, server_hostname="localhost")
        raw = self.connect(self.tls_port)

        # Complete TLS handshake over memory BIO
        while True:
            try:
                sslobj.do_handshake()
                break
            except ssl.SSLWantReadError:
                raw.sendall(outgoing.read())
                incoming.write(raw.recv(4096))
            except ssl.SSLWantWriteError:
                raw.sendall(outgoing.read())
        if out := outgoing.read():
            raw.sendall(out)

        # Send complete headers for a POST w/ declared body, then a single encrypted body record
        sslobj.write(POST_HEAD.format(100).encode())
        raw.sendall(outgoing.read())
        sslobj.write(b"0123456789abcdef0123456789abcdef")
        record = outgoing.read()

        # Send only the 5-byte TLS record header, then trickle record fragments past deadline
        raw.sendall(record[:5])
        fragments = [bytes([b]) for b in record[5:10]]
        self.assertTrue(trickle_until_closed(raw, fragments, interval=0.6),
                        "Fragmented incomplete TLS record stream should be disconnected by server")


# Closure detection must treat Windows aborts as closed without masking timeouts
class ClosureDetectionTests(unittest.TestCase):
    # Simulate abort while sending incomplete request body
    def test_aborted_send_counts_as_closed(self):
        sock = Mock()
        sock.send.side_effect = [None, ConnectionAbortedError("aborted")]
        self.assertTrue(trickle_until_closed(sock, [b"a", b"a"], interval=0))

    # Simulate abort while reading after slow transfer
    def test_aborted_receive_counts_as_closed(self):
        sock = Mock()
        sock.recv.side_effect = ConnectionAbortedError("aborted")
        self.assertTrue(trickle_until_closed(sock, [b"x"], interval=0))

    # Keep timeout distinct from confirmed connection closure
    def test_receive_timeout_is_not_closure(self):
        sock = Mock()
        sock.recv.side_effect = TimeoutError("still open")
        self.assertFalse(trickle_until_closed(sock, [b"x"], interval=0))


if __name__ == "__main__":
    unittest.main()
