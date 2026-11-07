"""Latest upstream recipes must be pinned before Buildroot's package eval."""
from pathlib import Path
import hashlib
import io
import json
import lzma
import struct
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import latest_packages as latest


class LatestPackagesTests(unittest.TestCase):
    def test_base_assignment_changes_before_eval_keep_feature_additions(self):
        original = "FOO_CONF_OPTS = --old \\\n+ --other\nFOO_CONF_OPTS += --feature\n$(eval $(generic-package))\n"
        text = latest.set_variable(original, "FOO_CONF_OPTS", "--current")
        text = latest.set_variable(text, "FOO_SITE_METHOD", "wget")
        self.assertIn("FOO_CONF_OPTS = --current\nFOO_CONF_OPTS += --feature", text)
        self.assertNotIn("--other", text)
        self.assertLess(text.index("FOO_SITE_METHOD"), text.index("$(eval"))
        with self.assertRaises(latest.LatestPackageError):
            latest.set_variable("FOO_VERSION=1\nFOO_VERSION=2\n", "FOO_VERSION", "3")

    def test_overlay_updates_license_source_and_only_reviewed_patches(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "backend"
            package = source / "package/foo"
            package.mkdir(parents=True)
            recipe = package / "foo.mk"
            recipe.write_text("FOO_VERSION = 1\nFOO_LICENSE_FILES = COPYING\n"
                              "FOO_CONF_OPTS += --feature\n$(eval $(generic-package))\n")
            (package / "foo.hash").write_text("sha256  " + "0"*64 + "  foo-1.tar.gz\n"
                                               "sha256  " + "0"*64 + "  COPYING\n")
            retired = package / "0001-merged.patch"
            retired.write_text("upstream change\n")
            untouched = package / "0002-required.patch"
            untouched.write_text("cross build change\n")
            archive = root / "userland/sources/foo-2.tar.gz"
            archive.parent.mkdir(parents=True)
            body = b"Current upstream license\n"
            with tarfile.open(archive, "w:gz") as stream:
                item = tarfile.TarInfo("foo-2/COPYING")
                item.size = len(body)
                stream.addfile(item, io.BytesIO(body))
            records = {"schema": 1, "packages": {"foo": {
                "version": "2", "filename": archive.name,
                "url": "https://example.test/refs/tags/v2.tar.gz",
                "sha256": latest.digest(archive),
                "remove_patches": {retired.name: latest.digest(retired)},
                "variables": {"FOO_CONF_OPTS": "--current"}}}}
            manifest = root / "latest.json"
            manifest.write_text(json.dumps(records))
            with patch.object(latest, "ROOT", root), patch.object(latest, "MANIFEST", manifest):
                report = latest.apply(source, root / "work", latest.manifest())
                latest.verify(source, records)
                self.assertFalse(retired.exists())
                self.assertTrue(untouched.exists())
                self.assertEqual(report["packages"]["foo"]["license_sha256"]["COPYING"], hashlib.sha256(body).hexdigest())
                self.assertIn(hashlib.sha256(body).hexdigest(), (package / "foo.hash").read_text())
                self.assertTrue((root / "work/downloads/foo/foo-2.tar.gz").exists())
                self.assertLess(recipe.read_text().index("FOO_VERSION = 2"), recipe.read_text().index("$(eval"))
                self.assertIn("FOO_CONF_OPTS += --feature", recipe.read_text())
                recipe.write_text(recipe.read_text() + "# altered\n")
                with self.assertRaisesRegex(latest.LatestPackageError, "altered"):
                    latest.verify(source, records)

    def test_complete_url_is_used_when_release_basename_differs(self):
        records = {"packages": {"foo": {"version": "2", "filename": "foo-2.tar.gz",
                    "url": "https://example.test/archive/refs/tags/v2.tar.gz", "sha256": "1"*64}}}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with patch.object(latest, "ROOT", root), patch.object(latest, "download") as download:
                latest.cache_sources(records, root / "work")
                self.assertEqual(download.call_args.args[0], records["packages"]["foo"]["url"])
                self.assertEqual(download.call_args.args[1].name, "foo-2.tar.gz")

    def test_lzip_source_checks_header_sizes_and_crc(self):
        body = b"source archive bytes" * 30
        compressed = lzma.compress(body, format=lzma.FORMAT_RAW,
            filters=[{"id": lzma.FILTER_LZMA1, "dict_size": 1 << 20, "lc": 3, "lp": 0, "pb": 2}])
        member = b"LZIP\x01\x14" + compressed
        member += struct.pack("<IQQ", zlib.crc32(body), len(body), len(member) + 20)
        self.assertEqual(latest.lzip_member(member), body)
        corrupt = bytearray(member)
        corrupt[-20] ^= 1
        with self.assertRaisesRegex(latest.LatestPackageError, "checksum"):
            latest.lzip_member(bytes(corrupt))


if __name__ == "__main__":
    unittest.main()
