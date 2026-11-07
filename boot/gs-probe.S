/* SPDX-License-Identifier: GPL-2.0-only */
/* Stopping diagnostic: inherit LaunchELF timing and show a red GS background.
 * No stack, BIOS, IOP, framebuffer or libc is required. Power-cycle to leave.
 * GS privileged registers require 64-bit writes (PS2SDK gs_privileged.h).
 */

    .set noreorder
    .set noat
    .set gp=64 /* Emit native SD, rather than the o32 two-SW pseudo-op. */
    .text
    .globl _start
    .ent _start
_start:
    lui     $t0, 0x3001
    mtc0    $t0, $12
    sync.p
    nop
    lui     $t0, 0xb200
    ori     $t1, $zero, 0x00ff
    sd      $t1, 0x00e0($t0) /* BGCOLOR: red, 0x00BBGGRR */
    ori     $t1, $zero, 0x00a5
    sd      $t1, 0x0000($t0) /* PMODE: display background, retain GS timing */
    sync.p
1:
    b       1b
    nop
    .end _start
