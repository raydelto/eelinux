"""A source-only checkout fetches pinned inputs into WORK, never into the repo."""
from hashlib import sha256
from io import BytesIO
from pathlib import Path
import os
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import source_cache
from source_archives import source_archive


class LockedSourceCache(unittest.TestCase):
    data = b'pinned upstream archive fixture'
    url = 'https://upstream.example/package.tar.xz'

    def test_missing_legacy_source_downloads_once_to_work_cache(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            legacy, cache = root / 'source-tree/sources', root / 'work/downloads'
            digest = sha256(self.data).hexdigest()
            with patch('urllib.request.urlopen', return_value=BytesIO(self.data)) as fetch:
                archive = source_cache.locked_archive('package.tar.xz', self.url, digest, cache, legacy)
            self.assertEqual(archive, cache / 'package.tar.xz')
            self.assertEqual(archive.read_bytes(), self.data)
            self.assertFalse(legacy.exists())
            fetch.assert_called_once_with(self.url, timeout=90)
            with patch('urllib.request.urlopen') as fetch:
                self.assertEqual(source_cache.locked_archive('package.tar.xz', self.url, digest, cache, legacy), archive)
            fetch.assert_not_called()

    def test_verified_legacy_archive_is_read_without_copying(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            legacy = root / 'sources'
            legacy.mkdir()
            archive = legacy / 'package.tar.xz'
            archive.write_bytes(self.data)
            with patch('source_cache.download') as fetch:
                self.assertEqual(source_cache.locked_archive(archive.name, self.url,
                    sha256(self.data).hexdigest(), root / 'cache', legacy), archive)
            fetch.assert_not_called()
            self.assertFalse((root / 'cache').exists())

    def test_bad_legacy_source_fails_without_replacing_it(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            archive = root / 'package.tar.xz'
            archive.write_bytes(b'corrupt')
            with patch('source_cache.download') as fetch:
                with self.assertRaisesRegex(RuntimeError, 'SHA256 mismatch'):
                    source_cache.locked_archive(archive.name, self.url,
                        sha256(self.data).hexdigest(), root / 'cache', root)
            fetch.assert_not_called()
            self.assertEqual(archive.read_bytes(), b'corrupt')

    def test_bad_download_never_becomes_a_cached_archive(self):
        with tempfile.TemporaryDirectory() as folder:
            cache = Path(folder)
            with patch('urllib.request.urlopen', return_value=BytesIO(b'corrupt')):
                with self.assertRaisesRegex(RuntimeError, 'Checksum mismatch'):
                    source_cache.locked_archive('package.tar.xz', self.url,
                        sha256(self.data).hexdigest(), cache)
            self.assertEqual(list(cache.iterdir()), [])

    def test_archive_input_requires_a_filename_and_complete_lock(self):
        with tempfile.TemporaryDirectory() as folder:
            with patch('source_cache.download') as fetch:
                for filename in ('../archive.tar', '/archive.tar', 'a\\b.tar', 'C:archive.tar', ''):
                    with self.subTest(filename=filename), self.assertRaises(ValueError):
                        source_cache.locked_archive(filename, self.url, sha256(self.data).hexdigest(), Path(folder))
                with self.assertRaises(ValueError):
                    source_cache.locked_archive('archive.tar', self.url, 'unlocked', Path(folder))
                with self.assertRaises(ValueError):
                    source_cache.locked_archive('archive.tar', 'file:///tmp/archive.tar', sha256(self.data).hexdigest(), Path(folder))
            fetch.assert_not_called()

    def test_optional_test_sources_skip_without_network(self):
        with tempfile.TemporaryDirectory() as folder:
            with patch.dict(os.environ, {'EELINUX_TEST_SOURCES': folder}), patch('urllib.request.urlopen') as fetch:
                with self.assertRaisesRegex(unittest.SkipTest, 'tests never download'):
                    source_archive('not-installed.tar.xz')
                expected = Path(folder) / 'installed.tar.xz'
                expected.write_bytes(self.data)
                self.assertEqual(source_archive(expected.name), expected)
            fetch.assert_not_called()


if __name__ == '__main__':
    unittest.main()
