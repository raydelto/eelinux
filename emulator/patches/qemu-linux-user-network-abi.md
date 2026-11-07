# QEMU Linux-user network ABI correction

`qemu-linux-user-network-abi.patch` follows `qemu-linux-user-cli-abi.patch`
on the pinned frno7 QEMU commit
`5f48e76da75409ac7aa23716c8a626d8f40539ef`.

The old `SOL_TCP` getsockopt implementation treated every option as one integer.
For `TCP_CONGESTION`, this copied only four bytes of `cubic\0`; iperf3 then read
past the missing terminator and emitted invalid JSON. The correction copies
the opaque bytes within the guest buffer length and returns the host kernel's
actual option length. Other supported TCP integer options keep their existing
path.

The correction also dispatches `ppoll_time64` through the existing poll and
signal-mask implementation. It uses the existing time64 converters for both
timeout input and remaining-time output, including their o32 nanosecond-padding
handling. The missing dispatch previously prevented TFTP retransmission under
this emulator.

The canonical `scripts/emulator_userland.py` builder applies both patches with
zero fuzz and locks source files, patch hashes, build inputs and binary output.
The separate V4 work directory preserves frozen V3 sources and failure evidence.
No guest application, library or image was changed for these emulator fixes.

Evidence is recorded in `build/network-work/qemu-v4-source-patch.json`,
`qemu-v4-socket-timeout-runtime.json`, and
`iperf3-v4-final-raw-loopback.json`. The runtime probe covers complete and short
string buffers, an ordinary integer option, timeout/readiness/interruption,
nonzero o32 padding, and invalid signal-mask size. Actual TFTP normal and
initial-response-loss transfers are recorded separately. These Linux-user
checks do not execute the PS2 kernel or qualify physical networking hardware.
