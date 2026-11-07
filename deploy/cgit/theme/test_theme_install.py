#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exercise theme deployment against temporary Linux account directories."""

from contextlib import redirect_stdout
from hashlib import sha256
from html.parser import HTMLParser
from io import StringIO
import json
import os
from pathlib import Path
import tempfile
import unittest

import install_theme as theme


class FragmentParser(HTMLParser):
    """Includes must be self-contained at their actual cgit insertion points."""
    VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack = []
        self.errors = []
        self.tags = []

    def handle_starttag(self, tag, attrs):
        self.tags.append((tag, dict(attrs)))
        if tag not in self.VOID:
            self.stack.append(tag)

    def handle_endtag(self, tag):
        if not self.stack or self.stack.pop() != tag:
            self.errors.append(tag)


class IncludeTests(unittest.TestCase):
    def test_header_is_a_closed_sidebar_not_a_wrapper_around_cgit(self):
        # cgit emits header immediately after <body>; footer is emitted inside
        # #cgit after its .content. An open wrapper spanning these is invalid.
        root = Path(__file__).parent
        for name in theme.PRIVATE_FILES:
            with self.subTest(include=name):
                parser = FragmentParser()
                parser.feed((root / name).read_text(encoding="utf-8"))
                self.assertEqual(parser.errors, [])
                self.assertEqual(parser.stack, [])
                self.assertFalse(any(tag in {"html", "body", "main"} for tag, _ in parser.tags))
        header = (root / "header.html").read_text(encoding="utf-8")
        self.assertIn('href="#cgit"', header)
        self.assertIn('<aside class="ee-sidebar"', header)
        self.assertIn('href="/git/" aria-current="page"', header)
        self.assertNotIn("JAVASCRIPT FREE", header)
        head = (root / "head.html").read_text(encoding="utf-8")
        self.assertEqual(head.count("{{THEME_URL}}"), 1)
        self.assertIn("viewport", head)


