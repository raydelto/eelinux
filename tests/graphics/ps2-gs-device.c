/* SPDX-License-Identifier: MIT */
/* Compile the actual provider with a lifecycle/syscall scaffold and the real
 * GS packet layouts. This is not a hardware, fbcon or lockdep simulation. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "ps2-gs-surface.h"
#ifndef __MIPSEL__
#define __MIPSEL__ 1
#endif
#include "asm/gif.h"
typedef uint32_t u32; typedef uint8_t u8; typedef uint64_t u64;
#define __user
#define THIS_MODULE NULL
#define GFP_KERNEL 0
#define CAP_SYS_RAWIO 1
#define MISC_DYNAMIC_MINOR 255
#define KD_GRAPHICS 1
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define max(a,b) ((a)>(b)?(a):(b))
#define container_of(p,t,f) ((t*)((char*)(p)-offsetof(t,f)))
#define WARN_ON_ONCE(c) (c)
#define cpu_relax() ((void)0)
#define u64_to_user_ptr(p) ((void*)(uintptr_t)(p))
#define kzalloc(n,f) calloc(1,n)
#define kfree free
#define IS_ERR(p) (!(p))
#define PTR_ERR(p) (-ENOMEM)
#define GIF_PACKAGE_TAG(p) ((p)++)->gif.tag = (struct gif_tag)
#define GIF_PACKAGE_AD(p) ((p)++)->gif.packed.ad = (struct gif_packed_ad)
union package { union gif_data gif; };
struct mutex { pthread_mutex_t value; };
static void mutex_init(struct mutex *m) { assert(!pthread_mutex_init(&m->value,NULL)); }
static void mutex_lock(struct mutex *m) { assert(!pthread_mutex_lock(&m->value)); }
static void mutex_unlock(struct mutex *m) { assert(!pthread_mutex_unlock(&m->value)); }
static int mutex_trylock(struct mutex *m) { return pthread_mutex_trylock(&m->value)==0; }
static struct mutex console;
static int console_trylock(void) { return mutex_trylock(&console); }
static void console_unlock(void) { mutex_unlock(&console); }
#define spin_lock_irqsave(m,f) do { (f)=0; mutex_lock(m); } while(0)
#define spin_unlock_irqrestore(m,f) do { (void)(f); mutex_unlock(m); } while(0)
struct atomic { int value; };
static int atomic_read(struct atomic *a) { return a->value; }
static void atomic_set(struct atomic *a,int v) { a->value=v; }
struct kref { int count; };
static void kref_init(struct kref *r) { r->count=1; }
static void kref_get(struct kref *r) { ++r->count; }
static void kref_put(struct kref *r,void (*destroy)(struct kref*)) { if (!--r->count) destroy(r); }
struct inode { int unused; };
struct file { void *private_data; };
struct file_operations {
    void *owner;
    int (*open)(struct inode*,struct file*),(*release)(struct inode*,struct file*);
    long (*unlocked_ioctl)(struct file*,unsigned int,unsigned long),(*compat_ioctl)(struct file*,unsigned int,unsigned long);
    void *llseek;
};
#define noop_llseek NULL
struct miscdevice { int minor; const char *name; const struct file_operations *fops; int mode; };
static int misc_register(struct miscdevice *d) { assert(d->mode==0600); return 0; }
static void misc_deregister(struct miscdevice *d) { (void)d; }
struct vc_data { int vc_mode; };
static struct vc_data vc;
static struct { struct vc_data *d; } vc_cons[1]={{&vc}};
static int fg_console;
struct ps2gs_device;
struct ps2fb_par {
    struct mutex lock;
    struct atomic shadow_maps,gs_owned;
    u32 gs_font_blocks;
    struct { u32 block_count; } cb;
    struct { size_t capacity; union package *buffer; } package;
    struct ps2gs_caps gs_stats;
    u64 gs_epoch;
    struct ps2gs_device *gs_device;
};
struct fb_info {
    struct ps2fb_par *par;
    struct { u32 xres,yres,xres_virtual,yres_virtual,bits_per_pixel,xoffset,yoffset; } var;
    struct mutex lock,mm_lock;
    int deferred_work;
};
static int permission=1,fail_copy_out,hardware_fail;
static unsigned dma_calls;
static int capable(int c) { (void)c; return permission; }
static int copy_from_user(void *d,const void *s,size_t n) { if (!s) return 1; memcpy(d,s,n); return 0; }
static int copy_to_user(void *d,const void *s,size_t n) { if (!d||fail_copy_out) return 1; memcpy(d,s,n); return 0; }
static void *memdup_user(const void *s,size_t n) { void *p=malloc(n); if (p) memcpy(p,s,n); return p; }
static void flush_delayed_work(int *w) { (void)w; }
static u32 block_address_for_index(u32 index,struct fb_info *info) { (void)info; return index; }
static void ps2gs_caps(struct fb_info *info,struct ps2gs_caps *c)
{
    *c=(struct ps2gs_caps){.abi=1,.width=info->var.xres,.height=info->var.yres,
        .bits_per_pixel=info->var.bits_per_pixel,.features=31,.depth_base=3*1024*1024};
}
static bool gif_wait(void) { return !hardware_fail; }
static void gif_write(union gif_data *p,size_t n) { assert(p&&n<=200); ++dma_calls; }
/* CSR polling is scaffolded; raster packet fields use real kernel layouts. */
struct gs_csr { int finish; };
static void gs_write_csr(struct gs_csr csr) { assert(csr.finish); }
static struct gs_csr gs_read_csr(void) { return (struct gs_csr){.finish=1}; }
#include "ps2-gs-image-packet.h"
#include "ps2-gs-vertex-packet.h"
#include "ps2fb-gs-device.h"

