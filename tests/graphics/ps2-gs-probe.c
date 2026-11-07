/* SPDX-License-Identifier: MIT */
/* Run the real probe's console acquisition, uploads and recovery with wrapped
 * device syscalls. Raising SIGINT also exercises its actual signal handler. */
#include "ps2_gs_client.h"
#include <assert.h>
#include <errno.h>
#include <linux/kd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static unsigned uploads, submissions, changes, restores, closes;
static int failure, interrupt_wait;
int ps2_probe_main(int,char **);
int __wrap_open(const char *path,int flags,...)
{
    (void)flags;
    if (!strcmp(path,"/dev/fb0")) return 9;
    assert(!strcmp(path,"/dev/tty0")); return 10;
}
int __wrap_close(int fd)
{
    assert(fd==9 || fd==10); ++closes; return 0;
}
int __wrap_nanosleep(const struct timespec *duration,struct timespec *remaining)
{
    (void)remaining; assert(duration->tv_sec==1 && !duration->tv_nsec);
    if (interrupt_wait) { raise(SIGINT); errno=EINTR; return -1; }
    return 0;
}
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list args; va_start(args,op);
    if (fd==10) {
        if (op==KDGETMODE) *va_arg(args,int *)=KD_TEXT;
        else {
            assert(op==KDSETMODE);
            int mode=va_arg(args,int);
            if (mode==KD_GRAPHICS) ++changes;
            else { assert(mode==KD_TEXT); ++restores; }
        }
        va_end(args); return 0;
    }
    assert(fd==9); void *p=va_arg(args,void *); va_end(args);
    if (op==PS2GS_GET_CAPS) {
        *(struct ps2gs_caps *)p=(struct ps2gs_caps){.abi=1,.features=31,
            .width=720,.height=576,.bits_per_pixel=32,.max_commands=64}; return 0;
    }
    if (op==PS2GS_SUBMIT) {
        struct ps2gs_submit *s=p;
        assert(s->count==1 && ((struct ps2gs_command *)(uintptr_t)s->commands)->op==PS2GS_FILL);
        ++submissions; return 0;
    }
    assert(op==PS2GS_UPLOAD); struct ps2gs_upload *u=p;
    assert(u->image_width==131 && u->bytes<=3072 && u->x==16);
    ++uploads;
    if (failure) { errno=EIO; return -1; }
    return 0;
}
int main(int argc,char **argv)
{
    assert(argc==2);
    failure=!strcmp(argv[1],"failure"); interrupt_wait=!strcmp(argv[1],"interrupt");
    assert(failure || interrupt_wait || !strcmp(argv[1],"success"));
    char *args[]={"ps2-gs-upload-test","--brief",NULL};
    int status=ps2_probe_main(2,args);
    assert(status==(failure || interrupt_wait ? 1 : 0));
    assert(changes==1 && restores==1 && closes==2 && submissions==1);
    assert(uploads==(failure ? 1 : 13));
    puts("PS2 upload probe console lifecycle test passed"); return 0;
}
