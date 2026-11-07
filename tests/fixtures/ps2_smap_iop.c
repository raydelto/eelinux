/* SPDX-License-Identifier: GPL-2.0-only */
/* Independent IOP bus/descriptor model, including absent EE visibility. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32;
#define BIT(n) (1u<<(n))
#define SPD_REGBASE 0xb0000000u
#define THREAD_ATTR_C 0x02000000
#define mb() do {} while (0)
#define pr_err(...) do { errors++; } while (0)
struct sifcmd_rpc_data_queue { int unused; };
struct sifcmd_rpc_server_data { int unused; };
struct iop_thread { u32 attr; void (*thread)(void *); u32 stacksize,priority; };
static int thbase_create(const struct iop_thread *t) { assert(t->stacksize>=4096); return 1; }
static int thbase_start(int i,void *p) { (void)p; assert(i==1); return 0; }
static void thbase_delete(int i) { (void)i; }
static void sifcmd_set_rpc_queue(struct sifcmd_rpc_data_queue *q,int i) { (void)q; (void)i; }
static void sifcmd_register_rpc(struct sifcmd_rpc_server_data *s,u32 id,void *(*f)(int,void *,size_t),
    void *b,void *c,void *d,struct sifcmd_rpc_data_queue *q) {
    (void)s;(void)f;(void)b;(void)c;(void)d;(void)q;assert(id==0x800001d0);
}
static void sifcmd_remove_rpc(struct sifcmd_rpc_server_data *s,struct sifcmd_rpc_data_queue *q) { (void)s;(void)q; }
static void sifcmd_remove_rpc_queue(struct sifcmd_rpc_data_queue *q) { (void)q; }
static void sifcmd_rpc_loop(struct sifcmd_rpc_data_queue *q) { (void)q; }
struct dev9_request_packet { u32 operation,cookie; };
static u32 lease_cookie,hardware=0x30;
static u8 mac_address[6]={0,4,0x1f,0x82,0xb6,0x9c};
static u16 mac_checksum;
static int process_request(const struct dev9_request_packet *r) {
    if (r->operation==1) {
        if (lease_cookie && lease_cookie!=r->cookie) return -EBUSY;
        lease_cookie=r->cookie;
    } else if (r->cookie==lease_cookie) lease_cookie=0;
    return 0;
}
static u16 registers[0x4000/2];
static u8 tx_fifo[4096],rx_fifo[16384];
static u16 tx_ptr,rx_ptr;
static unsigned int fifo_done,fifo_started,delay_calls,errors,tx_words,rx_words,mdio_ops;
static int fault;
static unsigned int offset(u32 addr) {
    /* All real SMAP access must use the IOP window, never EE 0x14000000. */
    assert(addr>=0xb0000000u && addr<0xb0004000u);return addr-0xb0000000u;
}
static void wait_us(unsigned int us) { assert(us==100);delay_calls++; }
static u8 iord8(u32 a) {
    unsigned int r=offset(a);
    if (r==0x1000 || r==0x1030) {
        unsigned int bit=r==0x1000?1:2;
        if ((fault==1 && bit==1) || (fault==2 && bit==2)) return 1;
        fifo_done|=bit;return 0;
    }
    return registers[r/2]>>(8*(r&1));
}
static u16 iord16(u32 a) {
    unsigned int r=offset(a);
    if (r==0x2000 && fault!=3 && fifo_done==3) registers[r/2]&=~0x2000;
    return registers[r/2];
}
static u32 iord32(u32 a) {
    unsigned int r=offset(a);u32 v=0;
    assert(r==0x1200);for (unsigned int i=0;i<4;i++) v|=(u32)rx_fifo[(rx_ptr+i)&16383]<<(8*i);
    rx_ptr=(rx_ptr+4)&16383;rx_words++;return v;
}
static void iowr8(u8 v,u32 a) {
    unsigned int r=offset(a);
    if (r==0x1000 || r==0x1030) {
        assert(v==1);fifo_started|=r==0x1000?1:2;
    }
    registers[r/2]=(registers[r/2]&~(0xffu<<(8*(r&1))))|((u16)v<<(8*(r&1)));
}
static void iowr16(u16 v,u32 a) {
    unsigned int r=offset(a);registers[r/2]=v;
    if (r==0x1004) tx_ptr=v;
    if (r==0x1034) rx_ptr=v;
    if (r==0x205e) {
        unsigned int phy=(v>>5)&31,reg=v&31;
        mdio_ops++;
        if (fault==4) return;
        registers[r/2]|=0x8000;
        if (v&0x1000) registers[(r-2)/2]=phy==1 ? (reg==2?0x2000:0x1234) : 0xffff;
    }
    if (r==0x200a) {
        /* Hardware completes all queued descriptors; next poll reclaims. */
        for (unsigned int i=0;i<64;i++) registers[(0x3000+i*8)/2]=0;
    }
}
static void iowr32(u32 v,u32 a) {
    unsigned int r=offset(a);assert(r==0x1100);
    for(unsigned int i=0;i<4;i++) tx_fifo[(tx_ptr+i)&4095]=v>>(i*8);
    tx_ptr=(tx_ptr+4)&4095;tx_words++;
}
/* IOP_SERVICE */
static struct smap_rpc_packet request;
static struct smap_rpc_packet *call(int op) {
    return smap_service(op,&request,sizeof(request));
}
static void initialize(void) {
    memset(registers,0,sizeof(registers));memset(&request,0,sizeof(request));
    request.abi=SMAP_RPC_ABI;request.cookie=7;request.mode1=EMAC_MODE1_DEFAULT;
    request.rx_mode=EMAC_RX_DEFAULT;memcpy(request.mac,mac_address,6);
    fifo_done=fifo_started=0;delay_calls=0;fault=0;
    mac_checksum=(mac_address[0]|mac_address[1]<<8)+(mac_address[2]|mac_address[3]<<8)+(mac_address[4]|mac_address[5]<<8);
}
int main(void) {
    assert(sizeof(request)==1616);
    initialize();assert(smap_server_start()==0);
    assert(call(SMAP_RPC_INIT)->status==0 && fifo_done==3 && fifo_started==3);
    assert(!memcmp(smap_rpc_result.mac,mac_address,6));
    request.phy=1;request.reg=2;
    assert(call(SMAP_RPC_MDIO_READ)->status==0 && smap_rpc_result.value==0x2000 && mdio_ops==1);
    request.phy=31;assert(call(SMAP_RPC_MDIO_READ)->value==0xffff);
    request.phy=32;assert(call(SMAP_RPC_MDIO_READ)->status==-EINVAL);
    request.phy=1;assert(call(SMAP_RPC_START)->status==0);
    request.length=61;for(unsigned int i=0;i<61;i++)request.data[i]=i;
    assert(call(SMAP_RPC_POLL)->status==61);
    assert(!memcmp(tx_fifo,request.data,61) && tx_fifo[61]==0 && tx_words==16);
    request.length=0;assert(call(SMAP_RPC_POLL)->status==0 && smap_rpc_result.tx_packets==1 && smap_rpc_result.tx_bytes==61);
    /* Real RX wrapping at the 16 KiB FIFO edge, malformed packet then valid. */
    registers[0x3200/2]=0x8;registers[0x3204/2]=60;registers[0x3206/2]=0x4000;
    registers[0x3208/2]=0;registers[0x320c/2]=61;registers[0x320e/2]=0x7ffc;
    for(unsigned int i=0;i<61;i++)rx_fifo[(16380+i)&16383]=i+1;
    assert(call(SMAP_RPC_POLL)->length==61 && smap_rpc_result.rx_errors==1 && rx_words==16);
    for(unsigned int i=0;i<61;i++)assert(smap_rpc_result.data[i]==i+1);
    assert(registers[0x3200/2]==0x8000 && registers[0x3208/2]==0x8000);
    request.length=1519;assert(call(SMAP_RPC_POLL)->status==-EINVAL);
    request.cookie=8;assert(call(SMAP_RPC_POLL)->status==-ENODEV);request.cookie=7;
    assert(((struct smap_rpc_packet *)smap_service(SMAP_RPC_POLL,NULL,0))->status==-EINVAL);
    request.length=0;assert(call(SMAP_RPC_STOP)->status==0);
    assert(call(SMAP_RPC_POLL)->status==-SMAP_RPC_NETDOWN);
    for(int f=1;f<=3;f++) {
        fault=f;fifo_done=0;delay_calls=0;
        assert(call(SMAP_RPC_RESET)->status==-SMAP_RPC_TIMEOUT && delay_calls==100);
    }
    fault=0;assert(call(SMAP_RPC_RESET)->status==0);
    fault=4;request.phy=1;delay_calls=0;unsigned int commands=mdio_ops;
    assert(call(SMAP_RPC_MDIO_READ)->status==-SMAP_RPC_TIMEOUT && delay_calls==100);
    assert(call(SMAP_RPC_MDIO_READ)->status==-SMAP_RPC_TIMEOUT && mdio_ops==commands+1);
    fault=0;assert(call(SMAP_RPC_RELEASE)->status==0 && !lease_cookie);
    puts("IOP SMAP: cold reset, PHY, TX/RX wrap, invalid DMA sizes, lease, bounded timeout PASS");
    return 0;
}
