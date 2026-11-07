"""Check the actual generic syscall wrapper shipped in the emulator patch."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

from test_pcsx2_mmu_patch import PATCH, added_function


class SyscallInterception(unittest.TestCase):
    def test_setting_preserves_upstream_default_and_participates_in_reload(self):
        patch = PATCH.read_text()
        self.assertIn("EnableBIOSHLE : 1;", patch)
        self.assertIn("EnableBIOSHLE = true;", patch)
        self.assertIn("SettingsWrapBitBool(EnableBIOSHLE);", patch)
        self.assertIn("OpEqu(EnableBIOSHLE)", patch)
        self.assertIn("static void HandleBIOSSyscall()", patch)

    @unittest.skipUnless(os.name != "nt" and shutil.which("c++"), "requires native C++ compiler")
    def test_actual_syscall_disabled_never_intercepts_or_changes_registers(self):
        syscall = added_function("pcsx2/R5900OpcodeImpl.cpp", "void SYSCALL()")
        harness = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <initializer_list>
using u32 = std::uint32_t;
struct { struct { bool EnableBIOSHLE = true; } Cpu; } EmuConfig;
struct { u32 pc, branch; std::array<u32, 32> GPR; } cpuRegs;
static unsigned intercepts, exceptions, exception_code, exception_bd, exception_pc;
void HandleBIOSSyscall() { ++intercepts; cpuRegs.GPR[2] = 0xf00d; }
void cpuException(u32 code, u32 bd) {
    ++exceptions; exception_code = code; exception_bd = bd; exception_pc = cpuRegs.pc;
}
''' + syscall + r'''
int main() {
    EmuConfig.Cpu.EnableBIOSHLE = false;
    for (unsigned bd : {0u, 1u}) {
        // Linux syscall numbers, BIOS numbers and arbitrary values all receive
        // the same instruction exception. No guest PC/ABI heuristic is allowed.
        for (unsigned v0 : {2u, 4002u, 4011u, 4238u, 0xffffffffu}) {
            for (unsigned v1 : {2u, 0x4bu, 0x6fu, 0xfffffffeu, 0xffffffffu}) {
                intercepts = exceptions = 0;
                cpuRegs.pc = 0x77c6fdf8; cpuRegs.branch = bd;
                for (unsigned i = 0; i < 32; ++i) cpuRegs.GPR[i] = i ? i * 123 : 0;
                cpuRegs.GPR[2] = v0; cpuRegs.GPR[3] = v1;
                const auto original = cpuRegs.GPR;
                SYSCALL();
                assert(intercepts == 0 && exceptions == 1);
                assert(exception_code == 0x20 && exception_bd == bd);
                assert(exception_pc == 0x77c6fdf4 && cpuRegs.GPR == original);
            }
        }
    }
    // The true/default branch delegates to the unchanged upstream dispatcher.
    intercepts = exceptions = 0;
    EmuConfig.Cpu.EnableBIOSHLE = true; cpuRegs.pc = 0x12345678;
    SYSCALL();
    assert(intercepts == 1 && exceptions == 0);
    assert(cpuRegs.pc == 0x12345678 && cpuRegs.GPR[2] == 0xf00d);
}
'''
        with tempfile.TemporaryDirectory(prefix="eelinux-pcsx2-syscall-") as temporary:
            directory = Path(temporary)
            (directory / "check.cpp").write_text(harness)
            result = subprocess.run(
                ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 str(directory / "check.cpp"), "-o", str(directory / "check")],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(directory / "check")], check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
