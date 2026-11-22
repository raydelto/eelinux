/* SPDX-License-Identifier: GPL-2.0-only */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "edid.h"

struct mode {
    const char *id,*sysfs;
    unsigned width,height,hz;
    int interlaced,rgb,experimental;
};
static const struct mode modes[]={
#include "modes.inc"
};
#define MODE_COUNT (sizeof(modes)/sizeof(modes[0]))
static volatile sig_atomic_t interrupted;
static void signal_stop(int signal_number) { interrupted=signal_number; }

/* Include width/row padding and room for a 512-glyph 8x16 font and palette. */
static unsigned budget_bpp(unsigned width,unsigned height)
{
    uint64_t pixels=((width+63)/64)*64ULL*((height+7)/8)*8;
    if(pixels*4+32768+256<=4194304) return 32;
    if(pixels*2+32768+256<=4194304) return 16;
    return 0;
}

static int read_text(const char *directory,const char *name,char *buffer,size_t capacity)
{
    char path[PATH_MAX];
    if(snprintf(path,sizeof(path),"%s/%s",directory,name)>=(int)sizeof(path)) { errno=ENAMETOOLONG;return -1; }
    FILE *file=fopen(path,"r");if(!file) return -1;
    size_t length=fread(buffer,1,capacity-1,file);
    int failed=ferror(file) || !feof(file);fclose(file);
    if(failed) { errno=EOVERFLOW;return -1; }
    buffer[length]=0;return 0;
}

static int write_text(const char *directory,const char *name,const char *value)
{
    char path[PATH_MAX],line[256];
    if(snprintf(path,sizeof(path),"%s/%s",directory,name)>=(int)sizeof(path)) { errno=ENAMETOOLONG;return -1; }
    int size=snprintf(line,sizeof(line),"%s\n",value);
    if(size<0 || size>=(int)sizeof(line)) { errno=EOVERFLOW;return -1; }
    int file=open(path,O_WRONLY|O_TRUNC);if(file<0) return -1;
    ssize_t written=write(file,line,(size_t)size);int saved=errno;
    int closed=close(file);
    if(written!=size) { errno=written<0?saved:EIO;return -1; }
    return closed;
}

static void strip_newline(char *text) { text[strcspn(text,"\r\n")]=0; }
static int offered(const char *text,const char *choice)
{
    size_t n=strlen(choice);
    for(const char *p=text;*p;) {
        const char *end=strchr(p,'\n');size_t length=end?(size_t)(end-p):strlen(p);
        if(length==n && !memcmp(p,choice,n)) return 1;
        if(!end) break;
        p=end+1;
    }
    return 0;
}

static size_t read_edid(const char *path,uint8_t data[1024])
{
    FILE *file=fopen(path,"rb");if(!file) return 0;
    size_t size=fread(data,1,1024,file);
    int extra=fgetc(file), failed=ferror(file);fclose(file);
    return !failed && extra==EOF && edid_valid(data,size)?size:0;
}

static const struct mode *select_auto(const char *available,const uint8_t *edid,size_t size,int rgb)
{
    const struct mode *best=NULL;uint64_t score=0;
    for(size_t i=0;i<MODE_COUNT;i++) {
        const struct mode *mode=modes+i;
        if(mode->experimental || mode->rgb!=rgb || !offered(available,mode->sysfs) ||
           !budget_bpp(mode->width,mode->height) ||
           !edid_supports(edid,size,mode->width,mode->height,mode->hz,mode->interlaced)) continue;
        uint64_t value=(uint64_t)mode->width*mode->height*100;
        value+=mode->hz==50?2:0;value+=!mode->interlaced;
        if(!best || value>score) { best=mode;score=value; }
    }
    return best;
}

static int restore(const char *directory,const char *old_mode,const char *old_bpp)
{
    int result=0;
    if(write_text(directory,"bits_per_pixel","16")) result=-1;
    if(write_text(directory,"mode",old_mode)) result=-1;
    if(write_text(directory,"bits_per_pixel",old_bpp)) result=-1;
    if(result) fprintf(stderr,"ps2-video: could not restore the previous mode; reboot returns to the kernel boot mode\n");
    return result;
}

