/* SPDX-License-Identifier: MIT */
#include "previous-rop.c"
#include "ps2_plane.h"
static uint32_t reading_source;
static unsigned readbacks;
static int readback_failure;
static struct ps2_pipe_copy_box *changing_boxes;
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list a;va_start(a,op);void *arg=va_arg(a,void*);va_end(a);
    if(op==PS2GS_GET_CAPS){int status=rop_ioctl(fd,op,arg);
        if(!status)((struct ps2gs_caps*)arg)->features|=PS2GS_HW_CHANNEL_MAP_BLIT|PS2GS_HW_CHANNEL_MAP|
            PS2GS_HW_CHANNEL_LUT|PS2GS_HW_RGBA_TRANSFER;
        return status;}
    if(op==PS2GS_CHANNEL_MAP_BLIT && readback_failure){errno=EIO;return -1;}
    if(op==PS2GS_RENDER_SUBMIT && changing_boxes){changing_boxes->width=9000;changing_boxes=NULL;}
    if(op==PS2GS_SURFACE_DOWNLOAD){
        const struct ps2gs_surface_download *r=arg;
        /* Pixel readback is restricted to the masked source scratch surface. */
        assert(r->handle && r->handle!=reading_source && r->handle!=destination_handle());
        ++readbacks;if(readback_failure){errno=EIO;return -1;}
    }
    return rop_ioctl(fd,op,arg);
}
static void check_plane(uint32_t sh,uint32_t dh,const struct ps2_pipe_copy_box *boxes,
    unsigned n,uint32_t plane,unsigned alu,uint32_t mask)
{
    struct pixels *s=model(sh),*d=model(dh);size_t bytes=d->width*d->height*4;
    uint32_t *saved=malloc(s->width*s->height*4),*expected=malloc(bytes);assert(saved && expected);
    memcpy(saved,s->data,s->width*s->height*4);memcpy(expected,d->data,bytes);
    uint32_t bits=model_bpp(d)==16?0x7fff:0xffffff,fg=0x624a15&bits,bg=0x1956b3&bits;
    for(unsigned i=0;i<n;++i)for(int y=0;y<boxes[i].height;++y)for(int x=0;x<boxes[i].width;++x){
        unsigned si=(boxes[i].sy+y)*s->width+boxes[i].sx+x,di=(boxes[i].dy+y)*d->width+boxes[i].dx+x;
        uint32_t colour=(saved[si]&plane)?fg:bg;
        expected[di]=(expected[di]&~mask)|(oracle(alu,colour,expected[di])&mask);
    }
    reading_source=sh;
    assert(!ps2_pipe_plane_copy_boxes(ctx,sh,dh,boxes,n,plane,fg,bg,mask,alu));
    assert(!memcmp(expected,d->data,bytes));free(expected);free(saved);
}
static void all_planes(unsigned sbpp,unsigned dbpp,int every_plane)
{
    polygon_setup(1,dbpp);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    uint32_t sh;assert(!ps2_surface_create(gs,83,61,sbpp,&sh));
    struct pixels *s=model(sh),*d=model(destination_handle());seed(s);
    struct ps2_pipe_copy_box box={2,1,7,5,71,43};
    for(unsigned bit=0;bit<(sbpp==16?15U:24U);bit+=every_plane?1:7)
        for(unsigned alu=0;alu<16;++alu){seed(d);
            check_plane(sh,destination_handle(),&box,1,1U<<bit,alu,
                alu&1?(dbpp==16?0x7fff:0xffffff):(dbpp==16?0x514a:0x819135));}
    assert(!readbacks);assert(!ps2_surface_destroy(gs,sh));polygon_finish();
}
static void plane_overlap(int bands)
{
    polygon_setup(1,32);struct pixels *d=model(destination_handle());
    for(unsigned direction=0;direction<4;++direction)for(unsigned alu=0;alu<16;++alu){seed(d);
        int sx=direction&1?3:9,sy=direction&2?2:7,dx=direction&1?9:3,dy=direction&2?7:2;
        struct ps2_pipe_copy_box boxes[]={{sx,sy,dx,dy,70,39},{sx,sy,dx,dy,23,16},{sx+30,sy,dx+30,dy,40,16}};
        check_plane(destination_handle(),destination_handle(),bands?boxes+1:boxes,bands?2:1,16,alu,0x81a153);
    }
    polygon_finish();
}
static void plane_frontend(int source_window)
{
    polygon_setup(1,32);xs.SourceValidate=source_validate;window.realized=TRUE;
    if(source_window)pixman_region_init_rect(&window.clipList,xp.screen_x,xp.screen_y,xp.drawable.width,xp.drawable.height);
    struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    uint32_t sh;assert(!ps2_surface_create(gs,83,61,32,&sh));
    PixmapRec pixmap={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=24,.bitsPerPixel=32,.width=83,.height=61},.devPrivates=(void*)&sh};
    struct pixels *s=model(sh),*d=model(destination_handle());seed(s);
    DrawablePtr sd=source_window?&window.drawable:&pixmap.drawable;
    if(source_window){seed(d);s=d;}
    size_t bytes=d->width*d->height*4;uint32_t *expected=malloc(bytes),*saved=malloc(s->width*s->height*4);assert(expected && saved);
    for(unsigned alu=0;alu<16;++alu){seed(d);memcpy(saved,s->data,s->width*s->height*4);memcpy(expected,d->data,bytes);
        gc.alu=alu;gc.planemask=0x81a153;gc.fgPixel=0x624a15;gc.bgPixel=0x1956b3;
        for(unsigned y=0;y<d->height;++y)for(unsigned x=0;x<d->width;++x){
            int dx=x+xp.screen_x-window.drawable.x+7,dy=y+xp.screen_y-window.drawable.y+3;
            int sx=dx+3+(source_window?window.drawable.x-xp.screen_x:0),sy=dy+2+(source_window?window.drawable.y-xp.screen_y:0);
            if(dx<0 || dy<0 || dx>=37 || dy>=29 || sx<0 || sy<0 || sx>=(int)s->width || sy>=(int)s->height ||
               !pixman_region_contains_point(&clip_region,x+xp.screen_x,y+xp.screen_y,NULL))continue;
            uint32_t colour=saved[sy*s->width+sx]&16?gc.fgPixel:gc.bgPixel,*p=expected+y*d->width+x;
            *p=(*p&~gc.planemask)|(oracle(alu,colour,*p)&gc.planemask);
        }
        reading_source=source_window?destination_handle():sh;
        RegionPtr exposure=gc.ops->CopyPlane(sd,&window.drawable,&gc,3,2,37,29,-7,-3,16);
        if(exposure)RegionDestroy(exposure);
        assert(!memcmp(expected,d->data,bytes));
    }
    free(saved);free(expected);if(source_window)pixman_region_fini(&window.clipList);
    assert(!ps2_surface_destroy(gs,sh));polygon_finish();
}
static void random_plane(int streaming)
{
    setup_rop(32);struct pixels *s=model(source_handle),*d=model(destination_handle());
    uint32_t state=19;
    for(unsigned k=0;k<(streaming?1U:40U);++k){
        for(unsigned i=0;i<s->width*s->height;++i){state=state*1664525+1013904223;s->data[i]=state;}
        seed(d);struct ps2_pipe_copy_box box={0,0,3,2,streaming?80:19,streaming?55:11};
        check_plane(source_handle,destination_handle(),&box,1,1U<<(k%24),k%16,0x81a153);
    }
    finish_rop();
}
static void plane_guards(int noop,int allocation)
{
    setup_rop(32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,1,1,0x80332211));unsigned before=attempts,reads=readbacks;
    struct ps2_pipe_copy_box box={0,0,1,1,10,10};
    if(noop){assert(!ps2_pipe_plane_copy_boxes(ctx,source_handle,0,&box,1,16,1,0,0,GXxor));
        assert(!ps2_pipe_plane_copy_boxes(ctx,source_handle,0,&box,1,16,1,0,0xffffff,GXnoop));}
    else if(allocation){for(int i=1;i<=1;++i){malloc_fail=i;mal_calls=0;
        assert(ps2_pipe_plane_copy_boxes(ctx,source_handle,0,&box,1,16,1,0,0xffffff,GXcopy)<0 && errno==ENOMEM);malloc_fail=0;}}
    else {for(unsigned i=0;i<6;++i){struct ps2_pipe_copy_box bad=box;uint32_t plane=16,mask=0xffffff;unsigned alu=3;
        switch(i){case 0:bad.sx=-1;break;case 1:bad.width=INT32_MAX;break;case 2:plane=0;break;
            case 3:plane=3;break;case 4:mask=0x1000000;break;case 5:alu=16;break;}
        assert(ps2_pipe_plane_copy_boxes(ctx,source_handle,0,&bad,1,plane,1,0,mask,alu)<0 && errno==EINVAL);}}
    assert(attempts==before && readbacks==reads && r->count==1);finish_rop();
}
static void plane_failure(void)
{
    setup_rop(32);struct ps2_pipe_copy_box box={0,0,3,2,20,12};reading_source=source_handle;
    struct pixels *d=model(destination_handle());uint32_t *saved=malloc(d->width*d->height*4);assert(saved);memcpy(saved,d->data,d->width*d->height*4);
    readback_failure=1;assert(ps2_pipe_plane_copy_boxes(ctx,source_handle,destination_handle(),&box,1,16,1,0,0xffffff,3)<0 && errno==EIO);
    assert(!memcmp(saved,d->data,d->width*d->height*4));unsigned reads=readbacks;readback_failure=0;
    assert(ps2_pipe_plane_copy_boxes(ctx,source_handle,destination_handle(),&box,1,16,1,0,0xffffff,3)<0 && readbacks==reads);
    free(saved);polygon_finish();
}
static void plane_snapshot(void)
{
    setup_rop(32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,1,1,0x80332211));
    struct ps2_pipe_copy_box box={0,0,5,4,19,11};changing_boxes=&box;reading_source=source_handle;
    assert(!ps2_pipe_plane_copy_boxes(ctx,source_handle,destination_handle(),&box,1,16,1,0,0xffffff,3));
    assert(box.width==9000 && !r->count);finish_rop();
}
static void mono_destination(unsigned bpp)
{
    polygon_setup(1,bpp);xs.SourceValidate=source_validate;
    struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    uint32_t sh;assert(!ps2_surface_create(gs,83,61,bpp,&sh));
    PixmapRec source={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=bpp==16?15:24,
        .bitsPerPixel=bpp,.width=83,.height=61},.devPrivates=(void*)&sh};
    int pitch;PixmapRec mono={.drawable={.type=DRAWABLE_PIXMAP,.pScreen=&xs,.depth=1,.bitsPerPixel=1,.width=43,.height=32}};
    mono.devPrivates=exa.driver.CreatePixmap2(&xs,43,32,1,0,1,&pitch);assert(mono.devPrivates);mono.devKind=pitch;
    struct ps2_mono_source output;assert(ps2_surface_exa_mono_source(&mono,&output)==1);
    unsigned char expected[256];assert(output.bytes<=sizeof(expected));
    struct pixels *s=model(sh);seed(s);
    pixman_region_fini(&clip_region);pixman_region_init_rect(&clip_region,0,0,43,32);
    gc.depth=1;gc.funcs->ValidateGC(&gc,GCAllBits,&window.drawable);gc.fgPixel=1;gc.bgPixel=0;gc.planemask=1;
    for(unsigned alu=0;alu<16;++alu){gc.alu=alu;memset((void*)output.bits,0xa7,output.bytes);memcpy(expected,output.bits,output.bytes);
        for(unsigned y=1;y<25;++y)for(unsigned x=3;x<38;++x){unsigned dm=BITMAP_BIT_ORDER==MSBFirst?0x80U>>(x%8):1U<<(x%8);
            uint32_t sv=!!(s->data[(y+4)*s->width+x+4]&16),dv=!!(expected[y*pitch+x/8]&dm);
            unsigned bit=oracle(alu,sv,dv)&1;expected[y*pitch+x/8]=(expected[y*pitch+x/8]&~dm)|(bit?dm:0);}
        reading_source=sh;RegionPtr exposure=gc.ops->CopyPlane(&source.drawable,&mono.drawable,&gc,7,5,35,24,3,1,16);
        if(exposure)RegionDestroy(exposure);
        assert(!memcmp(expected,output.bits,output.bytes));
    }
    exa.driver.DestroyPixmap(&xs,mono.devPrivates);assert(!ps2_surface_destroy(gs,sh));polygon_finish();
}
static void rgba_overlap_boundaries(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    const unsigned widths[]={1,2,3,7,63,64,65,71};
    for(unsigned i=0;i<sizeof(widths)/sizeof(*widths);++i){unsigned width=widths[i];uint32_t h;
        assert(!ps2_surface_create(gs,width,3,32,&h) && !ps2_surface_set_depth(gs,h,32));
        struct pixels *d=model(h);uint32_t original[71*3];
        struct ps2_pipe_copy_box box={0,0,0,0,width,3};
        for(unsigned alu=0;alu<16;++alu){
            for(unsigned j=0;j<width*3;++j)d->data[j]=original[j]=0xa58fc329U^(j*0x01030507U);
            assert(!ps2_pipe_plane_copy_boxes(ctx,h,h,&box,1,0x80000000,0x817f3b19,0xf1392b47,UINT32_MAX,alu));
            for(unsigned j=0;j<width*3;++j){uint32_t selected=original[j]&0x80000000?0x817f3b19:0xf1392b47;
                assert(d->data[j]==oracle(alu,selected,original[j]));}
        }
        assert(!ps2_surface_destroy(gs,h));
    }
    assert(!readbacks);finish_rop();
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"rgbx"))all_planes(32,32,0);
    else if(!strcmp(argv[1],"rgb555"))all_planes(16,16,0);
    else if(!strcmp(argv[1],"cross-depth")){all_planes(16,32,0);all_planes(32,16,0);}
    else if(!strcmp(argv[1],"planes")){all_planes(16,16,1);all_planes(32,32,1);}
    else if(!strcmp(argv[1],"overlap"))plane_overlap(0);
    else if(!strcmp(argv[1],"bands"))plane_overlap(1);
    else if(!strcmp(argv[1],"clipping"))plane_frontend(0);
    else if(!strcmp(argv[1],"window"))plane_frontend(1);
    else if(!strcmp(argv[1],"random"))random_plane(0);
    else if(!strcmp(argv[1],"streaming"))random_plane(1);
    else if(!strcmp(argv[1],"preflight"))plane_guards(0,0);
    else if(!strcmp(argv[1],"noop"))plane_guards(1,0);
    else if(!strcmp(argv[1],"allocation"))plane_guards(0,1);
    else if(!strcmp(argv[1],"failure"))plane_failure();
    else if(!strcmp(argv[1],"snapshot"))plane_snapshot();
    else if(!strcmp(argv[1],"mono-rgbx"))mono_destination(32);
    else if(!strcmp(argv[1],"mono-rgb555"))mono_destination(16);
    else if(!strcmp(argv[1],"rgba-overlap"))rgba_overlap_boundaries();
    else return 2;
    puts("PS2 colour CopyPlane PASS");return 0;
}
