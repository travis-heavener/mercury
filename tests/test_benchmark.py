#!/usr/bin/env python3
"""Regression checks for benchmark lifecycle, validation, and report updates"""

from contextlib import ExitStack, contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
import xml.etree.ElementTree as ET
from unittest import mock

from build_tools import benchmark as bench
from build_tools import update_benchmark_readme as readme

REPO_ROOT = Path(__file__).resolve().parents[1]


@contextmanager
def http_fixture(status=200, payload=b"benchmark"):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_GET(self):
            self.send_response(status)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        def log_message(self, *args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield server.server_port
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


class LoadTests(unittest.TestCase):
    def test_wrong_response_never_becomes_successful_throughput(self):
        with http_fixture(status=404) as port:
            with self.assertRaisesRegex(RuntimeError, "Payload validation"):
                bench.LoadGenerator.validate_payload("127.0.0.1", port, "/", b"benchmark")
            with self.assertRaisesRegex(RuntimeError, "HTTP 404"):
                bench.LoadGenerator._run_warmup("127.0.0.1", port, "/", 1)
            with self.assertRaisesRegex(RuntimeError, "failed requests"):
                bench.LoadGenerator._run_python("127.0.0.1", port, "/", 1, 0.05)

    def test_payload_validation_checks_bytes(self):
        with http_fixture() as port:
            bench.LoadGenerator.validate_payload("127.0.0.1", port, "/", b"benchmark")
            with self.assertRaisesRegex(RuntimeError, "Payload validation"):
                bench.LoadGenerator.validate_payload("127.0.0.1", port, "/", b"different")

    def test_explicit_tool_does_not_silently_fall_back(self):
        with mock.patch.object(bench.shutil, "which", return_value=None):
            self.assertEqual(bench.LoadGenerator.detect_tool("auto"), "python")
            with self.assertRaises(FileNotFoundError):
                bench.LoadGenerator.detect_tool("wrk")

    def test_external_tools_reject_failures_and_parse_units(self):
        # Validate wrk latency units and explicit transport/status failures
        wrk = "Latency 100us 20us 1ms\n50% 80us\n99% 1ms\nRequests/sec: 1234.50\n"
        with mock.patch.object(
            bench.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, wrk, "")
        ) as run:
            self.assertEqual(
                bench.LoadGenerator._run_wrk("127.0.0.1", 8080, "/", 2, 1), (1234.5, 0.1, 0.08, 1)
            )
            self.assertIn("Accept-Encoding: identity", run.call_args.args[0])

        for output in [
            wrk + "Non-2xx or 3xx responses: 4",
            wrk + "Socket errors: connect 1, read 0, write 0, timeout 0",
            "unparseable",
            "Requests/sec: 1000",
        ]:
            with self.subTest(output=output), mock.patch.object(
                bench.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, output, "")
            ):
                with self.assertRaises(RuntimeError):
                    bench.LoadGenerator._run_wrk("127.0.0.1", 8080, "/", 2, 1)

        # Validate hey seconds conversion and response status accounting
        hey = "Requests/sec: 500\nAverage: 0.002 secs\n50% in 0.001 secs\n99% in 0.010 secs\n[200] 500 responses\n"
        with mock.patch.object(
            bench.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, hey, "")
        ) as run:
            self.assertEqual(
                bench.LoadGenerator._run_hey("127.0.0.1", 8080, "/", 2, 1), (500, 2, 1, 10)
            )
            self.assertIn("-disable-compression", run.call_args.args[0])
        for output in [hey + "[404] 5 responses", hey + "Error distribution:\nconnection reset"]:
            with self.subTest(output=output), mock.patch.object(
                bench.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, output, "")
            ):
                with self.assertRaises(RuntimeError):
                    bench.LoadGenerator._run_hey("127.0.0.1", 8080, "/", 2, 1)

        # Preserve failing external command diagnostics
        with mock.patch.object(
            bench.subprocess,
            "run",
            side_effect=subprocess.CalledProcessError(1, "wrk", stderr="connection failed"),
        ):
            with self.assertRaisesRegex(RuntimeError, "connection failed"):
                bench.LoadGenerator._run_wrk("127.0.0.1", 8080, "/", 2, 1)


class SupervisorTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="mercury benchmark ")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / "bin").mkdir()
        (self.root / "conf/default").mkdir(parents=True)
        (self.root / "conf/default/mimes.conf").write_text(".html text/html\n")

        # Reserve independent test port before writing server config
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            self.port = sock.getsockname()[1]

        self.config = self.root / "mercury.conf"
        content = (REPO_ROOT / "tests/conf_files/benchmark/mercury.conf").read_text()
        self.config.write_text(content.replace("8080", str(self.port)))
        (self.root / "tests/root").mkdir(parents=True)
        (self.root / "logs/benchmark").mkdir(parents=True)
        self.binary = self.root / "bin" / ("mercury.exe" if sys.platform == "win32" else "mercury")
        self.binary.touch()
        self.supervisor = bench.ServerSupervisor(
            "mercury", str(self.binary), self.port, str(self.config), str(self.root)
        )

        # Avoid host-mounted /proc namespace differences in subprocess-only tests
        self.process_lookup = mock.patch.object(bench.psutil, "Process")
        lookup = self.process_lookup.start()
        lookup.return_value.children.return_value = []
        self.addCleanup(self.process_lookup.stop)
        self.addCleanup(self.supervisor.stop)

    def test_missing_ignored_mime_file_is_initialized_and_existing_file_preserved(self):
        self.supervisor._prepare_config()
        target = self.root / "conf/mimes.conf"
        self.assertEqual(target.read_text(), ".html text/html\n")
        target.write_text("custom config\n")
        self.supervisor._prepare_config()
        self.assertEqual(target.read_text(), "custom config\n")
        target.unlink()
        (self.root / "conf/default/mimes.conf").unlink()
        with self.assertRaisesRegex(FileNotFoundError, "requires.*mimes.conf"):
            self.supervisor._prepare_config()

    def test_occupied_port_is_rejected_before_launch(self):
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", self.port))
            sock.listen()
            with mock.patch.object(bench.subprocess, "Popen") as launch:
                with self.assertRaisesRegex(RuntimeError, "already in use"):
                    self.supervisor.start()
                launch.assert_not_called()

    @unittest.skipIf(sys.platform == "win32", "Use native executable regression on Windows")
    def test_stdout_stderr_and_zero_exit_are_reported(self):
        self.binary.write_text(
            "#!/usr/bin/env python3\n"
            "import sys\n\n"
            'if "-t" in sys.argv:\n'
            "    sys.exit(0)\n\n"
            'print("stdout diagnostic", flush=True)\n'
            'print("stderr diagnostic", file=sys.stderr, flush=True)\n'
        )
        self.binary.chmod(0o755)
        with self.assertRaisesRegex(RuntimeError, "Exit code: 0") as error:
            self.supervisor.start()
        self.assertIn("stdout diagnostic", str(error.exception))
        self.assertIn("stderr diagnostic", str(error.exception))
        self.assertIsNone(self.supervisor.process)
        self.assertIsNone(self.supervisor.log_handle)

    @unittest.skipIf(sys.platform == "win32", "Use native executable regression on Windows")
    def test_stdin_stays_open_and_large_stdout_does_not_block(self):
        self.binary.write_text(
            "#!/usr/bin/env python3\n"
            "import os\n"
            "import socket\n"
            "import sys\n"
            "import threading\n\n"
            'if "-t" in sys.argv:\n'
            "    sys.exit(0)\n\n"
            "# Exit if benchmark runner closes stdin prematurely\n"
            "def wait_for_eof():\n"
            "    sys.stdin.read()\n"
            "    os._exit(9)\n\n"
            'print("x" * 200000, flush=True)\n'
            "threading.Thread(target=wait_for_eof, daemon=True).start()\n\n"
            "listener = socket.socket()\n"
            f'listener.bind(("127.0.0.1", {self.port}))\n'
            "listener.listen()\n\n"
            "while True:\n"
            "    connection, _ = listener.accept()\n"
            "    connection.close()\n"
        )
        self.binary.chmod(0o755)
        self.supervisor.start()
        process = self.supervisor.process
        self.assertIsNone(process.poll())
        self.supervisor.stop()
        self.assertIsNotNone(process.poll())

    def _start_native_mercury(self):
        # Use distributed executable so config tests also cover release compatibility
        binary = REPO_ROOT / "bin" / ("mercury.exe" if sys.platform == "win32" else "mercury")
        if not binary.is_file():
            self.skipTest("Build or acquire Mercury binary to run native benchmark regressions")

        shutil.copy2(binary, self.binary)
        self.supervisor.start()

    def test_native_mercury_starts_from_clean_checkout(self):
        self._start_native_mercury()
        self.assertTrue((self.root / "conf/mimes.conf").is_file())
        process = self.supervisor.process
        self.assertIsNone(process.poll())

        self.supervisor.stop()
        self.assertIsNotNone(process.poll())

    def test_native_mercury_serves_50_keep_alive_clients(self):
        # Exercise actual CI concurrency instead of checking only two-client smoke load
        fixture = self.root / "tests/root/benchmark/1k.html"
        fixture.parent.mkdir()
        fixture.write_bytes(b"x" * 1024)
        self._start_native_mercury()

        rps, _, _, _ = bench.LoadGenerator._run_python(
            "127.0.0.1", self.port, "/benchmark/1k.html", concurrency=50, duration=1
        )
        self.assertGreater(rps, 0)

    def test_runtime_thread_limits_follow_requested_concurrency(self):
        for concurrency, expected in [(1, 20), (50, 60), (100, 110)]:
            with self.subTest(concurrency=concurrency):
                supervisor = bench.ServerSupervisor(
                    "mercury",
                    str(self.binary),
                    self.port,
                    str(self.config),
                    str(self.root),
                    concurrency=concurrency,
                )
                generated = ET.parse(supervisor._prepare_config()).getroot()
                limit = int(generated.findtext("MaxThreadsPerChild"))

                self.assertEqual(limit, expected)
                self.assertGreater(limit, concurrency)
                self.assertGreater(limit, int(generated.findtext("IdleThreadsPerChild")))

                # Keep both Apache MPMs and ThreadLimit aligned with Mercury ceiling
                supervisor.name = "apache"
                supervisor.conf_file = str(REPO_ROOT / "tests/conf_files/benchmark/apache.conf")
                with mock.patch.object(
                    supervisor,
                    "_apache_config_values",
                    return_value={"LOAD_MODULES": "", "RUN_AS_USER": ""},
                ):
                    content = Path(supervisor._prepare_config()).read_text()

                self.assertEqual(content.count(f"ThreadsPerChild {expected}"), 2)
                self.assertIn(f"ThreadLimit {expected}", content)
                self.assertIn(f"MaxRequestWorkers {expected}", content)
                self.assertNotIn("@WORKER_THREADS@", content)


