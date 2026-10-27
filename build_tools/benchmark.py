#!/usr/bin/env python3
"""
Benchmark runner comparing Mercury, Apache, and Nginx.
Measures Idle RSS, Load RSS, Peak RSS, Throughput (RPS), and Latency.
"""

import argparse
from collections import Counter
from contextlib import closing
from dataclasses import asdict, dataclass, field
import glob
import http.client
import json
import os
import platform
import re
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
from typing import Dict, List, Optional, Tuple
import xml.etree.ElementTree as ET

import psutil

# ============================================================================
# Data Models
# ============================================================================


@dataclass
class TrialMetrics:
    server: str
    trial_index: int
    payload: str
    idle_rss_mb: float = 0.0
    load_rss_mb: float = 0.0
    peak_rss_mb: float = 0.0
    rps: float = 0.0
    latency_mean_ms: float = 0.0
    latency_p50_ms: float = 0.0
    latency_p99_ms: float = 0.0


@dataclass
class ServerSummary:
    server: str
    version: str
    trials_completed: int
    idle_rss_mb: float = 0.0
    load_rss_mb: float = 0.0
    peak_rss_mb: float = 0.0
    rps: float = 0.0
    latency_mean_ms: float = 0.0
    latency_p50_ms: float = 0.0
    latency_p99_ms: float = 0.0


@dataclass
class BenchmarkResults:
    timestamp: str
    platform_name: str
    os_release: str
    cpu_architecture: str
    cpu_count: int
    duration_seconds: int
    concurrency: int
    payload_name: str
    payload_size_bytes: int
    tool_used: str
    trials: int = 5
    worker_thread_limit: int = 0
    skipped_servers: List[str] = field(default_factory=list)
    summaries: Dict[str, ServerSummary] = field(default_factory=dict)
    raw_trials: List[TrialMetrics] = field(default_factory=list)


# ============================================================================
# Memory Sampler
# ============================================================================


class MemorySampler:
    """Samples total RSS memory in MB for a parent process and all its children"""

    def __init__(self, pid: int, interval_seconds: float = 0.05):
        self.pid = pid
        self.interval_seconds = interval_seconds
        self.samples: List[float] = []
        self._stop_event = threading.Event()
        self._worker_thread: Optional[threading.Thread] = None

    def _sample_loop(self):
        while not self._stop_event.is_set():
            rss = self.sample_now()
            if rss > 0.0:
                self.samples.append(rss)
            self._stop_event.wait(self.interval_seconds)

    def sample_now(self) -> float:
        try:
            parent = psutil.Process(self.pid)
            total_bytes = parent.memory_info().rss
            for child in parent.children(recursive=True):
                try:
                    total_bytes += child.memory_info().rss
                except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                    pass
            return total_bytes / (1024.0 * 1024.0)
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            return 0.0

    def start(self):
        self.samples.clear()
        self._stop_event.clear()
        self._worker_thread = threading.Thread(target=self._sample_loop, daemon=True)
        self._worker_thread.start()

    def stop(self) -> List[float]:
        self._stop_event.set()
        if self._worker_thread is not None:
            self._worker_thread.join(timeout=2.0)
        return list(self.samples)


# ============================================================================
# Load Generation Engines
# ============================================================================


