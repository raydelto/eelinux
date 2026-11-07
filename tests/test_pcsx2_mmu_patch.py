"""Native semantic checks of the MMU helpers shipped in the PCSX2 patch."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / "emulator/patches/pcsx2-2.6.3-mmu.patch"


def patched_header():
    patch = PATCH.read_text()
    before, header = patch.split("--- /dev/null\n+++ b/pcsx2/R5900MMU.h\n", 1)
    if "--- " in header:
        raise ValueError("unexpected trailing patch file")
    return "\n".join(line[1:] for line in header.splitlines()
                     if line.startswith("+") and not line.startswith("+++")) + "\n"


def added_function(path, declaration):
    section = PATCH.read_text().split("+++ b/" + path + "\n", 1)[1].split("\n--- ", 1)[0]
    additions = "\n".join(line[1:] for line in section.splitlines()
                          if line.startswith(("+", " ")) and not line.startswith("+++"))
    start = additions.index(declaration)
    opening = additions.index("{", start)
    depth = 1
    position = opening + 1
    while depth:
        depth += (additions[position] == "{") - (additions[position] == "}")
        position += 1
    return additions[start:position] + "\n"


class Pcsx2MMU(unittest.TestCase):
    def test_patch_has_only_reviewed_source_files(self):
        patch = PATCH.read_bytes()
        self.assertNotIn(b"\r", patch)
        paths = [line[6:] for line in patch.decode().splitlines() if line.startswith("+++ b/")]
        self.assertEqual(paths, ["pcsx2/R5900.h", "pcsx2/R5900.cpp", "pcsx2/COP0.cpp",
                                 "pcsx2/Interpreter.cpp", "pcsx2/vtlb.cpp",
                                 "pcsx2/R5900OpcodeImpl.cpp", "pcsx2/IopIrq.cpp",
                                 "pcsx2/USB/USB.h", "pcsx2/USB/USB.cpp",
                                 "pcsx2/Cache.h", "pcsx2/Cache.cpp", "pcsx2/vtlb.h",
                                 "pcsx2/Config.h", "pcsx2/Pcsx2Config.cpp",
                                 "pcsx2/R5900MMU.h"])
        self.assertIn("SPDX-License-Identifier: GPL-3.0+", patched_header())

    @unittest.skipUnless(os.name != "nt" and shutil.which("c++"), "requires native C++ compiler")
    def test_matching_vectors_and_random_bounds(self):
        # Compile the actual added header, rather than an independent reimplementation.
        harness = r'''
#include "R5900MMU.h"
#include <cassert>
#include <set>
using namespace R5900::MMU;
int main() {
    // Privilege applies to pointer mappings as well as TLB miss handlers.
    for (unsigned address : {0u, 0x7fffffffu}) {
        assert(AddressAllowed(address, 0x10)); // User.
        assert(AddressAllowed(address, 0x08)); // Supervisor.
    }
    for (unsigned address : {0x80000000u, 0x9fffffffu, 0xa0000000u,
                             0xbfffffffu, 0xc0000000u, 0xdfffffffu,
                             0xe0000000u, 0xffffe830u, 0xffffffffu}) {
        assert(!AddressAllowed(address, 0x10));
        assert(AddressAllowed(address, 0)); // Kernel.
        assert(AddressAllowed(address, 0x12)); // EXL overrides User.
        assert(AddressAllowed(address, 0x14)); // ERL overrides User.
        assert(AddressAllowed(address, 0x0a)); // EXL overrides Supervisor.
        assert(AddressAllowed(address, 0x0c)); // ERL overrides Supervisor.
    }
    assert(AddressAllowed(0xc0000000, 0x08));
    assert(AddressAllowed(0xdfffffff, 0x08));
    for (unsigned address : {0x80000000u, 0xbfffffffu, 0xe0000000u, 0xffffffffu})
        assert(!AddressAllowed(address, 0x08));
    // V is intentionally absent from matching: an invalid entry is still a hit.
    assert(Matches(0x004a6001, 0, false, 0x004a6000, 1));
    assert(Matches(0x004a6001, 0, false, 0x004a7fff, 1));
    assert(!Matches(0x004a6001, 0, false, 0x004a8000, 1));
    assert(!Matches(0x004a6001, 0, false, 0x004a6000, 2));
    assert(Matches(0x004a6001, 0, true, 0x004a6000, 2));
    // 16 KiB pages use a 32 KiB pair and ignore the corresponding VPN2 bits.
    assert(Matches(0x004a6007, 0x6000, false, 0x004a0000, 7));
    assert(Matches(0x004a6007, 0x6000, false, 0x004a7fff, 7));
    assert(!Matches(0x004a6007, 0x6000, false, 0x004a8000, 7));
    for (unsigned code : {0x08u, 0x0cu}) {
        assert(ExceptionOffset(code, true, false) == 0);
        assert(ExceptionOffset(code, false, false) == 0x180);
        assert(ExceptionOffset(code, true, true) == 0x180);
        assert(ExceptionOffset(code, false, true) == 0x180);
    }
    assert(ExceptionOffset(0x04, false, false) == 0x180); // TLB modified.
    assert(ExceptionOffset(0x20, false, false) == 0x180); // SYSCALL.
    assert(ExceptionOffset(0, false, false) == 0x200); // Interrupt.
    assert(ExceptionOffset(0, false, true) == 0x180); // Nested exception.
    for (unsigned wired : {0u, 5u, 47u}) {
        unsigned random = 47;
        std::set<unsigned> seen;
        for (unsigned i = 0; i < 48 - wired; ++i) {
            assert(RandomIndexWritable(random, wired));
            assert(random >= wired && random <= 47);
            seen.insert(random);
            random = NextRandom(random, wired);
        }
        assert(random == 47);
        assert(seen.size() == 48 - wired);
        assert(NextRandom(wired, wired) == 47);
    }
    assert(NextRandom(63, 5) == 47);
    assert(!RandomIndexWritable(4, 5));
    assert(!RandomIndexWritable(48, 0));
    // Reserved Wired values cannot cause a random write to a wired entry.
    for (unsigned i = 0; i < 48; ++i)
        assert(!RandomIndexWritable(i, 48));
}
'''
        with tempfile.TemporaryDirectory(prefix="eelinux-pcsx2-mmu-") as temporary:
            directory = Path(temporary)
            (directory / "R5900MMU.h").write_text(patched_header())
            (directory / "check.cpp").write_text(harness)
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(directory / "check.cpp"), "-o", str(directory / "check")],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(directory / "check")], check=True, timeout=5)

    @unittest.skipUnless(os.name != "nt" and shutil.which("c++"), "requires native C++ compiler")
    def test_actual_reserved_handler_raises_and_cancels_without_register_changes(self):
        handler = added_function("pcsx2/R5900OpcodeImpl.cpp", "void Unknown()")
        harness = r'''
#include "R5900MMU.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <initializer_list>
using u32 = std::uint32_t;
#define CPU_LOG(...) ((void)0)
struct { u32 pc, branch, code; std::array<u32, 32> GPR; } cpuRegs;
struct Cancelled {};
static unsigned calls, cancellations, exception_code, exception_bd, exception_pc;
static void cancel() { cancellations++; throw Cancelled{}; }
struct { void (*CancelInstruction)(); } interpreter{cancel};
auto* Cpu = &interpreter;
void cpuException(u32 code, u32 bd) {
    calls++; exception_code = code; exception_bd = bd; exception_pc = cpuRegs.pc;
}
'''
        harness += handler + r'''
int main() {
    // LL and SC are unsupported on R5900. A different undefined opcode must
    // follow the same architected path rather than a Linux-only special case.
    for (unsigned opcode : {0xc0820000u, 0xe0810000u, 0x74000000u}) {
        for (unsigned bd : {0u, 1u}) {
            calls = cancellations = 0;
            cpuRegs.pc = 0x00410ae0; cpuRegs.branch = bd; cpuRegs.code = opcode;
            for (unsigned i = 0; i < cpuRegs.GPR.size(); i++)
                cpuRegs.GPR[i] = i ? 0x12345600u + i : 0u;
            const auto original_regs = cpuRegs.GPR;
            bool cancelled = false;
            try { Unknown(); } catch (const Cancelled&) { cancelled = true; }
            assert(cancelled && cancellations == 1 && calls == 1);
            assert(exception_code == 0x28 && exception_bd == bd);
            assert(exception_pc == 0x00410adc && cpuRegs.code == opcode);
            assert(cpuRegs.GPR == original_regs && cpuRegs.GPR[0] == 0);
            assert(R5900::MMU::ExceptionOffset(exception_code, true, false) == 0x180);
            assert(R5900::MMU::ExceptionOffset(exception_code, true, true) == 0x180);
        }
    }
}
'''
        with tempfile.TemporaryDirectory(prefix="eelinux-pcsx2-reserved-") as temporary:
            directory = Path(temporary)
            (directory / "R5900MMU.h").write_text(patched_header())
            (directory / "check.cpp").write_text(harness)
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(directory / "check.cpp"), "-o", str(directory / "check")],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(directory / "check")], check=True, timeout=5)

    @unittest.skipUnless(os.name != "nt" and shutil.which("c++"), "requires native C++ compiler")
    def test_actual_privilege_guard_reports_and_cancels_fault(self):
        address_error = added_function("pcsx2/R5900.cpp", "void cpuAddressError(")
        guard = added_function("pcsx2/vtlb.cpp", "static __fi void CheckInterpreterAddress(")
        harness = r'''
#include "R5900MMU.h"
#include <cassert>
#include <cstdint>
#include <initializer_list>
using u32 = std::uint32_t;
#define __fi inline
struct { struct { struct { u32 BadVAddr; struct { u32 val; } Status; } n; } CP0;
         u32 pc, branch; } cpuRegs;
struct R5900cpu { void (*CancelInstruction)(); };
struct Cancelled {};
static void cancel() { throw Cancelled{}; }
R5900cpu intCpu{cancel}, recCpu{cancel};
R5900cpu* Cpu = &intCpu;
thread_local auto cpuMemoryContext = R5900::MMU::AccessContext::Data;
static unsigned calls, fault_code, fault_bd, fault_pc;
void cpuException(u32 code, u32 bd) {
    calls++; fault_code = code; fault_bd = bd; fault_pc = cpuRegs.pc;
}
'''
        harness += address_error + guard + r'''
int main() {
    for (unsigned bd : {0u, 1u}) {
        for (bool store : {false, true}) {
            calls = 0; cpuRegs.pc = 0x0042136c; cpuRegs.branch = bd;
            cpuRegs.CP0.n.Status.val = 0x10; cpuRegs.CP0.n.BadVAddr = 0;
            bool cancelled = false;
            try { CheckInterpreterAddress(0xffffe830, store); }
            catch (const Cancelled&) { cancelled = true; }
            assert(cancelled && calls == 1);
            assert(fault_code == (store ? 0x14 : 0x10));
            assert(fault_bd == bd && fault_pc == 0x00421368);
            assert(cpuRegs.CP0.n.BadVAddr == 0xffffe830);
        }
    }
    for (unsigned status : {0u, 0x12u, 0x14u}) {
        calls = 0; cpuRegs.pc = 0x0042136c; cpuRegs.CP0.n.Status.val = status;
        CheckInterpreterAddress(0xffffe830, true);
        assert(calls == 0 && cpuRegs.pc == 0x0042136c);
    }
    // The supported patch does not claim an EE recompiler privilege implementation.
    Cpu = &recCpu; calls = 0; cpuRegs.CP0.n.Status.val = 0x10;
    CheckInterpreterAddress(0xffffe830, true);
    assert(calls == 0);
    // Debugger/PINE accesses do not inject exceptions into the guest interpreter.
    Cpu = &intCpu; cpuMemoryContext = R5900::MMU::AccessContext::Host;
    CheckInterpreterAddress(0xffffe830, true);
    assert(calls == 0);
}
'''
        with tempfile.TemporaryDirectory(prefix="eelinux-pcsx2-privilege-") as temporary:
            directory = Path(temporary)
            (directory / "R5900MMU.h").write_text(patched_header())
            (directory / "check.cpp").write_text(harness)
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(directory / "check.cpp"), "-o", str(directory / "check")],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(directory / "check")], check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
