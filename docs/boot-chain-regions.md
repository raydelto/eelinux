# Boot chain and media: region audit (NTSC-J / NTSC-U)

This audit lists every region-dependent step between power-on and the Linux
root filesystem. For each step it says what was checked, the result, and what
only real hardware can settle. Video modes are covered separately in
`video-modes.md` and `ntsc-media.md`.

## Summary

| Step | Region-specific? | Status |
|---|---|---|
| FreeDVDBoot DVD region code (`VIDEO_TS.IFO`) | Possible in principle | **Region-free.** All three pinned payloads have VMG region mask `0x00`, so DVD regions 1 (US) and 2 (Japan) are both allowed. The build now enforces this. |
| FreeDVDBoot exploit vs DVD Player version | **Yes** | Covered by the per-region `dvd-compatibility.json` text (PS2L-3). The DVD Player version must be checked on the console. |
| `SYSTEM.CNF` on FreeDVDBoot discs | Must be absent | Absent. The build now refuses a FreeDVDBoot tree that contains one. |
| uLaunchELF `cdfs:/EELINUX.ELF`, `mass:/EELINUX.ELF` | No | Device paths are the same in every region. |
| Kernel load (ELF at physical 8 MiB, decompresses to 64 KiB) | No | Same RAM map on every retail and DTL-H EE. |
| IOP state at kernel entry | No | Linux resets the IOP to the ROM defaults (`sif.c`, empty reset argument), so the launcher's IOP modules don't matter. |
| `rom0:` modules (`ADDDRV` etc.) | No (by name) | Module names are identical across regions. Very old ROMs (1.00/1.01) are a separate, model-based risk, not a region one. |
| ROMVER parsing (`rom.c`) | Handled | Accepts `J`, `A`, `E`, `C`, `H`, `T`, `X` regions and `C`/`D` types, so DTL-H units (`…JD…`) parse. |
| ps2fb default mode from ROMVER | Bypassed | Only used without `video=`; every eeLinux kernel sets `video=ps2fb:…` explicitly. A `J` ROM would default to NTSC anyway (`gs_region_pal()` is true only for `E`). |
| Machine name (`identify.c`) | No | SCMD on ROM ≥ 1.10; if it fails on a DEX unit, only a log line is lost. |
| USB image layout (MBR, FAT32 `EELINUX.ELF`, ext4 root) | No | No region data. |
| Standalone DVD root (`eecdvd.irx`, raw DVD reads) | No region check | Reads raw 2064-byte DVD sectors; no region or disc-type gate in the driver. |

**Conclusion:** the boot chain has no region lock. The only region-dependent
factor is the FreeDVDBoot/DVD Player-version pairing, which is a firmware-version
problem that PS2L-3 already documented. USB via uLaunchELF is region-independent.

## Changes made in PS2L-4

1. **Build-time region check.** `ps2_media.verify_freedvdboot_tree()` reads the
   VMG category in `VIDEO_TS/VIDEO_TS.IFO` (byte `0x23`, a set bit forbids that
   region). It fails the build if any region is blocked, or if a `SYSTEM.CNF`
   is present. Each FreeDVDBoot image in `dvd-compatibility.json` now records
   `boot.DVD_region_mask`.
2. **Experimental DTL-H TEST-unit discs** (opt-in, never part of `--variant all`):
   `make dvd-iso-test-unit VIDEO_MODE=480p` builds
   - `eelinux-ntsc-test-unit.iso`, with `SYSTEM.CNF`
     `BOOT2 = cdrom0:\EELINUX.ELF;1`, `VMODE = NTSC`;
   - `eelinux-ntsc-test-unit-interlaced.iso`, with `BOOT2 = cdrom0:\EENTSC.ELF;1`.

   Both carry the same standalone Rock Ridge root as the FreeDVDBoot discs, but
   no `VIDEO_TS`. With the default `VIDEO_MODE=576p` the target builds
   `eelinux-pal-test-unit.iso` with `VMODE = PAL`. No Sony logo or licence data
   is written: the system area stays zeroed.

## Why a TEST-unit disc

On a DTL-H TEST unit with no launcher installed, FreeDVDBoot is the only other
disc path, and it depends on a DVD Player version that is still unknown.
TEST units are documented to boot unlicensed burned discs. A `SYSTEM.CNF` disc
can therefore start the kernel directly, without FreeDVDBoot, uLaunchELF or a
specific DVD Player version. The kernel ELF already loads like a normal PS2
program (user RAM, `0x00800000`). Linux resets the IOP on entry, so starting
it from the OSD should match starting it from uLaunchELF.

## Hardware-only questions (checklist items for PS2L-5/PS2L-6)

1. Record `ROMVER`: after boot, `cat /sys/rom/rom0/version/*`, or the
   `rom0: Version …` kernel line. Expect `Japan` and `DEX` (ROMVER `…JD…`) on a Japanese DTL-H.
2. FreeDVDBoot on the DTL-H: does the TEST unit's DVD Player match one of the
   pinned exploit versions (3.10/3.11 Slim, 2.10/2.12 Fat, 3.04 Fat but not J)?
   Unknown until the Version screen is read.
3. TEST-unit disc, **unverified**:
   - does the DTL-H accept a burned DVD-R with `SYSTEM.CNF` and a zeroed logo
     area?
   - does `EELINUX.ELF` start from `BOOT2`?
   - does `eecdvd.irx` read the disc once the drive has identified it as a
     PS2 DVD rather than DVD-Video? If the kernel starts but cannot mount
     `/dev/ps2dvd`, the cause is the last of these.
4. USB via uLaunchELF is expected to behave as on retail consoles; confirm
   `mass:/EELINUX.ELF`.

Until these are confirmed, every TEST-unit result is a hypothesis. A failure
there does not affect the USB or FreeDVDBoot paths.
