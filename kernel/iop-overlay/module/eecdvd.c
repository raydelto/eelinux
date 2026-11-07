// SPDX-License-Identifier: GPL-2.0-only
/* Exclusive, read-only DVD-video transport. Polls bounded CDVD/DMA completion;
 * does not depend on DVD-player-only CDVDMAN exports on older Fat ROMs. */
#include "iopmod/types.h"
#include "iopmod/module.h"
#include "iopmod/sifcmd.h"
#include "iopmod/thread.h"
#include "ps2-cdvd-rpc.h"

#define R8(n) (*(volatile u8 *)(0xbf402000u + (n)))
#define R32(n) (*(volatile u32 *)(n))
static struct sifcmd_rpc_server_data server;
static struct sifcmd_rpc_data_queue queue;
static struct eecdvd_packet request, result;
/* Bounded 16 KiB transfers avoid a drive command and RPC for every 2 KiB. */
#define READ_AHEAD EECDVD_SECTORS
static u8 raw[2064*READ_AHEAD] __attribute__((aligned(64)));
static unsigned int cached_first, cached_count, capacity=2295104u;

static int wait_idle(void)
{
 unsigned int i;
 for (i=0;i<10000;i++) {
  if ((R8(5)&0xc0)==0x40 && !(R32(0xbf8010b8)&0x01000000)) return 0;
  thbase_delay(1000);
 }
 return -110;
}
static int dvd_sectors(unsigned int lsn, unsigned int count)
{
 unsigned int i;
 static unsigned int next=~0u;
 if (!count || count>READ_AHEAD || lsn>=capacity || count>capacity-lsn) return -22;
 if (cached_count && lsn>=cached_first && lsn-cached_first+count<=cached_count) goto copy;
 cached_count=0;
 int err=wait_idle();
 if (err) return err;
 /* ROM CDVDMAN's handler could acknowledge completion while we poll it.
  * Mask only the drive interrupt; SIF, USB and timer interrupts stay active.
  * The Linux disc owns this drive for the remainder of this session. */
 R32(0xbf801074)&=~4u;
 R8(8)=3;
 /* Flush the controller's read-ahead state before a nonsequential read.
  * Explicit seek completes without an armed DMA buffer. */
 if (next!=lsn) {
  R8(5)=lsn; R8(5)=lsn>>8; R8(5)=lsn>>16; R8(5)=lsn>>24;
  R8(4)=5;
  err=wait_idle(); if (err) return err;
  if (R8(6)) return -5;
  R8(8)=3;
 }
 R32(0xbf8010f0)|=0x8000u;
 R32(0xbf8010b8)=0;
 R8(6)=0x8c; /* DVD-video DMA transfer shape: 12 words x 43 blocks. */
 R32(0xbf8010b0)=(u32)raw;
 R32(0xbf8010b4)=((43u*count)<<16)|12u;
 R32(0xbf8010b8)=0x41000200u;
 /* N-command 08 returns 2064-byte DVD sectors, including a 12-byte header. */
 R8(5)=lsn; R8(5)=lsn>>8; R8(5)=lsn>>16; R8(5)=lsn>>24;
 R8(5)=count; R8(5)=0; R8(5)=0; R8(5)=0;
 R8(5)=16; R8(5)=3; R8(5)=0;
 R8(4)=8;
 for (i=0;i<10000;i++) {
  if (!(R32(0xbf8010b8)&0x01000000) && (R8(5)&0xc0)==0x40) break;
  thbase_delay(1000);
 }
 if (i==10000) return -110; /* Never reuse the DMA buffer after timeout. */
 if (R8(6)) return -5;
 for (i=0;i<count;i++) {
  u8 *sector=raw+2064*i;
  if ((((u32)sector[1]<<16)|((u32)sector[2]<<8)|sector[3]) != lsn+i+0x30000u) return -5;
 }
 R8(8)=3;
 next=lsn+count; cached_first=lsn; cached_count=count;
 if (lsn==16 && raw[13]=='C' && raw[14]=='D' && raw[15]=='0' && raw[16]=='0' && raw[17]=='1') {
  unsigned int end=raw[92]|((u32)raw[93]<<8)|((u32)raw[94]<<16)|((u32)raw[95]<<24);
  if (end>=32 && end<=2295104u) capacity=end;
 }
copy:
 for (unsigned int sector=0;sector<count;sector++) {
  u32 *from=(u32 *)(raw+2064*(lsn-cached_first+sector)+12);
  u32 *to=(u32 *)(result.data+2048*sector);
  for (i=0;i<512;i+=4) {
   to[i]=from[i]; to[i+1]=from[i+1]; to[i+2]=from[i+2]; to[i+3]=from[i+3];
  }
 }
 return 0;
}
static void *service(int op, void *buffer, size_t size)
{
 struct eecdvd_packet *p=buffer;
 unsigned int i;
 static int poisoned;
 for (i=0;i<64/4;i++) ((u32 *)&result)[i]=0;
 result.abi=EECDVD_ABI;
 if (size<64 || op!=EECDVD_READ || p->abi!=EECDVD_ABI || !p->sectors || p->sectors>EECDVD_SECTORS || p->lsn>=2295104u) {
  result.status=-22;
  for (i=0;i<512*EECDVD_SECTORS;i++) ((u32 *)result.data)[i]=0;
  return &result;
 }
 result.lsn=p->lsn; result.sectors=p->sectors;
 result.status=poisoned ? -5 : dvd_sectors(p->lsn,p->sectors);
 result.reserved[0]=R8(5); result.reserved[1]=R8(6); result.reserved[2]=R8(8);
 result.reserved[3]=R32(0xbf8010b8);
 result.reserved[4]=((u32)raw[1]<<16)|((u32)raw[2]<<8)|raw[3];
 if (result.status==-110) poisoned=1;
 if (result.status) for (i=0;i<512*EECDVD_SECTORS;i++) ((u32 *)result.data)[i]=0;
 return &result;
}
static void worker(void *arg)
{
 (void)arg; sifcmd_rpc_loop(&queue);
}
static enum module_init_status eecdvd_init(int argc, char *argv[])
{
 const struct iop_thread th={.attr=THREAD_ATTR_C,.thread=worker,.stacksize=2048,.priority=0x2a};
 int id=thbase_create(&th);
 (void)argc; (void)argv;
 if (id<0) return MODULE_EXIT;
 sifcmd_set_rpc_queue(&queue,id);
 sifcmd_register_rpc(&server,EECDVD_SERVER,service,&request,NULL,NULL,&queue);
 if (thbase_start(id,NULL)<0) {
  sifcmd_remove_rpc(&server,&queue); sifcmd_remove_rpc_queue(&queue); thbase_delete(id);
  return MODULE_EXIT;
 }
 return MODULE_RESIDENT;
}
module_init(eecdvd_init);
