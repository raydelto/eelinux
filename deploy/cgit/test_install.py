"""Exercise account isolation, CGI generation and non-destructive updates."""

from hashlib import sha256
from pathlib import Path
from tempfile import TemporaryDirectory
import json
import unittest

import install


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.doc = self.root / "public_html"
        self.doc.mkdir()
        self.private = self.root / "private"
        self.repo = self.root / "repos/eelinux.git"
        self.build = self.root / "build"
        self.build.mkdir()
        (self.build / "cgit").write_bytes(b"\x7fELFfixture")
        for name in install.ASSETS:
            (self.build / name).write_bytes(("fixture " + name).encode())

    def tearDown(self):
        self.temp.cleanup()

    def plan(self, **kwargs):
        return install.make_plan(self.doc, kwargs.get("private", self.private),
                                 kwargs.get("repo", self.repo), self.build,
                                 "https://eelinux.falanet.org/git")

    def test_private_storage_cannot_be_inside_webroot(self):
        for kwargs in ({"private": self.doc / "private"}, {"repo": self.doc / "repo.git"}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                self.plan(**kwargs)

    def test_public_origin_and_path_are_restricted(self):
        for value in ("http://eelinux.falanet.org/git", "https://user:pass@example.org/git",
                      "https://example.org/other", "https://example.org/git?config=secret"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                install.url_settings(value)

    def test_generated_wrapper_and_pretty_routes(self):
        plan, _ = self.plan()
        wrapper = plan[self.doc / "git/index.cgi"].data.decode()
        config = plan[self.private / "cgitrc"].data.decode()
        apache = plan[self.doc / "git/.htaccess"].data.decode()
        self.assertIn(f"CGIT_CONFIG={self.private}/cgitrc", wrapper)
        self.assertIn(f"exec {self.private}/bin/cgit", wrapper)
        self.assertIn("virtual-root=/git/", config)
        self.assertIn("repo.clone-url=https://eelinux.falanet.org/git/eelinux", config)
        self.assertIn("RewriteRule ^(.*)$ index.cgi/$1 [END,QSA]", apache)
        self.assertIn("AddHandler cgi-script .cgi", apache)
        self.assertIn("\njs=/git/static/cgit.js\n", config)

    def test_existing_upstream_files_require_opt_in_and_are_preserved(self):
        public = self.doc / "git"
        public.mkdir()
        unknown = public / "README"
        unknown.write_text("existing upstream source")
        plan, locations = self.plan()
        with self.assertRaises(ValueError):
            install.preflight(plan, locations)
        install.install(plan, locations, {}, existing_public=True)
        self.assertEqual(unknown.read_text(), "existing upstream source")
        self.assertTrue((public / "index.cgi").exists())

    def test_unknown_htaccess_is_never_replaced(self):
        public = self.doc / "git"
        public.mkdir()
        original = public / ".htaccess"
        original.write_text("existing host rules")
        plan, locations = self.plan()
        with self.assertRaises(ValueError):
            install.install(plan, locations, {}, update=True, existing_public=True)
        self.assertEqual(original.read_text(), "existing host rules")
        self.assertFalse(self.private.exists())

    def test_only_hash_recorded_unchanged_files_can_update(self):
        plan, locations = self.plan()
        install.install(plan, locations, {})
        self.assertEqual((self.private / "cgitrc").stat().st_mode & 0o777, 0o600)
        asset = self.doc / "git/static/cgit.css"
        plan[asset] = install.Payload(b"new css", 0o644)
        with self.assertRaises(ValueError):
            install.install(plan, locations, {})
        install.install(plan, locations, {}, update=True)
        self.assertEqual(asset.read_bytes(), b"new css")
        asset.write_bytes(b"custom user change")
        plan[asset] = install.Payload(b"next css", 0o644)
        with self.assertRaises(ValueError):
            install.install(plan, locations, {}, update=True)
        self.assertEqual(asset.read_bytes(), b"custom user change")

    def test_dry_run_writes_nothing(self):
        plan, locations = self.plan()
        install.install(plan, locations, {}, dry_run=True)
        self.assertFalse(self.private.exists())
        self.assertFalse((self.doc / "git").exists())

    def test_symlink_cannot_redirect_installation(self):
        alias = self.root / "alias"
        alias.symlink_to(self.doc, target_is_directory=True)
        with self.assertRaises(ValueError):
            install.account_path(alias / "git/index.cgi")

    def test_runtime_bundle_hashes_are_required_before_install(self):
        lock = json.loads(install.LOCK.read_text())
        report = {"cgit_commit": lock["commit"], "git_commit": lock["git_submodule_commit"],
                  "binary_sha256": sha256((self.build / "cgit").read_bytes()).hexdigest()}
        (self.build / "eelinux-build.json").write_text(json.dumps(report))
        (self.build / "install.py").write_bytes(Path(install.__file__).read_bytes())
        (self.build / "source-lock.json").write_bytes(install.LOCK.read_bytes())
        names = ("cgit", "eelinux-build.json", "install.py", "source-lock.json", *install.ASSETS)
        bundle = {"schema": 1, "cgit_commit": lock["commit"], "git_commit": lock["git_submodule_commit"],
                  "files": {name: sha256((self.build / name).read_bytes()).hexdigest() for name in names}}
        (self.build / "runtime-bundle.json").write_text(json.dumps(bundle))
        self.assertEqual(install.load_build(self.build)[1], report)
        (self.build / "cgit.css").write_bytes(b"altered in transit")
        with self.assertRaises(ValueError):
            install.load_build(self.build)


if __name__ == "__main__":
    unittest.main()