class LoadGenerator:
    """Dispatches load testing to wrk, hey, or pure Python fallback"""

    @staticmethod
    def detect_tool(preferred: str = "auto") -> str:
        if preferred != "auto":
            if preferred == "python" or shutil.which(preferred):
                return preferred
            raise FileNotFoundError(f"Requested load generator not found: {preferred}")

        # Auto-detect preferring wrk -> hey -> python
        if shutil.which("wrk"):
            return "wrk"
        if shutil.which("hey"):
            return "hey"
        return "python"

    @classmethod
    def run(
        cls,
        tool: str,
        host: str,
        port: int,
        path: str,
        concurrency: int,
        duration: int,
        warmup_requests: int = 200,
    ) -> Tuple[float, float, float, float]:
        """Runs warmup and timed load phase. Returns (rps, mean_ms, p50_ms, p99_ms)"""
        # Run warmup requests to prime socket buffers and filesystem caches
        cls._run_warmup(host, port, path, count=warmup_requests)

        # Run timed load phase via selected tool
        if tool == "wrk":
            return cls._run_wrk(host, port, path, concurrency, duration)
        elif tool == "hey":
            return cls._run_hey(host, port, path, concurrency, duration)
        else:
            return cls._run_python(host, port, path, concurrency, duration)

    @staticmethod
    def _run_warmup(host: str, port: int, path: str, count: int = 200):
        # Used to prime file caches, socket buffers, and thread pool workers
        if count <= 0:
            return

        headers = {
            "Accept-Encoding": "identity",
            "Connection": "keep-alive",
            "User-Agent": "MercuryBenchmark/1.0",
        }
        with closing(http.client.HTTPConnection(host, port, timeout=5)) as conn:
            for _ in range(count):
                conn.request("GET", path, headers=headers)
                resp = conn.getresponse()
                resp.read()
                if resp.status != 200:
                    raise RuntimeError(f"Warmup returned HTTP {resp.status} for {path}")

    @staticmethod
    def validate_payload(host: str, port: int, path: str, expected: bytes):
        # Reject error pages, redirects, compression, and incorrect document roots before timing
        with closing(http.client.HTTPConnection(host, port, timeout=5)) as conn:
            conn.request("GET", path, headers={"Accept-Encoding": "identity"})
            resp = conn.getresponse()
            body = resp.read()
            if (
                resp.status != 200
                or body != expected
                or resp.getheader("Content-Encoding") not in (None, "identity")
            ):
                raise RuntimeError(
                    f"Payload validation failed for {path}: HTTP {resp.status}, {len(body)} bytes"
                )

    @staticmethod
    def _run_wrk(
        host: str, port: int, path: str, concurrency: int, duration: int
    ) -> Tuple[float, float, float, float]:
        # Returns (rps, mean_ms, p50_ms, p99_ms) via wrk load generator
        url = f"http://{host}:{port}{path}"

        # Bound worker thread count to concurrency and available CPU cores
        threads = min(concurrency, max(1, os.cpu_count() or 2))

        # Construct execution command
        cmd = [
            "wrk",
            "-t",
            str(threads),
            "-c",
            str(concurrency),
            "-d",
            f"{duration}s",
            "--latency",
            "-H",
            "Accept-Encoding: identity",
            url,
        ]

        # Execute wrk load generator via subprocess
        try:
            res = subprocess.run(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=True,
                timeout=duration + 30,
            )
            out = res.stdout
        except (subprocess.CalledProcessError, FileNotFoundError, subprocess.TimeoutExpired) as exc:
            raise RuntimeError(
                f"Load generator failed: {exc}; {getattr(exc, 'stderr', '')}"
            ) from exc

        if re.search(r"Non-2xx or 3xx responses:\s*[1-9]", out) or "Socket errors:" in out:
            raise RuntimeError(f"wrk reported failed requests:\n{out}")

        # Initialize metric accumulators
        rps = 0.0
        mean_ms = 0.0
        p50_ms = 0.0
        p99_ms = 0.0

        # Parse requests per second
        rps_match = re.search(r"Requests/sec:\s+([\d\.]+)([kKmM]?)", out)
        if rps_match:
            val = float(rps_match.group(1))
            mult = rps_match.group(2).lower()
            # Apply unit multiplier for kilo and mega notations
            if mult == "k":
                val *= 1000.0
            elif mult == "m":
                val *= 1000000.0
            rps = val

        # Parse mean latency
        lat_match = re.search(r"Latency\s+([\d\.]+)(us|ms|s|m)", out)
        if lat_match:
            val = float(lat_match.group(1))
            unit = lat_match.group(2)
            # Normalize latency value to milliseconds
            if unit == "us":
                val /= 1000.0
            elif unit == "s":
                val *= 1000.0
            elif unit == "m":
                val *= 60000.0
            mean_ms = val

        # Parse 50th percentile (median) latency
        p50_match = re.search(r"50%\s+([\d\.]+)(us|ms|s|m)", out)
        if p50_match:
            val = float(p50_match.group(1))
            unit = p50_match.group(2)
            # Normalize latency value to milliseconds
            if unit == "us":
                val /= 1000.0
            elif unit == "s":
                val *= 1000.0
            elif unit == "m":
                val *= 60000.0
            p50_ms = val

        # Parse 99th percentile (tail) latency
        p99_match = re.search(r"99%\s+([\d\.]+)(us|ms|s|m)", out)
        if p99_match:
            val = float(p99_match.group(1))
            unit = p99_match.group(2)
            # Normalize latency value to milliseconds
            if unit == "us":
                val /= 1000.0
            elif unit == "s":
                val *= 1000.0
            elif unit == "m":
                val *= 60000.0
            p99_ms = val

        if rps <= 0 or not all((rps_match, lat_match, p50_match, p99_match)):
            raise RuntimeError("Load generator completed without successful requests")
        return (rps, mean_ms, p50_ms, p99_ms)

    @staticmethod
    def _run_hey(
        host: str, port: int, path: str, concurrency: int, duration: int
    ) -> Tuple[float, float, float, float]:
        # Returns (rps, mean_ms, p50_ms, p99_ms) via hey load generator
        url = f"http://{host}:{port}{path}"

        # Construct execution command
        cmd = [
            "hey",
            "-c",
            str(concurrency),
            "-z",
            f"{duration}s",
            "-disable-compression",
            "-disable-redirects",
            url,
        ]

        # Execute hey load generator via subprocess
        try:
            res = subprocess.run(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=True,
                timeout=duration + 30,
            )
            out = res.stdout
        except (subprocess.CalledProcessError, FileNotFoundError, subprocess.TimeoutExpired) as exc:
            raise RuntimeError(
                f"Load generator failed: {exc}; {getattr(exc, 'stderr', '')}"
            ) from exc

        statuses = re.findall(r"\[([0-9]{3})\]\s+(\d+) responses", out)
        if (
            not statuses
            or any(code != "200" and int(count) > 0 for code, count in statuses)
            or "Error distribution:" in out
        ):
            raise RuntimeError(f"hey reported failed requests or missing status counts:\n{out}")

        # Initialize metric accumulators
        rps = mean_ms = p50_ms = p99_ms = 0.0

        # Parse requests per second
        rps_match = re.search(r"Requests/sec:\s+([\d\.]+)", out)
        if rps_match:
            rps = float(rps_match.group(1))

        # Parse 50th percentile latency and convert seconds to milliseconds
        p50_match = re.search(r"50%\s+in\s+([\d\.]+)\s+secs", out)
        if p50_match:
            p50_ms = float(p50_match.group(1)) * 1000.0

        # Parse 99th percentile latency and convert seconds to milliseconds
        p99_match = re.search(r"99%\s+in\s+([\d\.]+)\s+secs", out)
        if p99_match:
            p99_ms = float(p99_match.group(1)) * 1000.0

        # Parse mean latency and convert seconds to milliseconds
        mean_match = re.search(r"Average:\s+([\d\.]+)\s+secs", out)
        if mean_match:
            mean_ms = float(mean_match.group(1)) * 1000.0
        else:
            mean_ms = p50_ms

        if rps <= 0 or not all((rps_match, mean_match, p50_match, p99_match)):
            raise RuntimeError("Load generator completed without successful requests")
        return (rps, mean_ms, p50_ms, p99_ms)

    @staticmethod
    def _run_python(
        host: str, port: int, path: str, concurrency: int, duration: int
    ) -> Tuple[float, float, float, float]:
        # Returns (rps, mean_ms, p50_ms, p99_ms) via multithreaded keep-alive client
        stop_time = time.perf_counter() + duration

        # Thread-safe container collecting per-thread request counts and latency samples
        stats_list: List[Tuple[int, List[float], int]] = []
        stats_lock = threading.Lock()
        error_counts = Counter()
        headers = {
            "Accept-Encoding": "identity",
            "Connection": "keep-alive",
            "User-Agent": "MercuryBenchmark/1.0",
        }

        # Worker routine executing persistent HTTP keep-alive loop
        def worker():
            local_reqs = 0
            local_latencies: List[float] = []
            conn: Optional[http.client.HTTPConnection] = None
            consecutive_errors = 0
            failed_reqs = 0
            local_errors = Counter()

            # Instantiate persistent HTTP connection
            def get_conn():
                return http.client.HTTPConnection(host, port, timeout=5)

            # Attempt initial connection before entering timed loop
            try:
                conn = get_conn()
            except Exception:
                pass

            # Execute timed keep-alive request loop until deadline
            while time.perf_counter() < stop_time:
                t0 = time.perf_counter()
                try:
                    if conn is None:
                        conn = get_conn()

                    # Dispatch HTTP GET request with keep-alive headers
                    conn.request("GET", path, headers=headers)
                    resp = conn.getresponse()

                    # Read and drain full response payload to enable socket reuse
                    resp.read()
                    if resp.status != 200:
                        raise RuntimeError(f"HTTP {resp.status}")
                    t1 = time.perf_counter()

                    local_reqs += 1
                    # Record round-trip request latency in milliseconds
                    local_latencies.append((t1 - t0) * 1000.0)

                    # Reset consecutive error counter on successful exchange
                    consecutive_errors = 0
                except (OSError, http.client.HTTPException, RuntimeError) as exc:
                    # Close corrupted socket connection on error
                    if conn:
                        try:
                            conn.close()
                        except Exception:
                            pass
                        conn = None

                    failed_reqs += 1
                    local_errors[f"{type(exc).__name__}: {exc}"] += 1
                    consecutive_errors += 1
                    # Throttle briefly when encountering consecutive connection failures
                    if consecutive_errors >= 5:
                        time.sleep(0.01)

            # Clean up active connection upon worker exit
            if conn:
                try:
                    conn.close()
                except Exception:
                    pass

            # Store thread performance metrics in thread-safe container
            with stats_lock:
                stats_list.append((local_reqs, local_latencies, failed_reqs))
                error_counts.update(local_errors)

        # Record start timestamp across worker pool
        start_actual = time.perf_counter()
        threads = []

        # Spawn worker thread pool matching requested concurrency
        for _ in range(concurrency):
            t = threading.Thread(target=worker, daemon=True)
            # Launch worker thread
            t.start()
            threads.append(t)

        # Wait for all worker threads to reach completion deadline
        for t in threads:
            t.join()

        # Record completion timestamp across worker pool
        end_actual = time.perf_counter()

        # Compute aggregate throughput and elapsed duration
        elapsed = max(0.001, end_actual - start_actual)
        total_reqs = sum(reqs for reqs, _, _ in stats_list)
        all_latencies = [lat for _, lats, _ in stats_list for lat in lats]

        failures = sum(errors for _, _, errors in stats_list)
        if failures:
            details = "; ".join(f"{error}: {count}" for error, count in error_counts.most_common())
            raise RuntimeError(
                f"Python load generator recorded {failures} failed requests; discard trial. {details}"
            )
        rps = total_reqs / elapsed

        # Extract and sort aggregated latencies for percentile calculations
        if all_latencies:
            all_latencies.sort()
            # Compute mean, p50, and p99 percentiles
            mean_ms = sum(all_latencies) / len(all_latencies)
            idx_50 = int(len(all_latencies) * 0.50)
            idx_99 = int(len(all_latencies) * 0.99)
            p50_ms = all_latencies[min(idx_50, len(all_latencies) - 1)]
            p99_ms = all_latencies[min(idx_99, len(all_latencies) - 1)]
        else:
            mean_ms = p50_ms = p99_ms = 0.0

        if rps <= 0:
            raise RuntimeError("Load generator completed without successful requests")
        return (rps, mean_ms, p50_ms, p99_ms)


