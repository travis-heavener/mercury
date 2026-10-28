"""Exercise Linux CGI process lifecycle through controlled executable fixtures."""

from concurrent.futures import ThreadPoolExecutor
from contextlib import redirect_stdout
import hashlib
import io
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import MagicMock, patch

from regressions import Connection, request
from run import mercury_process


PROJECT_ROOT = Path(__file__).resolve().parent.parent


# Returns True if fixture process is running in current namespace
def is_process_running(pid):
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


# Keep fixture behavior independent of installed PHP version and INI extensions
WORKER_SOURCE = r'''
import hashlib
import os
from pathlib import Path
import signal
import sys
import time

mode = os.environ.get("QUERY_STRING", "")
output = sys.stdout.buffer

# Emit diagnostics before valid output, including more than one pipe buffer
if mode == "warning":
    sys.stderr.write("worker warning\nX-Injected: rejected\n")
    sys.stderr.flush()
elif mode == "stderr_flood":
    sys.stderr.buffer.write(b"E" * 262144)
    sys.stderr.buffer.flush()

# Keep worker alive until Mercury cancels or times out its request
if mode in ("hang", "closed_hang", "escaped_hang"):
    if mode == "escaped_hang":
        os.setpgid(0, os.getpgid(os.getppid()))
    Path(os.environ["SCRIPT_FILENAME"]).with_suffix(".started").write_text(str(os.getpid()))
    if mode == "closed_hang":
        os.close(1)
        os.close(2)
    time.sleep(60)

# Terminate worker process abnormally on crash request
if mode == "crash":
    os.kill(os.getpid(), signal.SIGKILL)

# Write response header delimiter
output.write(b"Content-Type: application/octet-stream\r\n\r\n")

# Fill stdout before reading large body to require concurrent pipe transfers
if mode == "duplex":
    output.write(b"P" * 131072)
    output.flush()

# Read full request payload from stdin
body = sys.stdin.buffer.read()

# Detect inherited CGI pipes after exec; ignore transient directory descriptors
if mode == "inspect":
    inherited = []
    for name in os.listdir("/proc/self/fd"):
        if int(name) < 3:
            continue
        try:
            target = os.readlink("/proc/self/fd/" + name)
        except FileNotFoundError:
            continue
        if target.startswith("pipe:"):
            inherited.append(name)
    output.write(("leaked:" + ",".join(inherited)).encode() if inherited else b"clean")
elif mode == "duplex":
    output.write(hashlib.sha256(body).hexdigest().encode())
else:
    output.write(b"ok")

# Flush buffered response and exit w/ requested status
output.flush()
sys.exit(7 if mode == "nonzero" else 0)
'''


