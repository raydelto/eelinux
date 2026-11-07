"""Keep --jobs from allowing unbounded Buildroot helper bootstraps."""
import argparse
from contextlib import ExitStack
import importlib.util
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("eelinux_userland_jobs", ROOT / "scripts/userland.py")
USERLAND = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(USERLAND)


class UserlandJobTests(unittest.TestCase):
    def test_cli_job_limit_reaches_buildroot_helper_and_make(self):
        with tempfile.TemporaryDirectory() as temporary, ExitStack() as mocks:
            directory = Path(temporary)
            output = directory / "output"
            output.mkdir()
            args = argparse.Namespace(jobs=3, profile="base", work=directory / "work",
                                      sysroot=directory / "rootfs", toolchain_prefix="/gnu/bin/mipsr5900el-linux-gnu-")
            mocks.enter_context(patch.object(USERLAND, "sys", SimpleNamespace(platform="linux")))
            mocks.enter_context(patch.object(USERLAND, "use_native_linux_path"))
            mocks.enter_context(patch.object(USERLAND.shutil, "which", return_value="/usr/bin/tool"))
            mocks.enter_context(patch.object(USERLAND, "manifest", return_value={"source_date_epoch": 1}))
            mocks.enter_context(patch.object(USERLAND, "toolchain_probe", return_value={}))
            mocks.enter_context(patch.object(USERLAND, "prepare_config", return_value=(
                ["make", "-C", "/buildroot"], output, {"fixture": "job limit"})))
            for name in ("reject_busybox", "compile_runtime_helpers", "append_project_archive", "verify_target"):
                mocks.enter_context(patch.object(USERLAND, name))
            mocks.enter_context(patch.object(USERLAND, "import_libc_tools", return_value=[]))
            mocks.enter_context(patch.object(USERLAND, "compile_keyboard", return_value=[]))
            mocks.enter_context(patch.object(USERLAND, "build_utf8_locales", return_value=[]))
            mocks.enter_context(patch.object(USERLAND, "build_loader_cache", return_value=[]))
            runner = mocks.enter_context(patch.object(USERLAND, "run", return_value="{}"))
            mocks.enter_context(patch("builtins.print"))
            USERLAND.build(args)
            compile_command = runner.call_args_list[0].args[0]
            self.assertIn("BR2_JLEVEL=3", compile_command)
            self.assertIn("-j3", compile_command)

    def test_zero_jobs_cannot_fall_back_to_buildroot_cpu_autosizing(self):
        args = argparse.Namespace(jobs=0)
        with patch.object(USERLAND, "sys", SimpleNamespace(platform="linux")), \
                patch.object(USERLAND, "use_native_linux_path"), \
                patch.object(USERLAND, "run") as runner:
            with self.assertRaises(USERLAND.BuildError):
                USERLAND.build(args)
            runner.assert_not_called()


if __name__ == "__main__":
    unittest.main()
