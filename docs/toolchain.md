# GNU/Linux R5900 toolchain

The current source lock selects **GCC 16.2.0, binutils 2.47 and glibc 2.44**,
verified on 2 October 2026 against the GNU projects' release pages:
[GCC](https://gcc.gnu.org/gcc-16/),
[binutils](https://sourceware.org/pipermail/binutils/2026-July/150520.html) and
[glibc](https://sourceware.org/pipermail/libc-announce/2026/000058.html).
The lock includes official archive URLs and checksums. GCC and binutils
downloads matched published checksums. The glibc archive's detached signature
verified with the official GNU keyring before its SHA256 was recorded.

Their paths, sizes and SHA256 hashes are recorded in `toolchain/latest-validation.json`;
they are not extracted on the Windows drive. Build and installation use the native WSL
filesystem:

The complete toolchain has now built and installed there. All four dynamic and
static C/C++ runtime probes passed under the pinned R5900 Linux-user emulator,
including multiplication, soft float, TLS, pthreads, futex results, C++ exception
unwinding and atomic flags. `toolchain/latest-validation.json` records executable
and library hashes, installed versions, header provenance and test results.

```sh
python3 toolchain/bootstrap.py build \
  --work "$HOME/.cache/eelinux-linux-latest/toolchain" \
  --prefix "$HOME/.local/eelinux-linux-latest" \
  --kernel-source "$HOME/.cache/eelinux/sources/linux-ps2" \
  --jobs 2
```

The build order is Linux headers, binutils, a C/libgcc bootstrap, glibc, then
complete GCC C/C++ and target runtime libraries. The compiler targets
`mipsr5900el-unknown-linux-gnu`: little-endian o32, R5900, soft float. Target
runtime flags explicitly select the CPU, short-loop workaround and avoidance
of generated LL/SC instructions. Host compiler flags are kept separate.
glibc 2.44 uses GCC lock-free atomic builtins, so libc compilation explicitly
enables `-mllsc`. The R5900 has no hardware LL/SC: Linux must emulate these
reserved instructions. Earlier glibc 2.41 emitted explicit LL/SC assembly for
the same kernel path. Disabling LL/SC while building 2.44 fails its lock-free
atomic assertions; the recipe keeps those assertions intact. Compiler target
runtimes and application builds continue to use `-mno-llsc`.

GCC's no-instruction atomic-flag fallback exposes upstream
[PR122078](https://gcc.gnu.org/pipermail/gcc-patches/2025-November/699826.html):
the compiler emits `__atomic_test_and_set`, while the release's libatomic only
defines suffixed implementations. A tracked adaptation adds the proposed alias
to its existing byte implementation and exports it for shared-library consumers.
The patch and exact before/after source hashes are pinned in the source lock.
It preserves GNU libatomic's existing locking and memory-order behavior; compiler
frontends and glibc source are unchanged. Programs using these fallback atomic
operations link `-latomic`.

The headers use the established PS2 Linux 5.4.221 port, with glibc's minimum kernel
set to 5.4.0. Export forces `archheaders` before `headers_install` and checks all
six generated MIPS syscall headers. This also handles a source tree previously
built in place: Make's VPATH can otherwise omit generated headers from a separate
output directory despite a successful install command. This header choice does not open the incomplete Linux 7.2.8 release
gate. The ps2dev bare-metal SDK remains the separate compiler for IOP firmware.

The previous GCC 14.4.0/binutils 2.44/glibc 2.41 installation and native build
cache are preserved. Its source lock is `toolchain/source-lock-14.4.0.json`:

```sh
python3 toolchain/bootstrap.py plan \
  --source-lock toolchain/source-lock-14.4.0.json
```

Every stage checks its full source lock, installation prefix, exported headers and
recipe identity before reusing a completed build. Changing the recipe or source lock
normally requires a new work directory. During the latest build, failed libc atomic
configuration and incomplete MIPS header export were repaired with the same verified
sources and object directories. The previous markers and failure logs were preserved;
affected stages were configured, built and installed again before receiving new markers.
Do not reuse the previous release's stage markers for the latest release.

After the complete compiler installs, run the bounded runtime check with the
already built checksum-pinned R5900 Linux-user emulator:

```sh
python3 toolchain/smoke.py \
  --emulator-work "$HOME/.cache/eelinux-linux/emulator-userland" \
  --report /path/to/output/verification/toolchain-latest-smoke.json
```

This compiles both dynamic and static target executables and tests ordinary
R5900 multiplication, soft-float arithmetic, initial TLS, separate pthread TLS
and futex wait/wake results. Dynamic and static C++ programs test GNU standard-library
containers, exception unwinding, atomic-flag state and output. The report hashes the test sources, binaries,
emulator and dynamic loader/library inventory. The emulator delegates system
calls and atomics to the host kernel; these results do not prove the PS2 kernel's
exception, TLS, futex, MMU or device implementation.
