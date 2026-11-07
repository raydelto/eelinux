"""Target runtime flags and explicit source-lock selection for the GNU compiler."""
from contextlib import redirect_stdout
import io
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "toolchain"))
import bootstrap


class ToolchainBootstrapTests(unittest.TestCase):
    def test_release_patch_is_pinned_and_idempotent(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            source.mkdir()
            (source / "file.c").write_bytes(b"before")
            (root / "change.patch").write_bytes(b"pinned patch")
            package = {"patches": [{"path": "change.patch",
                       "sha256": hashlib.sha256(b"pinned patch").hexdigest(),
                       "files": {"file.c": {"before_sha256": hashlib.sha256(b"before").hexdigest(),
                                            "after_sha256": hashlib.sha256(b"after").hexdigest()}}}]}
            def apply(command, **kwargs):
                if "--dry-run" not in command:
                    (source / "file.c").write_bytes(b"after")
            with mock.patch.object(bootstrap, "ROOT", root), mock.patch.object(bootstrap, "run", side_effect=apply) as run:
                bootstrap.apply_source_patches(source, package)
                self.assertEqual(run.call_count, 2)
                bootstrap.apply_source_patches(source, package)
                self.assertEqual(run.call_count, 2)
                (source / "file.c").write_bytes(b"unexpected")
                with self.assertRaisesRegex(RuntimeError, "pristine release files"):
                    bootstrap.apply_source_patches(source, package)
                (root / "change.patch").write_bytes(b"altered patch")
                with self.assertRaisesRegex(RuntimeError, "checksum mismatch"):
                    bootstrap.apply_source_patches(source, package)

    def test_kernel_export_forces_archheaders_and_checks_all_syscall_abis(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            sysroot = base / "sysroot"
            with mock.patch.object(bootstrap, "run") as run:
                with self.assertRaisesRegex(RuntimeError, "unistd_o32.h"):
                    bootstrap.install_kernel_headers(base / "kernel", base / "headers", sysroot, {})
            command = run.call_args.args[0]
            self.assertIn("-B", command)
            self.assertLess(command.index("archheaders"), command.index("headers_install"))
            headers = sysroot / "usr/include/asm"
            headers.mkdir(parents=True)
            for name in ("unistd.h", "unistd_o32.h", "unistd_n32.h", "unistd_n64.h",
                         "unistd_nr_o32.h", "unistd_nr_n32.h", "unistd_nr_n64.h"):
                (headers / name).write_text("/* exported kernel syscall header */\n")
            with mock.patch.object(bootstrap, "run"):
                bootstrap.install_kernel_headers(base / "kernel", base / "headers", sysroot, {})

    def test_new_glibc_enables_kernel_emulated_atomic_instructions_only_for_libc(self):
        modern = bootstrap.glibc_cpu_flags("2.44")
        old = bootstrap.glibc_cpu_flags("2.41")
        self.assertIn("-mllsc", modern)
        self.assertNotIn("-mno-llsc", modern)
        self.assertIn("-mno-llsc", old)
        self.assertEqual(modern[:-1], old[:-1])
        self.assertIn("-mno-llsc", bootstrap.target_environment({})["CFLAGS_FOR_TARGET"])

    def test_target_flags_do_not_change_host_compiler_flags(self):
        host = {"PATH": "/host/bin", "CFLAGS": "-O1", "CXXFLAGS": "-O1"}
        target = bootstrap.target_environment(host)
        self.assertEqual(host, {"PATH": "/host/bin", "CFLAGS": "-O1", "CXXFLAGS": "-O1"})
        self.assertEqual(target["CFLAGS"], "-O1")
        self.assertEqual(target["CXXFLAGS"], "-O1")
        for name in ("CFLAGS_FOR_TARGET", "CXXFLAGS_FOR_TARGET"):
            for flag in ("-march=r5900", "-mabi=32", "-msoft-float", "-mfix-r5900", "-mno-llsc"):
                self.assertIn(flag, target[name].split())

    def test_plan_can_select_preserved_source_lock(self):
        lock = ROOT / "toolchain/source-lock-14.4.0.json"
        output = io.StringIO()
        with mock.patch.object(sys, "argv", ["bootstrap.py", "plan", "--source-lock", str(lock)]), redirect_stdout(output):
            bootstrap.main()
        plan, order = output.getvalue().rsplit("\nBuild order:", 1)
        self.assertEqual(json.loads(plan), json.loads(lock.read_text()))
        self.assertIn("GCC C/C++", order)

    def test_default_build_uses_separate_latest_cache_and_prefix(self):
        with mock.patch.object(sys, "argv", ["bootstrap.py", "build", "--kernel-source", "/kernel"]), mock.patch.object(bootstrap, "build") as build:
            bootstrap.main()
        args = build.call_args.args[0]
        self.assertEqual(args.work, Path.home() / ".cache/eelinux-linux-latest/toolchain")
        self.assertEqual(args.prefix, Path.home() / ".local/eelinux-linux-latest")
        self.assertEqual(args.source_lock, ROOT / "toolchain/source-lock.json")


if __name__ == "__main__":
    unittest.main()
