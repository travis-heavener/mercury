#!/usr/bin/env python3
"""
Benchmark runner comparing Mercury, Apache, and Nginx.
Measures Idle RSS, Load RSS, Peak RSS, Throughput (RPS), and Latency.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, dataclass, field
import http.client
import json
import os
import platform
import psutil
import re
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
from typing import Dict, List, Optional, Tuple


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
            time.sleep(self.interval_seconds)

    def sample_now(self) -> float:
        try:
            parent = psutil.Process(self.pid)
            total_bytes = parent.memory_info().rss
            for child in parent.children(recursive=True):
                try:
                    total_bytes += child.memory_info().rss
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    pass
            return total_bytes / (1024.0 * 1024.0)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
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
        # Check explicit tool preferences
        if preferred in ["wrk", "hey", "python"]:
            if preferred == "wrk" and shutil.which("wrk"):
                return "wrk"
            if preferred == "hey" and shutil.which("hey"):
                return "hey"
            if preferred == "python":
                return "python"

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

        conn = None
        headers = {"Connection": "keep-alive", "User-Agent": "MercuryBenchmark/1.0"}

        # Fire warmup requests over keep-alive socket, reconnecting on drop
        for _ in range(min(count, 500)):
            try:
                if conn is None:
                    conn = http.client.HTTPConnection(host, port, timeout=5)
                conn.request("GET", path, headers=headers)
                resp = conn.getresponse()
                resp.read()
            except Exception:
                if conn:
                    try:
                        conn.close()
                    except Exception:
                        pass
                    conn = None

        # Clean up connection
        if conn:
            try:
                conn.close()
            except Exception:
                pass

    @staticmethod
    def _run_wrk(host: str, port: int, path: str, concurrency: int, duration: int) -> Tuple[float, float, float, float]:
        # Returns (rps, mean_ms, p50_ms, p99_ms) via wrk load generator
        url = f"http://{host}:{port}{path}"

        # Bound worker thread count to concurrency and available CPU cores
        threads = min(concurrency, max(1, os.cpu_count() or 2))

        # Construct execution command
        cmd = ["wrk", "-t", str(threads), "-c", str(concurrency), "-d", f"{duration}s", "--latency", url]

        # Execute wrk load generator via subprocess
        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
            out = res.stdout
        except (subprocess.CalledProcessError, FileNotFoundError):
            return (0.0, 0.0, 0.0, 0.0)

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

        return (rps, mean_ms, p50_ms, p99_ms)

    @staticmethod
    def _run_hey(host: str, port: int, path: str, concurrency: int, duration: int) -> Tuple[float, float, float, float]:
        # Returns (rps, mean_ms, p50_ms, p99_ms) via hey load generator
        url = f"http://{host}:{port}{path}"

        # Construct execution command
        cmd = ["hey", "-c", str(concurrency), "-z", f"{duration}s", url]

        # Execute hey load generator via subprocess
        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
            out = res.stdout
        except (subprocess.CalledProcessError, FileNotFoundError):
            return (0.0, 0.0, 0.0, 0.0)

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

        return (rps, mean_ms, p50_ms, p99_ms)

    @staticmethod
    def _run_python(host: str, port: int, path: str, concurrency: int, duration: int) -> Tuple[float, float, float, float]:
        # Returns (rps, mean_ms, p50_ms, p99_ms) via multithreaded keep-alive client
        stop_time = time.perf_counter() + duration

        # Thread-safe container collecting per-thread request counts and latency samples
        stats_list: List[Tuple[int, List[float]]] = []
        headers = {"Connection": "keep-alive", "User-Agent": "MercuryBenchmark/1.0"}

        # Worker routine executing persistent HTTP keep-alive loop
        def worker():
            local_reqs = 0
            local_latencies: List[float] = []
            conn: Optional[http.client.HTTPConnection] = None
            consecutive_errors = 0

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
                    t1 = time.perf_counter()

                    local_reqs += 1
                    # Record round-trip request latency in milliseconds
                    local_latencies.append((t1 - t0) * 1000.0)

                    # Reset consecutive error counter on successful exchange
                    consecutive_errors = 0
                except Exception:
                    # Close corrupted socket connection on error
                    if conn:
                        try:
                            conn.close()
                        except Exception:
                            pass
                        conn = None

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
            stats_list.append((local_reqs, local_latencies))

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
        total_reqs = sum(reqs for reqs, _ in stats_list)
        all_latencies = [lat for _, lats in stats_list for lat in lats]

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

        return (rps, mean_ms, p50_ms, p99_ms)


# ============================================================================
# Server Process Supervisor
# ============================================================================

class ServerSupervisor:
    """Manages server startup, socket readiness, and graceful shutdown"""

    def __init__(self, name: str, binary_path: str, port: int, conf_file: str, repo_root: str):
        self.name = name
        self.binary_path = binary_path
        self.port = port
        self.conf_file = conf_file
        self.repo_root = repo_root
        self.process: Optional[subprocess.Popen] = None

    def start(self):
        # Validate binary existence
        if not os.path.exists(self.binary_path) and not shutil.which(self.binary_path):
            raise FileNotFoundError(f"{self.name} binary not found: {self.binary_path}")

        # Ensure benchmark log directory and unprivileged scratch paths exist
        bench_dir = os.path.join(self.repo_root, "logs", "benchmark")
        os.makedirs(bench_dir, exist_ok=True)
        for sub_dir in ["nginx_client_body", "nginx_proxy", "nginx_fastcgi", "nginx_uwsgi", "nginx_scgi"]:
            os.makedirs(os.path.join(bench_dir, sub_dir), exist_ok=True)

        # Resolve configuration path to absolute filesystem path
        conf_abs = os.path.abspath(os.path.join(self.repo_root, self.conf_file))

        # Build execution command
        if self.name == "mercury":
            cmd = [self.binary_path, "-c", conf_abs]
        elif self.name == "apache":
            cmd = [self.binary_path, "-d", self.repo_root, "-f", conf_abs, "-DFOREGROUND"]
        elif self.name == "nginx":
            cmd = [self.binary_path, "-p", self.repo_root, "-c", conf_abs, "-g", "daemon off;"]
        else:
            raise ValueError(f"Unknown server: {self.name}")

        # Ensure port is released before launching
        self._wait_port_free(self.port, timeout=3.0)

        # Launch server process with DEVNULL for stdin/stdout to prevent pipe stalls
        self.process = subprocess.Popen(
            cmd,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            cwd=self.repo_root,
            text=True,
        )

        # Wait until port accepts connections
        if not self._wait_port_ready(self.port, timeout=10.0):
            self.stop()
            raise RuntimeError(f"Server {self.name} failed to bind or listen on port {self.port}")

    def stop(self):
        if self.process is None:
            return

        pid = self.process.pid
        child_pids: List[int] = []

        try:
            parent_proc = psutil.Process(pid)
            child_pids = [c.pid for c in parent_proc.children(recursive=True)]
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass

        # Windows-specific Nginx stop command
        if self.name == "nginx" and sys.platform in ["win32", "cygwin"]:
            conf_abs = os.path.abspath(os.path.join(self.repo_root, self.conf_file))
            try:
                subprocess.run(
                    [self.binary_path, "-p", self.repo_root, "-c", conf_abs, "-s", "stop"],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=3.0,
                )
            except Exception:
                pass

        # Send termination signal to parent (SIGINT on Unix, CTRL_BREAK on Windows)
        try:
            if sys.platform in ["win32", "cygwin"]:
                self.process.send_signal(signal.CTRL_BREAK_EVENT)
            else:
                self.process.send_signal(signal.SIGINT)
            self.process.wait(timeout=10.0)
        except (subprocess.TimeoutExpired, ProcessLookupError):
            try:
                self.process.kill()
                self.process.wait(timeout=5.0)
            except (ProcessLookupError, subprocess.TimeoutExpired):
                pass

        # Clean up any detached child worker processes
        for cpid in child_pids:
            try:
                cproc = psutil.Process(cpid)
                cproc.terminate()
                cproc.wait(timeout=2.0)
            except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.TimeoutExpired):
                try:
                    cproc.kill()
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    pass

        self.process = None

        # Wait for port to clear and allow kernel TIME_WAIT cooldown
        self._wait_port_free(self.port, timeout=10.0)
        time.sleep(1.0)

    @staticmethod
    def _ping_socket(host: str, port: int) -> bool:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(0.5)
            sock.connect((host, port))
            sock.close()
            return True
        except Exception:
            return False

    @staticmethod
    def _is_port_bindable(port: int) -> bool:
        # Returns True if port can be bound without EADDRINUSE collisions
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(("0.0.0.0", port))
            sock.close()
            return True
        except OSError:
            return False

    def _wait_port_ready(self, port: int, timeout: float = 10.0) -> bool:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self._ping_socket("127.0.0.1", port):
                return True
            if self.process and self.process.poll() is not None:
                # Capture diagnostics from stderr and error log if process exited
                stderr_output = ""
                if self.process.stderr:
                    try:
                        stderr_output = self.process.stderr.read()
                    except Exception:
                        pass
                if not stderr_output:
                    err_log = os.path.join(self.repo_root, "logs", "benchmark", f"{self.name}_error.log")
                    if os.path.exists(err_log):
                        try:
                            with open(err_log, "r") as ef:
                                lines = ef.readlines()
                                stderr_output = "".join(lines[-5:])
                        except Exception:
                            pass
                raise RuntimeError(f"{self.name} process exited prematurely: {stderr_output.strip()}")
            time.sleep(0.1)
        return False

    def _wait_port_free(self, port: int, timeout: float = 10.0) -> bool:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self._is_port_bindable(port):
                return True
            time.sleep(0.2)
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
        if override_path and os.path.exists(override_path):
            return os.path.abspath(override_path)

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
        if override_path and os.path.exists(override_path):
            return os.path.abspath(override_path)

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
        return None

    @classmethod
    def find_nginx(cls, override_path: Optional[str] = None) -> Optional[str]:
        if override_path and os.path.exists(override_path):
            return os.path.abspath(override_path)

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
        return None

    @classmethod
    def get_version(cls, server: str, binary_path: str) -> str:
        if server == "mercury":
            version_file = os.path.join(cls.repo_root(), "version.txt")
            if os.path.exists(version_file):
                with open(version_file, "r") as f:
                    content = f.read().strip()
                    # Example format: Mercury v0.38.1
                    match = re.search(r"v?(\d+\.\d+\.\d+)", content)
                    if match:
                        return f"v{match.group(1)}"
            return "v0.38.1"

        try:
            if server == "apache":
                res = subprocess.run([binary_path, "-v"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                match = re.search(r"Apache/(\d+\.\d+\.\d+)", res.stdout)
                if match:
                    return match.group(1)
            elif server == "nginx":
                res = subprocess.run([binary_path, "-v"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
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
        is_windows = "windows" in results.platform_name.lower() or "win32" in results.platform_name.lower()
        if is_windows:
            platform_header = f"Windows ({results.cpu_architecture})"
        else:
            distro_desc = platform.freedesktop_os_release().get("PRETTY_NAME", "") if hasattr(platform, "freedesktop_os_release") else ""
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
        lines.append(format_row(f"Req / Second ({results.payload_name})", "rps", "rps", metric_type="rps"))
        lines.append(format_row("Avg. Latency (p99)", "latency_p99_ms", "ms", metric_type="latency"))

        lines.append("")
        lines.append(f"*Baseline averages across {results.duration_seconds}s trials over loopback using tests/conf_files/benchmark.*")
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
    ):
        self.requested_servers = servers
        self.trials = trials
        self.duration = duration
        self.concurrency = concurrency
        self.warmup_requests = warmup_requests
        self.payload_key = payload_key
        self.tool_preference = tool_preference

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
            "php": ("/benchmark/hello.php", "tests/root/benchmark/hello.php", "PHP FastCGI"),
        }

        if self.payload_key not in payload_map:
            raise ValueError(f"Unknown payload key: {self.payload_key}")

        url_path, file_rel, display_name = payload_map[self.payload_key]
        full_path = os.path.join(self.repo_root, file_rel)
        size_bytes = os.path.getsize(full_path) if os.path.exists(full_path) else 0
        return (url_path, display_name, size_bytes)

    def execute(self) -> BenchmarkResults:
        url_path, display_name, size_bytes = self._resolve_payload()

        print("================================================================================")
        print(" Mercury HTTP Performance Benchmark Suite")
        print(f" Platform: {platform.system()} {platform.release()} ({platform.machine()})")
        print(f" CPU Cores: {os.cpu_count()} | Concurrency: {self.concurrency} | Duration: {self.duration}s")
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

        raw_trials: List[TrialMetrics] = []
        summaries: Dict[str, ServerSummary] = {}

        for server in self.requested_servers:
            cfg = server_configs.get(server)
            if not cfg or not cfg["bin"]:
                print(f"\n[INFO] {server.capitalize()} binary not found; skipping {server} trials")
                continue

            binary = cfg["bin"]
            port = cfg["port"]
            conf = cfg["conf"]
            version = Discovery.get_version(server, binary)

            print(f"\n--- Benchmarking {server.capitalize()} ({version}) on port {port} ---")
            supervisor = ServerSupervisor(server, binary, port, conf, self.repo_root)
            print(f"Starting {server}...")
            supervisor.start()

            try:
                # Settle process and measure baseline Idle RSS over 2 seconds
                time.sleep(2.0)
                idle_sampler = MemorySampler(supervisor.process.pid, interval_seconds=0.1)
                idle_rss = idle_sampler.sample_now()
                print(f"Baseline Idle RSS: {idle_rss:.1f} MB")

                for trial in range(1, self.trials + 1):
                    # Start active memory sampler for active load window
                    load_sampler = MemorySampler(supervisor.process.pid, interval_seconds=0.05)
                    load_sampler.start()

                    # Execute load generator
                    print(f"  [Trial {trial}/{self.trials}] Generating load ({self.duration}s)...")
                    rps, mean_ms, p50_ms, p99_ms = LoadGenerator.run(
                        tool=self.tool,
                        host="127.0.0.1",
                        port=port,
                        path=url_path,
                        concurrency=self.concurrency,
                        duration=self.duration,
                        warmup_requests=self.warmup_requests if trial == 1 else 0,
                    )

                    # Stop memory sampler
                    load_samples = load_sampler.stop()
                    load_rss = sum(load_samples) / len(load_samples) if load_samples else idle_rss
                    peak_rss = max(load_samples) if load_samples else idle_rss

                    trial_metric = TrialMetrics(
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
                    raw_trials.append(trial_metric)

                    print(
                        f"  [Trial {trial}/{self.trials}] Result: {rps:,.0f} rps | "
                        f"Idle: {idle_rss:.1f} MB | Load: {load_rss:.1f} MB | Peak: {peak_rss:.1f} MB | "
                        f"p99: {p99_ms:.2f} ms"
                    )

                    # Pause briefly between trials to let buffers settle
                    if trial < self.trials:
                        time.sleep(1.0)

            finally:
                supervisor.stop()

            # Compute server summary across trials
            summary = Statistics.average_trials(raw_trials, server, version)
            summaries[server] = summary

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
            summaries=summaries,
            raw_trials=raw_trials,
        )

        return results


# ============================================================================
# CLI Entry Point
# ============================================================================

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Mercury Performance Benchmark Suite")
    parser.add_argument( "--servers", type=str, default="mercury,apache,nginx", help="Comma-separated list of servers to benchmark (default: mercury,apache,nginx)" )
    parser.add_argument( "--trials", type=int, default=5, help="Number of trials per server (default: 5)" )
    parser.add_argument( "--duration", type=int, default=10, help="Duration of load test in seconds per trial (default: 10)" )
    parser.add_argument( "--concurrency", type=int, default=50, help="Number of concurrent client connections (default: 50)" )
    parser.add_argument( "--warmup", type=int, default=200, help="Number of warmup requests before measurement (default: 200)" )
    parser.add_argument( "--payload", type=str, default="1k", choices=["1k", "64k", "1m", "php"], help="Standardized payload fixture to test (default: 1k)" )
    parser.add_argument( "--tool", type=str, default="auto", choices=["auto", "wrk", "hey", "python"], help="Load generator tool (default: auto)" )
    parser.add_argument( "--output-format", type=str, default="markdown", help="Output format: markdown, json, or markdown,json (default: markdown)" )
    parser.add_argument( "--output", type=str, default=None, help="Path to file to write output results" )
    parser.add_argument( "--mercury-path", type=str, default=None, help="Custom path to Mercury executable" )
    parser.add_argument( "--apache-path", type=str, default=None, help="Custom path to Apache executable" )
    parser.add_argument( "--nginx-path", type=str, default=None, help="Custom path to Nginx executable" )
    return parser.parse_args()


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
    )

    results = suite.execute()

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
        with open(args.output, "w") as f:
            if "markdown" in formats and "json" in formats:
                f.write(f"# Benchmark Results\n\n{markdown_output}\n\n```json\n{json_output}\n```\n")
            elif "json" in formats:
                f.write(json_output + "\n")
            else:
                f.write(markdown_output + "\n")
        print(f"[INFO] Results saved to {args.output}")


if __name__ == "__main__":
    main()

