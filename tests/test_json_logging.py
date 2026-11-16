"""Exercise structured JSON logging."""

import http.client
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import threading
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
                   sec_mode="Minimal", extra_directives=""):
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

        if extra_directives:
            conf_lines.append(f'    {extra_directives}')

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

    # Validate metrics configuration attributes and live exposition endpoint
    def test_metrics_config_and_exposition(self):
        with tempfile.TemporaryDirectory() as directory:
            access = Path(directory) / "access.log"
            error = Path(directory) / "error.log"

            # Reject invalid enabled attribute
            conf_bad_enabled = Path(self.write_conf(access, error))
            conf_bad_enabled.write_text(conf_bad_enabled.read_text().replace(
                "</Mercury>", '<Metrics enabled="invalid" path="/metrics" /></Mercury>'))
            proc_bad = subprocess.run([get_mercury_bin(), "-c", str(conf_bad_enabled)],
                                      capture_output=True, timeout=3)
            self.assertNotEqual(proc_bad.returncode, 0)
            self.assertIn(b"invalid enabled attribute for Metrics", proc_bad.stderr)

            # Reject invalid path attribute
            conf_bad_path = Path(self.write_conf(access, error))
            conf_bad_path.write_text(conf_bad_path.read_text().replace(
                "</Mercury>", '<Metrics enabled="true" path="no_slash" /></Mercury>'))
            proc_path = subprocess.run([get_mercury_bin(), "-c", str(conf_bad_path)],
                                       capture_output=True, timeout=3)
            self.assertNotEqual(proc_path.returncode, 0)
            self.assertIn(b"invalid path attribute for Metrics", proc_path.stderr)

            # Reject ambiguous paths, invalid CIDRs, and unsupported settings
            for directive in ('<Metrics path="/metrics?x=1" />',
                              '<Metrics path="/metrics#fragment" />',
                              '<Metrics><UnsupportedTag>val</UnsupportedTag></Metrics>',
                              '<Metrics><AllowCIDR>invalid_cidr</AllowCIDR></Metrics>',
                              '<Metrics><AllowCIDR>127.0.0.1/33</AllowCIDR></Metrics>',
                              '<Metrics><AllowCIDR attr="bad">127.0.0.1/32</AllowCIDR></Metrics>',
                              '<Metrics><TrackLatency enabled="invalid" /></Metrics>',
                              '<Metrics><TrackLatency attr="bad" /></Metrics>'):
                with self.subTest(directive=directive):
                    invalid = Path(self.write_conf(access, error))
                    invalid.write_text(invalid.read_text().replace("</Mercury>", directive + "</Mercury>"))
                    result = subprocess.run([get_mercury_bin(), "-c", str(invalid)],
                                            capture_output=True, timeout=3)
                    self.assertNotEqual(result.returncode, 0)

            # Start server w/ valid metrics configuration
            conf_valid = Path(self.write_conf(access, error))
            conf_valid.write_text(conf_valid.read_text().replace(
                "</Mercury>", '<Metrics enabled="true" path="/metrics"><AllowCIDR>127.0.0.1/32</AllowCIDR><TrackLatency enabled="true" /></Metrics></Mercury>'))
            proc = self.start_server(str(conf_valid))

            try:
                # Scrape metrics endpoint
                conn = http.client.HTTPConnection(HOST, self.port, timeout=3)
                conn.request("GET", "/metrics")
                res = conn.getresponse()
                self.assertEqual(res.status, 200)
                self.assertIn("text/plain", res.headers.get("Content-Type", ""))
                self.assertIn("version=0.0.4", res.headers.get("Content-Type", ""))
                payload = res.read().decode("utf-8")
                self.assertIn("mercury_http_requests_total", payload)
                self.assertIn("mercury_http_request_duration_seconds_bucket", payload)
                self.assertIn('le="0.005"', payload)
                self.assertIn('le="1.0"', payload)
                self.assertIn('le="+Inf"', payload)
                self.assertIn("mercury_http_request_duration_seconds_sum", payload)
                self.assertIn("mercury_http_request_duration_seconds_count", payload)
                self.assertIn("mercury_active_connections", payload)
                self.assertIn("mercury_process_resident_memory_bytes", payload)
                self.assertIn("mercury_process_cpu_seconds_total", payload)
                self.assertIn("mercury_process_uptime_seconds", payload)
                self.assertIn('method="POST",status="5xx",handler="fastcgi"} 0', payload)

                # Reject requests that disallow every supported content coding
                for method in ("GET", "HEAD"):
                    conn.request(method, "/metrics", headers={"Accept-Encoding": "identity;q=0, *;q=0"})
                    rejected = conn.getresponse()
                    self.assertEqual(rejected.status, 406)
                    rejected.read()

                # Reject non-GET/HEAD methods with 405 Method Not Allowed
                conn.request("POST", "/metrics", body=b"data")
                res_post = conn.getresponse()
                self.assertEqual(res_post.status, 405)
                self.assertIn("GET", res_post.headers.get("Allow", ""))
                res_post.read()
                conn.close()

                # Preserve version and request validation for internal route
                invalid_version = self.send_raw_request(
                    "GET /metrics HTTP/9.9\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                self.assertIn("505", invalid_version.split("\r\n", 1)[0])
                invalid_host = self.send_raw_request(
                    "GET /metrics HTTP/1.1\r\nConnection: close\r\n\r\n")
                self.assertIn("400", invalid_host.split("\r\n", 1)[0])
            finally:
                self.stop_server(proc)

    # Enforce existing access rules on internal metrics endpoint
    def test_metrics_respects_access_rules(self):
        with tempfile.TemporaryDirectory() as directory:
            conf = Path(self.write_conf(Path(directory) / "access.log", Path(directory) / "error.log"))
            conf.write_text(conf.read_text().replace('mode="allow all"', 'mode="deny all"').replace(
                "</Mercury>", '<Metrics enabled="true" /></Mercury>'))
            proc = self.start_server(str(conf))
            try:
                response = self.send_raw_request(
                    "GET /metrics HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                self.assertIn("403", response.split("\r\n", 1)[0])
            finally:
                self.stop_server(proc)

    # Enforce CIDR subnet restrictions on metrics endpoint
    def test_metrics_cidr_filtering(self):
        with tempfile.TemporaryDirectory() as directory:
            # Deny non-matching CIDR
            conf_deny = Path(self.write_conf(Path(directory) / "access.log", Path(directory) / "error.log"))
            conf_deny.write_text(conf_deny.read_text().replace(
                "</Mercury>", '<Metrics enabled="true"><AllowCIDR>10.0.0.0/8</AllowCIDR></Metrics></Mercury>'))
            proc_deny = self.start_server(str(conf_deny))
            try:
                response = self.send_raw_request(
                    "GET /metrics HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                self.assertIn("403", response.split("\r\n", 1)[0])
            finally:
                self.stop_server(proc_deny)

            # Allow matching CIDR
            conf_allow = Path(self.write_conf(Path(directory) / "access.log", Path(directory) / "error.log"))
            conf_allow.write_text(conf_allow.read_text().replace(
                "</Mercury>", '<Metrics enabled="true"><AllowCIDR>127.0.0.1/32</AllowCIDR></Metrics></Mercury>'))
            proc_allow = self.start_server(str(conf_allow))
            try:
                response = self.send_raw_request(
                    "GET /metrics HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                self.assertIn("200", response.split("\r\n", 1)[0])
            finally:
                self.stop_server(proc_allow)

    # Verify TrackLatency disabled toggle omits request duration histogram
    def test_metrics_track_latency_disabled(self):
        with tempfile.TemporaryDirectory() as directory:
            conf = Path(self.write_conf(Path(directory) / "access.log", Path(directory) / "error.log"))
            conf.write_text(conf.read_text().replace(
                "</Mercury>", '<Metrics enabled="true"><TrackLatency enabled="false" /></Metrics></Mercury>'))
            proc = self.start_server(str(conf))
            try:
                response = self.send_raw_request(
                    "GET /metrics HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                self.assertIn("200", response.split("\r\n", 1)[0])
                self.assertNotIn("mercury_http_request_duration_seconds", response)
            finally:
                self.stop_server(proc)

    # Verify histogram buckets remain monotonic and +Inf matches _count under concurrent traffic
    def test_metrics_histogram_concurrent_consistency(self):
        with tempfile.TemporaryDirectory() as directory:
            conf = Path(self.write_conf(Path(directory) / "access.log", Path(directory) / "error.log"))
            conf.write_text(conf.read_text().replace(
                "</Mercury>", '<Metrics enabled="true"><TrackLatency enabled="true" /></Metrics></Mercury>'))
            proc = self.start_server(str(conf))

            try:
                stop_event = threading.Event()
                errors = []
                successful_scrapes = []

                def worker():
                    while not stop_event.is_set():
                        try:
                            conn = http.client.HTTPConnection(HOST, self.port, timeout=1)
                            conn.request("GET", "/index.html")
                            res = conn.getresponse()
                            res.read()
                            conn.close()
                        except Exception:
                            pass

                def scraper():
                    bucket_re = re.compile(r'mercury_http_request_duration_seconds_bucket\{le="([^"]+)"\}\s+(\d+)')
                    count_re = re.compile(r'mercury_http_request_duration_seconds_count\s+(\d+)')
                    sum_re = re.compile(r'mercury_http_request_duration_seconds_sum\s+([0-9.]+)')

                    for _ in range(25):
                        if stop_event.is_set():
                            break
                        try:
                            conn = http.client.HTTPConnection(HOST, self.port, timeout=1)
                            conn.request("GET", "/metrics")
                            res = conn.getresponse()
                            body = res.read().decode("utf-8")
                            conn.close()

                            if res.status == 503:
                                continue
                            if res.status != 200:
                                errors.append(f"Scrape status was {res.status}")
                                continue
                            successful_scrapes.append(1)

                            buckets = bucket_re.findall(body)
                            count_match = count_re.search(body)
                            sum_match = sum_re.search(body)

                            if not buckets or not count_match or not sum_match:
                                errors.append("Missing histogram metrics in scrape payload")
                                continue

                            count_val = int(count_match.group(1))
                            sum_val = float(sum_match.group(1))

                            if sum_val < 0.0:
                                errors.append(f"Histogram sum negative: {sum_val}")

                            prev_val = 0
                            previous_bound = 0.0
                            minimum_sum = 0.0
                            for le, val_str in buckets:
                                val = int(val_str)
                                if val < prev_val:
                                    errors.append(f"Histogram inversion: bucket le={le} ({val}) < previous ({prev_val})")
                                minimum_sum += (val - prev_val) * previous_bound
                                prev_val = val
                                if le != "+Inf":
                                    previous_bound = float(le)

                            # Account for per-observation microsecond truncation
                            if sum_val + count_val * 0.000001 + 0.000001 < minimum_sum:
                                errors.append(f"Histogram sum {sum_val} below bucket minimum {minimum_sum}")

                            # +Inf bucket must equal _count
                            inf_bucket_val = int(buckets[-1][1])
                            if inf_bucket_val != count_val:
                                errors.append(f"+Inf bucket ({inf_bucket_val}) != count ({count_val})")

                        except Exception as e:
                            errors.append(f"Scraper error: {e}")
                        time.sleep(0.02)

                workers = [threading.Thread(target=worker) for _ in range(6)]
                scrape_thread = threading.Thread(target=scraper)

                for w in workers:
                    w.start()
                scrape_thread.start()

                scrape_thread.join(timeout=5)
                scrape_finished = not scrape_thread.is_alive()
                stop_event.set()
                for w in workers:
                    w.join(timeout=2)
                scrape_thread.join(timeout=2)

                self.assertTrue(scrape_finished, "Concurrent scraper did not finish within deadline")
                self.assertEqual(errors, [])
                self.assertGreater(len(successful_scrapes), 0, "No successful concurrent scrapes")
            finally:
                self.stop_server(proc)

    # Verify absent or disabled Metrics configuration avoids metric exposition
    def test_metrics_disabled_omits_metrics(self):
        with tempfile.TemporaryDirectory() as directory:
            conf_absent = Path(self.write_conf(Path(directory) / "access.log", Path(directory) / "error.log"))
            proc_absent = self.start_server(str(conf_absent))
            try:
                response = self.send_raw_request(
                    "GET /metrics HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                # When metrics is absent, /metrics is not intercepted and yields 404 from static file router
                self.assertIn("404", response.split("\r\n", 1)[0])
            finally:
                self.stop_server(proc_absent)

            conf_disabled = Path(self.write_conf(Path(directory) / "access.log", Path(directory) / "error.log"))
            conf_disabled.write_text(conf_disabled.read_text().replace(
                "</Mercury>", '<Metrics enabled="false" /></Mercury>'))
            proc_disabled = self.start_server(str(conf_disabled))
            try:
                response = self.send_raw_request(
                    "GET /metrics HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
                self.assertIn("404", response.split("\r\n", 1)[0])
            finally:
                self.stop_server(proc_disabled)

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

    # Comprehensive schema compliance test for all JSON log record variants
    def test_json_schema_validation_across_all_event_types(self):
        with tempfile.TemporaryDirectory() as directory:
            acc_path = Path(directory) / "access.json.log"
            err_path = Path(directory) / "error.json.log"
            redirect_directive = '<Redirect pattern="/redirect_fallback" to="/index.html"> 307 </Redirect>'
            conf = self.write_conf(str(acc_path), str(err_path), access_format="json", error_format="json",
                                   extra_directives=redirect_directive)
            proc = self.start_server(conf)

            try:
                # 1. Successful GET request with headers (status 200, level "info", event "request")
                self.send_raw_request(
                    f"GET /index.html HTTP/1.1\r\nHost: {HOST}:{self.port}\r\n"
                    "User-Agent: SchemaTester/2.0\r\nReferer: https://test.local/\r\nConnection: close\r\n\r\n")

                # 2. 404 Not Found request (status 404, level "warn", event "request")
                self.send_raw_request(
                    f"GET /missing_document.html HTTP/1.1\r\nHost: {HOST}:{self.port}\r\nConnection: close\r\n\r\n")

                # 3. Request without User-Agent or Referer headers
                self.send_raw_request(
                    f"HEAD /index.html HTTP/1.1\r\nHost: {HOST}:{self.port}\r\nConnection: close\r\n\r\n")

                # 4. HTTP/1.0 request triggering redirect fallback to provoke error log output
                self.send_raw_request(
                    f"GET /redirect_fallback HTTP/1.0\r\nHost: {HOST}:{self.port}\r\nConnection: close\r\n\r\n")

                time.sleep(0.3)
            finally:
                self.stop_server(proc)

            iso_regex = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$")

            # Validate access log lines
            acc_lines = acc_path.read_text(encoding="utf-8").splitlines()
            self.assertGreater(len(acc_lines), 0)

            seen_events = set()
            seen_request_levels = set()
            for raw_line in acc_lines:
                raw_line = raw_line.strip()
                if not raw_line:
                    continue
                record = json.loads(raw_line)
                self.assertIn("timestamp", record)
                self.assertTrue(iso_regex.match(record["timestamp"]), f"Malformed timestamp: {record['timestamp']}")
                self.assertIn("level", record)

                event = record.get("event")
                self.assertIn(event, ("request", "startup", "listen", "shutdown"), f"Unknown or missing event type: {event}")
                seen_events.add(event)
                if event == "request":
                    expected_keys = {
                        "timestamp", "level", "event", "client_ip", "method", "uri", "status",
                        "http_version", "bytes_sent", "latency_ms", "user_agent", "referer"
                    }
                    self.assertEqual(set(record.keys()), expected_keys)
                    if record["status"] >= 500:
                        self.assertEqual(record["level"], "error")
                    elif record["status"] >= 400:
                        self.assertEqual(record["level"], "warn")
                    else:
                        self.assertEqual(record["level"], "info")
                    seen_request_levels.add(record["level"])
                    self.assertIn(record["method"], ("GET", "HEAD", "POST", "PUT", "DELETE", "OPTIONS", "TRACE", "PATCH"))
                    self.assertIsInstance(record["uri"], str)
                    self.assertTrue(record["uri"].startswith("/"))
                    self.assertIsInstance(record["status"], int)
                    self.assertTrue(100 <= record["status"] <= 599)
                    self.assertIn(record["http_version"], ("HTTP/1.1", "HTTP/1.0", "HTTP/0.9"))
                    self.assertIsInstance(record["bytes_sent"], int)
                    self.assertGreaterEqual(record["bytes_sent"], 0)
                    self.assertIsInstance(record["latency_ms"], (int, float))
                    self.assertGreaterEqual(record["latency_ms"], 0.0)
                    self.assertIsInstance(record["user_agent"], str)
                    self.assertIsInstance(record["referer"], str)
                elif event == "startup":
                    self.assertEqual(record["level"], "info")
                    self.assertEqual(set(record.keys()), {"timestamp", "level", "event", "version", "message"})
                    self.assertIsInstance(record["version"], str)
                    self.assertIsInstance(record["message"], str)
                elif event == "listen":
                    self.assertEqual(record["level"], "info")
                    self.assertEqual(set(record.keys()), {"timestamp", "level", "event", "protocol", "port", "tls", "message"})
                    self.assertIn(record["protocol"], ("tcp4", "tcp6"))
                    self.assertIsInstance(record["port"], int)
                    self.assertIsInstance(record["tls"], bool)
                    self.assertIsInstance(record["message"], str)
                elif event == "shutdown":
                    self.assertEqual(record["level"], "info")
                    self.assertEqual(set(record.keys()), {"timestamp", "level", "event", "message"})
                    self.assertIsInstance(record["message"], str)

            self.assertIn("startup", seen_events)
            self.assertIn("listen", seen_events)
            self.assertIn("request", seen_events)
            self.assertIn("shutdown", seen_events)
            self.assertIn("info", seen_request_levels)
            self.assertIn("warn", seen_request_levels)

            # Validate error log lines
            self.assertTrue(err_path.exists())
            err_lines = err_path.read_text(encoding="utf-8").splitlines()
            self.assertGreater(len(err_lines), 0, "Error log was unexpectedly empty")

            seen_error_events = set()
            for raw_line in err_lines:
                raw_line = raw_line.strip()
                if not raw_line:
                    continue
                record = json.loads(raw_line)
                self.assertEqual(set(record.keys()), {"timestamp", "level", "event", "message"})
                self.assertTrue(iso_regex.match(record["timestamp"]), f"Malformed timestamp in error log: {record['timestamp']}")
                self.assertEqual(record["level"], "error")
                self.assertEqual(record["event"], "error")
                self.assertIsInstance(record["message"], str)
                self.assertGreater(len(record["message"]), 0)
                seen_error_events.add(record["event"])

            self.assertIn("error", seen_error_events)

    # Formal Prometheus 0.0.4 exposition format parser and specification compliance test
    def test_prometheus_exposition_format_parser(self):
        with tempfile.TemporaryDirectory() as directory:
            access = Path(directory) / "access.log"
            error = Path(directory) / "error.log"
            conf = Path(self.write_conf(access, error))
            conf.write_text(conf.read_text().replace(
                "</Mercury>", '<Metrics enabled="true" path="/metrics"><AllowCIDR>127.0.0.1/32</AllowCIDR><TrackLatency enabled="true" /></Metrics></Mercury>'))
            proc = self.start_server(str(conf))

            try:
                # Transmit a variety of HTTP requests to populate metric counters
                conn = http.client.HTTPConnection(HOST, self.port, timeout=3)
                conn.request("GET", "/index.html")
                res = conn.getresponse()
                res.read()

                conn.request("HEAD", "/index.html")
                res = conn.getresponse()
                res.read()

                conn.request("GET", "/nonexistent.html")
                res = conn.getresponse()
                res.read()

                # Scrape Prometheus exposition endpoint
                conn.request("GET", "/metrics")
                res = conn.getresponse()
                self.assertEqual(res.status, 200)
                content_type = res.headers.get("Content-Type", "")
                self.assertIn("text/plain", content_type)
                self.assertIn("version=0.0.4", content_type)
                payload = res.read().decode("utf-8")
                conn.close()

                # Parse and validate Prometheus format per OpenMetrics / 0.0.4 spec
                families = self._parse_prometheus_exposition(payload)

                expected_families = [
                    ("mercury_http_requests_total", "counter"),
                    ("mercury_http_request_duration_seconds", "histogram"),
                    ("mercury_http_bytes_received_total", "counter"),
                    ("mercury_http_bytes_sent_total", "counter"),
                    ("mercury_active_connections", "gauge"),
                    ("mercury_total_connections", "counter"),
                    ("mercury_workers_active", "gauge"),
                    ("mercury_process_resident_memory_bytes", "gauge"),
                    ("mercury_process_cpu_seconds_total", "counter"),
                    ("mercury_process_uptime_seconds", "gauge"),
                ]

                for name, expected_type in expected_families:
                    self.assertIn(name, families, f"Missing metric family: {name}")
                    fam = families[name]
                    self.assertEqual(fam["type"], expected_type, f"Type mismatch for {name}")
                    self.assertGreater(len(fam["help"]), 0, f"Empty HELP doc for {name}")
                    self.assertGreater(len(fam["samples"]), 0, f"No samples for {name}")

                # Assert that requests sent changed their expected counters
                req_fam = families["mercury_http_requests_total"]
                def get_request_sample(method, status, handler="static"):
                    target = {"method": method, "status": status, "handler": handler}
                    matches = [s for s in req_fam["samples"] if s["labels"] == target]
                    self.assertEqual(len(matches), 1, f"Missing or duplicate sample for {target}")
                    return matches[0]["value"]

                self.assertEqual(get_request_sample("GET", "2xx"), 1.0)
                self.assertEqual(get_request_sample("HEAD", "2xx"), 1.0)
                self.assertEqual(get_request_sample("GET", "4xx"), 1.0)
                self.assertEqual(get_request_sample("POST", "2xx"), 0.0)
                self.assertEqual(get_request_sample("GET", "5xx"), 0.0)

                # Validate histogram specification compliance and counter tracking
                hist = families["mercury_http_request_duration_seconds"]
                buckets = [s for s in hist["samples"] if s["name"].endswith("_bucket")]
                counts = [s for s in hist["samples"] if s["name"].endswith("_count")]
                sums = [s for s in hist["samples"] if s["name"].endswith("_sum")]

                self.assertEqual(len(counts), 1)
                self.assertEqual(len(sums), 1)
                total_count = counts[0]["value"]
                total_sum = sums[0]["value"]

                # Exactly 3 client requests were completed before scraping
                self.assertEqual(total_count, 3.0)
                self.assertGreater(total_sum, 0.0)

                expected_le_order = ["0.005", "0.01", "0.025", "0.05", "0.1", "0.25", "0.5", "1.0", "2.5", "5.0", "+Inf"]
                self.assertEqual([b["labels"]["le"] for b in buckets], expected_le_order)

                prev_val = 0
                for b in buckets:
                    val = b["value"]
                    self.assertGreaterEqual(val, prev_val, f"Histogram non-monotonic: {val} < {prev_val}")
                    prev_val = val

                # Final +Inf bucket value must equal _count (3.0)
                self.assertEqual(buckets[-1]["value"], 3.0)
                self.assertEqual(buckets[-1]["value"], total_count)

                # Validate process, connection, and byte throughput metrics
                self.assertGreater(families["mercury_http_bytes_received_total"]["samples"][0]["value"], 0.0)
                self.assertGreater(families["mercury_http_bytes_sent_total"]["samples"][0]["value"], 0.0)
                self.assertGreaterEqual(families["mercury_total_connections"]["samples"][0]["value"], 1.0)
                self.assertGreater(families["mercury_process_resident_memory_bytes"]["samples"][0]["value"], 0.0)
                self.assertGreaterEqual(families["mercury_process_uptime_seconds"]["samples"][0]["value"], 0.0)
                self.assertGreaterEqual(families["mercury_process_cpu_seconds_total"]["samples"][0]["value"], 0.0)

                # Negative parser tests verifying rejection of malformed Prometheus formats
                with self.assertRaises(ValueError):
                    self._parse_labels('method="GET" malformed_token')
                with self.assertRaises(ValueError):
                    self._parse_labels('method="GET",,')
                with self.assertRaises(ValueError):
                    self._parse_labels('123="invalid_identifier"')
                with self.assertRaises(ValueError):
                    self._parse_labels('method="unclosed_quote')
                with self.assertRaises(ValueError):
                    self._parse_labels('method="bad\\x_escape"')
                with self.assertRaises(ValueError):
                    self._parse_labels('dup="1",dup="2"')
                with self.assertRaises(ValueError):
                    self._parse_prometheus_exposition('invalid_line_without_space_or_brace')

            finally:
                self.stop_server(proc)

    def _parse_labels(self, labels_raw):
        # Strict tokenizer/parser for Prometheus label pairs inside { ... }
        labels_raw = labels_raw.strip()
        if not labels_raw:
            return {}

        labels = {}
        idx = 0
        n = len(labels_raw)

        while idx < n:
            while idx < n and labels_raw[idx].isspace():
                idx += 1
            if idx >= n:
                break

            # Parse label name: [a-zA-Z_][a-zA-Z0-9_]*
            name_start = idx
            if not (labels_raw[idx].isalpha() or labels_raw[idx] == '_'):
                raise ValueError(f"Invalid label name start at index {idx} in {labels_raw!r}")
            while idx < n and (labels_raw[idx].isalnum() or labels_raw[idx] == '_'):
                idx += 1
            label_name = labels_raw[name_start:idx]

            while idx < n and labels_raw[idx].isspace():
                idx += 1
            if idx >= n or labels_raw[idx] != '=':
                raise ValueError(f"Expected '=' after label name {label_name!r} in {labels_raw!r}")
            idx += 1
            while idx < n and labels_raw[idx].isspace():
                idx += 1

            if idx >= n or labels_raw[idx] != '"':
                raise ValueError(f"Expected '\"' for value of label {label_name!r} in {labels_raw!r}")
            idx += 1

            val_chars = []
            while idx < n:
                c = labels_raw[idx]
                if c == '\\':
                    idx += 1
                    if idx >= n:
                        raise ValueError(f"Dangling backslash in label value for {label_name!r}")
                    esc = labels_raw[idx]
                    if esc == 'n':
                        val_chars.append('\n')
                    elif esc == '\\':
                        val_chars.append('\\')
                    elif esc == '"':
                        val_chars.append('"')
                    else:
                        raise ValueError(f"Invalid escape sequence \\{esc} in {labels_raw!r}")
                elif c == '"':
                    idx += 1
                    break
                else:
                    val_chars.append(c)
                idx += 1
            else:
                raise ValueError(f"Unterminated quoted value for {label_name!r} in {labels_raw!r}")

            if label_name in labels:
                raise ValueError(f"Duplicate label {label_name!r} in {labels_raw!r}")
            labels[label_name] = "".join(val_chars)

            while idx < n and labels_raw[idx].isspace():
                idx += 1

            if idx < n:
                if labels_raw[idx] == ',':
                    idx += 1
                    while idx < n and labels_raw[idx].isspace():
                        idx += 1
                else:
                    raise ValueError(f"Expected ',' delimiter at index {idx} in {labels_raw!r}")

        return labels

    def _parse_prometheus_exposition(self, text):
        metric_name_re = re.compile(r'^[a-zA-Z_:][a-zA-Z0-9_:]*$')
        families = {}
        current_help = {}
        current_type = {}

        for line in text.splitlines():
            line = line.strip()
            if not line:
                continue
            if line.startswith("# HELP "):
                parts = line[7:].split(" ", 1)
                name = parts[0]
                doc = parts[1] if len(parts) > 1 else ""
                self.assertTrue(metric_name_re.match(name), f"Invalid name in HELP: {name}")
                current_help[name] = doc
            elif line.startswith("# TYPE "):
                parts = line[7:].split(" ", 1)
                name = parts[0]
                mtype = parts[1] if len(parts) > 1 else ""
                self.assertTrue(metric_name_re.match(name), f"Invalid name in TYPE: {name}")
                self.assertIn(mtype, ("counter", "gauge", "histogram", "summary", "untyped"))
                current_type[name] = mtype
            elif line.startswith("#"):
                continue
            else:
                # Sample line
                brace_open = line.find("{")
                if brace_open != -1:
                    name = line[:brace_open].strip()
                    self.assertTrue(metric_name_re.match(name), f"Invalid metric name in sample: {name}")
                    brace_close = line.rfind("}")
                    if brace_close == -1 or brace_close <= brace_open:
                        raise ValueError(f"Unclosed or malformed label set: {line}")
                    labels_raw = line[brace_open + 1:brace_close]
                    val_str = line[brace_close + 1:].strip()
                    if not val_str:
                        raise ValueError(f"Missing sample value after labels: {line}")
                    labels = self._parse_labels(labels_raw)
                else:
                    space_idx = line.find(" ")
                    if space_idx == -1:
                        raise ValueError(f"Missing value delimiter in sample line: {line}")
                    name = line[:space_idx].strip()
                    self.assertTrue(metric_name_re.match(name), f"Invalid metric name in sample: {name}")
                    val_str = line[space_idx + 1:].strip()
                    if not val_str:
                        raise ValueError(f"Missing sample value: {line}")
                    labels = {}

                family_name = name
                if name.endswith("_bucket"):
                    family_name = name[:-7]
                elif name.endswith("_count"):
                    family_name = name[:-6]
                elif name.endswith("_sum"):
                    family_name = name[:-4]

                if family_name not in families:
                    families[family_name] = {
                        "help": current_help.get(family_name, ""),
                        "type": current_type.get(family_name, "untyped"),
                        "samples": []
                    }
                families[family_name]["samples"].append({
                    "name": name,
                    "labels": labels,
                    "value": float(val_str)
                })

        return families


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
