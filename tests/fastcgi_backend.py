"""Isolated real PHP backends for HTTP integration runs."""

from contextlib import ExitStack
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time


# Manage isolated backend fixtures and generated Mercury configs per test run
class FastCGIBackend:
    # Used to initialize fixture ownership for requested transport
    def __init__(self, transport):
        self.transport = transport
        # Track temporary files, sockets, and owned process for reverse-order cleanup
        self.stack = ExitStack()
        self.proc = None

    # Used to start fixture and return generated config path to runner
    def __enter__(self):
        try:
            return self.start()
        except BaseException:
            # Release partial setup on startup failure or interruption
            self.stack.close()
            raise

    # Used to release fixture resources after Mercury stops, including failed runs
    def __exit__(self, *_):
        self.stack.close()

    # Used to terminate only PHP process owned by this fixture
    def stop(self):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                # Kill unresponsive backend after graceful termination deadline
                self.proc.kill()
                self.proc.wait(timeout=5)

    # Used to prepare backend transport, launch fixture if needed, and write config
    def start(self):
        # Resolve checkout independently of runner working directory
        root = Path(__file__).resolve().parent.parent
        # Keep pool config, socket, logs, and Mercury config scoped to this run
        directory = Path(self.stack.enter_context(tempfile.TemporaryDirectory(prefix="mercury-fpm-")))
        # Permit FPM workers to traverse private fixture directory
        directory.chmod(0o755)
        # Reserve ephemeral port without listening to avoid fixed-port conflicts
        # Use 127.0.0.2 for failure tests to bypass Mercury Windows auto-start policy
        reservation = self.stack.enter_context(socket.socket())
        reservation.bind(("127.0.0.2" if self.transport == "unavailable" else "127.0.0.1", 0))
        port = reservation.getsockname()[1]
        address = f"127.0.0.1:{port}"
        listen = address
        # Separate FPM listen value from Mercury unix: address syntax
        if self.transport == "unix":
            listen = str(directory / "php.sock")
            address = "unix:" + listen
        elif self.transport == "unavailable":
            # Keep bound socket open without listening so backend connections fail
            address = f"127.0.0.2:{port}"

        # Start Linux FPM and Windows external fixtures here
        # Leave Windows tcp startup to Mercury and skip PHP for unavailable tests
        windows = sys.platform in ("win32", "cygwin")
        if self.transport != "unavailable" and not (windows and self.transport == "tcp"):
            if windows:
                # Launch external Windows listener to verify Mercury reuses it
                command = [str(root / "php/php-cgi.exe"), "-b", listen]
            else:
                # Locate executable FPM binaries across supported Linux install paths
                candidates = [p for base in ("/usr/sbin", "/usr/bin", "/usr/local/sbin")
                              for p in Path(base).glob("php-fpm*") if os.access(p, os.X_OK) and p.is_file()]
                # Include custom installations exposed through PATH
                if shutil.which("php-fpm"):
                    candidates.append(Path(shutil.which("php-fpm")))
                if not candidates:
                    raise RuntimeError("PHP-FPM is required; install php-fpm before running full suite")
                # Used to rank versioned binary names numerically
                def version(path):
                    return tuple(int(n) for n in re.findall(r"\d+", path.name))
                # Prefer highest discovered version without changing system services
                binary = max(candidates, key=version)
                pool = directory / "fpm.conf"
                user = ""
                # Run workers as nobody when harness starts as root, e.g. Docker
                # Leave worker identity unchanged for non-root harnesses
                if os.geteuid() == 0:
                    import pwd
                    account = pwd.getpwnam("nobody")
                    import grp
                    user = f"user = {account.pw_name}\ngroup = {grp.getgrgid(account.pw_gid).gr_name}\n"
                # Generate private pool instead of using distro FPM configuration
                # Keep eight workers available for concurrent HTTP assertions
                pool.write_text(
                    f"[global]\ndaemonize = no\nerror_log = {directory}/fpm.log\n"
                    f"[mercury_tests]\n{user}listen = {listen}\n"
                    "pm = static\npm.max_children = 8\ncatch_workers_output = yes\n"
                    "security.limit_extensions = .php\nphp_admin_value[cgi.fix_pathinfo] = 1\n"
                )
                # Keep master in foreground so fixture can wait for and stop it
                command = [str(binary), "-F", "-y", str(pool)]
            # Release reserved port immediately before launching backend
            reservation.close()
            environment = os.environ.copy()
            if windows:
                # Keep external Windows fixture alive beyond PHP request recycle limit
                environment["PHP_FCGI_MAX_REQUESTS"] = "0"
            # Capture startup diagnostics and retain handle for owned-process cleanup
            self.proc = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=environment)
            # Register callbacks so backend stops before stderr and temporary files close
            self.stack.callback(self.proc.stderr.close)
            self.stack.callback(self.stop)
            # Wait up to ten seconds for transport readiness before starting Mercury
            deadline = time.monotonic() + 10
            while True:
                # Report early backend exit with captured startup diagnostics
                if self.proc.poll() is not None:
                    raise RuntimeError("PHP backend exited: " + self.proc.communicate()[1].decode(errors="replace"))
                try:
                    # Probe selected Unix or TCP listener without sending PHP requests
                    with socket.socket(socket.AF_UNIX if self.transport == "unix" else socket.AF_INET) as probe:
                        probe.settimeout(0.2)
                        probe.connect(listen if self.transport == "unix" else ("127.0.0.1", port))
                    break
                except OSError:
                    # Retry transient startup failures until monotonic deadline expires
                    if time.monotonic() >= deadline:
                        raise RuntimeError("PHP backend did not become ready")
                    time.sleep(0.05)
        elif self.transport != "unavailable":
            # Let Mercury launch Windows PHP using generated local endpoint
            reservation.close()

        # Reuse routing, TLS, access rules, and limits required by wire regressions
        config = (root / "tests/conf_files/regressions.conf").read_text()
        # Disable CGI for successful FastCGI runs to prevent accidental CGI coverage
        # Keep both enabled for unavailable run to verify gateway failure precedence
        if self.transport != "unavailable":
            config = config.replace("<EnablePHPCGI> on </EnablePHPCGI>", "<EnablePHPCGI> off </EnablePHPCGI>")
        # Enable FastCGI explicitly and select fixture endpoint with two-second I/O timeout
        config = config.replace("<Mercury>", f"<Mercury>\n    <EnableFastCGI> on </EnableFastCGI>\n"
                                f"    <FastCGIAddress> {address} </FastCGIAddress>\n    <FastCGITimeout> 2 </FastCGITimeout>")
        # Return generated config for Mercury -c without modifying checked-in configs
        path = directory / "mercury.conf"
        path.write_text(config)
        return str(path)