# ============================================================================
# Server Process Supervisor
# ============================================================================


class ServerSupervisor:
    """Manages server startup, socket readiness, diagnostics, and process cleanup"""

    def __init__(
        self,
        name: str,
        binary_path: str,
        port: int,
        conf_file: str,
        repo_root: str,
        concurrency: int = 50,
    ):
        self.name = name
        self.binary_path = os.path.abspath(binary_path)
        self.port = port
        self.conf_file = conf_file
        self.repo_root = os.path.abspath(repo_root)
        self.concurrency = concurrency
        self.worker_threads = self.worker_threads_for(concurrency)
        self.process: Optional[subprocess.Popen] = None
        self.log_handle = None
        self.command: List[str] = []
        self.runtime_conf = ""
        self.bench_dir = os.path.join(self.repo_root, "logs", "benchmark")
        self.startup_log = os.path.join(self.bench_dir, f"{name}_startup.log")

    @staticmethod
    def worker_threads_for(concurrency: int) -> int:
        # Reserve worker per persistent client plus room for connection turnover
        if concurrency <= 0:
            raise ValueError("Concurrency must be positive")

        return max(20, concurrency + 10)

    @staticmethod
    def _config_path(path: str) -> str:
        # Use forward slashes for Apache and Nginx on Windows
        return os.path.abspath(path).replace("\\", "/")

    def _prepare_mercury_config(self, source: str, destination: str):
        # Initialize ignored MIME file without replacing local customizations
        runtime_root = os.path.dirname(os.path.dirname(self.binary_path))
        mime_file = os.path.join(runtime_root, "conf", "mimes.conf")

        if not os.path.exists(mime_file):
            default_mimes = os.path.join(runtime_root, "conf", "default", "mimes.conf")
            if not os.path.isfile(default_mimes):
                raise FileNotFoundError(
                    f"Mercury requires {mime_file}; default MIME config missing: {default_mimes}"
                )

            shutil.copyfile(default_mimes, mime_file)

        # Retain template comments and readable indentation in runtime XML
        parser = ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))
        tree = ET.parse(source, parser=parser)
        root = tree.getroot()
        values = {
            "DocumentRoot": os.path.join(self.repo_root, "tests", "root"),
            "AccessLogFile": os.devnull,
            "ErrorLogFile": os.path.join(self.bench_dir, "mercury_error.log"),
            "MaxThreadsPerChild": str(self.worker_threads),
        }

        # Assign worker ceiling for requested persistent-client concurrency
        for key, value in values.items():
            root.find(key).text = value

        ET.indent(tree, space="    ")
        tree.write(destination, encoding="utf-8")

    def _apache_config_values(self) -> Dict[str, str]:
        # Resolve platform MPM and authorization modules from installed Apache
        modules, builtins = self._apache_modules()
        required = [
            "mpm_winnt" if sys.platform == "win32" else "mpm_event",
            "authz_core",
            "dir",
            "mime",
        ]
        loads = []

        for module in required:
            if f"{module}.c" in builtins or f"mod_{module}.c" in builtins:
                continue

            path = os.path.join(modules, f"mod_{module}.so")
            if not os.path.isfile(path):
                raise FileNotFoundError(f"Required Apache module missing: {path}")

            loads.append(f'LoadModule {module}_module "{self._config_path(path)}"')

        # Match invoking Unix identity while leaving Windows MPM identity unchanged
        run_as_user = ""
        if sys.platform != "win32":
            import grp
            import pwd

            if os.geteuid() == 0:
                raise RuntimeError("Run Apache benchmarks as an unprivileged user, without sudo")

            user = pwd.getpwuid(os.geteuid()).pw_name
            group = grp.getgrgid(os.getegid()).gr_name
            run_as_user = f'User "{user}"\nGroup "{group}"'

        return {"LOAD_MODULES": "\n".join(loads), "RUN_AS_USER": run_as_user}

    def _prepare_config(self) -> str:
        source = os.path.join(self.repo_root, self.conf_file)
        destination = os.path.join(self.bench_dir, f"{self.name}-runtime.conf")

        if self.name == "mercury":
            self._prepare_mercury_config(source, destination)
            return self._config_path(destination)

        with open(source, encoding="utf-8") as handle:
            content = handle.read()

        # Match threaded ceilings and preserve Nginx event-driven connection handling
        values = {
            "REPO_ROOT": self._config_path(self.repo_root),
            "WORKER_THREADS": str(self.worker_threads),
            "WORKER_CONNECTIONS": str(max(1024, self.concurrency * 2)),
        }
        if self.name == "apache":
            values.update(self._apache_config_values())

        for key, value in values.items():
            content = content.replace(f"@{key}@", value)

        with open(destination, "w", encoding="utf-8") as handle:
            handle.write(content)

        return self._config_path(destination)

    def _apache_modules(self) -> Tuple[str, str]:
        probe = subprocess.run(
            [self.binary_path, "-l"], capture_output=True, text=True, timeout=15, check=True
        )
        binary_dir = os.path.dirname(os.path.realpath(self.binary_path))
        candidates = [
            os.path.join(binary_dir, "..", "modules"),
            "/usr/lib/apache2/modules",
            "/usr/lib64/httpd/modules",
            "/usr/lib/httpd/modules",
            "/usr/local/apache2/modules",
        ]
        for directory in candidates:
            if os.path.isdir(directory):
                return os.path.abspath(directory), probe.stdout
        raise FileNotFoundError(f"Cannot locate Apache modules beside {self.binary_path}")

    def _diagnostics(self) -> str:
        details = []
        for path in [self.startup_log, os.path.join(self.bench_dir, f"{self.name}_error.log")]:
            try:
                with open(path, "rb") as handle:
                    handle.seek(0, os.SEEK_END)
                    handle.seek(max(0, handle.tell() - 8192))
                    tail = handle.read().decode("utf-8", errors="replace").strip()
                if tail:
                    details.append(f"{path}:\n{tail}")
            except OSError:
                pass
        code = self.process.poll() if self.process else None
        return f"Exit code: {code}; command: {self.command!r}\n" + (
            "\n".join(details) or "No server output; inspect runner crash diagnostics"
        )

    def start(self):
        if not os.path.isfile(self.binary_path):
            raise FileNotFoundError(f"{self.name} binary not found: {self.binary_path}")
        if not self._wait_port_free(self.port, timeout=3):
            raise RuntimeError(
                f"Port {self.port} is already in use; stop existing listener before benchmarking"
            )

        os.makedirs(self.bench_dir, exist_ok=True)
        for sub_dir in [
            "nginx_client_body",
            "nginx_proxy",
            "nginx_fastcgi",
            "nginx_uwsgi",
            "nginx_scgi",
        ]:
            os.makedirs(os.path.join(self.bench_dir, sub_dir), exist_ok=True)
        error_log = os.path.join(self.bench_dir, f"{self.name}_error.log")
        with open(error_log, "w", encoding="utf-8"):
            pass
        self.runtime_conf = self._prepare_config()
        prefix = self._config_path(self.repo_root) + "/"
        if self.name == "mercury":
            base = [self.binary_path, "-c", self.runtime_conf]
            self.command = base
        elif self.name == "apache":
            # Preserve installed ServerRoot for Windows child process initialization
            server_root = (
                os.path.dirname(os.path.dirname(self.binary_path))
                if sys.platform == "win32"
                else self.repo_root
            )
            base = [self.binary_path, "-d", self._config_path(server_root), "-f", self.runtime_conf]
            self.command = base + ["-DFOREGROUND"]
        elif self.name == "nginx":
            base = [self.binary_path, "-p", prefix, "-c", self.runtime_conf]
            self.command = base + ["-g", "daemon off;"]
        else:
            raise ValueError(f"Unknown server: {self.name}")

        # Retain stdout and stderr without bounded pipe buffers or inherited stdin EOF
        self.log_handle = open(self.startup_log, "wb")
        try:
            check = subprocess.run(
                base + ["-t"],
                stdin=subprocess.DEVNULL,
                stdout=self.log_handle,
                stderr=subprocess.STDOUT,
                cwd=self.repo_root,
                timeout=15,
            )
            if check.returncode != 0:
                raise RuntimeError(
                    f"{self.name} config check failed (exit {check.returncode})\n{self._diagnostics()}"
                )
            self.process = subprocess.Popen(
                self.command,
                stdin=subprocess.PIPE,
                stdout=self.log_handle,
                stderr=subprocess.STDOUT,
                cwd=self.repo_root,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == "win32" else 0,
                start_new_session=sys.platform != "win32",
            )
            if not self._wait_port_ready(self.port, timeout=10):
                raise RuntimeError(
                    f"{self.name} failed to listen on port {self.port}\n{self._diagnostics()}"
                )
        except BaseException:
            self.stop()
            raise

    def stop(self):
        try:
            if self.process is None:
                return
            children = []
            try:
                children = psutil.Process(self.process.pid).children(recursive=True)
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass

            if self.process.poll() is None:
                try:
                    self.process.send_signal(
                        signal.CTRL_BREAK_EVENT if sys.platform == "win32" else signal.SIGINT
                    )
                    self.process.wait(timeout=5)
                except (OSError, ValueError, subprocess.TimeoutExpired):
                    self.process.kill()
                    self.process.wait(timeout=5)

            for child in children:
                try:
                    child.terminate()
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    pass
            _, alive = psutil.wait_procs(children, timeout=2)
            for child in alive:
                try:
                    child.kill()
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    pass
            psutil.wait_procs(alive, timeout=2)
            if self.process.stdin:
                self.process.stdin.close()
        finally:
            self.process = None
            if self.log_handle:
                self.log_handle.close()
                self.log_handle = None

    @staticmethod
    def _ping_socket(host: str, port: int) -> bool:
        try:
            with socket.create_connection((host, port), timeout=0.25):
                return True
        except OSError:
            return False

    @staticmethod
    def _is_port_bindable(port: int) -> bool:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
                if sys.platform == "win32":
                    sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
                else:
                    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                sock.bind(("127.0.0.1", port))
            return True
        except OSError:
            return False

    def _wait_port_ready(self, port: int, timeout: float = 10) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"{self.name} exited before readiness\n{self._diagnostics()}")
            if self._ping_socket("127.0.0.1", port):
                return True
            time.sleep(0.1)
        return False

    def _wait_port_free(self, port: int, timeout: float = 10) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not self._ping_socket("127.0.0.1", port) and self._is_port_bindable(port):
                return True
            time.sleep(0.1)
        return False


