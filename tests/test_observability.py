"""Exercise structured JSON logging and Prometheus metrics exposition."""

import http.client
import json
from pathlib import Path
import re
import threading
import time
import unittest

from harness import HOST, ROOT_DIR, ServerTestCase

JSON_LOGS = {"access_format": "json", "error_format": "json"}
METRICS = ('<Metrics enabled="true" path="/metrics"><AllowCIDR>127.0.0.1/32</AllowCIDR>'
           '<TrackLatency enabled="true" /></Metrics>')

ISO_TIMESTAMP = re.compile(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z")
BASE_KEYS = {"timestamp", "level", "event"}
RECORD_FIELDS = {
    "request": {"client_ip": str, "method": str, "uri": str, "status": int, "http_version": str,
                "bytes_sent": int, "latency_ms": (int, float), "user_agent": str, "referer": str},
    "startup": {"version": str, "message": str},
    "listen": {"protocol": str, "port": int, "tls": bool, "message": str},
    "shutdown": {"message": str},
}

METRIC_NAME = r"[a-zA-Z_:][a-zA-Z0-9_:]*"
SAMPLE_LINE = re.compile(rf"({METRIC_NAME})(?:\{{(.*)\}})?\s+(\S+)")
LABEL_PAIR = re.compile(r'([a-zA-Z_]\w*)="((?:[^"\\]|\\[n\\"])*)"')
DURATION = "mercury_http_request_duration_seconds"


# Returns {family: {type, help, samples}} from Prometheus 0.0.4 text, asserting it is well-formed
def parse_exposition(text: str) -> dict:
    helps, types, families = {}, {}, {}
    for line in filter(None, (raw.strip() for raw in text.splitlines())):
        if line.startswith(("# HELP ", "# TYPE ")):
            name, _, rest = line[7:].partition(" ")
            assert re.fullmatch(METRIC_NAME, name), f"Invalid metric name: {line}"
            if line.startswith("# HELP "):
                helps[name] = rest
            else:
                assert rest in ("counter", "gauge", "histogram", "summary", "untyped"), f"Invalid type: {line}"
                types[name] = rest
        elif not line.startswith("#"):
            match = SAMPLE_LINE.fullmatch(line)
            assert match, f"Malformed sample: {line}"
            name, raw_labels, value = match.groups()

            # Labels must be exactly comma-joined, quoted pairs w/ no repeats
            pairs = LABEL_PAIR.findall(raw_labels or "")
            assert ",".join(f'{k}="{v}"' for k, v in pairs) == (raw_labels or ""), f"Malformed labels: {line}"
            assert len({k for k, _ in pairs}) == len(pairs), f"Duplicate label: {line}"

            base = re.sub(r"_(bucket|count|sum)$", "", name)
            family = base if types.get(base) == "histogram" else name
            families.setdefault(family, {"type": types.get(family, "untyped"), "help": helps.get(family, ""),
                                         "samples": []})["samples"].append(
                {"name": name, "labels": dict(pairs), "value": float(value)})
    return families


# Returns violations in histogram family: non-monotonic buckets, +Inf != _count, or impossible _sum
def histogram_errors(hist: dict) -> list[str]:
    samples = hist["samples"]
    buckets = [(s["labels"]["le"], s["value"]) for s in samples if s["name"].endswith("_bucket")]
    count = next(s["value"] for s in samples if s["name"].endswith("_count"))
    total = next(s["value"] for s in samples if s["name"].endswith("_sum"))

    errors = []
    previous, lower_bound, minimum_sum = 0, 0.0, 0.0
    for le, value in buckets:
        if value < previous:
            errors.append(f"Histogram inversion: bucket le={le} ({value}) < previous ({previous})")

        # Every observation in a bucket is at least that bucket's lower bound
        minimum_sum += (value - previous) * lower_bound
        previous = value
        if le != "+Inf":
            lower_bound = float(le)

    if buckets[-1][1] != count:
        errors.append(f"+Inf bucket ({buckets[-1][1]}) != count ({count})")

    # Account for per-observation microsecond truncation
    if total < 0 or total + count * 1e-6 + 1e-6 < minimum_sum:
        errors.append(f"Histogram sum {total} below bucket minimum {minimum_sum}")
    return errors


class LoggingTests(ServerTestCase):
    # Returns parsed JSON records from a log (flushed once the instance has stopped)
    def records(self, path=None) -> list[dict]:
        records = []
        for line in filter(None, (raw.strip() for raw in Path(path or self.access_log).read_text(encoding="utf-8").splitlines())):
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError as exc:
                self.fail(f"Log line failed JSON decode: {line} ({exc})")
        return records

    # Returns the sole record for method, after the instance stops and flushes
    def only_request(self, proc, method):
        self.stop_server(proc)
        [entry] = [record for record in self.records() if record.get("method") == method]
        return entry

    # Verify access record carries request metadata (full schema checked separately)
    def test_access_log_record_has_request_metadata(self):
        proc = self.serve(**JSON_LOGS)
        status, _, _ = self.fetch(headers={"User-Agent": "MercuryTestClient/1.0", "Referer": "https://example.com/home"})
        self.assertEqual(status, 200)

        entry = self.only_request(proc, "GET")
        self.assertEqual((entry["uri"], entry["status"], entry["level"], entry["http_version"]),
                         ("/index.html", 200, "info", "HTTP/1.1"))
        self.assertGreater(entry["bytes_sent"], 0)
        self.assertEqual(entry["user_agent"], "MercuryTestClient/1.0")
        self.assertEqual(entry["referer"], "https://example.com/home")

    # Verify startup and listener events expose structured metadata
    def test_structured_startup_and_listener_events(self):
        self.stop_server(self.serve(**JSON_LOGS))
        entries = self.records()

        self.assertTrue(all(entry.get("event") and entry.get("level") for entry in entries))
        self.assertTrue(any(entry["event"] == "shutdown" for entry in entries))

        listener = next(entry for entry in entries if entry["event"] == "listen" and entry["protocol"] == "tcp4")
        self.assertEqual((listener["port"], listener["tls"], listener["level"]), (self.port, False, "info"))
        self.assertEqual(listener["message"], f"Listening on port {self.port} (IPv4).")

        startup = next(entry for entry in entries if entry["event"] == "startup")
        version = (ROOT_DIR / "version.txt").read_text(encoding="utf-8").strip().removeprefix("Mercury v")
        self.assertEqual((startup["level"], startup["version"]), ("info", version))
        self.assertIn("started successfully", startup["message"])

    # Verify control characters, quotes, and newlines in headers/URIs do not break JSON framing
    def test_json_injection_defense(self):
        proc = self.serve(**JSON_LOGS)
        user_agent = 'Evil"Agent\\with\t"and_quotes"\xff'
        referer = 'https://example.com/path?query="test"\\escape'
        status, _, _ = self.fetch('/index.html?test=value&quote="1"',
                                  headers={"User-Agent": user_agent, "Referer": referer})
        self.assertEqual(status, 200)

        # Every record must parse (records() fails otherwise) w/ escaped payloads intact
        entry = self.only_request(proc, "GET")
        self.assertEqual(entry["user_agent"], user_agent)
        self.assertEqual(entry["referer"], referer)

    # Include request body receive time in logged latency
    def test_latency_includes_body_receive(self):
        proc = self.serve(**JSON_LOGS)
        with self.connect() as sock:
            sock.sendall(b"POST /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: 2\r\n\r\nx")
            time.sleep(0.2)
            sock.sendall(b"y")
            while sock.recv(4096):
                pass

        self.assertGreaterEqual(self.only_request(proc, "POST")["latency_ms"], 150)

    # Exclude keep-alive idle time while retaining fragmented header receive time
    def test_latency_excludes_keep_alive_idle(self):
        proc = self.serve(**JSON_LOGS)
        conn = http.client.HTTPConnection(HOST, self.port, timeout=3)
        self.addCleanup(conn.close)
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

        self.stop_server(proc)
        heads = [record for record in self.records() if record.get("method") == "HEAD"]
        self.assertEqual(len(heads), 2)
        self.assertGreaterEqual(heads[1]["latency_ms"], 150)
        self.assertLess(heads[1]["latency_ms"], elapsed_ms + 250)

    # Verify GPC/DNT privacy headers mask client IPs in JSON log records
    def test_gpc_masking_in_json_log(self):
        proc = self.serve(sec_mode="GPCMasked", **JSON_LOGS)
        self.fetch(headers={"Sec-GPC": "1"})

        # Client IP lower 8 bits masked to zero per privacy policy
        self.assertEqual(self.only_request(proc, "GET")["client_ip"], "127.0.0.0")

    # Verify unsupported log format attribute fails configuration validation
    def test_invalid_format_attribute_fails_config(self):
        result = self.run_config(access_format="yaml")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"invalid format attribute for AccessLogFile", result.stderr)

    # Verify omitting format attribute, or requesting "text", yields legacy combined text format
    def test_text_format_matches_default(self):
        for log_format in (None, "text"):
            with self.subTest(format=log_format):
                proc = self.serve(access_format=log_format, error_format=log_format)
                self.fetch()
                self.stop_server(proc)

                # Text format begins w/ timestamp bracket rather than JSON brace
                line = next(l for l in reversed(self.access_log.read_text(encoding="utf-8").splitlines()) if "GET" in l)
                self.assertTrue(line.startswith("["))
                self.assertIn("GET 127.0.0.1 /index.html -- (200) [HTTP/1.1]", line)

    # Schema compliance for every JSON log record variant, access and error
    def test_json_schema_across_all_event_types(self):
        proc = self.serve('<Redirect pattern="/redirect_fallback" to="/index.html"> 307 </Redirect>', **JSON_LOGS)

        # Cover 2xx (w/ and w/o metadata headers), 4xx, and HTTP/1.0 redirect provoking error log output
        self.fetch(headers={"User-Agent": "SchemaTester/2.0", "Referer": "https://test.local/"})
        self.fetch("/missing_document.html")
        self.fetch(method="HEAD")
        self.fetch("/redirect_fallback", version="HTTP/1.0")
        self.stop_server(proc)

        seen_events, seen_levels = set(), set()
        for record in self.records():
            event = record.get("event")
            self.assertIn(event, RECORD_FIELDS, f"Unknown or missing event type: {event}")
            self.assertEqual(set(record), BASE_KEYS | set(RECORD_FIELDS[event]))
            self.assertRegex(record["timestamp"], ISO_TIMESTAMP)
            for key, kind in RECORD_FIELDS[event].items():
                self.assertIsInstance(record[key], kind, key)
            seen_events.add(event)

            if event == "request":
                status = record["status"]
                self.assertTrue(100 <= status <= 599)
                self.assertEqual(record["level"], "error" if status >= 500 else "warn" if status >= 400 else "info")
                self.assertIn(record["method"], ("GET", "HEAD", "POST", "PUT", "DELETE", "OPTIONS", "TRACE", "PATCH"))
                self.assertTrue(record["uri"].startswith("/"))
                self.assertIn(record["http_version"], ("HTTP/1.1", "HTTP/1.0", "HTTP/0.9"))
                self.assertGreaterEqual(record["bytes_sent"], 0)
                self.assertGreaterEqual(record["latency_ms"], 0.0)
                seen_levels.add(record["level"])
            else:
                self.assertEqual(record["level"], "info")
                if event == "listen":
                    self.assertIn(record["protocol"], ("tcp4", "tcp6"))

        self.assertEqual(seen_events, set(RECORD_FIELDS))
        self.assertTrue({"info", "warn"} <= seen_levels)

        errors = self.records(self.error_log)
        self.assertTrue(errors, "Error log was unexpectedly empty")
        for record in errors:
            self.assertEqual(set(record), BASE_KEYS | {"message"})
            self.assertRegex(record["timestamp"], ISO_TIMESTAMP)
            self.assertEqual((record["level"], record["event"]), ("error", "error"))
            self.assertIsInstance(record["message"], str)
            self.assertGreater(len(record["message"]), 0)


