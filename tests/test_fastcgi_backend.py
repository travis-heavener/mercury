"""Verify Linux automatic PHP-FPM ownership through real HTTP requests."""

import os
from pathlib import Path
import psutil
import re
import socket
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

from fastcgi_backend import FastCGIBackend
from regressions import Connection, request
from run import mercury_process


PROJECT_ROOT = Path(__file__).resolve().parent.parent
IS_ROOT = sys.platform == "linux" and os.geteuid() == 0


@unittest.skipUnless(sys.platform == "linux", "Require Linux PHP-FPM")
class ManagedBackendTests(unittest.TestCase):
    # Used to prepare readable-by-owner-only document root and test configuration
    def setUp(self):
        self.root = PROJECT_ROOT
        os.chdir(self.root / "tests")
        self.directory = tempfile.TemporaryDirectory(prefix="mercury-owned-test-")
        self.addCleanup(self.directory.cleanup)
        self.document_root = Path(self.directory.name)
        (self.document_root / "index.php").write_text('<?php echo "owned PHP works";')
        (self.document_root / "small.txt").write_text("ready")
        source = (self.root / "tests/conf_files/regressions.conf").read_text()
        self.config = re.sub(r"<DocumentRoot>.*?</DocumentRoot>",
                             f"<DocumentRoot> {self.document_root} </DocumentRoot>", source)
        self.config = self.config.replace("<EnablePHPCGI> on </EnablePHPCGI>",
                                          "<EnablePHPCGI> off </EnablePHPCGI>")

    # Used to write per-test Mercury configuration without editing checked-in files
    def write_config(self, address=None, enabled=True):
        address_node = f"<FastCGIAddress> {address} </FastCGIAddress>" if address is not None else ""
        text = self.config.replace("<Mercury>", f"<Mercury><EnableFastCGI> {'on' if enabled else 'off'} </EnableFastCGI>"
                                   f"{address_node}<FastCGITimeout> 2 </FastCGITimeout>")
        path = self.document_root / "mercury.conf"
        path.write_text(text)
        return str(path)

    # Used to assert PHP execution through HTTP rather than socket readiness alone
    def assert_php(self):
        with Connection("127.0.0.1", 8080) as connection:
            connection.send(request(b"/index.php"))
            status, _, body = connection.response()
            self.assertEqual((status, body), (200, b"owned PHP works"))

    # Used to verify owned descendants and runtime files disappear after shutdown
    def assert_cleaned(self, children, directory):
        deadline = time.monotonic() + 5
        while any(child.is_running() for child in children) and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertFalse(any(child.is_running() for child in children))
        self.assertFalse(directory.exists())

    @unittest.skipIf(IS_ROOT, "Run ownership test as development user, not root")
    def test_auto_and_omitted_address_own_private_pool(self):
        for address in (None, "auto"):
            with self.subTest(address=address):
                with mercury_process(self.write_config(address)) as process:
                    self.assert_php()
                    children = psutil.Process(process.pid).children(recursive=True)
                    masters = [child for child in children if "master process (" in " ".join(child.cmdline())]
                    self.assertEqual(len(masters), 1)
                    title = " ".join(masters[0].cmdline())
                    config_path = re.search(r"\((/tmp/mercury-fpm-[^/]+/php-fpm.conf)\)", title)
                    self.assertIsNotNone(config_path, title)
                    directory = Path(config_path.group(1)).parent
                    self.assertEqual(directory.stat().st_mode & 0o777, 0o700)
                    self.assertTrue((directory / "php.sock").is_socket())
                    self.assertEqual((directory / "php.sock").stat().st_mode & 0o777, 0o600)
                    self.assertTrue(all(child.uids().effective == os.geteuid() for child in children))
                self.assert_cleaned(children, directory)

    @unittest.skipIf(IS_ROOT, "Run external fixture as unprivileged user")
    def test_explicit_backend_is_reused_and_survives(self):
        with FastCGIBackend("tcp") as path:
            with mercury_process(path) as process:
                with Connection("127.0.0.1", 8080) as connection:
                    connection.send(request(b"/regression_state.php"))
                    self.assertEqual(connection.response()[2], b"unset")
                self.assertEqual(psutil.Process(process.pid).children(), [])
            # Verify explicit listener still serves PHP after Mercury exits
            with mercury_process(path):
                with Connection("127.0.0.1", 8080) as connection:
                    connection.send(request(b"/regression_state.php"))
                    self.assertEqual(connection.response()[2], b"unset")

    @unittest.skipIf(IS_ROOT, "Run startup failure test as unprivileged user")
    def test_failed_launch_does_not_fall_back_to_system_pool(self):
        # Prefer deliberately failing highest-version binary over installed FPM
        executable = self.document_root / "php-fpm999.999"
        executable.write_text("#!/bin/sh\necho intentional-startup-failure >&2\nexit 9\n")
        executable.chmod(0o700)
        environment = {"PATH": str(self.document_root) + os.pathsep + os.environ.get("PATH", "")}
        with patch.dict(os.environ, environment):
            with mercury_process(self.write_config("auto")) as process:
                self.assertEqual(psutil.Process(process.pid).children(), [])
                with Connection("127.0.0.1", 8080) as connection:
                    connection.send(request(b"/index.php"))
                    self.assertEqual(connection.response()[0], 502)

    def test_disabled_fastcgi_does_not_launch_php(self):
        with mercury_process(self.write_config(enabled=False)) as process:
            self.assertEqual(psutil.Process(process.pid).children(), [])

    def test_explicit_unavailable_address_does_not_start_fpm(self):
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            address = f"127.0.0.1:{reservation.getsockname()[1]}"
            with mercury_process(self.write_config(address)) as process:
                self.assertEqual(psutil.Process(process.pid).children(), [])
                with Connection("127.0.0.1", 8080) as connection:
                    connection.send(request(b"/index.php"))
                    self.assertEqual(connection.response()[0], 502)

    @unittest.skipUnless(IS_ROOT, "Exercise direct-root startup policy only as root")
    def test_direct_root_does_not_start_privileged_pool(self):
        with mercury_process(self.write_config("auto")) as process:
            self.assertEqual(psutil.Process(process.pid).children(), [])
            with Connection("127.0.0.1", 8080) as connection:
                connection.send(request(b"/index.php"))
                self.assertEqual(connection.response()[0], 502)


# Used to match HTTP runner output while retaining failure tracebacks and skip counts
def run_compact_suite():
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ManagedBackendTests)
    print(f"Starting run: Linux PHP-FPM Ownership Tests ({suite.countTestCases()} tests)", flush=True)
    result = unittest.TestResult()
    result.startTestRun()
    try:
        suite.run(result)
    finally:
        result.stopTestRun()

    if result.wasSuccessful():
        passed = result.testsRun - len(result.skipped)
        print(f"✅ Passing ({passed} passed, {len(result.skipped)} skipped)", flush=True)
    else:
        # Report individual failures and errors without unittest progress or summary banners
        for label, entries in (("failure", result.failures), ("error", result.errors)):
            for test, details in entries:
                print(f"[Ownership {label}] {test}\n{details}", end="")
        print(f"❌ Failing ({len(result.failures)} failures, {len(result.errors)} errors, "
              f"{len(result.skipped)} skipped)", flush=True)
    return result.wasSuccessful()


if __name__ == "__main__":
    if sys.argv[1:] == ["--mercury-output"]:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.exit(0 if run_compact_suite() else 1)
    unittest.main()
