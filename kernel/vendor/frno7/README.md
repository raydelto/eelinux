# Complete upstream PS2 driver import

`snapshot/` preserves all 208 added or modified files in the exact difference
between upstream Linux v5.4.221 (`b70bfeb98635040588883503d2760e0f46231491`) and
frno7's PS2 commit `77174dd4c8958a91aa5b76fb064a60eb68119e70`. This includes
the platform, R5900 core changes, headers, all IOP/GS drivers, experimental GS
DRM, USB OHCI, ATA, UART, RTC, controller, checker, and upstream integration.
The COPYING and SPDX license texts are retained. `manifest.json` records every
original file's SHA-256. These files are immutable provenance, not patched code.

`ps2-full.patch` is the entire original delta. `ps2-compat.patch` contains the
modified-file hunks. The Linux 7.2.8 importer copies complete added source files
and tries the modified-file hunks against the modern tree, preserving rejects.
It never overwrites a modern core file with a complete 5.4 core file.

`features/ps2-gamepad/` and `features/ps2-v5.4-ata/` preserve the focused PS2
driver code from the separately pinned experimental feature branches. Their
manifests explain scope. They are deliberately not merged blindly: those
branches also include unrelated changes against older Linux versions.

Project fixes are separate reviewable patches in `kernel/patches/`. Imported
source, code that compiles, code that links, and code verified on actual PS2
hardware are tracked separately. No import is a claim of complete hardware
support.
