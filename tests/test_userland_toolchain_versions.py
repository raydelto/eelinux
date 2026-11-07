"""Reject compiler/backend mismatches before native SDK packages are selected."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("eelinux_userland_versions", ROOT / "scripts/userland.py")
USERLAND = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(USERLAND)


class ToolchainVersionTests(unittest.TestCase):
    def test_legacy_base_remains_supported(self):
        self.assertEqual(USERLAND.validate_gcc_version("14.4.0", "base"), 14)

    def test_latest_exact_compiler_is_required_for_matching_native_sdk(self):
        self.assertEqual(USERLAND.validate_gcc_version("16.2.0", "full"), 16)
        for version in ("14.4.0", "16.1.0", "16.3.0"):
            with self.subTest(version=version), self.assertRaises(USERLAND.BuildError):
                USERLAND.validate_gcc_version(version, "full")

    def test_unadapted_compiler_choices_are_rejected(self):
        for version in ("15.2.0", "17.1.0", "16", "sixteen", "16.2.0-dev"):
            with self.subTest(version=version), self.assertRaises(USERLAND.BuildError):
                USERLAND.validate_gcc_version(version, "base")

    def test_configure_uses_same_full_profile_gate(self):
        self.assertEqual(USERLAND.validate_gcc_version("16.2.0", "full", configure_only=True), 16)
        with self.assertRaises(USERLAND.BuildError):
            USERLAND.validate_gcc_version("14.4.0", "full", configure_only=True)


if __name__ == "__main__":
    unittest.main()