# ============================================================================
# Environment & Binary Discovery
# ============================================================================


class Discovery:
    """Detects server binaries, versions, and paths across platforms"""

    @staticmethod
    def repo_root() -> str:
        # Resolve repository root from script location
        script_dir = os.path.dirname(os.path.abspath(__file__))
        return os.path.abspath(os.path.join(script_dir, ".."))

    @classmethod
    def find_mercury(cls, override_path: Optional[str] = None) -> Optional[str]:
        if override_path:
            resolved = shutil.which(override_path) or override_path
            if not os.path.isfile(resolved):
                raise FileNotFoundError(f"Executable override not found: {override_path}")
            return os.path.abspath(resolved)

        root = cls.repo_root()
        is_windows = sys.platform in ["win32", "cygwin"]
        candidates = [
            os.path.join(root, "bin", "mercury.exe" if is_windows else "mercury"),
            os.path.join(root, "mercury.exe" if is_windows else "mercury"),
        ]

        for cand in candidates:
            if os.path.exists(cand):
                return os.path.abspath(cand)
        return None

    @classmethod
    def find_apache(cls, override_path: Optional[str] = None) -> Optional[str]:
        if override_path:
            resolved = shutil.which(override_path) or override_path
            if not os.path.isfile(resolved):
                raise FileNotFoundError(f"Executable override not found: {override_path}")
            return os.path.abspath(resolved)

        if sys.platform == "win32":
            roots = [
                os.environ.get("ChocolateyToolsLocation", "C:/tools"),
                os.environ.get("ChocolateyInstall", "C:/ProgramData/chocolatey") + "/lib",
                "C:/Apache24",
                "C:/nginx",
            ]
            for root in roots:
                for match in sorted(
                    glob.glob(os.path.join(root, "**", "httpd.exe"), recursive=True)
                ):
                    if os.path.isfile(match):
                        return os.path.abspath(match)

        candidates = [
            shutil.which("apache2"),
            shutil.which("httpd"),
            shutil.which("httpd.exe"),
            "/usr/sbin/apache2",
            "/usr/sbin/httpd",
            "C:\\Apache24\\bin\\httpd.exe",
            "C:\\tools\\apache24\\bin\\httpd.exe",
            "C:\\ProgramData\\chocolatey\\bin\\httpd.exe",
        ]

        for cand in candidates:
            if cand and os.path.exists(cand):
                return os.path.abspath(cand)

        # Search Chocolatey and tools directories on Windows
        if sys.platform in ["win32", "cygwin"]:
            for match in glob.glob("C:\\tools\\**\\httpd.exe", recursive=True):
                if os.path.exists(match):
                    return os.path.abspath(match)

        return None

    @classmethod
    def find_nginx(cls, override_path: Optional[str] = None) -> Optional[str]:
        if override_path:
            resolved = shutil.which(override_path) or override_path
            if not os.path.isfile(resolved):
                raise FileNotFoundError(f"Executable override not found: {override_path}")
            return os.path.abspath(resolved)

        if sys.platform == "win32":
            roots = [
                os.environ.get("ChocolateyToolsLocation", "C:/tools"),
                os.environ.get("ChocolateyInstall", "C:/ProgramData/chocolatey") + "/lib",
                "C:/Apache24",
                "C:/nginx",
            ]
            for root in roots:
                for match in sorted(
                    glob.glob(os.path.join(root, "**", "nginx.exe"), recursive=True)
                ):
                    if os.path.isfile(match):
                        return os.path.abspath(match)

        candidates = [
            shutil.which("nginx"),
            shutil.which("nginx.exe"),
            "/usr/sbin/nginx",
            "C:\\nginx\\nginx.exe",
            "C:\\tools\\nginx\\nginx.exe",
            "C:\\ProgramData\\chocolatey\\bin\\nginx.exe",
        ]

        for cand in candidates:
            if cand and os.path.exists(cand):
                return os.path.abspath(cand)

        # Search Chocolatey and tools directories on Windows
        if sys.platform in ["win32", "cygwin"]:
            for match in glob.glob("C:\\tools\\**\\nginx.exe", recursive=True):
                if os.path.exists(match):
                    return os.path.abspath(match)

        return None

    @classmethod
    def get_version(cls, server: str, binary_path: str) -> str:
        if server == "mercury":
            try:
                result = subprocess.run(
                    [binary_path, "-v"], capture_output=True, text=True, timeout=10, check=True
                )
                match = re.search(r"Mercury\s+(v?\d+\.\d+\.\d+)", result.stdout + result.stderr)
                if match:
                    return "v" + match.group(1).lstrip("v")
            except (OSError, subprocess.SubprocessError):
                pass
            return "unknown"

        try:
            if server == "apache":
                res = subprocess.run(
                    [binary_path, "-v"],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    timeout=10,
                    check=True,
                )
                match = re.search(r"Apache/(\d+\.\d+\.\d+)", res.stdout)
                if match:
                    return match.group(1)
            elif server == "nginx":
                res = subprocess.run(
                    [binary_path, "-v"],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    timeout=10,
                    check=True,
                )
                match = re.search(r"nginx/(\d+\.\d+\.\d+)", res.stderr or res.stdout)
                if match:
                    return match.group(1)
        except Exception:
            pass

        return "unknown"