class SuiteLifecycleTests(unittest.TestCase):
    def setUp(self):
        self.events = []
        self.supervisors = []
        self.samplers = []
        self.contexts = ExitStack()
        self.addCleanup(self.contexts.close)

        # Preserve real sizing policy while replacing process and sampling boundaries
        sizing_policy = bench.ServerSupervisor.worker_threads_for
        supervisor_class = self._patch("ServerSupervisor", side_effect=self._supervisor)
        supervisor_class.worker_threads_for.side_effect = sizing_policy
        self._patch("MemorySampler", side_effect=self._sampler)
        self.contexts.enter_context(
            mock.patch.object(bench.Discovery, "get_version", return_value="v0.38.1")
        )

        # Record warmup and load order without spending time generating real traffic
        self.contexts.enter_context(mock.patch.object(bench.LoadGenerator, "validate_payload"))
        self.contexts.enter_context(
            mock.patch.object(bench.LoadGenerator, "_run_warmup", side_effect=self._warmup)
        )
        self.load = self.contexts.enter_context(
            mock.patch.object(bench.LoadGenerator, "run", side_effect=self._load)
        )
        self.contexts.enter_context(mock.patch.object(bench.time, "sleep"))
        self.contexts.enter_context(mock.patch("sys.stdout", new_callable=io.StringIO))

    def _patch(self, name, **kwargs):
        return self.contexts.enter_context(mock.patch.object(bench, name, **kwargs))

    def _supervisor(self, *args, **kwargs):
        supervisor = mock.Mock()
        supervisor.process.pid = 100
        supervisor.process.poll.return_value = None
        supervisor.start.side_effect = lambda: self.events.append("start")
        supervisor.stop.side_effect = lambda: self.events.append("stop")
        supervisor.concurrency = kwargs["concurrency"]
        self.supervisors.append(supervisor)
        return supervisor

    def _sampler(self, *args, **kwargs):
        sampler = mock.Mock()
        sampler.sample_now.return_value = 10
        sampler.start.side_effect = lambda: self.events.append("sample")

        def stop():
            self.events.append("sample-stop")
            return [12, 14]

        sampler.stop.side_effect = stop
        self.samplers.append(sampler)
        return sampler

    def _warmup(self, *args):
        self.events.append("warmup")

    def _load(self, **kwargs):
        self.events.append("load")
        return (100, 1, 1, 2)

    def test_trials_restart_servers_and_sample_only_active_load(self):
        suite = bench.BenchmarkSuite(["mercury"], trials=2, duration=1, tool_preference="python")
        suite.mercury_bin = "fake-mercury"
        result = suite.execute()

        self.assertEqual(
            self.events,
            ["start", "warmup", "sample", "load", "sample-stop", "stop"] * 2,
        )
        self.assertEqual(len(self.supervisors), 2)
        self.assertTrue(all(server.concurrency == 50 for server in self.supervisors))
        self.assertEqual(result.worker_thread_limit, 60)
        self.assertEqual(result.summaries["mercury"].load_rss_mb, 13)
        self.assertEqual(result.summaries["mercury"].trials_completed, 2)

    def test_sampler_and_server_stop_after_load_failure(self):
        self.load.side_effect = RuntimeError("load failed")
        suite = bench.BenchmarkSuite(["mercury"], duration=1, tool_preference="python")
        suite.mercury_bin = "fake-mercury"

        with self.assertRaisesRegex(RuntimeError, "load failed"):
            suite.execute()

        self.samplers[-1].stop.assert_called_once()
        self.supervisors[-1].stop.assert_called_once()


