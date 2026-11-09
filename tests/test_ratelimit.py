"""Exercise Mercury IP-Based Rate Limiting (#443)."""

import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest

PORT = 8092
HOST = "127.0.0.1"


# Returns path to compiled Mercury server binary
def get_mercury_bin():
    script_dir = Path(__file__).resolve().parent
    root_dir = script_dir.parent
    bin_name = "mercury.exe" if sys.platform in ["win32", "cygwin"] else "mercury"
    return str(root_dir / "bin" / bin_name)


class RateLimitTests(unittest.TestCase):
    # Used to write temporary configuration for rate limit testing
    def write_conf(self, enabled=True, rate=5.0, burst=2.0, whitelist=None, sec_mode="Minimal"):
        conf_lines = [
            "<Mercury>",
            "    <DocumentRoot> ./tests/root/ </DocumentRoot>",
            "    <BindAddressIPv4> 0.0.0.0 </BindAddressIPv4>",
            f"    <Port> {PORT} </Port>",
            "    <TLSPort> off </TLSPort>",
            "    <EnableLegacyHTTPVersions> on </EnableLegacyHTTPVersions>",
            "    <AccessLogFile> ./logs/access.log </AccessLogFile>",
            "    <ErrorLogFile> ./logs/error.log </ErrorLogFile>",
            f"    <ClientSecurityMode> {sec_mode} </ClientSecurityMode>",
            "    <ClientSecurityIPSalt> fAhGD0OnCe4YhSyx </ClientSecurityIPSalt>",
            f'    <RateLimit enabled="{"on" if enabled else "off"}" rate="{rate}" burst="{burst}">',
        ]
        if whitelist:
            for wl in whitelist:
                conf_lines.append(f"        <Whitelist> {wl} </Whitelist>")
        conf_lines.extend([
            "    </RateLimit>",
            '    <Match pattern=".*">',
            '        <Access mode="allow all" />',
            "    </Match>",
            "</Mercury>",
        ])
        temp = tempfile.NamedTemporaryFile(mode="w", suffix=".conf", delete=False)
        temp.write("\n".join(conf_lines))
        temp.close()
        self.addCleanup(lambda: os.path.exists(temp.name) and os.remove(temp.name))
        return temp.name

    # Used to start a background Mercury instance with the given configuration
    def start_server(self, conf_path):
        proc = subprocess.Popen([get_mercury_bin(), "-c", conf_path],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.addCleanup(lambda: self.stop_server(proc))

        # Wait for server readiness
        for _ in range(50):
            try:
                sock = socket.create_connection((HOST, PORT), timeout=0.2)
                sock.close()
                return proc
            except Exception:
                if proc.poll() is not None:
                    break
                time.sleep(0.1)
        self.fail("Mercury failed to start for rate limiting test")

    # Used to stop server process cleanly
    def stop_server(self, proc):
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()

    # Returns parsed status code, headers dictionary, and response body
    def send_request(self, headers=""):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(3)
            sock.connect((HOST, PORT))
            req = f"GET /small.txt HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n{headers}\r\n"
            sock.sendall(req.encode())
            res = b""
            while True:
                try:
                    chunk = sock.recv(4096)
                    if not chunk:
                        break
                    res += chunk
                except Exception:
                    break

        lines = res.decode(errors="replace").split("\r\n")
        if not lines or not lines[0]:
            return 0, {}, ""
        status_parts = lines[0].split()
        status_code = int(status_parts[1]) if len(status_parts) > 1 else 0
        res_headers = {}
        idx = 1
        while idx < len(lines) and lines[idx]:
            if ":" in lines[idx]:
                k, v = lines[idx].split(":", 1)
                res_headers[k.strip().lower()] = v.strip()
            idx += 1
        body = "\r\n".join(lines[idx + 1:]) if idx < len(lines) else ""
        return status_code, res_headers, body

    # Test burst limits, 429 response, Retry-After header, and recovery
    def test_burst_and_recovery(self):
        conf_path = self.write_conf(enabled=True, rate=2.0, burst=2.0, whitelist=[])
        self.start_server(conf_path)

        code1, _, _ = self.send_request()
        self.assertEqual(code1, 200)

        code2, _, _ = self.send_request()
        self.assertEqual(code2, 200)

        code3, headers3, body3 = self.send_request()
        self.assertEqual(code3, 429)
        self.assertIn("retry-after", headers3)
        self.assertGreaterEqual(int(headers3["retry-after"]), 1)
        self.assertEqual(headers3.get("connection"), "close")
        self.assertIn("Too Many Requests", body3)

        # Allow token bucket replenishment
        time.sleep(1.2)

        code4, _, _ = self.send_request()
        self.assertEqual(code4, 200)

    # Test whitelist bypass under restrictive burst limit
    def test_whitelist_bypass(self):
        conf_path = self.write_conf(enabled=True, rate=1.0, burst=1.0, whitelist=["127.0.0.1/32"])
        self.start_server(conf_path)

        for _ in range(5):
            code, _, _ = self.send_request()
            self.assertEqual(code, 200)

    # Test rate limiting with GPCHashed privacy mode
    def test_gpc_hashed_mode(self):
        conf_path = self.write_conf(enabled=True, rate=2.0, burst=2.0, whitelist=[], sec_mode="GPCHashed")
        self.start_server(conf_path)

        gpc_header = "Sec-GPC: 1\r\n"
        code1, _, _ = self.send_request(gpc_header)
        code2, _, _ = self.send_request(gpc_header)
        code3, headers3, _ = self.send_request(gpc_header)

        self.assertEqual(code1, 200)
        self.assertEqual(code2, 200)
        self.assertEqual(code3, 429)
        self.assertIn("retry-after", headers3)


# Used to execute test suite matching Mercury compact terminal progress output
def run_compact_suite() -> bool:
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    total = suite.countTestCases()
    print(f"Starting run: Rate Limiting Tests ({total} tests)", flush=True)

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
                print(f"[RateLimit {label}] {test}\n{details}", end="")
        print(f"❌ Failing ({len(result.failures)} failures, {len(result.errors)} errors)", flush=True)
    return result.wasSuccessful()


if __name__ == "__main__":
    if sys.argv[1:] == ["--mercury-output"]:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.exit(0 if run_compact_suite() else 1)
    unittest.main()
