"""Safety checks for the PS2 image builder; run with unittest discovery."""
import importlib.util
import gzip
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location("eelinux_kernel_builder", ROOT / "scripts/kernel.py")
kernel = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(kernel)
CACHE_SPEC = importlib.util.spec_from_file_location("eelinux_cache_port", ROOT / "scripts/kernel_cache_port.py")
cache_port = importlib.util.module_from_spec(CACHE_SPEC)
CACHE_SPEC.loader.exec_module(cache_port)


def executable(machine=8, interpreter=False, bad_entry=False):
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    count = 2 if interpreter else 1
    offset = 52 + count * 32
    address = 0x400000
    header = struct.pack("<16sHHIIIIIHHHHHH", ident, 2, machine, 1,
                         address + (8 if bad_entry else 0), 52, 0, 0x20924001,
                         52, 32, count, 0, 0, 0)
    load = struct.pack("<IIIIIIII", 1, offset, address, address, 4, 4, 5, 4096)
    interp = struct.pack("<IIIIIIII", 3, offset, 0, 0, 4, 4, 4, 1) if interpreter else b""
    return header + load + interp + b"\x00" * 4


def boot_executable(address, memory=4096):
    data = bytearray(executable())
    struct.pack_into("<I", data, 24, address)
    struct.pack_into("<I", data, 52 + 8, address)
    struct.pack_into("<I", data, 52 + 12, address)
    struct.pack_into("<I", data, 52 + 20, memory)
    return data


class EarlyBootTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)

    def write_modules(self, text):
        (self.root / "modules.dep").write_text(text)
        for line in text.splitlines():
            path = self.root / line.split(":", 1)[0]
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"module")

    def test_transitive_modules_precede_consumers_once(self):
        self.write_modules("kernel/ohci-ps2.ko: kernel/sif.ko kernel/usbcore.ko\n"
                           "kernel/usbcore.ko: kernel/sif.ko\n"
                           "kernel/sif.ko:\n")
        self.assertEqual(kernel.module_manifest(self.root, ["ohci_ps2", "sif"]),
                         ["kernel/sif.ko", "kernel/usbcore.ko", "kernel/ohci-ps2.ko"])

    def test_module_cycle_fails(self):
        self.write_modules("a.ko: b.ko\nb.ko: a.ko\n")
        with self.assertRaisesRegex(RuntimeError, "dependency cycle"):
            kernel.module_manifest(self.root, ["a"])

    def test_missing_module_dependency_fails(self):
        self.write_modules("a.ko: b.ko\n")
        with self.assertRaisesRegex(RuntimeError, "Missing early boot module"):
            kernel.module_manifest(self.root, ["a"])

    def test_unbuilt_root_module_fails(self):
        self.write_modules("a.ko:\n")
        with self.assertRaisesRegex(RuntimeError, "was not built"):
            kernel.module_manifest(self.root, ["ohci-ps2"])

    def test_built_in_bridge_is_not_packed_or_loaded_as_missing_modules(self):
        self.write_modules("kernel/ohci-ps2.ko: kernel/usbcore.ko\n"
                           "kernel/usbcore.ko:\n")
        (self.root / "modules.builtin").write_text(
            "kernel/drivers/ps2/sif.ko\n"
            "kernel/drivers/ps2/iop-memory.ko\n"
            "kernel/drivers/ps2/iop-module.ko\n"
            "kernel/drivers/ps2/iop-irq.ko\n"
            "kernel/drivers/net/ethernet/ps2/ps2-smap.ko\n")
        roots = ["sif", "iop_memory", "iop-module", "iop-irq", "ps2_smap", "ohci_ps2"]
        self.assertEqual(kernel.module_manifest(self.root, roots),
                         ["kernel/usbcore.ko", "kernel/ohci-ps2.ko"])
        self.assertFalse((self.root / "kernel/drivers/ps2/sif.ko").exists())

    def test_absent_builtin_metadata_does_not_hide_an_unbuilt_bridge(self):
        self.write_modules("kernel/usbcore.ko:\n")
        with self.assertRaisesRegex(RuntimeError, "was not built: sif"):
            kernel.module_manifest(self.root, ["sif", "usbcore"])

    def test_dependency_cannot_escape_module_directory(self):
        (self.root / "modules.dep").write_text("a.ko: ../outside.ko\n")
        with self.assertRaisesRegex(RuntimeError, "Unsafe module dependency path"):
            kernel.module_manifest(self.root, ["a"])

    def check_init(self, data):
        path = self.root / "init"
        path.write_bytes(data)
        kernel.validate_static_init(path)

    def test_static_mips_executable_passes(self):
        self.check_init(executable())

    def test_host_executable_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "MIPS machine"):
            self.check_init(executable(machine=62))

    def test_dynamic_interpreter_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "PT_INTERP"):
            self.check_init(executable(interpreter=True))

    def test_truncated_executable_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "program header table"):
            self.check_init(executable()[:60])

    def test_entry_outside_executable_segment_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "entry is outside"):
            self.check_init(executable(bad_entry=True))

    def test_pruned_ps2_config_rejected(self):
        path = self.root / ".config"
        values = dict(kernel.REQUIRED)
        values["CPU_R5900"] = "n"
        path.write_text("\n".join(f"CONFIG_{key}={value}" for key, value in values.items()))
        with self.assertRaisesRegex(RuntimeError, "CONFIG_CPU_R5900: required y, got n"):
            kernel.verify_config(path)

    def test_native_ethernet_and_ext4_must_be_built_in(self):
        path = self.root / ".config"
        for symbol in ("PS2_SMAP", "PS2_IOP_BRIDGE", "EXT4_FS"):
            for value in ("m", "n"):
                with self.subTest(symbol=symbol, value=value):
                    values = dict(kernel.REQUIRED)
                    values[symbol] = value
                    path.write_text("\n".join(f"CONFIG_{key}={val}" for key, val in values.items()))
                    with self.assertRaisesRegex(RuntimeError, f"CONFIG_{symbol}: required y, got {value}"):
                        kernel.verify_config(path)

    def test_other_ethernet_and_usb_network_drivers_are_rejected(self):
        path = self.root / ".config"
        required = "\n".join(f"CONFIG_{key}={value}" for key, value in kernel.REQUIRED.items()) + "\n"
        prohibited = ("USB_NET_DRIVERS", "USB_USBNET", "USB_NET_AX88179_178A",
                      "USB_NET_AX8817X", "USB_NET_CDCETHER", "USB_RTL8152", "NET_VENDOR_ASIX")
        for symbol in prohibited:
            for value in ("y", "m"):
                with self.subTest(symbol=symbol, value=value):
                    path.write_text(required + f"CONFIG_{symbol}={value}\n")
                    with self.assertRaisesRegex(RuntimeError, f"CONFIG_{symbol} must be disabled"):
                        kernel.verify_config(path)
        path.write_text(required + "".join(f"# CONFIG_{symbol} is not set\n" for symbol in prohibited))
        kernel.verify_config(path)

    def test_incomplete_drm_cannot_be_enabled(self):
        path = self.root / ".config"
        path.write_text("\n".join(f"CONFIG_{key}={value}" for key, value in kernel.REQUIRED.items())
                        + "\nCONFIG_DRM_GS=m\n")
        with self.assertRaisesRegex(RuntimeError, "must be disabled"):
            kernel.verify_config(path)

    def test_o32_glibc_futex_time32_compatibility_cannot_be_pruned(self):
        path = self.root / ".config"
        values = dict(kernel.REQUIRED)
        values["COMPAT_32BIT_TIME"] = "n"
        path.write_text("\n".join(f"CONFIG_{key}={value}" for key, value in values.items()))
        with self.assertRaisesRegex(RuntimeError, "CONFIG_COMPAT_32BIT_TIME: required y, got n"):
            kernel.verify_config(path)

    def test_initramfs_budget_counts_uncompressed_archive(self):
        (self.root / "usr").mkdir()
        with gzip.open(self.root / "usr/initramfs_data.cpio.gz", "wb") as stream:
            stream.write(bytes(4097))
        with self.assertRaisesRegex(RuntimeError, "archive exceeds"):
            kernel.initramfs_archive_bytes(self.root, limit=4096)

    def test_modern_embedded_archive_takes_precedence_over_stale_old_archive(self):
        (self.root / "usr").mkdir()
        (self.root / "usr/initramfs_data.cpio").write_bytes(b"070701" + bytes(506))
        (self.root / "usr/initramfs_inc_data").write_bytes(gzip.compress(b"070701" + bytes(4091)))
        with self.assertRaisesRegex(RuntimeError, "archive exceeds"):
            kernel.initramfs_archive_bytes(self.root, limit=4096)
        self.assertEqual(kernel.initramfs_archive_bytes(self.root), 4097)

    def test_modern_raw_archive_and_unsupported_compression(self):
        (self.root / "usr").mkdir()
        path = self.root / "usr/initramfs_inc_data"
        path.write_bytes(b"070701" + bytes(506))
        self.assertEqual(kernel.initramfs_archive_bytes(self.root), 512)
        path.write_bytes(b"\xfd7zXZ\x00" + bytes(506))
        with self.assertRaisesRegex(RuntimeError, "Expected gzip or uncompressed"):
            kernel.initramfs_archive_bytes(self.root)

    def layout(self, memory=4096, compressed_address=0x800000, compressed_memory=4096):
        normal = self.root / "vmlinux"
        compressed = self.root / "vmlinuz"
        normal.write_bytes(boot_executable(0x80010000, memory))
        compressed.write_bytes(boot_executable(compressed_address, compressed_memory))
        return kernel.boot_layout(normal, compressed)

    def test_boot_layout_normalizes_mips_aliases(self):
        self.assertEqual(self.layout()["kernel_physical_ranges"], [(0x10000, 0x11000)])

    def test_embedded_kernel_cannot_overlap_decompressor(self):
        with self.assertRaisesRegex(RuntimeError, "overlaps compressed ELF"):
            self.layout(memory=0x800000)

    def test_boot_loads_must_fit_32_mib(self):
        with self.assertRaisesRegex(RuntimeError, "32 MiB RAM"):
            self.layout(compressed_memory=32 * 1024 * 1024)

    def test_unreviewed_compressed_boot_address_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "physical 8 MiB"):
            self.layout(compressed_address=0x900000)