# ============================================================================
# Statistics & Multi-Trial Aggregator
# ============================================================================


class Statistics:
    """Computes averages and relative comparison metrics across trials"""

    @staticmethod
    def average_trials(trials: List[TrialMetrics], server_name: str, version: str) -> ServerSummary:
        server_trials = [t for t in trials if t.server == server_name]
        n = len(server_trials)
        if n == 0:
            return ServerSummary(server=server_name, version=version, trials_completed=0)

        # Average metrics across all completed trials
        avg_idle = sum(t.idle_rss_mb for t in server_trials) / n
        avg_load = sum(t.load_rss_mb for t in server_trials) / n
        avg_peak = sum(t.peak_rss_mb for t in server_trials) / n
        avg_rps = sum(t.rps for t in server_trials) / n
        avg_mean = sum(t.latency_mean_ms for t in server_trials) / n
        avg_p50 = sum(t.latency_p50_ms for t in server_trials) / n
        avg_p99 = sum(t.latency_p99_ms for t in server_trials) / n

        return ServerSummary(
            server=server_name,
            version=version,
            trials_completed=n,
            idle_rss_mb=avg_idle,
            load_rss_mb=avg_load,
            peak_rss_mb=avg_peak,
            rps=avg_rps,
            latency_mean_ms=avg_mean,
            latency_p50_ms=avg_p50,
            latency_p99_ms=avg_p99,
        )

    @staticmethod
    def compare_percent(base: float, target: float, metric_type: str = "memory") -> str:
        """Returns comparison string relative to base (Mercury)"""
        if target <= 0.0 or base <= 0.0:
            return "N/A"

        if metric_type in ["throughput", "rps"]:
            # Throughput comparison (higher is better)
            diff = ((base - target) / target) * 100.0
            if diff >= 0.0:
                return f"**Mercury is {diff:.0f}% faster**"
            else:
                return f"Mercury is {abs(diff):.0f}% slower"
        elif metric_type == "memory":
            # Memory comparison (lower is better)
            diff = ((target - base) / target) * 100.0
            if diff >= 0.0:
                return f"**Mercury is {diff:.0f}% lighter**"
            else:
                return f"Mercury is {abs(diff):.0f}% heavier"
        else:
            # Latency comparison (lower is better)
            diff = ((target - base) / target) * 100.0
            if diff >= 0.0:
                return f"**Mercury is {diff:.0f}% lower**"
            else:
                return f"Mercury is {abs(diff):.0f}% higher"