class ArgumentAndReportTests(unittest.TestCase):
    def test_invalid_arguments_fail_before_execution(self):
        cases = [
            ["--trials", "0"],
            ["--duration", "-1"],
            ["--concurrency", "0"],
            ["--warmup", "-1"],
            ["--servers", ""],
            ["--servers", "mercury,typo"],
            ["--servers", "mercury,mercury"],
            ["--output-format", "yaml"],
            ["--payload", "php"],
        ]
        for args in cases:
            with self.subTest(args=args), mock.patch.object(
                sys, "argv", ["benchmark.py"] + args
            ), mock.patch("sys.stderr", new_callable=io.StringIO):
                with self.assertRaises(SystemExit):
                    bench.parse_args()

    def test_readme_update_preserves_other_sections_and_is_repeatable(self):
        results = bench.BenchmarkResults(
            "now", "Linux", "test", "x86_64", 2, 1, 2, "1 KB", 1024, "python", trials=2
        )
        results.summaries["mercury"] = bench.ServerSummary("mercury", "v0.38.1", 2, rps=123)
        results.skipped_servers = ["apache"]
        markdown = bench.OutputFormatter.to_markdown(results)
        parsed = json.loads(bench.OutputFormatter.to_json(results))
        self.assertEqual(parsed["skipped_servers"], ["apache"])
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            report = directory / "linux.md"
            report.write_text(markdown)
            target = directory / "README.md"
            target.write_text((REPO_ROOT / "README.md").read_text())
            original = target.read_text()
            with mock.patch("sys.stdout", new_callable=io.StringIO):
                self.assertTrue(readme.update_readme(str(target), str(report)))
                first = target.read_text()
                self.assertTrue(readme.update_readme(str(target), str(report)))
            self.assertEqual(first, target.read_text())
            self.assertIn("Skipped missing servers: apache", first)
            self.assertEqual(first.count("*Baseline averages"), 1)
            self.assertIn("#### Windows (x64)", first)
            self.assertEqual(original.split("\n---\n")[-1], first.split("\n---\n")[-1])
            self.assertEqual(
                original.split("### Performance")[0], first.split("### Performance")[0]
            )


if __name__ == "__main__":
    unittest.main()