class ImportedSourceTests(unittest.TestCase):
    def test_ethernet_reference_files_match_immutable_hashes(self):
        folder = ROOT / "kernel/vendor/ethernet"
        manifest = json.loads((folder / "manifest.json").read_text())
        self.assertEqual(len(manifest["files"]), 9)
        for entry in manifest["files"]:
            with self.subTest(path=entry["path"]):
                self.assertEqual(kernel.sha256(folder / entry["path"]), entry["sha256"])

    def test_full_ps2_snapshot_matches_locked_hashes(self):
        self.assertEqual(len(kernel.verify_vendor()["files"]), 208)

    def test_iop_snapshot_matches_locked_hashes(self):
        folder = ROOT / "kernel/vendor/iopmod"
        manifest = json.loads((folder / "manifest.json").read_text())
        self.assertEqual(len(manifest["files"]), 104)
        for entry in manifest["files"]:
            with self.subTest(path=entry["path"]):
                self.assertEqual(kernel.sha256(folder / "snapshot" / entry["path"]), entry["sha256"])

    def test_modern_reference_matches_locked_hashes(self):
        folder = ROOT / "kernel/vendor/linux-7.2.8"
        manifest = json.loads((folder / "manifest.json").read_text())
        for entry in manifest["files"]:
            with self.subTest(path=entry["path"]):
                self.assertEqual(kernel.sha256(folder / "snapshot" / entry["path"]), entry["sha256"])


class CachePortTests(unittest.TestCase):
    def setUp(self):
        relative = "arch/mips/include/asm/r4kcache.h"
        self.pristine = (ROOT / "kernel/vendor/linux-7.2.8/snapshot" / relative).read_text()
        self.baseline = (ROOT / "kernel/vendor/frno7/snapshot" / relative).read_text()
        self.partial = self.pristine.replace(
            "#define __BUILD_BLAST_CACHE_RANGE(pfx, desc, hitop, prot, extra)",
            "#define __BUILD_BLAST_CACHE_RANGE(fn_pfx, pfx, desc, hitop, prot, extra)")
        self.partial = self.partial.replace("\t\tprot##cache_op(hitop, addr);\t\t\t\t\\",
                                            "\t\tprot##cache_op##fn_pfx(hitop, addr);\t\t\t\\")

    def test_unexpected_cache_source_edits_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "differs from the verified"):
            cache_port.modernize_r4kcache(self.partial.replace("-EFAULT", "0"), self.baseline, self.pristine)

    def test_modern_protected_eva_and_node_helpers_preserved(self):
        adapted = cache_port.modernize_r4kcache(self.partial, self.baseline, self.pristine)
        start = self.pristine.index("#ifdef CONFIG_EVA\n")
        end = self.pristine.index("\n#endif\n", start) + len("\n#endif\n")
        self.assertIn(self.pristine[start:end], adapted)
        start = self.pristine.index("/* Currently, this is very specific to Loongson-3 */")
        self.assertEqual(adapted[adapted.index("/* Currently, this is very specific to Loongson-3 */"):],
                         self.pristine[start:])

    def test_r5900_unroll_assembly_retained_exactly(self):
        adapted = cache_port.modernize_r4kcache(self.partial, self.baseline, self.pristine)
        start = self.baseline.index("#ifdef CONFIG_CPU_R5900\n#define cache64_unroll32_d")
        end = self.baseline.index("#else\n#define cache64_unroll32_i", start)
        self.assertIn(self.baseline[start:end], adapted)


