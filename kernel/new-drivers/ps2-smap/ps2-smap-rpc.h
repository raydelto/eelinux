/* SPDX-License-Identifier: GPL-2.0-only */
/* Fixed o32/R3000 wire ABI. No native pointers or arbitrary MMIO requests. */
#ifndef PS2_SMAP_RPC_H
#define PS2_SMAP_RPC_H
#define SMAP_RPC_SERVER 0x800001d0u
#define SMAP_RPC_ABI 0x534d0100u
#define SMAP_RPC_INIT 1
#define SMAP_RPC_RESET 2
#define SMAP_RPC_START 3
#define SMAP_RPC_STOP 4
#define SMAP_RPC_MDIO_READ 5
#define SMAP_RPC_MDIO_WRITE 6
#define SMAP_RPC_FILTER 7
#define SMAP_RPC_POLL 8
#define SMAP_RPC_RELEASE 9
#define SMAP_RPC_DATA 1536u
/* Linux/MIPS o32 values; the minimal IOP errno header only defines 1..34. */
#define SMAP_RPC_TIMEOUT 145
#define SMAP_RPC_NETDOWN 127
struct smap_rpc_packet {
	u32 abi, cookie;
	s32 status;
	u32 length, phy, reg, value, mode1, rx_mode, hash[4];
	u8 mac[6];
	u16 checksum;
	u32 hardware, tx_packets, tx_bytes, tx_errors, rx_errors;
	u8 data[SMAP_RPC_DATA];
};
#endif
