#!/usr/bin/env python3
"""
Update Utility Test Suite

Validates version parsing, process detection, XML configuration reconciliation,
pre-flight syntax verification, and atomic rollback in conf/update.py.
"""

from pathlib import Path
import shutil
import sys
import tempfile
import unittest

# Add project root to path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "conf"))
import update

class TestUpdateUtility(unittest.TestCase):

    def setUp(self):
        # Resolve repository paths
        self.repo_root = Path(__file__).resolve().parent.parent
        self.conf_path = self.repo_root / "conf" / "mercury.conf"
        self.default_conf_path = self.repo_root / "conf" / "default" / "mercury.conf"
        self.binary_path = self.repo_root / "bin" / ("mercury.exe" if sys.platform in ("win32", "cygwin") else "mercury")

    # ==================== Version Parsing Tests ====================

    def test_parse_version(self):
        # Verify version tuple conversions
        self.assertEqual(update.parse_version("v0.35.0"), (0, 35, 0))
        self.assertEqual(update.parse_version("Mercury v1.2.3"), (1, 2, 3))
        self.assertEqual(update.parse_version("0.34.1"), (0, 34, 1))
        self.assertEqual(update.parse_version("v2.0.0-rc1"), (2, 0, 0))
        self.assertTrue(update.parse_version("v0.36.0") > update.parse_version("v0.35.0"))
        self.assertTrue(update.parse_version("v0.35.1") > update.parse_version("v0.35.0"))
        self.assertTrue(update.parse_version("v1.0.0") > update.parse_version("v0.35.0"))

    def test_get_current_version(self):
        # Verify reading version from root directory
        version_str = update.get_current_version(self.repo_root)
        self.assertTrue(version_str.startswith("v"))
        self.assertTrue(len(version_str.split(".")) >= 3)

    # ==================== Asset Selection Tests ====================

    def test_select_platform_asset_linux(self):
        assets = [
            {
                "name": "Windows_v0_35_0.zip",
                "browser_download_url": "https://github.com/travis-heavener/mercury/releases/download/v0.35.0/Windows_v0_35_0.zip"
            },
            {
                "name": "Linux_v0_35_0.tar.gz",
                "browser_download_url": "https://github.com/travis-heavener/mercury/releases/download/v0.35.0/Linux_v0_35_0.tar.gz"
            },
        ]
        url, name = update.select_platform_asset(assets, "linux")
        self.assertEqual(name, "Linux_v0_35_0.tar.gz")
        self.assertEqual(url, "https://github.com/travis-heavener/mercury/releases/download/v0.35.0/Linux_v0_35_0.tar.gz")

    def test_select_platform_asset_windows(self):
        assets = [
            {
                "name": "Windows_v0_35_0.zip",
                "browser_download_url": "https://github.com/travis-heavener/mercury/releases/download/v0.35.0/Windows_v0_35_0.zip"
            },
            {
                "name": "Linux_v0_35_0.tar.gz",
                "browser_download_url": "https://github.com/travis-heavener/mercury/releases/download/v0.35.0/Linux_v0_35_0.tar.gz"
            },
        ]
        url, name = update.select_platform_asset(assets, "win32")
        self.assertEqual(name, "Windows_v0_35_0.zip")
        self.assertEqual(url, "https://github.com/travis-heavener/mercury/releases/download/v0.35.0/Windows_v0_35_0.zip")

    def test_compute_sha256(self):
        # Verify SHA-256 calculation on known content
        with tempfile.NamedTemporaryFile(delete=False) as f:
            temp_path = Path(f.name)
            temp_path.write_bytes(b"Mercury Test Content\n")

        try:
            digest = update.compute_sha256(temp_path)
            # Precomputed sha256 for "Mercury Test Content\n"
            import hashlib
            expected = hashlib.sha256(b"Mercury Test Content\n").hexdigest()
            self.assertEqual(digest, expected)
        finally:
            temp_path.unlink(missing_ok=True)

    def test_fetch_expected_release_hash_parsing(self):
        # Mock releases.json response from website
        mock_json = (
            '[\n'
            '    {\n'
            '        "name": "v0.35.0",\n'
            '        "linuxHash": "11109bd69ce1e2a685d7290d9e5d10c3dcd5804ebc566f9f06b4eb80b9f192ab",\n'
            '        "winHash": "8cc8355d9866964a1ed9b5ad0f947c190212f412d4506b050217fe1887967312"\n'
            '    }\n'
            ']\n'
        )
        class MockResponse:
            def __enter__(self):
                return self
            def __exit__(self, *args):
                pass
            def read(self):
                return mock_json.encode("utf-8")

        orig_urlopen = update.urllib.request.urlopen
        update.urllib.request.urlopen = lambda req, timeout=10: MockResponse()
        try:
            linux_hash = update.fetch_expected_release_hash("v0.35.0", "linux")
            win_hash = update.fetch_expected_release_hash("v0.35.0", "win32")
            self.assertEqual(linux_hash, "11109bd69ce1e2a685d7290d9e5d10c3dcd5804ebc566f9f06b4eb80b9f192ab")
            self.assertEqual(win_hash, "8cc8355d9866964a1ed9b5ad0f947c190212f412d4506b050217fe1887967312")
        finally:
            update.urllib.request.urlopen = orig_urlopen

    def test_construct_direct_asset_url(self):
        # Verify direct download URL generation without API
        url_linux, name_linux = update.construct_direct_asset_url("v0.35.0", "linux")
        self.assertEqual(name_linux, "Linux_v0_35_0.tar.gz")
        self.assertEqual(
            url_linux,
            "https://github.com/travis-heavener/mercury/releases/download/v0.35.0/Linux_v0_35_0.tar.gz"
        )

        url_win, name_win = update.construct_direct_asset_url("v0.35.0", "win32")
        self.assertEqual(name_win, "Windows_v0_35_0.zip")
        self.assertEqual(
            url_win,
            "https://github.com/travis-heavener/mercury/releases/download/v0.35.0/Windows_v0_35_0.zip"
        )

    def test_fetch_latest_remote_version_parsing(self):
        # Mock remote response matching official /latest format
        class MockResponse:
            def __enter__(self):
                return self
            def __exit__(self, *args):
                pass
            def read(self):
                return b"Mercury v0.35.0\n"

        orig_urlopen = update.urllib.request.urlopen
        update.urllib.request.urlopen = lambda req, timeout=10: MockResponse()
        try:
            version_str = update.fetch_latest_remote_version()
            self.assertEqual(version_str, "v0.35.0")
        finally:
            update.urllib.request.urlopen = orig_urlopen

    # ==================== XML Reconciliation Tests ====================

    def test_reconcile_config_preserves_customizations(self):
        # Setup temporary mock configuration w/ custom values
        with tempfile.TemporaryDirectory() as tmp_dir_str:
            tmp_dir = Path(tmp_dir_str)
            user_conf = tmp_dir / "user.conf"
            template_conf = tmp_dir / "template.conf"

            # Create user config w/ custom port and salt
            user_xml = (
                "<Mercury>\n"
                "    <Port> 9090 </Port>\n"
                "    <DocumentRoot> ./custom_public/ </DocumentRoot>\n"
                "    <ClientSecurityIPSalt> custom_salt_xyz </ClientSecurityIPSalt>\n"
                "    <Match pattern=\"^/api/.*$\">\n"
                "        <Header name=\"X-Custom\"> 1 </Header>\n"
                "    </Match>\n"
                "</Mercury>\n"
            )
            user_conf.write_text(user_xml)

            # Create template config w/ default values and extra setting
            template_xml = (
                "<Mercury>\n"
                "    <Port> 80 </Port>\n"
                "    <DocumentRoot> ./public/ </DocumentRoot>\n"
                "    <ClientSecurityIPSalt> default_salt </ClientSecurityIPSalt>\n"
                "    <StartupCheckLatestRelease> true </StartupCheckLatestRelease>\n"
                "    <MaxRequestBody> 268435456 </MaxRequestBody>\n"
                "    <Match pattern=\"^.*$\">\n"
                "        <ShowDirectoryIndexes> on </ShowDirectoryIndexes>\n"
                "    </Match>\n"
                "</Mercury>\n"
            )
            template_conf.write_text(template_xml)

            reconciled_tree, reconciled_text, added_tags = update.reconcile_config(user_conf, template_conf)
            root = reconciled_tree.getroot()

            # Verify user customizations were preserved
            self.assertEqual(root.find("Port").text.strip(), "9090")
            self.assertEqual(root.find("DocumentRoot").text.strip(), "./custom_public/")
            self.assertEqual(root.find("ClientSecurityIPSalt").text.strip(), "custom_salt_xyz")

            # Verify user Match rule was preserved and template Match rule was not copied
            match_nodes = root.findall("Match")
            self.assertEqual(len(match_nodes), 1)
            self.assertEqual(match_nodes[0].attrib.get("pattern"), "^/api/.*$")

            # Verify missing scalar settings were appended
            self.assertIn("StartupCheckLatestRelease", added_tags)
            self.assertIn("MaxRequestBody", added_tags)
            self.assertEqual(root.find("StartupCheckLatestRelease").text.strip(), "true")
            self.assertEqual(root.find("MaxRequestBody").text.strip(), "268435456")

            # Verify existing structure and text were preserved in text
            self.assertIn("\n    <Port> 9090 </Port>\n    <DocumentRoot> ./custom_public/ </DocumentRoot>", reconciled_text)

    def test_reconcile_config_exact_text_when_up_to_date(self):
        # Verify untouched byte-for-byte preservation when no tags are missing
        with tempfile.TemporaryDirectory() as tmp_dir_str:
            tmp_dir = Path(tmp_dir_str)
            user_conf = tmp_dir / "user.conf"
            template_conf = tmp_dir / "template.conf"

            user_xml = (
                "<Mercury>\n"
                "    <!-- Important custom port -->\n"
                "    <Port> 8080 </Port>\n\n"
                "    <DocumentRoot> ./public/ </DocumentRoot>\n"
                "</Mercury>\n"
            )
            user_conf.write_text(user_xml)
            template_conf.write_text(user_xml)

            _, reconciled_text, added_tags = update.reconcile_config(user_conf, template_conf)
            self.assertEqual(added_tags, [])
            self.assertEqual(reconciled_text, user_xml)

    # ==================== Pre-Flight Validation Tests ====================

    def test_validate_config_with_real_binary(self):
        # Skip if binary has not been compiled yet
        if not self.binary_path.exists():
            self.skipTest("Mercury binary not present in bin/")

        # Validate syntax of actual repository config
        is_ok, msg = update.validate_config(self.binary_path, self.conf_path, self.repo_root)
        self.assertTrue(is_ok, f"Validation failed: {msg}")

    def test_validate_config_detects_syntax_errors(self):
        if not self.binary_path.exists():
            self.skipTest("Mercury binary not present in bin/")

        with tempfile.TemporaryDirectory() as tmp_dir_str:
            tmp_dir = Path(tmp_dir_str)
            bad_conf = tmp_dir / "bad.conf"
            # Unclosed tag syntax error
            bad_conf.write_text("<Mercury><Port>80</Mercury>")

            is_ok, msg = update.validate_config(self.binary_path, bad_conf, self.repo_root)
            self.assertFalse(is_ok)
            self.assertTrue(len(msg) > 0)

    def test_validate_config_accepts_minimal_and_optional_nodes(self):
        if not self.binary_path.exists():
            self.skipTest("Mercury binary not present in bin/")

        with tempfile.TemporaryDirectory() as tmp_dir_str:
            tmp_dir = Path(tmp_dir_str)

            # Test completely empty root node (all default values)
            empty_conf = tmp_dir / "empty.conf"
            empty_conf.write_text("<Mercury>\n</Mercury>\n")
            is_ok, msg = update.validate_config(self.binary_path, empty_conf, self.repo_root)
            self.assertTrue(is_ok, f"Empty config validation failed: {msg}")

            # Test minimal config with only Port specified
            port_conf = tmp_dir / "port.conf"
            port_conf.write_text("<Mercury>\n    <Port>8080</Port>\n</Mercury>\n")
            is_ok, msg = update.validate_config(self.binary_path, port_conf, self.repo_root)
            self.assertTrue(is_ok, f"Port-only config validation failed: {msg}")

            # Test custom DocumentRoot and KeepAlive toggle
            partial_conf = tmp_dir / "partial.conf"
            partial_conf.write_text("<Mercury>\n    <DocumentRoot>./public/</DocumentRoot>\n    <KeepAlive>off</KeepAlive>\n</Mercury>\n")
            is_ok, msg = update.validate_config(self.binary_path, partial_conf, self.repo_root)
            self.assertTrue(is_ok, f"Partial config validation failed: {msg}")

    # ==================== Full Update & Rollback Tests ====================

    def test_apply_update_and_automatic_rollback(self):
        if not self.binary_path.exists():
            self.skipTest("Mercury binary not present in bin/")

        with tempfile.TemporaryDirectory() as tmp_dir_str:
            root = Path(tmp_dir_str)
            (root / "bin").mkdir()
            (root / "conf").mkdir()
            (root / "conf" / "default").mkdir()
            (root / "logs").mkdir()

            shutil.copytree(self.repo_root / "public", root / "public")
            shutil.copy2(self.binary_path, root / "bin" / self.binary_path.name)
            shutil.copy2(self.conf_path, root / "conf" / "mercury.conf")
            shutil.copy2(self.default_conf_path, root / "conf" / "default" / "mercury.conf")
            shutil.copy2(self.repo_root / "conf" / "mimes.conf", root / "conf" / "mimes.conf")
            (root / "version.txt").write_text("Mercury v0.35.0\n")

            orig_conf_bytes = (root / "conf" / "mercury.conf").read_bytes()
            orig_bin_bytes = (root / "bin" / self.binary_path.name).read_bytes()

            # Construct mock payload
            payload = root / "payload"
            (payload / "bin").mkdir(parents=True)
            shutil.copy2(self.binary_path, payload / "bin" / self.binary_path.name)
            (payload / "version.txt").write_text("Mercury v0.36.0\n")

            reconciled_text = (root / "conf" / "mercury.conf").read_text()

            # Force validation failure to trigger rollback
            orig_validate = update.validate_config
            update.validate_config = lambda b, c, r: (False, "Simulated fault for rollback test")

            try:
                with self.assertRaises(RuntimeError):
                    update.apply_update(
                        root,
                        root / "conf" / "mercury.conf",
                        payload,
                        reconciled_text,
                        "v0.36.0",
                        config_modified=True
                    )
            finally:
                update.validate_config = orig_validate

            # Verify files were restored to original state
            self.assertEqual((root / "conf" / "mercury.conf").read_bytes(), orig_conf_bytes)
            self.assertEqual((root / "bin" / self.binary_path.name).read_bytes(), orig_bin_bytes)

            # Verify temporary binary backup was deleted
            binary_backups = list((root / "bin").glob("*.bak.*"))
            self.assertEqual(len(binary_backups), 0)

            # Verify config backup remains available for user
            conf_backups = list((root / "conf").glob("mercury.conf.bak.*"))
            self.assertEqual(len(conf_backups), 1)

if __name__ == "__main__":
    unittest.main()

