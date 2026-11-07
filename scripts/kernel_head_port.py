"""Guarded, optional R5900 main-entry CP0 synchronization candidate.

This adapter is deliberately separate from the canonical API port. Hardware
qualification is required before integrating it into the production source.
SPDX-License-Identifier: GPL-2.0-only
"""
import hashlib


_REFERENCE_SHA256 = "b50d8d0c1f52f2ea3664b5e448021cd73c0a4a898894f337edf538924e6449bd"
_REFERENCE_FRAGMENTS = (
    "#ifdef CONFIG_CPU_R5900\n\tsync.p\n#endif\n\tmfc0\tt0, CP0_STATUS\n",
    "\tmtc0\tt0, CP0_STATUS\n#ifdef CONFIG_CPU_R5900\n\tsync.p\n#endif\n",
    "\tMTC0\t\tzero, CP0_CONTEXT\t# clear context register\n#ifdef CONFIG_CPU_R5900\n\tsync.p\n#endif\n",
)
_ANCHORS = (
    "\t.set\tpush\n\tmfc0\tt0, CP0_STATUS\n",
    "\tmtc0\tt0, CP0_STATUS\n\t.set\tnoreorder\n",
    "\tMTC0\t\tzero, CP0_XCONTEXT\n#endif\n\tPTR_LA\t\t$28, init_thread_union\n",
)
_REPLACEMENTS = (
    "\t.set\tpush\n#ifdef CONFIG_CPU_R5900\n\tsync.p\n#endif\n\tmfc0\tt0, CP0_STATUS\n",
    "\tmtc0\tt0, CP0_STATUS\n#ifdef CONFIG_CPU_R5900\n\tsync.p\n#endif\n\t.set\tnoreorder\n",
    "\tMTC0\t\tzero, CP0_XCONTEXT\n#endif\n#ifdef CONFIG_CPU_R5900\n\tsync.p\n#endif\n\tPTR_LA\t\t$28, init_thread_union\n",
)


def synchronize_kernel_head(text: str, vendor_reference: str) -> str:
    """Restore three pinned guards while preserving modern CP0 masks/paths."""
    if (text.count("\t.macro\tsetup_c0_status set clr\n") != 1 or
            text.count("ST0_KERNEL_CUMASK") != 1 or "sync.p" in text or
            any(text.count(anchor) != 1 for anchor in _ANCHORS) or
            hashlib.sha256(vendor_reference.encode()).hexdigest() != _REFERENCE_SHA256 or
            any(vendor_reference.count(fragment) != 1
                for fragment in _REFERENCE_FRAGMENTS)):
        raise RuntimeError("Unexpected MIPS head source; review the R5900 CP0 entry synchronization candidate")
    # CONFIG_64BIT's context write remains in its original position. R5900
    # uses o32; this shared head still retains every other CPU's original path.
    for anchor, replacement in zip(_ANCHORS, _REPLACEMENTS):
        text = text.replace(anchor, replacement, 1)
    return text