# ============================================================================
# Output Formatters
# ============================================================================


class OutputFormatter:
    """Renders results into Markdown tables matching README specifications and JSON"""

    @classmethod
    def to_markdown(cls, results: BenchmarkResults) -> str:
        summaries = results.summaries
        mercury = summaries.get("mercury")

        # Determine OS header string
        is_windows = (
            "windows" in results.platform_name.lower() or "win32" in results.platform_name.lower()
        )
        if is_windows:
            arch = (
                "x64"
                if results.cpu_architecture.lower() in ("amd64", "x86_64", "x64")
                else results.cpu_architecture
            )
            platform_header = f"Windows ({arch})"
        else:
            distro_desc = ""
            if hasattr(platform, "freedesktop_os_release"):
                try:
                    distro_desc = platform.freedesktop_os_release().get("PRETTY_NAME", "")
                except Exception:
                    distro_desc = ""
            if not distro_desc:
                distro_desc = f"{platform.system()} {platform.release()}"
            platform_header = f"Linux ({distro_desc} {results.cpu_architecture})"

        lines = [
            f"#### {platform_header}",
            "",
        ]

        # Check which servers were benchmarked
        servers_present = [s for s in ["mercury", "apache", "nginx"] if s in summaries]

        # Build column header
        header_cols = ["Metric"]
        align_cols = ["|---"]

        for s in servers_present:
            summ = summaries[s]
            header_cols.append(f"{s.capitalize()} ({summ.version})")
            align_cols.append("|---")

        if len(servers_present) > 1 and mercury:
            header_cols.append("Comparison")
            align_cols.append("|:---:")

        lines.append("| " + " | ".join(header_cols) + " |")
        lines.append("".join(align_cols) + "|")

        # Formatter helper for rows
        def format_row(metric_name: str, key: str, unit: str, metric_type: str = "memory"):
            row = [f"**{metric_name}**"]
            for s in servers_present:
                val = getattr(summaries[s], key)
                if key == "rps":
                    formatted = f"{val:,.0f} {unit}"
                else:
                    formatted = f"{val:.1f} {unit}"
                if s == "mercury":
                    formatted = f"**{formatted}**"
                row.append(formatted)

            if len(servers_present) > 1 and mercury:
                # Compare against Apache baseline if present, else first available competitor
                competitors = [s for s in servers_present if s != "mercury"]
                primary_comp = "apache" if "apache" in competitors else competitors[0]
                target_val = getattr(summaries[primary_comp], key)
                base_val = getattr(mercury, key)

                comp_str = Statistics.compare_percent(base_val, target_val, metric_type=metric_type)
                row.append(comp_str)

            return "| " + " | ".join(row) + " |"

        lines.append(format_row("Avg. Idle Memory", "idle_rss_mb", "MB", metric_type="memory"))
        lines.append(format_row("Avg. Load Memory", "load_rss_mb", "MB", metric_type="memory"))
        lines.append(format_row("Avg. Peak Memory", "peak_rss_mb", "MB", metric_type="memory"))
        lines.append(
            format_row(f"Req / Second ({results.payload_name})", "rps", "rps", metric_type="rps")
        )
        lines.append(
            format_row("Avg. Latency (mean)", "latency_mean_ms", "ms", metric_type="latency")
        )
        lines.append(
            format_row("Avg. Latency (p50)", "latency_p50_ms", "ms", metric_type="latency")
        )
        lines.append(
            format_row("Avg. Latency (p99)", "latency_p99_ms", "ms", metric_type="latency")
        )

        if mercury and len(servers_present) > 1:
            lines.append("\nComparison uses Apache as baseline when present, otherwise Nginx.")
        if results.skipped_servers:
            lines.append("\nSkipped missing servers: " + ", ".join(results.skipped_servers) + ".")
        lines.append(
            f"\n*Baseline averages across {results.trials} trials over loopback "
            f"using tests/conf_files/benchmark; tool={results.tool_used}, "
            f"concurrency={results.concurrency}, duration={results.duration_seconds}s, "
            f"payload={results.payload_size_bytes} bytes, "
            f"threaded worker limit={results.worker_thread_limit}. "
            "RSS sums parent and child resident memory "
            "(shared pages may be counted repeatedly); CI results are host-dependent.*"
        )
        return "\n".join(lines)

    @classmethod
    def to_json(cls, results: BenchmarkResults) -> str:
        data = asdict(results)
        return json.dumps(data, indent=4)


# ============================================================================
# Main Benchmark Runner
# ============================================================================