class MetricsTests(ServerTestCase):
    # Returns parsed metric families from one scrape, asserting a successful response
    def scrape(self) -> dict:
        status, _, body = self.fetch("/metrics")
        self.assertEqual(status, 200)
        return parse_exposition(body)

    # Verify malformed Metrics settings fail startup (unsupported tags, bad CIDRs, ambiguous paths)
    def test_invalid_metrics_config_fails_startup(self):
        for directive, message in (
            ('<Metrics enabled="invalid" path="/metrics" />', b"invalid enabled attribute for Metrics"),
            ('<Metrics enabled="true" path="no_slash" />', b"invalid path attribute for Metrics"),
            ('<Metrics path="/metrics?x=1" />', b""),
            ('<Metrics path="/metrics#fragment" />', b""),
            ("<Metrics><UnsupportedTag>val</UnsupportedTag></Metrics>", b""),
            ("<Metrics><AllowCIDR>invalid_cidr</AllowCIDR></Metrics>", b""),
            ("<Metrics><AllowCIDR>127.0.0.1/33</AllowCIDR></Metrics>", b""),
            ('<Metrics><AllowCIDR attr="bad">127.0.0.1/32</AllowCIDR></Metrics>', b""),
            ('<Metrics><TrackLatency enabled="invalid" /></Metrics>', b""),
            ('<Metrics><TrackLatency attr="bad" /></Metrics>', b""),
        ):
            with self.subTest(directive):
                result = self.run_config(extra=directive)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)

    # Verify exposition endpoint complies w/ Prometheus 0.0.4 text format and counts requests
    def test_exposition_format_and_counters(self):
        self.serve(METRICS)

        # Populate counters w/ 3 completed requests, then scrape on the same connection
        conn = http.client.HTTPConnection(HOST, self.port, timeout=3)
        self.addCleanup(conn.close)
        for method, path in (("GET", "/index.html"), ("HEAD", "/index.html"), ("GET", "/nonexistent.html"), ("GET", "/metrics")):
            conn.request(method, path)
            response = conn.getresponse()
            payload = response.read().decode("utf-8")
        self.assertEqual(response.status, 200)
        self.assertIn("text/plain", response.headers.get("Content-Type", ""))
        self.assertIn("version=0.0.4", response.headers.get("Content-Type", ""))
        families = parse_exposition(payload)

        for name, expected_type in (
            ("mercury_http_requests_total", "counter"),
            (DURATION, "histogram"),
            ("mercury_http_bytes_received_total", "counter"),
            ("mercury_http_bytes_sent_total", "counter"),
            ("mercury_active_connections", "gauge"),
            ("mercury_total_connections", "counter"),
            ("mercury_workers_active", "gauge"),
            ("mercury_process_resident_memory_bytes", "gauge"),
            ("mercury_process_cpu_seconds_total", "counter"),
            ("mercury_process_uptime_seconds", "gauge"),
        ):
            self.assertIn(name, families, f"Missing metric family: {name}")
            self.assertEqual(families[name]["type"], expected_type, f"Type mismatch for {name}")
            self.assertTrue(families[name]["help"], f"Empty HELP doc for {name}")
            self.assertTrue(families[name]["samples"], f"No samples for {name}")

        # Requests sent must move exactly their counters (zero series are still exposed)
        def counter(method, status, handler="static"):
            target = {"method": method, "status": status, "handler": handler}
            matches = [s for s in families["mercury_http_requests_total"]["samples"] if s["labels"] == target]
            self.assertEqual(len(matches), 1, f"Missing or duplicate sample for {target}")
            return matches[0]["value"]

        self.assertEqual([counter("GET", "2xx"), counter("HEAD", "2xx"), counter("GET", "4xx")], [1.0, 1.0, 1.0])
        self.assertEqual([counter("POST", "2xx"), counter("GET", "5xx"), counter("POST", "5xx", "fastcgi")], [0.0] * 3)

        # Histogram: fixed bucket layout, all 3 completed requests observed, internally consistent
        samples = families[DURATION]["samples"]
        self.assertEqual([s["labels"]["le"] for s in samples if s["name"].endswith("_bucket")],
                         ["0.005", "0.01", "0.025", "0.05", "0.1", "0.25", "0.5", "1.0", "2.5", "5.0", "+Inf"])
        self.assertEqual(next(s["value"] for s in samples if s["name"].endswith("_count")), 3.0)
        self.assertGreater(next(s["value"] for s in samples if s["name"].endswith("_sum")), 0.0)
        self.assertEqual(histogram_errors(families[DURATION]), [])

        # Throughput, connection, and process gauges
        value = lambda name: families[name]["samples"][0]["value"]
        for name in ("mercury_http_bytes_received_total", "mercury_http_bytes_sent_total",
                     "mercury_process_resident_memory_bytes"):
            self.assertGreater(value(name), 0.0)
        self.assertGreaterEqual(value("mercury_total_connections"), 1.0)
        self.assertGreaterEqual(value("mercury_process_uptime_seconds"), 0.0)
        self.assertGreaterEqual(value("mercury_process_cpu_seconds_total"), 0.0)

    # Verify internal route preserves coding negotiation, method, version, and Host validation
    def test_metrics_route_validation(self):
        self.serve(METRICS)

        for method in ("GET", "HEAD"):
            status, _, _ = self.fetch("/metrics", method=method, headers={"Accept-Encoding": "identity;q=0, *;q=0"})
            self.assertEqual(status, 406)

        status, headers, _ = self.fetch("/metrics", method="POST", body=b"data")
        self.assertEqual(status, 405)
        self.assertIn("GET", headers.get("allow", ""))

        self.assertEqual(self.fetch("/metrics", version="HTTP/9.9")[0], 505)
        self.assertEqual(self.fetch("/metrics", headers={"Host": None})[0], 400)

    # Enforce existing access rules and CIDR allowlist on internal metrics endpoint
    def test_metrics_access_control(self):
        for name, access, cidr, expected in (("access rule", "deny all", "", 403),
                                             ("CIDR mismatch", "allow all", "10.0.0.0/8", 403),
                                             ("CIDR match", "allow all", "127.0.0.1/32", 200)):
            with self.subTest(name):
                allow = f"<AllowCIDR>{cidr}</AllowCIDR>" if cidr else ""
                proc = self.serve(f'<Metrics enabled="true">{allow}</Metrics>', access=access)
                self.assertEqual(self.fetch("/metrics")[0], expected)
                self.stop_server(proc)

    # Verify TrackLatency disabled toggle omits request duration histogram
    def test_track_latency_disabled(self):
        self.serve('<Metrics enabled="true"><TrackLatency enabled="false" /></Metrics>')
        status, _, body = self.fetch("/metrics")
        self.assertEqual(status, 200)
        self.assertNotIn(DURATION, body)

    # Verify absent or disabled Metrics configuration leaves /metrics to the static router
    def test_metrics_disabled_omits_endpoint(self):
        for name, extra in (("absent", ""), ("disabled", '<Metrics enabled="false" />')):
            with self.subTest(name):
                proc = self.serve(extra)
                self.assertEqual(self.fetch("/metrics")[0], 404)
                self.stop_server(proc)

    # Verify buckets stay monotonic and +Inf matches _count while traffic races scrapes
    def test_histogram_consistent_under_concurrent_traffic(self):
        self.serve(METRICS)
        stop, errors, scrapes = threading.Event(), [], []

        def worker():
            while not stop.is_set():
                try:
                    self.fetch()
                except Exception:
                    pass

        def scraper():
            for _ in range(25):
                if stop.is_set():
                    break
                try:
                    status, _, body = self.fetch("/metrics")
                    if status == 503:
                        continue
                    if status != 200:
                        errors.append(f"Scrape status was {status}")
                        continue
                    scrapes.append(status)

                    hist = parse_exposition(body).get(DURATION)
                    errors.extend(histogram_errors(hist) if hist else ["Missing histogram metrics in scrape payload"])
                except Exception as error:
                    errors.append(f"Scraper error: {error}")
                time.sleep(0.02)

        workers = [threading.Thread(target=worker, daemon=True) for _ in range(6)]
        scrape_thread = threading.Thread(target=scraper, daemon=True)
        for thread in workers + [scrape_thread]:
            thread.start()

        scrape_thread.join(timeout=5)
        finished = not scrape_thread.is_alive()
        stop.set()
        for thread in workers:
            thread.join(timeout=2)

        self.assertTrue(finished, "Concurrent scraper did not finish within deadline")
        self.assertEqual(errors, [])
        self.assertGreater(len(scrapes), 0, "No successful concurrent scrapes")


if __name__ == "__main__":
    unittest.main()
