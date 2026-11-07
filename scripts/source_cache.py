"""Resolve pinned source archives without writing downloads into the source tree.

SPDX-License-Identifier: GPL-2.0-only
"""
from pathlib import Path
import hashlib
import re
from urllib.parse import urlsplit

from common import download


def locked_archive(filename, url, digest, cache, legacy=None):
    """Use a verified legacy archive, or fetch the exact lock into WORK/downloads."""
    if not isinstance(filename, str) or not filename or filename in ('.', '..') or any(
            character in filename for character in ('/', '\\', ':', '\0')):
        raise ValueError('Source archive requires a plain filename')
    if not isinstance(digest, str) or not re.fullmatch(r'[0-9a-f]{64}', digest):
        raise ValueError('Source archive requires a locked SHA-256')
    if urlsplit(url).scheme not in ('https', 'http') or not urlsplit(url).netloc:
        raise ValueError('Source archive requires an HTTP(S) upstream URL')
    if legacy is not None:
        existing = Path(legacy) / filename
        if existing.is_file():
            with existing.open('rb') as source:
                actual = hashlib.file_digest(source, 'sha256').hexdigest()
            if actual != digest:
                raise RuntimeError('Source SHA256 mismatch: ' + str(existing))
            return existing
    return download(url, Path(cache) / filename, digest)
