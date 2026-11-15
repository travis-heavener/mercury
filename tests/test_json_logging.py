"""Exercise structured JSON logging."""

import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest

PORT_BASE = 8093
HOST = "127.0.0.1"
_port_counter = PORT_BASE


# Returns unique loopback port for each test case to prevent TIME_WAIT rebind collisions
def get_next_port():
    global _port_counter
    _port_counter += 1
    return _port_counter


# Returns path to compiled Mercury server binary
def get_mercury_bin():
    script_dir = Path(__file__).resolve().parent
    root_dir = script_dir.parent
    bin_name = "mercury.exe" if sys.platform in ["win32", "cygwin"] else "mercury"
    return str(root_dir / "bin" / bin_name)


class JsonLoggingTests(unittest.TestCase):
    # Setup test instance w/ dedicated loopback port
    def setUp(self):
        self.port = get_next_port()

    # Used to write temporary configuration for JSON logging tests
    def write_conf(self, access_log, error_log, access_format="json", error_format="json",
                   sec_mode="Minimal"):
        # Assemble minimal configuration lines
        conf_lines = [
            "<Mercury>",
            "    <DocumentRoot> ./tests/root/ </DocumentRoot>",
            "    <BindAddressIPv4> 0.0.0.0 </BindAddressIPv4>",
            f"    <Port> {self.port} </Port>",
            "    <TLSPort> off </TLSPort>",
            "    <StartupCheckLatestRelease> false </StartupCheckLatestRelease>",
            "    <EnableLegacyHTTPVersions> on </EnableLegacyHTTPVersions>",
            f"    <ClientSecurityMode> {sec_mode} </ClientSecurityMode>",
            "    <ClientSecurityIPSalt> fAhGD0OnCe4YhSyx </ClientSecurityIPSalt>",
        ]

        # Attach access log directive w/ optional format attribute
        if access_format:
            conf_lines.append(f'    <AccessLogFile format="{access_format}"> {access_log} </AccessLogFile>')
        else:
            conf_lines.append(f'    <AccessLogFile> {access_log} </AccessLogFile>')

        # Attach error log directive w/ optional format attribute
        if error_format:
            conf_lines.append(f'    <ErrorLogFile format="{error_format}"> {error_log} </ErrorLogFile>')
        else:
            conf_lines.append(f'    <ErrorLogFile> {error_log} </ErrorLogFile>')

        # Append default permissive routing rule
        conf_lines.extend([
            '    <Match pattern=".*">',
            '        <Access mode="allow all" />',
            "    </Match>",
            "</Mercury>",
        ])

        # Write configuration to temporary file
        temp = tempfile.NamedTemporaryFile(mode="w", suffix=".conf", delete=False)
        temp.write("\n".join(conf_lines))
        temp.close()
        self.addCleanup(lambda: os.path.exists(temp.name) and os.remove(temp.name))
        return temp.name

    # Used to start background Mercury instance and wait for socket readiness
    def start_server(self, conf_path):
        proc = subprocess.Popen([get_mercury_bin(), "-c", conf_path],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.addCleanup(lambda: self.stop_server(proc))

        # Wait for server listener readiness over loopback
        for _ in range(50):
            if proc.poll() is not None:
                _, err = proc.communicate()
                raise RuntimeError(f"Server exited early with code {proc.returncode}: {err.decode('utf-8', errors='replace')}")
            try:
                sock = socket.create_connection((HOST, self.port), timeout=0.2)
                sock.close()
                return proc
            except (ConnectionRefusedError, OSError):
                time.sleep(0.05)

        raise RuntimeError(f"Server failed to start on port {self.port}")

    # Used to terminate background Mercury process gracefully
    def stop_server(self, proc):
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            time.sleep(0.05)
        # Close standard streams to avoid resource leaks
        if proc.stdout:
            proc.stdout.close()
        if proc.stderr:
            proc.stderr.close()

    # Returns raw HTTP response string for given wire request
    def send_raw_request(self, raw_req):
        sock = socket.create_connection((HOST, self.port), timeout=2)
        try:
            # Transmit wire bytes to server
            sock.sendall(raw_req.encode("latin1"))
            data = b""
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                data += chunk
                if b"\r\n\r\n" in data:
                    break
            return data.decode("latin1", errors="replace")
        finally:
            sock.close()

    # Verify access log emits valid RFC 8259 single-line JSON records w/ metadata and timestamps
    def test_access_log_emits_valid_json_with_metadata(self):
        # Stage temporary log files for access and error outputs
        with tempfile.NamedTemporaryFile(suffix=".log", delete=False) as acc_file, \
             tempfile.NamedTemporaryFile(suffix=".log", delete=False) as err_file:
            acc_path = acc_file.name
            err_path = err_file.name
        self.addCleanup(lambda: os.path.exists(acc_path) and os.remove(acc_path))
        self.addCleanup(lambda: os.path.exists(err_path) and os.remove(err_path))

        # Start server w/ JSON access log format
        conf = self.write_conf(acc_path, err_path, access_format="json")
        self.start_server(conf)

        # Transmit HTTP request containing metadata headers
        req = (
            "GET /index.html HTTP/1.1\r\n"
            f"Host: {HOST}:{self.port}\r\n"
            "User-Agent: MercuryTestClient/1.0\r\n"
            "Referer: https://example.com/home\r\n"
            "Connection: close\r\n\r\n"
        )
        resp = self.send_raw_request(req)
        self.assertIn("200 OK", resp)

        # Allow logger worker thread time to flush buffer to disk
        time.sleep(0.3)

        # Read emitted log records
        with open(acc_path, "r", encoding="utf-8") as f:
            lines = [l.strip() for l in f if l.strip()]

        self.assertGreater(len(lines), 0)
        # Extract request log entry by method identifier
        req_lines = [l for l in lines if '"method":"GET"' in l]
        self.assertGreater(len(req_lines), 0)

        # Parse log entry as RFC 8259 JSON object
        entry = json.loads(req_lines[-1])
        self.assertEqual(entry["method"], "GET")
        self.assertEqual(entry["uri"], "/index.html")
        self.assertEqual(entry["status"], 200)
        self.assertEqual(entry["level"], "info")
        self.assertIsInstance(entry["bytes_sent"], int)
        self.assertGreater(entry["bytes_sent"], 0)
        self.assertEqual(entry["http_version"], "HTTP/1.1")
        self.assertEqual(entry["user_agent"], "MercuryTestClient/1.0")
        self.assertEqual(entry["referer"], "https://example.com/home")
        self.assertIsInstance(entry["latency_ms"], (int, float))
        self.assertGreaterEqual(entry["latency_ms"], 0.0)

        # Validate ISO 8601 UTC timestamp format (YYYY-MM-DDTHH:MM:SS.sssZ)
        iso_regex = r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$"
        self.assertRegex(entry["timestamp"], iso_regex)

    # Verify startup and listener events expose structured metadata
    def test_structured_startup_and_listener_events(self):
        with tempfile.TemporaryDirectory() as directory:
            access = Path(directory) / "access.log"
            error = Path(directory) / "error.log"
            proc = self.start_server(self.write_conf(access, error))
            self.stop_server(proc)
            entries = [json.loads(line) for line in access.read_text(encoding="utf-8").splitlines()]
            self.assertTrue(all(entry.get("event") for entry in entries))
            self.assertTrue(all(entry.get("level") for entry in entries))
            self.assertTrue(any(entry.get("event") == "shutdown" for entry in entries))
            listeners = [entry for entry in entries if entry.get("event") == "listen"]
            listener = next(entry for entry in listeners if entry["protocol"] == "tcp4")
            self.assertEqual(listener["port"], self.port)
            self.assertIs(listener["tls"], False)
            self.assertEqual(listener["level"], "info")
            self.assertEqual(listener["message"], f"Listening on port {self.port} (IPv4).")
            startup = next(entry for entry in entries if entry.get("event") == "startup")
            self.assertEqual(startup["level"], "info")
            version_file = Path(__file__).resolve().parent.parent / "version.txt"
            self.assertEqual(startup["version"], version_file.read_text(encoding="utf-8").strip().removeprefix("Mercury v"))
            self.assertIn("started successfully", startup["message"])

    # Verify control characters, quotes, and newlines in headers/URIs do not break JSON framing
    def test_json_injection_defense(self):
        # Stage temporary log files
        with tempfile.NamedTemporaryFile(suffix=".log", delete=False) as acc_file, \
             tempfile.NamedTemporaryFile(suffix=".log", delete=False) as err_file:
            acc_path = acc_file.name
            err_path = err_file.name
        self.addCleanup(lambda: os.path.exists(acc_path) and os.remove(acc_path))
        self.addCleanup(lambda: os.path.exists(err_path) and os.remove(err_path))

        # Launch server configured w/ JSON access logging
        conf = self.write_conf(acc_path, err_path, access_format="json")
        self.start_server(conf)

        # Craft malicious injection payloads containing quotes, control characters, tabs, and escapes
        injected_ua = 'Evil"Agent\\with\t"and_quotes"\xff'
        injected_ref = 'https://example.com/path?query="test"\\escape'

        # Send request carrying unescaped user-controlled header values
        req = (
            "GET /index.html?test=value&quote=\"1\" HTTP/1.1\r\n"
            f"Host: {HOST}:{self.port}\r\n"
            f"User-Agent: {injected_ua}\r\n"
            f"Referer: {injected_ref}\r\n"
            "Connection: close\r\n\r\n"
        )
        self.assertIn("200 OK", self.send_raw_request(req))
        time.sleep(0.3)

        # Ingest emitted access log lines
        with open(acc_path, "r", encoding="utf-8") as f:
            lines = [l.strip() for l in f if l.strip()]

        self.assertTrue(any('"method":"GET"' in line for line in lines), "Missing request log record")

        # Assert every emitted record parses as valid JSON w/ escaped payloads intact
        for line in lines:
            try:
                parsed = json.loads(line)
            except json.JSONDecodeError as exc:
                self.fail(f"Log line failed JSON decode: {line} ({exc})")

            # Verify sanitized fields contain decoded injection contents
            if parsed.get("method") == "GET":
                self.assertEqual(parsed["user_agent"], injected_ua)
                self.assertIn('"and_quotes"', parsed["user_agent"])
                self.assertIn("https://example.com/path", parsed["referer"])

    # Include request body receive time in logged latency
    def test_latency_includes_body_receive(self):
        with tempfile.TemporaryDirectory() as directory:
            access = Path(directory) / "access.log"
            error = Path(directory) / "error.log"
            proc = self.start_server(self.write_conf(access, error))
            with socket.create_connection((HOST, self.port), timeout=2) as sock:
                sock.sendall(b"POST /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: 2\r\n\r\nx")
                time.sleep(0.2)
                sock.sendall(b"y")
                while sock.recv(4096):
                    pass
            self.stop_server(proc)
            entries = [json.loads(line) for line in access.read_text(encoding="utf-8").splitlines()]
            requests = [entry for entry in entries if entry.get("method") == "POST"]
            self.assertEqual(len(requests), 1)
            self.assertGreaterEqual(requests[0]["latency_ms"], 150)

    # Exclude keep-alive idle time while retaining fragmented header receive time
    def test_latency_excludes_keep_alive_idle(self):
        with tempfile.TemporaryDirectory() as directory:
            access = Path(directory) / "access.log"
            error = Path(directory) / "error.log"
            proc = self.start_server(self.write_conf(access, error))
            conn = http.client.HTTPConnection(HOST, self.port, timeout=3)
            try:
                conn.request("HEAD", "/index.html")
                response = conn.getresponse()
                self.assertEqual(response.status, 200)
                response.read()
                self.assertIsNotNone(conn.sock)

                # Leave connection idle before sending fragmented second request
                time.sleep(1)
                started = time.monotonic()
                conn.sock.sendall(b"HEAD /index.html HTTP/1.1\r\nHost: localhost\r\n")
                time.sleep(0.2)
                conn.sock.sendall(b"Connection: close\r\n\r\n")
                response = http.client.HTTPResponse(conn.sock, method="HEAD")
                response.begin()
                self.assertEqual(response.status, 200)
                response.read()
                response.close()
                elapsed_ms = (time.monotonic() - started) * 1000
            finally:
                conn.close()
                self.stop_server(proc)
            entries = [json.loads(line) for line in access.read_text(encoding="utf-8").splitlines()]
            requests = [entry for entry in entries if entry.get("method") == "HEAD"]
            self.assertEqual(len(requests), 2)
            self.assertGreaterEqual(requests[1]["latency_ms"], 150)
            self.assertLess(requests[1]["latency_ms"], elapsed_ms + 250)

    # Reject metrics configuration until exporter implementation becomes available
    def test_unimplemented_metrics_fails_config(self):
        with tempfile.TemporaryDirectory() as directory:
            access = Path(directory) / "access.log"
            error = Path(directory) / "error.log"
            conf = Path(self.write_conf(access, error))
            conf.write_text(conf.read_text().replace(
                "</Mercury>", '<Metrics enabled="true" path="/metrics" /></Mercury>'))
            proc = subprocess.run([get_mercury_bin(), "-c", str(conf)],
                                  capture_output=True, timeout=3)
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn(b"Invalid node name in Mercury root node: Metrics", proc.stderr)

    # Verify GPC/DNT privacy headers mask client IPs in JSON log records
    def test_gpc_masking_in_json_log(self):
        # Stage temporary log paths
        with tempfile.NamedTemporaryFile(suffix=".log", delete=False) as acc_file, \
             tempfile.NamedTemporaryFile(suffix=".log", delete=False) as err_file:
            acc_path = acc_file.name
            err_path = err_file.name
        self.addCleanup(lambda: os.path.exists(acc_path) and os.remove(acc_path))
        self.addCleanup(lambda: os.path.exists(err_path) and os.remove(err_path))

        # Start server w/ GPC masking enabled in client security settings
        conf = self.write_conf(acc_path, err_path, access_format="json", sec_mode="GPCMasked")
        self.start_server(conf)

        # Transmit request w/ Global Privacy Control signal
        req = (
            "GET /index.html HTTP/1.1\r\n"
            f"Host: {HOST}:{self.port}\r\n"
            "Sec-GPC: 1\r\n"
            "Connection: close\r\n\r\n"
        )
        self.send_raw_request(req)
        time.sleep(0.3)

        # Read access log contents
        with open(acc_path, "r", encoding="utf-8") as f:
            lines = [l.strip() for l in f if l.strip()]

        # Extract GET request record
        req_lines = [l for l in lines if '"method":"GET"' in l]
        self.assertGreater(len(req_lines), 0)
        entry = json.loads(req_lines[-1])

        # Assert client IP lower 8 bits masked to zero per privacy policy
        self.assertEqual(entry["client_ip"], "127.0.0.0")

    # Verify unsupported log format attribute fails configuration validation
    def test_invalid_format_attribute_fails_config(self):
        # Stage temporary log paths
        with tempfile.NamedTemporaryFile(suffix=".log", delete=False) as acc_file, \
             tempfile.NamedTemporaryFile(suffix=".log", delete=False) as err_file:
            acc_path = acc_file.name
            err_path = err_file.name
        self.addCleanup(lambda: os.path.exists(acc_path) and os.remove(acc_path))
        self.addCleanup(lambda: os.path.exists(err_path) and os.remove(err_path))

        # Specify unsupported format attribute
        conf = self.write_conf(acc_path, err_path, access_format="yaml")
        proc = subprocess.Popen([get_mercury_bin(), "-c", conf],
                                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        _, stderr = proc.communicate(timeout=2)

        # Verify startup rejection and error message output
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn(b"invalid format attribute for AccessLogFile", stderr)

    # Verify omitting format attribute preserves legacy combined text format
    def test_text_format_preserved_by_default(self):
        # Stage temporary log files
        with tempfile.NamedTemporaryFile(suffix=".log", delete=False) as acc_file, \
             tempfile.NamedTemporaryFile(suffix=".log", delete=False) as err_file:
            acc_path = acc_file.name
            err_path = err_file.name
        self.addCleanup(lambda: os.path.exists(acc_path) and os.remove(acc_path))
        self.addCleanup(lambda: os.path.exists(err_path) and os.remove(err_path))

        # Configure server without format attributes
        conf = self.write_conf(acc_path, err_path, access_format=None, error_format=None)
        self.start_server(conf)

        # Transmit test request
        req = (
            "GET /index.html HTTP/1.1\r\n"
            f"Host: {HOST}:{self.port}\r\n"
            "Connection: close\r\n\r\n"
        )
        self.send_raw_request(req)
        time.sleep(0.3)

        # Read text log lines
        with open(acc_path, "r", encoding="utf-8") as f:
            lines = [l.strip() for l in f if l.strip()]

        req_lines = [l for l in lines if "GET" in l]
        self.assertGreater(len(req_lines), 0)
        # Verify text format begins w/ timestamp bracket rather than JSON brace
        self.assertTrue(req_lines[-1].startswith("["))
        self.assertIn("GET 127.0.0.1 /index.html -- (200) [HTTP/1.1]", req_lines[-1])

    # Verify explicit format="text" attribute functions identically to default
    def test_explicit_format_text(self):
        # Stage temporary log files
        with tempfile.NamedTemporaryFile(suffix=".log", delete=False) as acc_file, \
             tempfile.NamedTemporaryFile(suffix=".log", delete=False) as err_file:
            acc_path = acc_file.name
            err_path = err_file.name
        self.addCleanup(lambda: os.path.exists(acc_path) and os.remove(acc_path))
        self.addCleanup(lambda: os.path.exists(err_path) and os.remove(err_path))

        # Configure server w/ explicit format="text" attributes
        conf = self.write_conf(acc_path, err_path, access_format="text", error_format="text")
        self.start_server(conf)

        # Transmit test request
        req = (
            "GET /index.html HTTP/1.1\r\n"
            f"Host: {HOST}:{self.port}\r\n"
            "Connection: close\r\n\r\n"
        )
        self.send_raw_request(req)
        time.sleep(0.3)

        # Read log lines and verify text framing
        with open(acc_path, "r", encoding="utf-8") as f:
            lines = [l.strip() for l in f if l.strip()]

        req_lines = [l for l in lines if "GET" in l]
        self.assertGreater(len(req_lines), 0)
        self.assertTrue(req_lines[-1].startswith("["))


# Run compact test suite output when called from run.py
def run_compact_suite():
    # Load and execute all test cases defined in JsonLoggingTests
    suite = unittest.TestLoader().loadTestsFromTestCase(JsonLoggingTests)
    total = suite.countTestCases()
    print(f"Starting run: Structured JSON Logging Tests ({total} tests)", flush=True)
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
        print("✅ Passing", flush=True)
    else:
        for label, entries in (("failure", result.failures), ("error", result.errors)):
            for test, details in entries:
                print(f"[JSON logging {label}] {test}\n{details}", end="")
        print(f"❌ Failing ({len(result.failures)} failures, {len(result.errors)} errors)", flush=True)
    return result.wasSuccessful()


if __name__ == "__main__":
    # Handle compact run flag from test runner
    if sys.argv[1:] == ["--mercury-output"]:
        sys.stdout.reconfigure(encoding="utf-8")
        success = run_compact_suite()
        sys.exit(0 if success else 1)
    unittest.main()
