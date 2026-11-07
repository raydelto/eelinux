# Experimental PCSX2 interpreter changes

`pcsx2-2.6.3-mmu.patch` applies to PCSX2 commit
`bc8151d2a46d4aba039ea5580afbfc7bfcf6d730` (v2.6.3). It retains the upstream
GPL-3.0-or-later license; the added MMU header uses the same license.

The patch distinguishes TLB refill from matching-invalid exceptions, fixes
masked VPN2/ASID/global probing, rotates Random within Wired bounds, preserves
the saved branch-delay flag during nested exceptions, and raises architectural
user/supervisor address errors in the interpreter. Ordinary SQ decoding and its
16-byte address alignment remain intact. Kernel EXL/ERL overrides are preserved.
Undefined instruction dispatch now raises a real reserved-instruction exception
and cancels the instruction. This lets Linux emulate LL/SC on R5900 instead of
silently skipping the atomics used by glibc. The exception preserves EPC and
branch-delay semantics without changing guest registers.
Random advances once per interpreter instruction; its timing is an approximation,
while its read-only, reset and index-bound behavior follows the hardware contract.

Guest instruction fetches and data references now resolve the live 48-entry TLB
using the current ASID, masked VPN2 and both global bits. A matching invalid page
raises the general TLB exception; a write to a valid clean page raises TLB Modified
before any RAM, cache or device access. KSEG0/KSEG1 and ERL direct addressing retain
their architectural mappings. Scratchpad entries select the full 16 KiB region,
check V/D/ASID and remain uncached.

The selected physical mapping and cache mode are carried into cache accesses.
CACHE hit operations translate as reads; index operations keep upstream behavior.
Invalid physical cache tags use the physical handler key and retain upstream
zero-fill/discard behavior. An explicit thread-local fetch/data context excludes
loader, event, debugger and PINE accesses from guest exceptions. Every interpreter
fastjmp boundary resets that context because fastjmp does not unwind C++ scopes.

`[EmuCore/CPU] EnableBIOSHLE` defaults to `true` and retains the upstream BIOS
syscall dispatcher. eeLinux sessions set it to `false`: every SYSCALL then raises
the guest exception without interpreting service registers, changing GS settings
or reading/writing BIOS service buffers. The setting uses no guest address or
Linux syscall-number detection and does not disable ordinary BIOS execution.

The Toshiba TX79 architecture manual describes exception vectors in sections
5.5.7â€“5.5.9 and segment privileges in sections 6.2.6â€“6.2.8.
The [R5900 Linux TLS discussion](https://www.spinics.net/lists/mips/msg70031.html)
explains why GNU RDHWR accesses need the kernel's R5900 address-error emulation.
The adapter is in `scripts/kernel_tls_port.py`.
The [Emotion Engine manual](https://docs.alexrp.com/mips/ee.pdf), section 4.2.13,
defines the reserved-instruction exception used by this dispatch.

Use `scripts/emulator_pcsx2.py` with the pinned source and dependencies recorded
in `emulator/pcsx2-linux-build.json`. The builder reconstructs the patched files
from pristine Git objects and checks them byte for byte before compiling.
Native C++ regression checks compile the actual added helper and fault wrappers.
The translation checks exercise ASID-separated mappings, both global bits,
different page sizes, invalid and clean-page faults, a synthetic COW remapping,
direct segments, scratchpad halves, host access isolation and cancellation before
physical access. These checks do not execute Linux or establish hardware accuracy.
The actual syscall wrapper is also checked with interception enabled and disabled,
including register preservation and normal/branch-delay exception arguments.

EE interpreter mode is required. Full userspace isolation, cache accuracy,
copy-on-write under real workloads and compatibility with BIOS interception
disabled still need qualification. Boot stages must be observed separately from
compiler and host-test results; see `docs/emulator.md`.

The Windows build uses the same MMU patch and the official dependency bundle
locked in `emulator/pcsx2-windows-build.json`. Its separate
`pcsx2-2.6.3-windows-compat.patch` adds the missing standard `<bit>` include
required by WIL with the selected MSVC headers and guards a Vulkan include when
Vulkan is disabled. It also preserves physical UK punctuation scan codes,
fixes SMAP TX completion/RX FIFO pointer handling, handles DHCP options
119/145, and puts routine DEV9 register traces behind DEBUG. Malformed packets
and unsupported registers retain their diagnostics.
`scripts/emulator_pcsx2_windows.py` independently verifies the dependency archive
and reconstructs all 24 modified source files before and after compilation.

Use native Windows Python, Visual Studio clang-cl 20.1.8, CMake, Ninja and 7-Zip.
After checking out the pinned source and applying both locked patches, run:

```powershell
python scripts/emulator_pcsx2_windows.py dependencies --source build/emulator/pcsx2-windows-source --archive build/emulator/pcsx2-windows-dependencies-18842cc1.7z
python scripts/emulator_pcsx2_windows.py build --source build/emulator/pcsx2-windows-source --output build/emulator/pcsx2-windows-build --runtime build/emulator/pcsx2-patched-windows/runtime --jobs 2
```

The output directory must be new. The builder packages the DLLs and Qt plugins
locally, checks CLI help with an isolated DLL search path, and writes
`eelinux-runtime.json` beside `pcsx2-qt.exe`. It leaves installed applications
unchanged and does not execute a BIOS or VM. The actual successful build and CLI
check are recorded in `docs/pcsx2-windows-validation.json`. Subsequent Windows
guest USB-root, UK-key, DHCP, DNS and external ICMP checks are recorded in
`docs/network-runtime-validation.json`. They do not establish physical Ethernet
acceptance.