class BenchmarkSuite:
    """Executes benchmark trials across selected web servers"""

    def __init__(
        self,
        servers: List[str],
        trials: int = 5,
        duration: int = 10,
        concurrency: int = 50,
        warmup_requests: int = 200,
        payload_key: str = "1k",
        tool_preference: str = "auto",
        mercury_path: Optional[str] = None,
        apache_path: Optional[str] = None,
        nginx_path: Optional[str] = None,
        mercury_version: Optional[str] = None,
    ):
        self.mercury_version = mercury_version
        self.requested_servers = servers
        self.trials = trials
        self.duration = duration
        self.concurrency = concurrency
        self.warmup_requests = warmup_requests
        self.payload_key = payload_key
        self.tool_preference = tool_preference

        if min(trials, duration, concurrency) <= 0 or warmup_requests < 0:
            raise ValueError(
                "Trials, duration, and concurrency must be positive; warmup must be nonnegative"
            )
        if not servers or set(servers) - {"mercury", "apache", "nginx"}:
            raise ValueError("Select at least one known server: mercury,apache,nginx")
        if payload_key == "php":
            raise ValueError(
                "PHP benchmarking requires backend configuration for all servers; use static payloads"
            )
        self.repo_root = Discovery.repo_root()
        self.mercury_bin = Discovery.find_mercury(mercury_path)
        self.apache_bin = Discovery.find_apache(apache_path)
        self.nginx_bin = Discovery.find_nginx(nginx_path)

        self.tool = LoadGenerator.detect_tool(self.tool_preference)

    def _resolve_payload(self) -> Tuple[str, str, int]:
        payload_map = {
            "1k": ("/benchmark/1k.html", "tests/root/benchmark/1k.html", "1 KB"),
            "64k": ("/benchmark/64k.bin", "tests/root/benchmark/64k.bin", "64 KB"),
            "1m": ("/benchmark/1m.bin", "tests/root/benchmark/1m.bin", "1 MB"),
        }

        if self.payload_key not in payload_map:
            raise ValueError(f"Unknown payload key: {self.payload_key}")

        url_path, file_rel, display_name = payload_map[self.payload_key]
        full_path = os.path.join(self.repo_root, file_rel)
        if not os.path.isfile(full_path) or os.path.getsize(full_path) == 0:
            raise FileNotFoundError(f"Benchmark fixture missing or empty: {full_path}")
        size_bytes = os.path.getsize(full_path)
        return (url_path, display_name, size_bytes)

    def execute(self) -> BenchmarkResults:
        url_path, display_name, size_bytes = self._resolve_payload()

        print("================================================================================")
        print(" Mercury HTTP Performance Benchmark Suite")
        print(f" Platform: {platform.system()} {platform.release()} ({platform.machine()})")
        print(
            f" CPU Cores: {os.cpu_count()} | Concurrency: {self.concurrency} | Duration: {self.duration}s"
        )
        print(f" Threaded Worker Limit: {ServerSupervisor.worker_threads_for(self.concurrency)}")
        print(f" Payload: {display_name} ({size_bytes} bytes) | Load Generator Tool: {self.tool}")
        print("================================================================================")

        server_configs = {
            "mercury": {
                "bin": self.mercury_bin,
                "port": 8080,
                "conf": os.path.join("tests", "conf_files", "benchmark", "mercury.conf"),
            },
            "apache": {
                "bin": self.apache_bin,
                "port": 8082,
                "conf": os.path.join("tests", "conf_files", "benchmark", "apache.conf"),
            },
            "nginx": {
                "bin": self.nginx_bin,
                "port": 8084,
                "conf": os.path.join("tests", "conf_files", "benchmark", "nginx.conf"),
            },
        }

        skipped_servers = []
        raw_trials: List[TrialMetrics] = []
        summaries: Dict[str, ServerSummary] = {}

        for server in self.requested_servers:
            cfg = server_configs.get(server)
            if not cfg or not cfg["bin"]:
                if server == "mercury":
                    raise FileNotFoundError(
                        "Mercury binary not found; build it or pass --mercury-path"
                    )
                skipped_servers.append(server)
                print(f"\n[INFO] {server.capitalize()} binary not found; skipping {server} trials")
                continue

            binary = cfg["bin"]
            port = cfg["port"]
            conf = cfg["conf"]
            version = (
                self.mercury_version
                if server == "mercury" and self.mercury_version
                else Discovery.get_version(server, binary)
            )

            print(f"\n--- Benchmarking {server.capitalize()} ({version}) on port {port} ---")
            for trial in range(1, self.trials + 1):
                supervisor = ServerSupervisor(
                    server,
                    binary,
                    port,
                    conf,
                    self.repo_root,
                    concurrency=self.concurrency,
                )
                print(f"Starting {server}...")
                try:
                    supervisor.start()
                    # Settle fresh process before measuring idle RSS
                    time.sleep(2.0)
                    idle_sampler = MemorySampler(supervisor.process.pid, interval_seconds=0.1)
                    idle_rss = idle_sampler.sample_now()
                    fixture = os.path.join(
                        self.repo_root, "tests", "root", "benchmark", url_path.rsplit("/", 1)[1]
                    )
                    with open(fixture, "rb") as handle:
                        expected = handle.read()
                    LoadGenerator.validate_payload("127.0.0.1", port, url_path, expected)
                    if idle_rss <= 0:
                        raise RuntimeError(
                            "Cannot read server RSS via psutil; verify process permissions and /proc PID visibility"
                        )
                    print(f"Baseline Idle RSS: {idle_rss:.1f} MB")

                    LoadGenerator._run_warmup("127.0.0.1", port, url_path, self.warmup_requests)
                    if supervisor.process.poll() is not None:
                        raise RuntimeError(supervisor._diagnostics())
                    # Start active memory sampler for active load window
                    load_sampler = MemorySampler(supervisor.process.pid, interval_seconds=0.05)
                    load_sampler.start()

                    # Execute load generator
                    print(f"  [Trial {trial}/{self.trials}] Generating load ({self.duration}s)...")
                    try:
                        rps, mean_ms, p50_ms, p99_ms = LoadGenerator.run(
                            tool=self.tool,
                            host="127.0.0.1",
                            port=port,
                            path=url_path,
                            concurrency=self.concurrency,
                            duration=self.duration,
                            warmup_requests=0,
                        )
                    finally:
                        load_samples = load_sampler.stop()
                    if supervisor.process.poll() is not None:
                        raise RuntimeError(supervisor._diagnostics())
                    if not load_samples:
                        raise RuntimeError("No valid RSS samples collected during load")
                    load_rss = sum(load_samples) / len(load_samples)
                    peak_rss = max(load_samples)

                    raw_trials.append(
                        TrialMetrics(
                            server=server,
                            trial_index=trial,
                            payload=self.payload_key,
                            idle_rss_mb=idle_rss,
                            load_rss_mb=load_rss,
                            peak_rss_mb=peak_rss,
                            rps=rps,
                            latency_mean_ms=mean_ms,
                            latency_p50_ms=p50_ms,
                            latency_p99_ms=p99_ms,
                        )
                    )
                    print(
                        f"  [Trial {trial}/{self.trials}] Result: {rps:,.0f} rps | "
                        f"Idle: {idle_rss:.1f} MB | Load: {load_rss:.1f} MB | Peak: {peak_rss:.1f} MB | "
                        f"p99: {p99_ms:.2f} ms"
                    )
                finally:
                    supervisor.stop()

            # Compute server summary across trials
            summary = Statistics.average_trials(raw_trials, server, version)
            summaries[server] = summary

        if not summaries:
            raise RuntimeError("No servers were benchmarked")

        # Package full benchmark results
        results = BenchmarkResults(
            timestamp=time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime()),
            platform_name=platform.system(),
            os_release=platform.release(),
            cpu_architecture=platform.machine(),
            cpu_count=os.cpu_count() or 1,
            duration_seconds=self.duration,
            concurrency=self.concurrency,
            payload_name=display_name,
            payload_size_bytes=size_bytes,
            tool_used=self.tool,
            trials=self.trials,
            worker_thread_limit=ServerSupervisor.worker_threads_for(self.concurrency),
            skipped_servers=skipped_servers,
            summaries=summaries,
            raw_trials=raw_trials,
        )

        return results