CP0_SPEC = importlib.util.spec_from_file_location("eelinux_cp0_port", ROOT / "scripts/kernel_cp0_port.py")
cp0_port = importlib.util.module_from_spec(CP0_SPEC)
CP0_SPEC.loader.exec_module(cp0_port)


class SourceGateTests(unittest.TestCase):
    def report(self, missing=(), rejects=(), version="7.2.8", expected="7.2.8"):
        return {"missing": list(missing), "rejects": list(rejects), "version": version,
                "expected_version": expected,
                "source_gate_passed": not missing and not rejects and version == expected}

    def test_passing_gate_needs_no_waiver(self):
        self.assertFalse(kernel.require_source_gate(self.report()))
        self.assertFalse(kernel.require_source_gate(self.report(), True))

    def test_rejects_fail_without_waiver(self):
        with self.assertRaisesRegex(RuntimeError, "rejected port hunk"):
            kernel.require_source_gate(self.report(rejects=["mm/fault.c.rej"]))

    def test_waiver_allows_rejects_only(self):
        self.assertTrue(kernel.require_source_gate(self.report(rejects=["mm/fault.c.rej"]), True))

    def test_waiver_does_not_cover_missing_drivers(self):
        for missing in (["drivers/video/fbdev/ps2fb.c"], ["Kconfig:SONY_PS2"]):
            with self.assertRaisesRegex(RuntimeError, "not waivable.*missing PS2 integration"):
                kernel.require_source_gate(self.report(missing=missing, rejects=["a.rej"]), True)

    def test_waiver_does_not_cover_version_mismatch(self):
        with self.assertRaisesRegex(RuntimeError, "not waivable.*7.2.7 != expected 7.2.8"):
            kernel.require_source_gate(self.report(version="7.2.7"), True)


class CorrectnessMarkerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name)

    def marker(self, **fields):
        (self.source / ".eelinux-port.json").write_text(json.dumps(fields))

    def test_marker_with_current_digest_skips_recheck(self):
        self.marker(driver_correctness_patch_sha256=kernel.sha256(kernel.CORRECTNESS_PATCH))
        self.assertIsNone(kernel.apply_correctness(self.source))

    def test_marker_without_digest_fails_closed(self):
        self.marker(target_version="7.2.8")
        with self.assertRaisesRegex(RuntimeError, "driver correctness patch"):
            kernel.apply_correctness(self.source)

    def test_marker_with_other_digest_fails_closed(self):
        self.marker(driver_correctness_patch_sha256="0" * 64)
        with self.assertRaisesRegex(RuntimeError, "driver correctness patch"):
            kernel.apply_correctness(self.source)


class Cp0ProvenanceTests(unittest.TestCase):
    PROBE = ("static void probe(void)\n{\n\tif (cpu_has_64bits) {\n"
             "\t\twrite_c0_entryhi_64(0x3fffffffffffe000ULL);\n\t}\n}\n")
    TLB = ("/* Read full EntryHi even with CONFIG_32BIT.  */\n"
           "static u64 get(void)\n{\n\treturn cpu_has_64bits ? read_c0_entryhi_64() : read_c0_entryhi();\n}\n"
           "static void put(u64 v)\n{\n\tif (cpu_has_64bits)\n\t\twrite_c0_entryhi_64(v);\n}\n"
           "static void mask(void)\n{\n\tvpn_mask |= cpu_has_64bits ? 3ULL << 62 : 1 << 31;\n}\n")

    def tree(self, name, padding=0):
        source = Path(self.temp.name) / name
        for path, text in zip(cp0_port.PATHS, (self.PROBE, self.TLB)):
            (source / path).parent.mkdir(parents=True, exist_ok=True)
            # Padding stays outside the diff context (and off line 1, which
            # git anchors to the file start), so only the hunk offsets move.
            (source / path).write_text("/* pad */\n" * padding + "/* context */\n" * 4 + text)
        return source

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)

    def test_shifted_offsets_record_committed_patch_and_rerun_verifies(self):
        patch = Path(self.temp.name) / "cp0.patch"
        cp0_port.install(self.tree("first"), patch)
        committed = patch.read_bytes()
        shifted = self.tree("shifted", padding=5)
        report = cp0_port.install(shifted, patch)
        self.assertEqual(patch.read_bytes(), committed)
        self.assertEqual(report["patch_sha256"], hashlib.sha256(committed).hexdigest())
        self.assertEqual(cp0_port.install(shifted, patch), report)


if __name__ == "__main__":
    unittest.main()