@unittest.skipUnless(sys.platform == "linux", "Require Linux CGI process handling")
class CGIProcessTests(unittest.TestCase):
    # Used to create isolated PATH entry, document root, logs, and configuration
    def setUp(self):
        os.chdir(PROJECT_ROOT / "tests")

        # Prepare isolated test directory structure
        self.directory = tempfile.TemporaryDirectory(prefix="mercury-cgi-test-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()

        # Write mock php-cgi executable fixture
        worker = self.bin / "php-cgi"
        worker.write_text(f"#!{sys.executable}\n" + WORKER_SOURCE)
        worker.chmod(0o755)

        # Prepare dummy script files and error log path
        (self.root / "worker.php").write_text("<?php echo 'fixture';")
        (self.root / "small.txt").write_text("ready")
        self.error_log = self.root / "error.log"

        # Apply fixture PATH only while running this test's Mercury processes
        self.environment = patch.dict(os.environ, {"PATH": str(self.bin) + os.pathsep + os.environ["PATH"]})
        self.environment.start()
        self.addCleanup(self.environment.stop)

    # Used to generate test configuration w/ custom CGI timeout
    def config(self, timeout=5):
        text = (PROJECT_ROOT / "tests/conf_files/normal.conf").read_text()

        # Rewrite paths and apply custom CGI timeout override
        text = re.sub(r"<DocumentRoot>.*?</DocumentRoot>", f"<DocumentRoot> {self.root} </DocumentRoot>", text)
        text = re.sub(r"<ErrorLogFile>.*?</ErrorLogFile>", f"<ErrorLogFile> {self.error_log} </ErrorLogFile>", text)
        text = text.replace("<Mercury>", f"<Mercury><CGITimeout> {timeout} </CGITimeout>"
                            "<StartupCheckLatestRelease> false </StartupCheckLatestRelease>")

        # Save customized runtime configuration
        path = self.root / "mercury.conf"
        path.write_text(text)
        return str(path)

    # Used to send binary bodies w/ exact framing and read complete responses
    def response(self, mode, body=b"", host="127.0.0.1", tls=False):
        headers = b"Connection: close\r\nContent-Length: " + str(len(body)).encode() + b"\r\n"

        # Connect and dispatch framed POST request
        with Connection(host, 8081 if tls else 8080, tls=tls) as connection:
            connection.send(request(b"/worker.php?" + mode.encode(), method=b"POST", headers=headers, body=body))
            return connection.response()

    # Verify CGI worker stderr warning logs escaped diagnostics without injecting headers
    def test_stderr_warning_keeps_valid_response(self):
        with mercury_process(self.config()):
            for host, tls in (("127.0.0.1", False), ("127.0.0.1", True), ("::1", False), ("::1", True)):
                with self.subTest(host=host, tls=tls):
                    status, headers, body = self.response("warning", host=host, tls=tls)
                    self.assertEqual((status, body), (200, b"ok"))
                    self.assertNotIn(b"x-injected", headers)

        # Confirm escaped newline diagnostic was written to error log
        self.assertIn("worker warning\\x0A", self.error_log.read_text())

    # Verify excessive stderr diagnostic data is truncated without corrupting stdout
    def test_stderr_flood_drains_without_corrupting_body(self):
        with mercury_process(self.config()):
            status, _, body = self.response("stderr_flood")
            self.assertEqual((status, body), (200, b"ok"))

        # Verify diagnostic truncation marker in error log
        log = self.error_log.read_text()
        self.assertIn("[truncated]", log)
        self.assertLess(len(log), 8192)

    # Verify large concurrent bidirectional pipe transfers preserve binary payload
    def test_large_duplex_transfer_preserves_binary_body(self):
        body = bytes(range(256)) * 4096

        with mercury_process(self.config()):
            status, _, output = self.response("duplex", body)
            self.assertEqual(status, 200)
            self.assertEqual(output, b"P" * 131072 + hashlib.sha256(body).hexdigest().encode())

    # Verify concurrent worker processes do not leak inherited pipe descriptors
    def test_concurrent_workers_do_not_inherit_request_pipes(self):
        with mercury_process(self.config(timeout=10)):
            with ThreadPoolExecutor(max_workers=8) as executor:
                futures = [executor.submit(self.response, "inspect", b"payload" * 16384) for _ in range(16)]
                for future in futures:
                    status, _, body = future.result(timeout=15)
                    self.assertEqual((status, body), (200, b"clean"))

    # Verify nonzero exit code and worker crashes yield 502 Bad Gateway
    def test_failed_worker_exit_rejects_partial_response(self):
        with mercury_process(self.config()):
            for mode in ("nonzero", "crash"):
                with self.subTest(mode=mode):
                    self.assertEqual(self.response(mode)[0], 502)

    # Verify missing CGI executable reports launch error and returns 502 Bad Gateway
    def test_missing_executable_reports_launch_failure(self):
        with patch.dict(os.environ, {"PATH": str(self.root / "missing-bin")}):
            with mercury_process(self.config()):
                self.assertEqual(self.response("normal")[0], 502)

        # Confirm launch failure diagnostic was written to error log
        self.assertIn("Failed to launch PHP CGI worker", self.error_log.read_text())

    # Verify worker timeout returns 504 Gateway Timeout and terminates hung process
    def test_timeout_covers_streams_and_worker_exit(self):
        with mercury_process(self.config(timeout=1)):
            for mode in ("hang", "closed_hang", "escaped_hang"):
                with self.subTest(mode=mode):
                    start = time.monotonic()
                    self.assertEqual(self.response(mode)[0], 504)
                    self.assertLess(time.monotonic() - start, 4)

                    # Verify worker process was terminated
                    worker_pid = int((self.root / "worker.started").read_text())
                    self.assertFalse(is_process_running(worker_pid))

    # Verify server startup rejects invalid zero timeout configuration
    def test_zero_execution_timeout_is_rejected(self):
        result = subprocess.run([str(PROJECT_ROOT / "bin/mercury"), "-c", self.config(timeout=0)],
                                cwd=PROJECT_ROOT, capture_output=True, timeout=5)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"CGITimeout", result.stdout + result.stderr)

    # Verify server shutdown cancels active worker process
    def test_shutdown_cancels_active_worker(self):
        worker_pid = None

        with socket.socket() as connection:
            with mercury_process(self.config(timeout=30)):
                connection.connect(("127.0.0.1", 8080))
                connection.sendall(request(b"/worker.php?hang"))

                # Wait for actual worker startup before requesting server shutdown
                marker = self.root / "worker.started"
                deadline = time.monotonic() + 5
                while not marker.exists() and time.monotonic() < deadline:
                    time.sleep(0.01)

                self.assertTrue(marker.exists())
                worker_pid = int(marker.read_text())
                self.assertTrue(is_process_running(worker_pid))
                start = time.monotonic()

            self.assertLess(time.monotonic() - start, 5)

        # Verify worker process was cleaned up after server shutdown
        self.assertFalse(is_process_running(worker_pid))


