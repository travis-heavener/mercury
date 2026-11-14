"""Exercise accepted connection queue ceiling and idle timeout expiry."""

import os
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import unittest

PORT = 8094
TLS_PORT = 8095
HOST = "127.0.0.1"


# Returns path to compiled Mercury server binary
def get_mercury_bin():
    script_dir = Path(__file__).resolve().parent
    root_dir = script_dir.parent
    bin_name = "mercury.exe" if sys.platform in ["win32", "cygwin"] else "mercury"
    return str(root_dir / "bin" / bin_name)


class ConnectionQueueTests(unittest.TestCase):
    # Used to write temporary configuration for queue testing
    def write_conf(self, timeout=2, idle_threads=1, max_threads=2, backlog=100, tls_port="off"):
        conf_lines = [
            "<Mercury>",
            "    <DocumentRoot> ./tests/root/ </DocumentRoot>",
            "    <BindAddressIPv4> 127.0.0.1 </BindAddressIPv4>",
            f"    <Port> {PORT} </Port>",
            f"    <TLSPort> {tls_port} </TLSPort>",
            "    <TLSCertFile> ./conf/ssl/cert.pem </TLSCertFile>",
            "    <TLSKeyFile> ./conf/ssl/key.pem </TLSKeyFile>",
            "    <EnableLegacyHTTPVersions> on </EnableLegacyHTTPVersions>",
            "    <AccessLogFile> ./logs/access.log </AccessLogFile>",
            "    <ErrorLogFile> ./logs/error.log </ErrorLogFile>",
            "    <StartupCheckLatestRelease> false </StartupCheckLatestRelease>",
            f"    <KeepAliveMaxTimeout> {timeout} </KeepAliveMaxTimeout>",
            f"    <IdleThreadsPerChild> {idle_threads} </IdleThreadsPerChild>",
            f"    <MaxThreadsPerChild> {max_threads} </MaxThreadsPerChild>",
            f"    <MaxRequestBacklog> {backlog} </MaxRequestBacklog>",
            '    <Match pattern=".*">',
            '        <Access mode="allow all" />',
            "    </Match>",
            "</Mercury>",
        ]
        temp = tempfile.NamedTemporaryFile(mode="w", suffix=".conf", delete=False)
        temp.write("\n".join(conf_lines))
        temp.close()
        self.addCleanup(lambda: os.path.exists(temp.name) and os.remove(temp.name))
        return temp.name

    # Used to start a background Mercury instance with given configuration
    def start_server(self, conf_path, port=PORT):
        proc = subprocess.Popen([get_mercury_bin(), "-c", conf_path],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.addCleanup(lambda: self.stop_server(proc))

        # Wait for server readiness
        for _ in range(50):
            try:
                sock = socket.create_connection((HOST, port), timeout=0.2)
                sock.close()
                return proc
            except Exception:
                if proc.poll() is not None:
                    break
                time.sleep(0.1)
        self.fail("Mercury failed to start for connection queue test")

    # Stop server process and ensure termination
    def stop_server(self, proc):
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

    # Verify queued connections expire when idle beyond keep-alive timeout
    def test_queued_connections_expire_on_timeout(self):
        conf = self.write_conf(timeout=2, idle_threads=1, max_threads=2, backlog=100)
        self.start_server(conf)
        socks = []
        try:
            for _ in range(12):
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(5)
                s.connect((HOST, PORT))
                socks.append(s)

            # Keep active worker threads occupied with body payload streaming above minimum rate
            chunk = b"x" * 1000
            headers = f"POST /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: {len(chunk) * 5}\r\n\r\n".encode() + chunk
            socks[0].send(headers)
            socks[1].send(headers)

            # Send body chunks above minimum rate while queued tasks expire
            for _ in range(3):
                time.sleep(0.6)
                socks[0].send(chunk)
                socks[1].send(chunk)

            # Await keep-alive deadline expiration for queued tasks
            time.sleep(0.7)

            # Assert queued connections expired and were closed by server
            for i in range(2, 12):
                socks[i].settimeout(0.5)
                try:
                    data = socks[i].recv(1024)
                    self.assertEqual(data, b"", f"Socket {i} should be closed by server")
                except (ConnectionResetError, BrokenPipeError):
                    pass

            # Complete requests on active workers and verify they remained occupied and finish normally
            socks[0].send(chunk)
            socks[1].send(chunk)
            for i in range(2):
                resp = socks[i].recv(1024)
                self.assertTrue(resp.startswith(b"HTTP/1.1 "), f"Worker socket {i} should receive HTTP response")
        finally:
            for s in socks:
                try: s.close()
                except: pass

    # Verify connections exceeding backlog ceiling are rejected immediately
    def test_queue_ceiling_rejects_excess_connections(self):
        conf = self.write_conf(timeout=10, idle_threads=1, max_threads=2, backlog=3)
        self.start_server(conf)
        socks = []
        try:
            # Busy worker threads
            for _ in range(2):
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.connect((HOST, PORT))
                s.send(b"POST /index.html HTTP/1.1\r\nHost: localhost\r\nContent-Length: 100\r\n\r\npart")
                socks.append(s)

            time.sleep(0.1)

            # Fill queued connection backlog
            for _ in range(3):
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.connect((HOST, PORT))
                socks.append(s)

            time.sleep(0.1)

            # Excess connections must be rejected immediately
            for _ in range(4):
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(1.0)
                s.connect((HOST, PORT))
                try:
                    data = s.recv(1024)
                    self.assertEqual(data, b"", "Excess connection should be closed immediately")
                except (ConnectionResetError, BrokenPipeError):
                    pass
                s.close()
        finally:
            for s in socks:
                try: s.close()
                except: pass

    # Verify concurrent queue modification and task execution during expiry pruning
    def test_concurrent_queue_modification_during_expiry(self):
        conf = self.write_conf(timeout=2, idle_threads=1, max_threads=2, backlog=50)
        self.start_server(conf)
        socks = []
        try:
            # Connect initial sockets to occupy worker threads
            chunk = b"x" * 1000
            headers = f"POST /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: {len(chunk) * 5}\r\n\r\n".encode() + chunk
            for _ in range(2):
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.connect((HOST, PORT))
                s.send(headers)
                socks.append(s)

            # Connect queued sockets that will wait in the queue
            for _ in range(6):
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.connect((HOST, PORT))
                socks.append(s)

            # Send body chunks on workers above minimum rate while queued tasks expire
            for _ in range(3):
                time.sleep(0.6)
                socks[0].send(chunk)
                socks[1].send(chunk)

            # Await queue deadline expiration for queued tasks
            time.sleep(0.7)

            # Send additional requests while expiry pruning and queue draining occur
            active_socks = []
            for _ in range(3):
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(3.0)
                s.connect((HOST, PORT))
                s.send(b"GET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                active_socks.append(s)

            # Complete requests on initial occupied workers
            socks[0].send(chunk)
            socks[1].send(chunk)
            for i in range(2):
                resp = socks[i].recv(1024)
                self.assertTrue(resp.startswith(b"HTTP/1.1 "), f"Worker socket {i} should receive response")

            # Verify queued connections expired and were closed by server
            for i in range(2, 8):
                socks[i].settimeout(0.5)
                try:
                    data = socks[i].recv(1024)
                    self.assertEqual(data, b"", f"Queued socket {i} should be closed")
                except (ConnectionResetError, BrokenPipeError):
                    pass

            # Verify active requests complete normally despite concurrent pruning
            for i, s in enumerate(active_socks):
                resp = s.recv(1024)
                self.assertTrue(resp.startswith(b"HTTP/1.1 "), f"Active socket {i} should receive response")
                s.close()
        finally:
            for s in socks:
                try: s.close()
                except: pass

    # Verify slow request bodies exceeding overall deadline or minimum rate are closed
    def test_slow_request_body_deadline_disconnects(self):
        conf = self.write_conf(timeout=2, idle_threads=1, max_threads=2, backlog=10)
        self.start_server(conf)
        try:
            # Trickle an eight-byte body past keep-alive timeout
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(5.0)
            s.connect((HOST, PORT))
            headers = b"POST /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: 8\r\n\r\n"
            s.send(headers)

            closed = False
            for _ in range(7):
                time.sleep(0.5)
                try:
                    s.send(b"a")
                except (ConnectionResetError, BrokenPipeError):
                    closed = True
                    break

            if not closed:
                s.settimeout(2.0)
                try:
                    data = s.recv(1024)
                    closed = (data == b"")
                except (ConnectionResetError, BrokenPipeError):
                    closed = True
                except (TimeoutError, socket.timeout):
                    closed = False

            self.assertTrue(closed, "Slow request body should be disconnected by server")
            s.close()

            # Verify prompt request body completes normally
            s2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s2.settimeout(5.0)
            s2.connect((HOST, PORT))
            s2.send(b"POST /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: 8\r\n\r\n12345678")
            resp = s2.recv(1024)
            self.assertTrue(resp.startswith(b"HTTP/1.1 "), "Prompt request body should receive HTTP response")
            s2.close()
        finally:
            pass

    # Verify request bodies falling below minimum receive rate are closed
    def test_slow_request_body_minimum_rate_disconnects(self):
        conf = self.write_conf(timeout=2, idle_threads=1, max_threads=2, backlog=10)
        self.start_server(conf)
        try:
            # Declare large body but trickle bytes below minimum receive rate
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(5.0)
            s.connect((HOST, PORT))
            headers = b"POST /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: 10000\r\n\r\n"
            s.send(headers)

            closed = False
            for _ in range(7):
                time.sleep(0.5)
                try:
                    s.send(b"x")
                except (ConnectionResetError, BrokenPipeError):
                    closed = True
                    break

            if not closed:
                s.settimeout(2.0)
                try:
                    data = s.recv(1024)
                    closed = (data == b"")
                except (ConnectionResetError, BrokenPipeError):
                    closed = True
                except (TimeoutError, socket.timeout):
                    closed = False

            self.assertTrue(closed, "Request body below minimum rate should be disconnected by server")
            s.close()
        finally:
            pass

    # Verify slow request bodies over TLS exceeding overall deadline are closed
    def test_slow_request_body_tls_deadline_disconnects(self):
        conf = self.write_conf(timeout=2, idle_threads=1, max_threads=2, backlog=10, tls_port=TLS_PORT)
        self.start_server(conf, port=TLS_PORT)
        try:
            # Trickle an eight-byte body over TLS past keep-alive timeout
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            raw = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            raw.settimeout(5.0)
            raw.connect((HOST, TLS_PORT))
            s = ctx.wrap_socket(raw, server_hostname="localhost")
            headers = b"POST /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: 8\r\n\r\n"
            s.send(headers)

            closed = False
            for _ in range(7):
                time.sleep(0.5)
                try:
                    s.send(b"a")
                except (ssl.SSLError, ConnectionResetError, BrokenPipeError):
                    closed = True
                    break

            if not closed:
                s.settimeout(2.0)
                try:
                    data = s.recv(1024)
                    closed = (data == b"")
                except (ssl.SSLError, ConnectionResetError, BrokenPipeError):
                    closed = True
                except (TimeoutError, socket.timeout):
                    closed = False

            self.assertTrue(closed, "Slow request body over TLS should be disconnected by server")
            try: s.close()
            except: pass

            # Verify prompt request body over TLS completes normally
            raw2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            raw2.settimeout(5.0)
            raw2.connect((HOST, TLS_PORT))
            s2 = ctx.wrap_socket(raw2, server_hostname="localhost")
            s2.send(b"POST /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: 8\r\n\r\n12345678")
            resp = s2.recv(1024)
            self.assertTrue(resp.startswith(b"HTTP/1.1 "), "Prompt TLS request body should receive HTTP response")
            s2.close()
        finally:
            pass

    # Verify incomplete TLS record streaming cannot monopolize worker past deadline
    def test_slow_tls_incomplete_record_deadline_disconnects(self):
        conf = self.write_conf(timeout=2, idle_threads=1, max_threads=2, backlog=10, tls_port=TLS_PORT)
        self.start_server(conf, port=TLS_PORT)
        try:
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE

            incoming = ssl.MemoryBIO()
            outgoing = ssl.MemoryBIO()
            sslobj = ctx.wrap_bio(incoming, outgoing, server_hostname="localhost")

            raw = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            raw.settimeout(5.0)
            raw.connect((HOST, TLS_PORT))

            # Complete TLS handshake over memory BIO
            while True:
                try:
                    sslobj.do_handshake()
                    break
                except ssl.SSLWantReadError:
                    out = outgoing.read()
                    if out:
                        raw.sendall(out)
                    inc = raw.recv(4096)
                    incoming.write(inc)
                except ssl.SSLWantWriteError:
                    out = outgoing.read()
                    if out:
                        raw.sendall(out)
            out = outgoing.read()
            if out:
                raw.sendall(out)

            # Send complete request headers for a POST with declared body
            sslobj.write(b"POST /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: 100\r\n\r\n")
            raw.sendall(outgoing.read())

            # Encrypt a single TLS body record
            sslobj.write(b"0123456789abcdef0123456789abcdef")
            record = outgoing.read()

            # Send only the 5-byte TLS record header to create an incomplete record
            raw.sendall(record[:5])

            # Trickle fragments of the encrypted record past keep-alive deadline
            closed = False
            for b in record[5:10]:
                time.sleep(0.6)
                try:
                    raw.send(bytes([b]))
                except (ConnectionResetError, BrokenPipeError):
                    closed = True
                    break

            if not closed:
                raw.settimeout(2.0)
                try:
                    data = raw.recv(1024)
                    closed = (data == b"")
                except (ConnectionResetError, BrokenPipeError):
                    closed = True
                except (TimeoutError, socket.timeout):
                    closed = False

            self.assertTrue(closed, "Fragmented incomplete TLS record stream should be disconnected by server")
            try: raw.close()
            except: pass
        finally:
            pass


# Run compact test suite output when called from run.py
def run_compact_suite():
    suite = unittest.TestLoader().loadTestsFromTestCase(ConnectionQueueTests)
    total = suite.countTestCases()
    print("=" * 80)
    print(f"Starting run: Connection Queue Tests ({total} tests)")
    is_tty = sys.stdout.isatty()

    class ProgressResult(unittest.TestResult):
        def stopTest(self, test):
            super().stopTest(test)
            if is_tty:
                sys.stdout.write(f"\r  Running tests... [{self.testsRun}/{total}]")
                sys.stdout.flush()

    result = ProgressResult()
    result.startTestRun()
    try:
        suite.run(result)
    finally:
        result.stopTestRun()

    if is_tty:
        sys.stdout.write("\r\033[K")
        sys.stdout.flush()

    if result.wasSuccessful():
        print(f"✅ Passing", flush=True)
    else:
        for label, entries in (("failure", result.failures), ("error", result.errors)):
            for test, details in entries:
                print(f"[Queue {label}] {test}\n{details}", end="")
        print(f"❌ Failing ({len(result.failures)} failures, {len(result.errors)} errors)", flush=True)
    return result.wasSuccessful()


if __name__ == "__main__":
    if sys.argv[1:] == ["--mercury-output"]:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.exit(0 if run_compact_suite() else 1)
    unittest.main()
