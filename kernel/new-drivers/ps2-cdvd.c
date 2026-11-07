// SPDX-License-Identifier: GPL-2.0-only
/* Native read-only optical block device. All RPC runs on one workqueue,
 * outside submit_bio and with private contiguous DMA buffers. */
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/unaligned.h>
#include <asm/mach-ps2/iop-module.h>
#include <asm/mach-ps2/sif.h>
#include "ps2-cdvd-rpc.h"

struct eecdvd {
 struct sif_rpc_client rpc;
 struct eecdvd_packet *request, *reply;
 void *rpc_default_buffer;
 struct gendisk *disk;
 struct workqueue_struct *queue;
 spinlock_t lock;
 struct bio_list bios;
 struct work_struct work;
 bool poisoned;
 unsigned int capacity, cached_first, cached_count;
 int major;
};
static struct eecdvd *cd;
static int read_sectors(struct eecdvd *s, unsigned int lsn, unsigned int count)
{
 int err;
 if (s->poisoned) return -EIO;
 memset(s->request,0,64); s->request->abi=EECDVD_ABI;
 s->request->lsn=lsn; s->request->sectors=count;
 s->cached_count=0;
 err=sif_rpc_timeout(&s->rpc,EECDVD_READ,s->request,64,s->reply,64+2048*count,15*HZ);
 if (err==-ETIMEDOUT) s->poisoned=true;
 if (err) { pr_err_ratelimited("ps2-cdvd: sector %u RPC failed: %d\n",lsn,err); return err; }
 if (s->reply->abi!=EECDVD_ABI || s->reply->lsn!=lsn || s->reply->sectors!=count) {
  pr_err_ratelimited("ps2-cdvd: invalid sector reply abi=%x lsn=%u sectors=%u (expected %u)\n",
    s->reply->abi,s->reply->lsn,s->reply->sectors,lsn);
  return -EPROTO;
 }
 if (s->reply->status==-ETIMEDOUT) s->poisoned=true;
 if (s->reply->status) pr_err_ratelimited("ps2-cdvd: sector %u drive failed: %d ready=%x error=%x irq=%x dma=%x header=%x\n",
     lsn,s->reply->status,s->reply->reserved[0],s->reply->reserved[1],s->reply->reserved[2],s->reply->reserved[3],s->reply->reserved[4]);
 if (!s->reply->status) { s->cached_first=lsn; s->cached_count=count; }
 return s->reply->status;
}
static void cd_work(struct work_struct *work)
{
 struct eecdvd *s=container_of(work,struct eecdvd,work);
 struct bio *bio;
 unsigned long flags;
 /* RPC allocation must not recurse into filesystem/optical reclaim while
  * this sole block worker is responsible for completing those reads. */
 unsigned int noio=memalloc_noio_save();
 for (;;) {
  int err=0;
  spin_lock_irqsave(&s->lock,flags); bio=bio_list_pop(&s->bios); spin_unlock_irqrestore(&s->lock,flags);
  if (!bio) { memalloc_noio_restore(noio); return; }
  while (bio->bi_iter.bi_size) {
   struct bio_vec bv=bio_iter_iovec(bio,bio->bi_iter);
   sector_t sector=bio->bi_iter.bi_sector;
   unsigned int lsn=sector>>2, offset=(sector&3)*512;
   unsigned int n=min_t(unsigned int,bv.bv_len,2048-offset);
   void *data;
   if (!s->cached_count || lsn<s->cached_first || lsn-s->cached_first>=s->cached_count) {
    unsigned int first=lsn&~(EECDVD_SECTORS-1), count;
    if (lsn>=s->capacity) { err=-EIO; break; }
    count=min_t(unsigned int,EECDVD_SECTORS,s->capacity-first);
    err=read_sectors(s,first,count); if (err) break;
   }
   data=bvec_kmap_local(&bv);
   memcpy(data,s->reply->data+2048*(lsn-s->cached_first)+offset,n); kunmap_local(data);
   bio_advance(bio,n);
  }
  if (err) bio_io_error(bio); else bio_endio(bio);
 }
}
static void cd_submit(struct bio *bio)
{
 struct eecdvd *s=bio->bi_bdev->bd_disk->private_data;
 unsigned long flags;
 if (bio_op(bio)!=REQ_OP_READ) { bio_io_error(bio); return; }
 if (bio->bi_opf&REQ_NOWAIT) { bio_wouldblock_error(bio); return; }
 spin_lock_irqsave(&s->lock,flags); bio_list_add(&s->bios,bio); spin_unlock_irqrestore(&s->lock,flags);
 queue_work(s->queue,&s->work);
}
static const struct block_device_operations cd_ops={.owner=THIS_MODULE,.submit_bio=cd_submit};
static int __init cd_init(void)
{
 struct queue_limits lim={.logical_block_size=2048,.physical_block_size=2048,.max_hw_sectors=128};
 int err;
 cd=kzalloc(sizeof(*cd),GFP_KERNEL); if (!cd) return -ENOMEM;
 cd->request=(void *)get_zeroed_page(GFP_KERNEL);
 cd->reply=alloc_pages_exact(sizeof(*cd->reply),GFP_KERNEL|__GFP_ZERO);
 if (!cd->request || !cd->reply) { err=-ENOMEM; goto free; }
 err=iop_module_request("eecdvd",0x0100,NULL); if (err) goto free;
 err=sif_rpc_bind(&cd->rpc,EECDVD_SERVER); if (err) goto free;
 /* The normal SIF client owns one page. Reserve a larger dedicated receive
  * area for this optical client only, before its first RPC. Keep the original
  * page so the generic unbind API still receives the buffer it allocated. */
 {
  void *buffer=alloc_pages_exact(sizeof(*cd->reply),GFP_KERNEL|GFP_DMA|__GFP_ZERO);
  if (!buffer) { err=-ENOMEM; goto unbind; }
  cd->rpc_default_buffer=cd->rpc.client_buffer;
  cd->rpc.client_buffer=buffer; cd->rpc.client_size_max=sizeof(*cd->reply);
 }
 err=read_sectors(cd,16,1); if (err) goto unbind;
 if (memcmp(cd->reply->data+1,"CD001",5)) { err=-ENOMEDIUM; goto unbind; }
 spin_lock_init(&cd->lock); bio_list_init(&cd->bios); INIT_WORK(&cd->work,cd_work);
 cd->queue=alloc_ordered_workqueue("ps2-cdvd",WQ_MEM_RECLAIM); if (!cd->queue) { err=-ENOMEM; goto unbind; }
 cd->major=register_blkdev(0,"ps2dvd"); if (cd->major<0) { err=cd->major; goto queue; }
 cd->disk=blk_alloc_disk(&lim,NUMA_NO_NODE); if (IS_ERR(cd->disk)) { err=PTR_ERR(cd->disk); goto major; }
 cd->disk->major=cd->major; cd->disk->first_minor=0; cd->disk->minors=1;
 cd->disk->fops=&cd_ops; cd->disk->private_data=cd;
 strscpy(cd->disk->disk_name,"ps2dvd");
 /* ISO PVD capacity is checked against the single-layer DVD maximum. */
 {
  unsigned int sectors=get_unaligned_le32(cd->reply->data+80);
  if (sectors<32 || sectors>2295104u) { err=-EINVAL; goto disk; }
  cd->capacity=sectors;
  set_capacity(cd->disk,(sector_t)sectors*4);
 }
 set_disk_ro(cd->disk,true); err=add_disk(cd->disk); if (err) goto disk;
 pr_info("ps2-cdvd: read-only DVD root device /dev/ps2dvd ready\n"); return 0;
disk: put_disk(cd->disk);
major: unregister_blkdev(cd->major,"ps2dvd");
queue: destroy_workqueue(cd->queue);
unbind:
 if (cd->poisoned) { pr_err("ps2-cdvd: timed-out DMA retained until reboot\n"); return err; }
 if (cd->rpc_default_buffer) {
  free_pages_exact(cd->rpc.client_buffer,sizeof(*cd->reply));
  cd->rpc.client_buffer=cd->rpc_default_buffer; cd->rpc.client_size_max=PAGE_SIZE;
 }
 sif_rpc_unbind(&cd->rpc);
free:
 pr_err("ps2-cdvd: DVD root device initialization failed: %d\n",err);
 if (cd->request) free_page((unsigned long)cd->request);
 if (cd->reply) free_pages_exact(cd->reply,sizeof(*cd->reply));
 kfree(cd); return err;
}
/* Built-in only: the boot root and its in-flight DMA live until reboot. */
late_initcall(cd_init);
MODULE_LICENSE("GPL");