class RunnerDiagnosticsTests(unittest.TestCase):
    # Used to emulate hung shutdown without waiting through real CI deadlines
    def exercise_shutdown(self, original_error=False):
        # Configure mock server process to simulate shutdown timeout
        process = MagicMock()
        process.pid = 12345
        process.returncode = None
        process.poll.side_effect = lambda: process.returncode
        process.wait.side_effect = [subprocess.TimeoutExpired("mercury", 15), -9]

        # Simulate kill signal terminating process
        def kill():
            process.returncode = -9

        # Simulate process writing diagnostic output to stderr
        def launch(*args, **kwargs):
            kwargs["stderr"].write(b"retained CGI diagnostic\n")
            kwargs["stderr"].flush()
            return process

        process.kill.side_effect = kill

        # Configure mock connection probe to simulate responsive worker
        probe = MagicMock()
        probe.__enter__.return_value.recv.return_value = b"ready"
        output = io.StringIO()
        expected = ValueError if original_error else RuntimeError

        # Execute shutdown lifecycle under mocked process hooks
        with patch("run.subprocess.Popen", side_effect=launch), \
             patch("run.wait_until_live", return_value=True), \
             patch("run.socket.create_connection", return_value=probe), \
             redirect_stdout(output):
            with self.assertRaises(expected):
                with mercury_process("unused"):
                    if original_error:
                        raise ValueError("original test failure")

        # Verify forced termination occurred and captured diagnostics were printed
        process.kill.assert_called_once()
        self.assertIn("retained CGI diagnostic", output.getvalue())
        self.assertIn("forced shutdown after timeout", output.getvalue())

    # Verify captured stderr diagnostics print on shutdown timeout
    def test_shutdown_timeout_prints_captured_diagnostics(self):
        self.exercise_shutdown()

    # Verify original test failure propagates despite subsequent shutdown timeout
    def test_shutdown_timeout_preserves_original_exception(self):
        self.exercise_shutdown(original_error=True)


# Used to retain concise runner output while printing full failure details
def run_compact_suite():
    # Load test suite from current module
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    print(f"Starting run: Linux CGI Process Tests ({suite.countTestCases()} tests)", flush=True)

    # Run tests and capture failure details
    result = unittest.TestResult()
    suite.run(result)

    # Output detailed failure traces if any occurred
    for label, entries in (("failure", result.failures), ("error", result.errors)):
        for test, details in entries:
            print(f"[CGI {label}] {test}\n{details}", end="")

    # Report test suite outcome
    print("✅ Passing" if result.wasSuccessful() else "❌ Failing", flush=True)
    return result.wasSuccessful()


if __name__ == "__main__":
    # Support compact runner output mode when invoked from main test runner
    if sys.argv[1:] == ["--mercury-output"]:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.exit(0 if run_compact_suite() else 1)

    # Fall back to standard unittest CLI execution
    unittest.main()
