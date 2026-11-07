"""Evaluate the real package override without changing the C++ dialect."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name == "posix" and shutil.which("make"), "GNU make host required")
class BashDialectTests(unittest.TestCase):
    def test_bash_selects_pre_c23_dialect_only_for_newer_c_compilers(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            for newer in ("n", "y"):
                with self.subTest(gcc_at_least_15=newer):
                    makefile = folder / "Makefile"
                    makefile.write_text(
                        f"BR2_EXTERNAL_EELINUX_PATH := {folder}\n"
                        f"BR2_TOOLCHAIN_GCC_AT_LEAST_15 := {newer}\n"
                        'TARGET_CFLAGS := -Os -mno-llsc\n'
                        'TARGET_CXXFLAGS := -O2 -std=gnu++20\n'
                        'BASH_CONF_ENV := sentinel=yes\n'
                        f"include {REPO / 'userland/external.mk'}\n"
                        '$(info BASH=$(BASH_CONF_ENV))\n'
                        '$(info C=$(TARGET_CFLAGS))\n'
                        '$(info CXX=$(TARGET_CXXFLAGS))\n'
                        'check:\n\t@true\n'
                    )
                    result = subprocess.run(
                        ["make", "--no-print-directory", "-f", str(makefile), "check"],
                        check=True, capture_output=True, text=True,
                    )
                    fields = dict(line.split("=", 1) for line in result.stdout.splitlines())
                    self.assertEqual(fields["C"], "-Os -mno-llsc")
                    self.assertEqual(fields["CXX"], "-O2 -std=gnu++20")
                    expected = 'sentinel=yes CFLAGS="-Os -mno-llsc -std=gnu17"' if newer == "y" else 'sentinel=yes'
                    self.assertEqual(fields["BASH"], expected)

    def test_ed_configure_preserves_cross_environment_with_supported_dialect(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            package = folder / "ed"
            package.mkdir()
            configure = package / "configure"
            configure.write_text('#!/bin/sh\nprintf "%s\\n" "$CROSS_SENTINEL" "$@" > received\n')
            configure.chmod(0o755)
            for newer in ("n", "y"):
                with self.subTest(gcc_at_least_15=newer):
                    makefile = folder / "Makefile"
                    makefile.write_text(
                        f"BR2_EXTERNAL_EELINUX_PATH := {folder}\n"
                        f"BR2_TOOLCHAIN_GCC_AT_LEAST_15 := {newer}\n"
                        "TARGET_MAKE_ENV := CROSS_SENTINEL=preserved\n"
                        "TARGET_CONFIGURE_OPTS := CC=r5900-linux-gcc\n"
                        "TARGET_CFLAGS := -Os -mno-llsc\n"
                        "define ED_CONFIGURE_CMDS\n"
                        "\t(cd $(@D); $(TARGET_MAKE_ENV) ./configure --prefix=/usr $(TARGET_CONFIGURE_OPTS) CFLAGS=\"$(TARGET_CFLAGS)\")\n"
                        "endef\n"
                        f"include {REPO / 'userland/external.mk'}\n"
                        f"{package / 'configured'}:\n\t$(ED_CONFIGURE_CMDS)\n"
                    )
                    subprocess.run(["make", "--no-print-directory", "-f", str(makefile)],
                                   check=True, capture_output=True, text=True)
                    self.assertEqual((package / "received").read_text().splitlines(), [
                        "preserved", "--prefix=/usr", "CC=r5900-linux-gcc",
                        "CFLAGS=-Os -mno-llsc" + (" -std=gnu17" if newer == "y" else ""),
                    ])

    def test_nano_hook_rejects_unrecognized_source_before_patching(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / "patches").mkdir()
            shutil.copyfile(REPO / "userland/patches/nano-8.2-explicit-bool.patch",
                            folder / "patches/nano-8.2-explicit-bool.patch")
            package = folder / "nano"
            (package / "src").mkdir(parents=True)
            original = b"source from an unrecognized release\n"
            source = package / "src/move.c"
            source.write_bytes(original)
            makefile = folder / "Makefile"
            makefile.write_text(
                f"BR2_EXTERNAL_EELINUX_PATH := {folder}\n"
                f"include {REPO / 'userland/external.mk'}\n"
                f"{package / 'patched'}:\n\t$(EELINUX_NANO_EXPLICIT_BOOL)\n"
            )
            result = subprocess.run(["make", "--no-print-directory", "-f", str(makefile)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("checksum did NOT match", result.stderr)
            self.assertEqual(source.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
