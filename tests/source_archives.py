"""Optional upstream-source fixtures; unit-test discovery never downloads them."""
from pathlib import Path
import os
import unittest

ROOT = Path(__file__).resolve().parents[1]


def source_archive(filename):
    """Locate an explicitly cached source, or report this integration check skipped."""
    directory = Path(os.environ['EELINUX_TEST_SOURCES']) if os.environ.get(
        'EELINUX_TEST_SOURCES') else ROOT / 'userland/sources'
    path = directory / filename
    if not path.is_file():
        raise unittest.SkipTest(
            f'Optional upstream archive {filename} is absent; populate a source cache '
            'and set EELINUX_TEST_SOURCES to run this check (tests never download)')
    return path