static struct fb_info *setup(void)
{
    struct fb_info *info=calloc(1,sizeof(*info)); info->par=calloc(1,sizeof(*info->par));
    mutex_init(&info->lock); mutex_init(&info->mm_lock); mutex_init(&info->par->lock);
    info->var.xres=info->var.xres_virtual=128; info->var.yres=info->var.yres_virtual=96;
    info->var.bits_per_pixel=32; info->par->cb.block_count=192; info->par->gs_font_blocks=8;
    info->par->package.buffer=calloc(256,sizeof(union package)); info->par->package.capacity=256;
    assert(!ps2gs_device_register(info)); return info;
}
static void free_info(struct fb_info *info)
{
    pthread_mutex_destroy(&info->lock.value); pthread_mutex_destroy(&info->mm_lock.value);
    pthread_mutex_destroy(&info->par->lock.value);
    free(info->par->package.buffer); free(info->par); free(info);
}
static int open_file(struct fb_info *info,struct file *f)
{
    f->private_data=&info->par->gs_device->misc;
    return ps2gs_device_open(NULL,f);
}
static long call(struct file *file,unsigned int cmd,void *arg)
{
    return ps2gs_device_ioctl(file,cmd,(unsigned long)arg);
}
static void ownership(void)
{
    struct fb_info *info=setup(); struct file a,b;
    permission=0; assert(open_file(info,&a)==-EPERM); permission=1;
    info->var.yoffset=1; assert(open_file(info,&a)==-EOPNOTSUPP); info->var.yoffset=0;
    atomic_set(&info->par->shadow_maps,1); assert(open_file(info,&a)==-EBUSY);
    atomic_set(&info->par->shadow_maps,0); assert(!open_file(info,&a));
    assert(atomic_read(&info->par->gs_owned)==1 && open_file(info,&b)==-EBUSY);
    struct ps2gs_caps caps; assert(!call(&a,PS2GS_GET_CAPS,&caps) && caps.features==51);
    struct ps2gs_surface_create create={.abi=1,.width=65,.height=33,.bits_per_pixel=32};
    vc.vc_mode=0; assert(call(&a,PS2GS_SURFACE_CREATE,&create)==-EBUSY);
    vc.vc_mode=KD_GRAPHICS;
    fail_copy_out=1; assert(call(&a,PS2GS_SURFACE_CREATE,&create)==-EFAULT);
    fail_copy_out=0; assert(!call(&a,PS2GS_SURFACE_CREATE,&create) && create.handle==2);
    assert(create.buffer_width==128 && create.allocated_bytes==32768);
    struct ps2gs_surface_fill fill={.abi=1,.handle=create.handle,.width=65,.height=33};
    unsigned before=dma_calls; assert(!call(&a,PS2GS_SURFACE_FILL,&fill) && dma_calls==before+1);
    ++info->par->gs_epoch;
    assert(call(&a,PS2GS_SURFACE_FILL,&fill)==-ESTALE && dma_calls==before+1);
    assert(!ps2gs_device_release(NULL,&a) && !atomic_read(&info->par->gs_owned));
    assert(!open_file(info,&b)); assert(!ps2gs_device_release(NULL,&b));
    ps2gs_device_unregister(info); free_info(info);
}
static void failures(void)
{
    struct fb_info *info=setup(); struct file f; assert(!open_file(info,&f));
    struct ps2gs_surface_create create={.abi=1,.width=1,.height=1,.bits_per_pixel=16};
    hardware_fail=1; assert(call(&f,PS2GS_SURFACE_CREATE,&create)==-ETIMEDOUT);
    hardware_fail=0; unsigned before=dma_calls;
    assert(call(&f,PS2GS_SURFACE_CREATE,&create)==-ETIMEDOUT && dma_calls==before);
    struct ps2gs_session *s=f.private_data;
    assert(!ps2gs_arena_lookup(&s->arena,1));
    assert(!ps2gs_device_release(NULL,&f)); ps2gs_device_unregister(info); free_info(info);
}
static void *ioctl_thread(void *data)
{
    struct file *f=data;
    for (unsigned i=0;i<10000;++i) {
        struct ps2gs_caps caps; long ret=call(f,PS2GS_GET_CAPS,&caps);
        assert(ret==0 || ret==-EAGAIN || ret==-ENODEV);
    }
    return NULL;
}
static void disconnect(void)
{
    struct fb_info *info=setup(); struct file f; assert(!open_file(info,&f));
    pthread_t thread; assert(!pthread_create(&thread,NULL,ioctl_thread,&f));
    ps2gs_device_unregister(info); free_info(info);
    assert(!pthread_join(thread,NULL));
    struct ps2gs_caps caps; assert(call(&f,PS2GS_GET_CAPS,&caps)==-ENODEV);
    assert(!ps2gs_device_release(NULL,&f));
}
int main(int argc,char **argv)
{
    assert(argc==2); mutex_init(&console); vc.vc_mode=KD_GRAPHICS;
    if (!strcmp(argv[1],"ownership")) ownership();
    else if (!strcmp(argv[1],"failures")) failures();
    else if (!strcmp(argv[1],"disconnect")) disconnect();
    else return 2;
    pthread_mutex_destroy(&console.value);
    puts("PS2 GS provider lifecycle test passed"); return 0;
}
