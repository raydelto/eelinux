# QEMU Linux-user protocol and statx compatibility

`qemu-linux-user-protocol-abi.patch` follows the CLI and network ABI patches
on pinned frno7 QEMU commit `5f48e76da75409ac7aa23716c8a626d8f40539ef`.
The canonical builder records its checksum and resulting source file hashes.

The pinned emulator rejected `NETLINK_NETFILTER` before opening a socket,
preventing even `nft --version`. Admission and protocol dispatch now permit
this protocol only when host and target byte order match. Netfilter's
fixed-width wire headers and attributes then remain opaque bytes; the patch
does not claim cross-endian attribute translation. Other unsupported
protocols retain rejection, using the target `EPFNOSUPPORT` value instead of
an untranslated host errno.

The native `statx` branch also now uses the existing `path()` mapping, as its
`fstatat` fallback already did. That mapper changes existing absolute paths
under the configured loader prefix and leaves relative and empty paths alone.
This fixes the false missing `/var/empty` result during prefixed `sshd -t`.
Directory descriptors, `AT_EMPTY_PATH`, flags and error translation retain
their existing handling.

Frozen V3 and V4 sources, binaries and failure evidence remain unchanged.
The fresh V5 build and focused runtime checks are recorded in
`build/network-work/qemu-v5-source-patch.json` and
`qemu-v5-netfilter-readonly-runtime.json`. Checks cover protocol admission,
target errno, statx path/descriptor/error behavior and the earlier socket and
time64 cases. `nft --version` is traced to verify that it sends no firewall
messages. Transfer/retry and iperf JSON regressions have separate V5 reports.
These tests neither modify firewall state nor qualify physical PS2 networking.