@unittest.skipUnless(os.name == "posix", "The deployment paths and permissions are Linux-specific")
class ThemeInstallTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="eelinux-theme-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.document = self.root / "public_html" / "eelinux"
        self.public = self.document / "git"
        self.private = self.root / "eelinux-cgit"
        self.repo = self.root / "repos" / "eelinux.git"
        for directory in (self.public / "static", self.private / "bin", self.private / "cache", self.repo):
            directory.mkdir(parents=True)
        self.config = self.private / "cgitrc"
        self.original = (
            "# Existing deployment; retain this exact comment.\n"
            "root-title=eeLinux source\nroot-desc=GNU/Linux for the PlayStation 2\n"
            "css=/git/static/cgit.css\njs=/git/static/cgit.js\n"
            "logo=/git/static/cgit.png\nfavicon=/git/static/favicon.ico\n"
            "logo-link=https://eelinux.falanet.org/\nvirtual-root=/git/\n"
            f"cache-root={self.private}/cache\n"
            "cache-size=256\ncache-dynamic-ttl=2\n"
            "enable-http-clone=1\nenable-filter-overrides=0\nenable-html-serving=0\n"
            "max-blob-size=1024\nsnapshots=tar.gz\n\n"
            "repo.url=eelinux\nrepo.name=eeLinux\n"
            f"repo.path={self.repo}\nrepo.defbranch=main\n"
            "repo.clone-url=https://eelinux.falanet.org/git/eelinux\n"
        ).encode()
        self.config.write_bytes(self.original)
        self.runtime = {self.config: self.original,
                        self.private / "bin" / "cgit": b"existing runtime",
                        self.public / "index.cgi": b"#!/bin/sh\nexisting wrapper\n",
                        self.public / "static" / "cgit.css": b"base cgit style\n",
                        self.public / "static" / "cgit.js": b"base cgit javascript\n"}
        for path, data in self.runtime.items():
            path.write_bytes(data)
        self.locations = theme.locations_for(self.document, self.private, self.repo, theme.DEFAULT_URL)
        self.state_path = self.private / "install-state.json"
        self.state = {"locations": self.locations, "build": {"binary_sha256": "unchanged-build"},
                      "files": {str(path): sha256(data).hexdigest() for path, data in self.runtime.items()},
                      "custom_metadata": {"preserve": True}}
        self.state_path.write_text(json.dumps(self.state), encoding="utf-8")
        self.state_bytes = self.state_path.read_bytes()
        self.theme_dir = Path(__file__).parent

    def apply(self, **kwargs):
        return theme.apply_theme(self.theme_dir, self.document, self.private, self.repo, theme.DEFAULT_URL, **kwargs)

    def nonpresentation(self, data):
        return b"".join(line for line in data.splitlines(keepends=True)
                        if not (theme.KEY_LINE.match(line.decode("utf-8"))
                                and theme.KEY_LINE.match(line.decode("utf-8")).group(1) in theme.MANAGED_KEYS))

    def test_install_preserves_repo_security_clone_and_runtime_and_updates_state(self):
        old_cache_file = self.private / "cache" / "keep-this-cache-entry"
        old_cache_file.write_bytes(b"untouched")
        result = self.apply()
        self.assertTrue(result["changed"])
        self.assertEqual(self.nonpresentation(self.config.read_bytes()), self.nonpresentation(self.original))
        for path, content in self.runtime.items():
            if path != self.config:
                self.assertEqual(path.read_bytes(), content)
        self.assertEqual(old_cache_file.read_bytes(), b"untouched")
        self.assertTrue(Path(result["cache_root"]).is_dir())
        self.assertEqual(Path(result["cache_root"]).stat().st_mode & 0o777, 0o700)
        state = json.loads(self.state_path.read_bytes())
        self.assertEqual(state["build"], self.state["build"])
        self.assertEqual(state["custom_metadata"], self.state["custom_metadata"])
        self.assertEqual(state["theme"]["id"], result["theme_id"])
        for name, expected in state["files"].items():
            self.assertEqual(sha256(Path(name).read_bytes()).hexdigest(), expected)
        head = (self.private / "themes" / result["theme_id"] / "head.html").read_text()
        self.assertIn(result["theme_url"] + "/eelinux-cgit.css", head)
        self.assertNotIn("{{THEME_URL}}", head)
        self.assertIn(b"css=/git/static/cgit.css\n", self.config.read_bytes())

    def test_backup_is_private_and_second_run_does_not_mutate(self):
        result = self.apply()
        backup = Path(result["backup"])
        self.assertEqual(backup.stat().st_mode & 0o777, 0o700)
        records = json.loads((backup / "backup.json").read_bytes())["files"]
        copies = {record["path"]: (backup / record["backup"]).read_bytes() for record in records}
        self.assertEqual(copies[str(self.config)], self.original)
        self.assertEqual(copies[str(self.state_path)], self.state_bytes)
        for file in backup.iterdir():
            self.assertEqual(file.stat().st_mode & 0o777, 0o600)
        config_before = self.config.stat().st_mtime_ns
        state_before = self.state_path.read_bytes()
        again = self.apply()
        self.assertFalse(again["changed"])
        self.assertEqual(again["cache_root"], result["cache_root"])
        self.assertEqual(self.config.stat().st_mtime_ns, config_before)
        self.assertEqual(self.state_path.read_bytes(), state_before)
        self.assertEqual(len(list((self.private / "theme-backups").iterdir())), 1)

    def test_refuses_unknown_destination_and_does_not_write_anything_else(self):
        _inputs, identity = theme.read_theme(self.theme_dir)
        target = self.public / "static" / f"eelinux-theme-{identity}" / "eelinux-cgit.css"
        target.parent.mkdir()
        target.write_bytes(b"unrecorded custom stylesheet")
        with self.assertRaisesRegex(ValueError, "immutable theme destination differs"):
            self.apply()
        self.assertEqual(target.read_bytes(), b"unrecorded custom stylesheet")
        self.assertEqual(self.config.read_bytes(), self.original)
        self.assertEqual(self.state_path.read_bytes(), self.state_bytes)
        self.assertFalse((self.private / "theme-backups").exists())

    def test_identical_unknown_theme_asset_is_adopted_and_backed_up(self):
        inputs, identity = theme.read_theme(self.theme_dir)
        target = self.public / "static" / f"eelinux-theme-{identity}" / "eelinux-cgit.css"
        target.parent.mkdir()
        target.write_bytes(inputs["eelinux-cgit.css"])
        result = self.apply()
        records = json.loads((Path(result["backup"]) / "backup.json").read_bytes())["files"]
        self.assertIn(str(target), [entry["path"] for entry in records])
        self.assertEqual(target.read_bytes(), inputs["eelinux-cgit.css"])

    def test_refuses_modified_recorded_config_before_mutations(self):
        modified = self.original + b"# administrator change\n"
        self.config.write_bytes(modified)
        with self.assertRaisesRegex(ValueError, "recorded cgit file changed"):
            self.apply()
        self.assertEqual(self.config.read_bytes(), modified)
        self.assertEqual(self.state_path.read_bytes(), self.state_bytes)
        self.assertFalse((self.private / "theme-backups").exists())

    def test_refuses_symlink_and_git_checkout_mutation_paths(self):
        (self.public / "static").rename(self.public / "static-original")
        (self.public / "static").symlink_to(self.public / "static-original", target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "symbolic-link"):
            self.apply()
        self.assertEqual(self.config.read_bytes(), self.original)
        (self.public / "static").unlink()
        (self.public / "static-original").rename(self.public / "static")
        (self.root / ".git").mkdir()
        with self.assertRaisesRegex(ValueError, "Git checkout"):
            self.apply()
        self.assertEqual(self.config.read_bytes(), self.original)

    def test_check_only_is_read_only_and_cli_aliases_work(self):
        before = sorted(str(path.relative_to(self.root)) for path in self.root.rglob("*"))
        result = self.apply(dry_run=True)
        self.assertTrue(result["changed"])
        self.assertEqual(before, sorted(str(path.relative_to(self.root)) for path in self.root.rglob("*")))
        self.assertEqual(self.config.read_bytes(), self.original)
        output = StringIO()
        with redirect_stdout(output):
            theme.main(["--theme-dir", str(self.theme_dir), "--private-root", str(self.private),
                        "--public-root", str(self.public), "--repository", str(self.repo), "--check-only"])
        self.assertTrue(json.loads(output.getvalue())["changed"])
        self.assertEqual(self.state_path.read_bytes(), self.state_bytes)


if __name__ == "__main__":
    unittest.main()