static int apply(const char *directory,const struct mode *mode,unsigned seconds)
{
    char old_mode[128],old_bpp[32],target_bpp[16];
    if(read_text(directory,"mode",old_mode,sizeof(old_mode)) ||
       read_text(directory,"bits_per_pixel",old_bpp,sizeof(old_bpp))) { perror("ps2-video: current mode");return 1; }
    strip_newline(old_mode);strip_newline(old_bpp);
    if(!old_mode[0] || !old_bpp[0]) { fprintf(stderr,"ps2-video: current mode is unavailable; preserving display\n");return 1; }
    unsigned bpp=budget_bpp(mode->width,mode->height);
    if(!bpp) { fprintf(stderr,"ps2-video: mode exceeds the GS memory budget\n");return 1; }
    snprintf(target_bpp,sizeof(target_bpp),"%u",bpp);
    struct sigaction action={0};action.sa_handler=signal_stop;sigemptyset(&action.sa_mask);
    sigaction(SIGINT,&action,NULL);sigaction(SIGTERM,&action,NULL);sigaction(SIGHUP,&action,NULL);
    printf("ps2-video: selecting %s (%s), %u bpp%s\n",mode->id,mode->sysfs,bpp,
           seconds?"; preview will restore the previous mode":"");fflush(stdout);
    /* Lower depth before changing geometry; raise it only after the new mode fits. */
    if(write_text(directory,"bits_per_pixel","16") ||
       write_text(directory,"mode",mode->sysfs) || write_text(directory,"bits_per_pixel",target_bpp)) {
        perror("ps2-video: mode rejected");restore(directory,old_mode,old_bpp);return 1;
    }
    if(seconds) {
        struct timespec until,now;clock_gettime(CLOCK_MONOTONIC,&until);until.tv_sec+=seconds;
        do {
            struct timespec delay={0,100000000};nanosleep(&delay,NULL);
            clock_gettime(CLOCK_MONOTONIC,&now);
        } while(!interrupted && (now.tv_sec<until.tv_sec || (now.tv_sec==until.tv_sec && now.tv_nsec<until.tv_nsec)));
        if(restore(directory,old_mode,old_bpp)) return 1;
        printf("ps2-video: restored %s\n",old_mode);
    } else if(interrupted) {
        restore(directory,old_mode,old_bpp);return 1;
    }
    return interrupted?1:0;
}

int main(int argc,char **argv)
{
    const char *directory="/sys/class/graphics/fb0",*edid_path=NULL,*operation=NULL,*choice=NULL;
    unsigned seconds=15;int rgb=0,experimental=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--sysfs") && i+1<argc) directory=argv[++i];
        else if(!strcmp(argv[i],"--edid") && i+1<argc) edid_path=argv[++i];
        else if(!strcmp(argv[i],"--rgb-sog")) rgb=1;
        else if(!strcmp(argv[i],"--experimental")) experimental=1;
        else if(!operation) operation=argv[i];
        else if(!choice) choice=argv[i];
        else if(!strcmp(operation,"test") && i==argc-1) {
            char *end;errno=0;unsigned long value=strtoul(argv[i],&end,10);
            if(errno || !*argv[i] || *end || value<1 || value>60) goto usage;
            seconds=(unsigned)value;
        } else goto usage;
    }
    if(!operation || (choice && strcmp(operation,"test") && strcmp(operation,"set"))) goto usage;
    char available[8192],current_mode[128];
    if(read_text(directory,"modes",available,sizeof(available))) { perror("ps2-video: framebuffer modes");return 1; }
    if(!strcmp(operation,"list")) {
        for(size_t i=0;i<MODE_COUNT;i++) if(offered(available,modes[i].sysfs))
            printf("%-12s %-24s %-9s %s\n",modes[i].id,modes[i].sysfs,modes[i].rgb?"RGB/SOG":"component",
                   modes[i].experimental?"experimental; preview required":"driver mode; display acceptance required");
        return 0;
    }
    if(!strcmp(operation,"status")) {
        char bpp[32];
        if(read_text(directory,"mode",current_mode,sizeof(current_mode)) ||
           read_text(directory,"bits_per_pixel",bpp,sizeof(bpp))) { perror("ps2-video: status");return 1; }
        strip_newline(current_mode);strip_newline(bpp);printf("%s %s bpp\n",current_mode,bpp);return 0;
    }
    if(!strcmp(operation,"auto")) {
        uint8_t edid[1024];size_t size=0;char path[PATH_MAX];
        if(edid_path) size=read_edid(edid_path,edid);
        else for(unsigned i=0;i<2 && !size;i++) {
            if(snprintf(path,sizeof(path),"%s/%s",directory,i?"device/edid":"edid")<(int)sizeof(path))
                size=read_edid(path,edid);
        }
        if(!size) {
            printf("ps2-video: no valid monitor EDID; retaining the current boot mode\n");
            return edid_path?1:0;
        }
        const struct mode *mode=select_auto(available,edid,size,rgb);
        if(!mode) { printf("ps2-video: no compatible EDID/GS/cable mode; retaining current mode\n");return 0; }
        return apply(directory,mode,0);
    }
    if((!strcmp(operation,"test") || !strcmp(operation,"set")) && choice) {
        for(size_t i=0;i<MODE_COUNT;i++) if(!strcmp(modes[i].id,choice)) {
            if(!offered(available,modes[i].sysfs)) { fprintf(stderr,"ps2-video: mode is absent from this kernel\n");return 1; }
            if(modes[i].experimental && (!experimental || strcmp(operation,"test"))) {
                fprintf(stderr,"ps2-video: experimental timing requires --experimental test\n");return 1;
            }
            if(modes[i].rgb && !rgb) { fprintf(stderr,"ps2-video: VGA mode requires --rgb-sog and an RGB sync-on-green connection\n");return 1; }
            return apply(directory,modes+i,!strcmp(operation,"test")?seconds:0);
        }
        fprintf(stderr,"ps2-video: unknown mode; use ps2-video list\n");return 1;
    }
usage:
    fprintf(stderr,"Usage: ps2-video [--rgb-sog] [--edid FILE] {list|status|auto|test MODE [1..60]|set MODE}\n");
    return 2;
}
