"""Compile the shipped guest translation and cancellation code on the host."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

from test_pcsx2_mmu_patch import PATCH, added_function, patched_header


@unittest.skipUnless(os.name != "nt" and shutil.which("c++"), "requires native C++ compiler")
class InterpreterTranslation(unittest.TestCase):
    def compile_run(self, code):
        with tempfile.TemporaryDirectory(prefix="eelinux-pcsx2-translation-") as temporary:
            directory = Path(temporary)
            (directory / "R5900MMU.h").write_text(patched_header())
            (directory / "check.cpp").write_text(code)
            result = subprocess.run(
                ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 str(directory / "check.cpp"), "-o", str(directory / "check")],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(directory / "check")], check=True, timeout=5)

    def test_live_asid_permissions_page_sizes_and_cow(self):
        self.compile_run(r'''
#include "R5900MMU.h"
#include <array>
#include <cassert>
#include <cstdint>
using namespace R5900::MMU;
using u32 = std::uint32_t;
constexpr u32 lo(u32 physical, u32 flags = 0x1e) {
    return (physical >> 6) | flags;
}
int main() {
    std::array<Entry, 48> tlb{};
    auto read = [&](unsigned index) { return tlb[index]; };
    auto resolve = [&](u32 address, unsigned asid, bool store=false) {
        return Resolve(address, 0x10, 0x10003, asid, store, read);
    };
    // The same VA can coexist in two address spaces. The active ASID selects it.
    tlb[1] = {0, 0x400002, lo(0x10000), lo(0x11000)};
    tlb[2] = {0, 0x400003, lo(0x20000), lo(0x21000)};
    assert(resolve(0x400038, 2).physical == 0x10038);
    assert(resolve(0x400038, 3).physical == 0x20038);
    assert(resolve(0x401abc, 2).physical == 0x11abc);
    assert(resolve(0x401abc, 3).physical == 0x21abc);
    assert(resolve(0x400000, 4).kind == Translation::Refill);
    // Global applies only when both halves set G, even for an even-page access.
    tlb[1].lo0 |= 1;
    assert(resolve(0x400000, 4).kind == Translation::Refill);
    tlb[1].lo1 |= 1;
    assert(resolve(0x400000, 4).physical == 0x10000);
    tlb[1].lo0 &= ~1u; tlb[1].lo1 &= ~1u;
    tlb[1].lo0 &= ~2u;
    assert(resolve(0x400000, 2).kind == Translation::Invalid);
    tlb[1].lo0 |= 2;
    tlb[1].lo0 &= ~4u;
    assert(resolve(0x400000, 2).kind == Translation::Physical);
    assert(resolve(0x400000, 2, true).kind == Translation::Modified);
    assert(resolve(0x400000, 3, true).kind == Translation::Physical);

    // COW: neither address space can write a shared clean page. After the guest
    // handles Mod and installs a private dirty PFN, the parent stays unchanged.
    tlb[1].lo0 = lo(0x10000, 0x1a);
    tlb[2].lo0 = lo(0x10000, 0x1a);
    std::array<unsigned, 2> physical_words{0x12345678, 0x12345678};
    auto store = [&](unsigned asid, unsigned value) {
        const auto result = resolve(0x400000, asid, true);
        if (result.kind != Translation::Physical) return false;
        assert(result.physical == 0x10000 || result.physical == 0x20000);
        physical_words[(result.physical - 0x10000) / 0x10000] = value;
        return true;
    };
    assert(!store(2, 0) && !store(3, 0));
    assert(physical_words[0] == 0x12345678 && physical_words[1] == 0x12345678);
    tlb[2].lo0 = lo(0x20000);
    assert(store(3, 0));
    assert(physical_words[0] == 0x12345678 && physical_words[1] == 0);
    assert(resolve(0x400000, 2).physical == 0x10000);

    // 16 KiB halves ignore lower PFN/VPN2 bits and select the odd half at bit14.
    tlb[3] = {0x6000, 0x800007, lo(0x315000), lo(0x426000)};
    assert(resolve(0x800fff, 7).physical == 0x314fff);
    assert(resolve(0x807fff, 7).physical == 0x427fff);
    assert(resolve(0x808000, 7).kind == Translation::Refill);
    // Cacheability comes from the selected ASID, never another flat mapping.
    tlb[1].lo0 = lo(0x10000, 0x16); // C=2.
    tlb[2].lo0 = lo(0x20000, 0x1e); // C=3.
    assert(resolve(0x400000, 2).cache_mode == 2);
    assert(resolve(0x400000, 3).cache_mode == 3);
    // A high PFN remains a translated physical address, for existing bus/cache policy.
    tlb[4] = {0, 0x600001, lo(0x70004000), lo(0x70005000)};
    assert(resolve(0x600044, 1).physical == 0x70004044);
}
''')

    def test_direct_segments_privilege_erl_and_scratchpad(self):
        self.compile_run(r'''
#include "R5900MMU.h"
#include <array>
#include <cassert>
#include <cstdint>
using namespace R5900::MMU;
int main() {
    std::array<Entry, 48> tlb{};
    auto read = [&](unsigned index) { return tlb[index]; };
    assert(Resolve(0x12345678, 4, 3, 0, true, read).physical == 0x12345678);
    assert(Resolve(0x12345678, 4, 3, 0, true, read).cache_mode == 2);
    assert(Resolve(0x80001234, 0, 3, 0, true, read).physical == 0x1234);
    assert(Resolve(0x80001234, 0, 3, 0, true, read).cache_mode == 3);
    assert(Resolve(0xa0001234, 0, 3, 0, true, read).cache_mode == 2);
    assert(Resolve(0x80001234, 0x10, 3, 0, false, read).kind == Translation::AddressError);
    assert(Resolve(0x80001234, 0x12, 3, 0, false, read).kind == Translation::Physical);
    // EXL forces kernel privilege, but mapped segments still require a TLB entry.
    assert(Resolve(0x400000, 0x12, 3, 4, false, read).kind == Translation::Refill);
    assert(Resolve(0xc0000000, 0x08, 3, 0, false, read).kind == Translation::Refill);
    assert(Resolve(0xe0000000, 0x08, 3, 0, false, read).kind == Translation::AddressError);
    tlb[3] = {0, 0x70000007, 0x8000001e, 0x1e};
    for (unsigned address : {0x70000000u, 0x70001fffu, 0x70002000u, 0x70003fffu}) {
        const auto result = Resolve(address, 0x10, 3, 7, true, read);
        assert(result.kind == Translation::Scratchpad && result.physical == (address & 0x3fff));
        assert(result.cache_mode == 2 && result.index == 3);
    }
    assert(Resolve(0x70004000, 0x10, 3, 7, true, read).kind == Translation::Refill);
    assert(Resolve(0x70002000, 0x10, 3, 8, true, read).kind == Translation::Refill);
    tlb[3].lo0 &= ~4u; tlb[3].lo1 &= ~4u;
    assert(Resolve(0x70002000, 0x10, 3, 7, true, read).kind == Translation::Modified);
    assert(Resolve(0x70002000, 0x10, 3, 7, false, read).kind == Translation::Scratchpad);
    tlb[3].lo0 &= ~2u; tlb[3].lo1 &= ~2u;
    assert(Resolve(0x70002000, 0x10, 3, 7, false, read).kind == Translation::Invalid);
}
''')

    def test_actual_instruction_cancel_resets_thread_local_guest_context(self):
        cancel = added_function("pcsx2/Interpreter.cpp", "static void intCancelInstruction()")
        self.compile_run(r'''
#include "R5900MMU.h"
#include <cassert>
#include <initializer_list>
thread_local auto cpuMemoryContext = R5900::MMU::AccessContext::Data;
int intJmpBuf;
struct Cancelled {};
void fastjmp_jmp(int* buffer, int result) {
    assert(buffer == &intJmpBuf && result == 0);
    assert(cpuMemoryContext == R5900::MMU::AccessContext::Host);
    throw Cancelled{};
}
''' + cancel + r'''
int main() {
    for (auto context : {R5900::MMU::AccessContext::Data, R5900::MMU::AccessContext::Fetch}) {
        cpuMemoryContext = context;
        bool cancelled = false;
        try { intCancelInstruction(); } catch (const Cancelled&) { cancelled = true; }
        assert(cancelled && cpuMemoryContext == R5900::MMU::AccessContext::Host);
    }
}
''')

    def test_actual_resolver_cancels_before_physical_or_cache_access(self):
        guard = added_function("pcsx2/vtlb.cpp", "static __fi void CheckInterpreterAddress(")
        resolver = added_function("pcsx2/vtlb.cpp", "VTLBVirtual vtlb_ResolveInterpreterMapping(")
        self.compile_run(r'''
#include "R5900MMU.h"
#include <array>
#include <cassert>
#include <cstdint>
using u32 = std::uint32_t;
using u8 = std::uint8_t;
using uptr = std::uintptr_t;
#define __fi inline
constexpr u32 VTLB_PAGE_BITS = 12, VTLB_PAGE_MASK = 4095, VTLB_PMAP_SZ = 0x20000000;
static unsigned fault, cancels, host_cache_reads, constructed;
struct Cancelled {};
struct R5900cpu { void (*CancelInstruction)(); };
void cancel() { ++cancels; throw Cancelled{}; }
R5900cpu intCpu{cancel};
auto* Cpu = &intCpu;
thread_local auto cpuMemoryContext = R5900::MMU::AccessContext::Data;
struct { struct { struct { struct { u32 val; } Status; u32 Config, EntryHi; } n; } CP0;
    u32 branch; } cpuRegs;
struct Raw { u32 UL = 0; };
struct { Raw PageMask, EntryHi, EntryLo0, EntryLo1; } tlb[48];
struct VTLBPhysical {
    uptr pointer = 0;
    bool handler = false;
    static VTLBPhysical fromHandler(unsigned) { return {0, true}; }
};
struct VTLBVirtual {
    uptr pointer = 0;
    u32 physical = 0;
    bool handler = false;
    VTLBVirtual() = default;
    VTLBVirtual(VTLBPhysical p, u32 address, u32) : pointer(p.pointer), physical(address), handler(p.handler) {
        ++constructed;
    }
    static VTLBVirtual fromPointer(uptr pointer, u32) {
        VTLBVirtual v; v.pointer = pointer; ++constructed; return v;
    }
};
// Maps expose reads only after a translation succeeds, with no actual guest execution.
struct PhysicalMap {
    VTLBPhysical operator[](unsigned index) {
        assert(index == 0x10 || index == 0x20);
        return {0x100000 + index * 4096, false};
    }
};
struct VirtualMap {
    VTLBVirtual operator[](unsigned) {
        VTLBVirtual v; v.pointer = 0xdeadbeef; return v;
    }
};
struct { PhysicalMap pmap; VirtualMap vmap; } vtlbdata;
struct { unsigned char Scratch[16384]; } memory;
auto* eeMem = &memory;
unsigned UnmappedPhyHandler;
thread_local bool guestPhysicalCacheable;
bool CheckCache(u32) { ++host_cache_reads; return true; }
void cpuAddressError(u32, bool store, u32) { fault = store ? 0x14 : 0x10; }
void cpuTlbMissW(u32, u32) { fault = 0x0c; }
void cpuTlbMissR(u32, u32) { fault = 0x08; }
void cpuTlbModify(u32, u32) { fault = 0x04; }
''' + guard + resolver + r'''
int main() {
    cpuRegs.CP0.n.Status.val = 0x10; cpuRegs.CP0.n.Config = 0x10003;
    tlb[2] = {{0}, {0x400002}, {(0x10000 >> 6) | 0x1a}, {0}};
    tlb[3] = {{0}, {0x400003}, {(0x20000 >> 6) | 0x1e}, {0}};
    bool cached = false;
    cpuRegs.CP0.n.EntryHi = 2;
    auto mapping = vtlb_ResolveInterpreterMapping(0x400000, false, &cached);
    assert(mapping.physical == 0x10000 && cached && constructed == 1 && host_cache_reads == 0);
    constructed = 0;
    for (auto context : {R5900::MMU::AccessContext::Data, R5900::MMU::AccessContext::Fetch}) {
        cpuMemoryContext = context;
        for (bool store : {false, true}) {
            fault = cancels = constructed = 0;
            const u32 address = store ? 0x400000 : 0x401000; // D=0 vs V=0.
            bool cancelled = false;
            try { (void)vtlb_ResolveInterpreterMapping(address, store, &cached); }
            catch (const Cancelled&) { cancelled = true; }
            assert(cancelled && cancels == 1 && fault == (store ? 4u : 8u));
            assert(constructed == 0 && host_cache_reads == 0);
        }
    }
    cpuRegs.CP0.n.EntryHi = 3; cpuMemoryContext = R5900::MMU::AccessContext::Data;
    mapping = vtlb_ResolveInterpreterMapping(0x400000, true, &cached);
    assert(mapping.physical == 0x20000 && cached && host_cache_reads == 0);
    constructed = fault = cancels = 0;
    bool cancelled = false;
    try { (void)vtlb_ResolveInterpreterMapping(0xffffe830, true, &cached); }
    catch (const Cancelled&) { cancelled = true; }
    assert(cancelled && fault == 0x14 && cancels == 1 && constructed == 0);
    // A GUI/debugger reference retains a manual mapping and never alters CP0.
    cpuMemoryContext = R5900::MMU::AccessContext::Host; fault = cancels = constructed = 0;
    mapping = vtlb_ResolveInterpreterMapping(0xffffe830, true, &cached);
    assert(mapping.pointer == 0xdeadbeef && cached && host_cache_reads == 1);
    assert(fault == 0 && cancels == 0 && constructed == 0);
}
''')


class TranslationIntegration(unittest.TestCase):
    def test_cache_and_non_cpu_paths_preserve_reference_context(self):
        patch = PATCH.read_text()
        cache_section = patch.split("+++ b/pcsx2/Cache.cpp\n", 1)[1].split("\n--- ", 1)[0]
        self.assertIn("vtlb_ResolveInterpreterMapping(addr, false, &cached)", cache_section)
        resolver = added_function("pcsx2/vtlb.cpp", "VTLBVirtual vtlb_ResolveInterpreterMapping(")
        self.assertLess(resolver.index("Translation::Modified"), resolver.index("vtlbdata.pmap["))
        self.assertIn("return vtlbdata.vmap[address >> VTLB_PAGE_BITS]", resolver)
        self.assertIn("thread_local R5900::MMU::AccessContext cpuMemoryContext", patch)
        self.assertIn("translatedPointer ? translatedPointer : vmv.assumePtr(mem)", patch)
        self.assertIn("guestPhysicalCacheable : CheckCache(address)", patch)
        interpreter = patch.split("+++ b/pcsx2/Interpreter.cpp\n", 1)[1].split("\n--- ", 1)[0]
        # Every nonlocal interpreter exit explicitly resets this non-RAII state.
        lines = [line[1:] for line in interpreter.splitlines() if line.startswith(("+", " "))]
        for index, line in enumerate(lines):
            if "fastjmp_jmp(&intJmpBuf," in line:
                self.assertIn("AccessContext::Host", lines[index - 1])


if __name__ == "__main__":
    unittest.main()
