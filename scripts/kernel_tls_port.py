"""Restore the reviewed R5900 RDHWR address-error path in modern MIPS code.

On the EE, the MIPS32 RDHWR encoding is an SQ to a kernel segment. User
execution raises AdES; the existing PS2 handler then reads Linux's saved TLS
pointer. This adapter preserves the modern instruction-fetch and user-access
checks and does not reinterpret ordinary quadword stores.
SPDX-License-Identifier: GPL-2.0-only
"""
import hashlib


_MODERN_ANCHOR = "\tcase spec3_op:\n\t\tif (insn.dsp_format.func == lx_op) {"
_VENDOR_ANCHOR = "\tcase spec3_op:\n\t\tif (IS_ENABLED(CONFIG_CPU_R5900)) {"
_VENDOR_END = "\t\t} else if (insn.dsp_format.func == lx_op) {"
_VENDOR_BRANCH_SHA256 = "0722621fa30049110ad74510b487c90f22549d05ac9f8788e5079a95c8d50ab6"
_PROTOTYPE = "int simulate_rdhwr(struct pt_regs *regs, int rd, int rt);\n"


def modernize_unaligned(text: str, vendor_reference: str) -> str:
    """Insert the exact pinned branch, rejecting changed or repeated anchors."""
    function_anchor = "static void emulate_load_store_insn("
    if (text.count(_MODERN_ANCHOR) != 1 or "simulate_rdhwr(" in text or
            text.count(function_anchor) != 1 or
            vendor_reference.count(_VENDOR_ANCHOR) != 1 or
            vendor_reference.count(_VENDOR_END) != 1):
        raise RuntimeError("Unexpected MIPS unaligned source; review the R5900 TLS address-error port")
    prefix = text[text.index(function_anchor):text.index(_MODERN_ANCHOR)]
    if (prefix.count("\tbool user = user_mode(regs);\n") != 1 or
            prefix.count("\t__get_inst32(&insn.word, pc, user);\n") != 1):
        raise RuntimeError("Unexpected MIPS instruction access checks; review the R5900 TLS address-error port")
    start = vendor_reference.index(_VENDOR_ANCHOR)
    end = vendor_reference.index(_VENDOR_END, start) + len("\t\t} else ")
    branch = vendor_reference[start:end]
    if hashlib.sha256(branch.encode()).hexdigest() != _VENDOR_BRANCH_SHA256:
        raise RuntimeError("R5900 TLS reference branch changed; verify the pinned PS2 source")
    # Only the spec3 dispatch prefix changes. Modern DSP/EVA access checks,
    # instruction fetching, signal paths and exception context stay intact.
    return text.replace(_MODERN_ANCHOR,
                        branch + "if (insn.dsp_format.func == lx_op) {", 1)


def modernize_traps_header(text: str, vendor_reference: str) -> str:
    """Restore the shared declaration used by traps.c and unaligned.c."""
    anchor = "asmlinkage void do_ri(struct pt_regs *regs);\n"
    if (text.count(anchor) != 1 or "simulate_rdhwr" in text or
            vendor_reference.count(_PROTOTYPE) != 1):
        raise RuntimeError("Unexpected MIPS traps header; review the R5900 TLS declaration")
    return text.replace(anchor, anchor + _PROTOTYPE, 1)
