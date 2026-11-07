/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EELINUX_CDVD_RPC_H
#define EELINUX_CDVD_RPC_H
#define EECDVD_SERVER 0x8000cd01u
#define EECDVD_ABI 0x00010002u
#define EECDVD_READ 1
#define EECDVD_SECTORS 8u
/* Up to eight optical sectors plus one isolated metadata cache line. */
struct eecdvd_packet {
 unsigned int abi, lsn, sectors;
 int status;
 unsigned int reserved[12];
 unsigned char data[2048*EECDVD_SECTORS];
} __attribute__((aligned(64)));
#endif