# ============================================================================
# CLI Entry Point
# ============================================================================


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Mercury Performance Benchmark Suite")

    # Select servers and trial parameters
    parser.add_argument(
        "--servers",
        type=str,
        default="mercury,apache,nginx",
        help="Comma-separated list of servers to benchmark (default: mercury,apache,nginx)",
    )
    parser.add_argument(
        "--trials", type=int, default=5, help="Number of trials per server (default: 5)"
    )
    parser.add_argument(
        "--duration",
        type=int,
        default=10,
        help="Duration of load test in seconds per trial (default: 10)",
    )
    parser.add_argument(
        "--concurrency",
        type=int,
        default=50,
        help="Number of concurrent client connections (default: 50)",
    )
    parser.add_argument(
        "--warmup",
        type=int,
        default=200,
        help="Number of warmup requests before measurement (default: 200)",
    )

    # Select static workload and load generator
    parser.add_argument(
        "--payload",
        type=str,
        default="1k",
        choices=["1k", "64k", "1m"],
        help="Standardized payload fixture to test (default: 1k)",
    )
    parser.add_argument(
        "--tool",
        type=str,
        default="auto",
        choices=["auto", "wrk", "hey", "python"],
        help="Load generator tool (default: auto)",
    )

    # Choose report formats and destination
    parser.add_argument(
        "--output-format",
        type=str,
        default="markdown",
        help="Output format: markdown, json, or markdown,json (default: markdown)",
    )
    parser.add_argument(
        "--output", type=str, default=None, help="Path to file to write output results"
    )

    # Resolve executable overrides and release label
    parser.add_argument(
        "--mercury-path", type=str, default=None, help="Custom path to Mercury executable"
    )
    parser.add_argument(
        "--apache-path", type=str, default=None, help="Custom path to Apache executable"
    )
    parser.add_argument(
        "--nginx-path", type=str, default=None, help="Custom path to Nginx executable"
    )
    parser.add_argument(
        "--mercury-version",
        help="Verified release version label (Mercury -v may read checkout version.txt)",
    )

    # Validate numeric limits and selections before starting servers
    args = parser.parse_args()
    if min(args.trials, args.duration, args.concurrency) <= 0 or args.warmup < 0:
        parser.error(
            "trials, duration, and concurrency must be positive; warmup must be nonnegative"
        )
    servers = [item.strip().lower() for item in args.servers.split(",") if item.strip()]
    if not servers or set(servers) - {"mercury", "apache", "nginx"}:
        parser.error("servers must select mercury,apache,nginx")
    if len(set(servers)) != len(servers):
        parser.error("servers must not contain duplicates")
    formats = [item.strip().lower() for item in args.output_format.split(",") if item.strip()]
    if not formats or set(formats) - {"markdown", "json"}:
        parser.error("output-format must select markdown,json")
    return args


def main():
    args = parse_args()
    server_list = [s.strip().lower() for s in args.servers.split(",") if s.strip()]

    suite = BenchmarkSuite(
        servers=server_list,
        trials=args.trials,
        duration=args.duration,
        concurrency=args.concurrency,
        warmup_requests=args.warmup,
        payload_key=args.payload,
        tool_preference=args.tool,
        mercury_path=args.mercury_path,
        apache_path=args.apache_path,
        nginx_path=args.nginx_path,
        mercury_version=args.mercury_version,
    )

    try:
        results = suite.execute()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        raise SystemExit(1) from exc

    # Generate requested formats
    formats = [f.strip().lower() for f in args.output_format.split(",")]
    markdown_output = OutputFormatter.to_markdown(results) if "markdown" in formats else ""
    json_output = OutputFormatter.to_json(results) if "json" in formats else ""

    print("\n================================================================================")
    print(" Benchmark Summary Results")
    print("================================================================================\n")

    if markdown_output:
        print(markdown_output)
        print("")

    if "json" in formats and not markdown_output:
        print(json_output)

    # Save to file if requested
    if args.output:
        os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
        base, ext = os.path.splitext(args.output)
        if "markdown" in formats and "json" in formats:
            md_path = args.output if ext.lower() == ".md" else f"{base}.md"
            json_path = f"{base}.json"
            with open(md_path, "w", encoding="utf-8") as f:
                f.write(markdown_output + "\n")
            with open(json_path, "w", encoding="utf-8") as f:
                f.write(json_output + "\n")
            print(f"[INFO] Markdown results saved to {md_path}")
            print(f"[INFO] JSON results saved to {json_path}")
        elif "json" in formats:
            with open(args.output, "w", encoding="utf-8") as f:
                f.write(json_output + "\n")
            print(f"[INFO] Results saved to {args.output}")
        else:
            with open(args.output, "w", encoding="utf-8") as f:
                f.write(markdown_output + "\n")
            print(f"[INFO] Results saved to {args.output}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
