/* SPDX-License-Identifier: MIT */
#include "previous-polygon.c"
#include "ps2_rop.h"
#include "ps2_gs_lut.h"
#include "ps2_readback.h"
#include "servermd.h"
static unsigned transfer_calls,rop_edge_rounding;
static unsigned rgba_enabled;
static int transfer_failure;
static float number(uint32_t v){float f;memcpy(&f,&v,4);return f;}
static uint32_t expanded(uint32_t v,unsigned bpp)
{return bpp==32?v&0xffffff:((v&31)<<3)|(((v>>5)&31)<<11)|(((v>>10)&31)<<19);}
static unsigned model_bpp(const struct pixels *p)
{return ps2_surface_lookup(ps2_pipe_context_render(ctx)->gs,p->handle)->bits_per_pixel;}
int __wrap_ioctl(int fd,unsigned long op,...)
{
    va_list args;va_start(args,op);void *arg=va_arg(args,void*);va_end(args);
    if(op==PS2GS_CHANNEL_MAP_BLIT){
        const struct ps2gs_channel_map_blit *r=arg;
        if(transfer_failure){errno=EIO;return -1;}
        struct pixels *s=model(r->source),*d=model(r->destination),*p=model(r->palette);
        assert(r->width<=64-(r->sx&63) && r->height<=32-(r->sy&31));
        for(unsigned y=0;y<r->height;++y)for(unsigned x=0;x<r->width;++x){
            unsigned i=(s->data[(r->sy+y)*s->width+r->sx+x]>>(r->source_channel*8))&255;
            i=(i&~24U)|((i&8)<<1)|((i&16)>>1);
            unsigned di=(r->dy+y)*d->width+r->dx+x;
            d->data[di]=(d->data[di]&~r->plane_mask)|(p->data[i]&r->plane_mask);
        }
        return 0;
    }
    if(op==PS2GS_SURFACE_DOWNLOAD){const struct ps2gs_surface_download *r=arg;struct pixels *p=model(r->handle);
        unsigned cpp=model_bpp(p)/8;unsigned char *out=(void*)(uintptr_t)r->pixels;
        assert(r->bytes==r->width*r->height*cpp);
        for(unsigned y=0;y<r->height;++y)for(unsigned x=0;x<r->width;++x)
            memcpy(out+(y*r->width+x)*cpp,p->data+(r->y+y)*p->width+r->x+x,cpp);
        return 0;}
    if(op!=PS2GS_TEXTURE_TRANSFER && op!=PS2GS_TEXTURE_RGBA_TRANSFER){int result=polygon_ioctl(fd,op,arg);
        if(!result && op==PS2GS_GET_CAPS){((struct ps2gs_caps*)arg)->features|=PS2GS_HW_TEXTURE_TRANSFER|PS2GS_HW_READBACK;
            if(rgba_enabled)((struct ps2gs_caps*)arg)->features|=PS2GS_HW_CHANNEL_MAP_BLIT|PS2GS_HW_CHANNEL_MAP|PS2GS_HW_CHANNEL_LUT|PS2GS_HW_RGBA_TRANSFER;}
        return result;}
    assert(fd==999);++transfer_calls;
    if(transfer_failure){errno=EIO;return -1;}
    const struct ps2gs_texture_transfer *r=arg;const struct ps2gs_texture_submit *draw=&r->draw;
    struct pixels *s=model(draw->texture),*d=model(draw->target);
    unsigned tw=1,th=1;while(tw<s->width)tw*=2;while(th<s->height)th*=2;
    const struct ps2gs_texture_triangle *t=(void*)(uintptr_t)draw->triangles;
    assert(draw->count==2 && !draw->depth && s!=d);
    int x1=t[0].v[0].position.x/16,y1=t[0].v[0].position.y/16;
    int x2=t[0].v[2].position.x/16,y2=t[0].v[2].position.y/16;
    float u1=number(t[0].v[0].s)*tw,v1=number(t[0].v[0].t)*th;
    float u2=number(t[0].v[2].s)*tw,v2=number(t[0].v[2].t)*th;
    for(int y=y1;y<y2;++y)for(int x=x1;x<x2;++x){
        /* Independently exercise both fragment-centre sampling and a tiny
         * downward rounding error at raster edges, as observed on native GS
         * copies at width41. Quarter-texel command coordinates must keep both
         * within the same nearest source pixel. */
        float phase=rop_edge_rounding?0.f:.5f,error=rop_edge_rounding?1.f/65536.f:0.f;
        int u=u1-error+((float)x-x1+phase)*(u2-u1)/(x2-x1),
            v=v1-error+((float)y-y1+phase)*(v2-v1)/(y2-y1);
        assert(u>=0 && v>=0 && u<(int)s->width && v<(int)s->height);
        unsigned index=y*d->width+x;uint32_t sv=op==PS2GS_TEXTURE_RGBA_TRANSFER?s->data[v*s->width+u]:expanded(s->data[v*s->width+u],model_bpp(s)),
            dv=expanded(d->data[index],model_bpp(d)),out=0;
        for(unsigned c=0;c<(op==PS2GS_TEXTURE_RGBA_TRANSFER?4U:3U);++c){unsigned source=(sv>>(c*8))&255,dest=(dv>>(c*8))&255;
            if(t[0].flags&PS2GS_TEXTURE_MODULATE){source=source*((t[0].v[0].position.rgba>>(c*8))&255)/128;if(source>255)source=255;}
            unsigned result=op==PS2GS_TEXTURE_RGBA_TRANSFER?
                (r->mode==PS2GS_RGBA_MULTIPLY && c<3?(source*(sv>>24)/128>255?255:source*(sv>>24)/128):source):
                r->mode==PS2GS_TRANSFER_ADD?(source+dest)&255:
                r->mode==PS2GS_TRANSFER_ADD_CLAMP?(source+dest>255?255:source+dest):
                r->mode==PS2GS_TRANSFER_SUBTRACT?(dest>=source?dest-source:0):source;
            out|=result<<(c*8);}
        if(model_bpp(d)==16)out=((out>>3)&31)|(((out>>11)&31)<<5)|(((out>>19)&31)<<10);
        d->data[index]=(d->data[index]&~r->plane_mask)|(out&r->plane_mask);
    }
    return 0;
}
static uint32_t oracle(unsigned alu,uint32_t s,uint32_t d)
{
    switch(alu){case 0:return 0;case 1:return s&d;case 2:return s&~d;case 3:return s;
    case 4:return ~s&d;case 5:return d;case 6:return s^d;case 7:return s|d;
    case 8:return ~(s|d);case 9:return ~(s^d);case 10:return ~d;case 11:return s|~d;
    case 12:return ~s;case 13:return ~s|d;case 14:return ~(s&d);case 15:return ~0U;default:abort();}
}
static uint32_t source_handle;
static void setup_rop(unsigned bpp)
{
    polygon_setup(rgba_enabled,bpp);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_surface_create(r->gs,83,61,bpp,&source_handle));
}
static void finish_rop(void)
{assert(!ps2_surface_destroy(ps2_pipe_context_render(ctx)->gs,source_handle));polygon_finish();}
static void check(unsigned alu,uint32_t mask,uint32_t sh,uint32_t dh,const struct ps2_pipe_copy_box *boxes,unsigned n)
{
    struct pixels *s=model(sh),*d=model(dh);size_t bytes=d->width*d->height*4,sbytes=s->width*s->height*4;
    uint32_t *expected=malloc(bytes),*saved=malloc(sbytes);assert(expected && saved);
    memcpy(expected,d->data,bytes);memcpy(saved,s->data,sbytes);
    for(unsigned i=0;i<n;++i)for(int y=0;y<boxes[i].height;++y)for(int x=0;x<boxes[i].width;++x){
        unsigned si=(boxes[i].sy+y)*s->width+boxes[i].sx+x,di=(boxes[i].dy+y)*d->width+boxes[i].dx+x;
        expected[di]=(expected[di]&~mask)|(oracle(alu,saved[si],expected[di])&mask);}
    assert(!ps2_pipe_rop_copy_boxes(ctx,sh,dh,boxes,n,mask,alu));
    for(unsigned i=0;i<d->width*d->height;++i)if((expected[i]^d->data[i])&
        (alu==3 && mask==(model_bpp(d)==16?0x7fffU:0xffffffU)?mask:~0U)){
        fprintf(stderr,"ROP mismatch alu=%u bpp=%u mask=%x index=%u expected=%x actual=%x\n",alu,model_bpp(d),mask,i,expected[i],d->data[i]);abort();}
    free(saved);free(expected);
}
static void all(unsigned bpp,int partial)
{
    setup_rop(bpp);struct pixels *s=model(source_handle),*d=model(destination_handle());
    for(unsigned i=0;i<s->width*s->height;++i)s->data[i]=(i*8191+0x814181)&(bpp==16?0x7fff:0xffffff);
    const struct ps2_pipe_copy_box box={2,1,7,5,71,43};
    for(unsigned alu=0;alu<16;++alu){seed(d);check(alu,partial?(bpp==16?0x5249:0x81a153):(bpp==16?0x7fff:0xffffff),source_handle,destination_handle(),&box,1);}
    finish_rop();
}
static void rop_overlap(int bands)
{
    setup_rop(32);struct pixels *d=model(destination_handle());
    for(unsigned alu=0;alu<16;++alu)for(unsigned direction=0;direction<4;++direction){seed(d);
        int sx=direction&1?3:9,sy=direction&2?2:7,dx=direction&1?9:3,dy=direction&2?7:2;
        struct ps2_pipe_copy_box boxes[3]={{sx,sy,dx,dy,70,39},{sx,sy,dx,dy,23,16},{sx+30,sy,dx+30,dy,40,16}};
        check(alu,0x81a153,destination_handle(),destination_handle(),bands?boxes+1:boxes,bands?2:1);}
    finish_rop();
}
static void rop_preflight(int noop)
{
    setup_rop(32);struct ps2_render_client *r=ps2_pipe_context_render(ctx);
    assert(!ps2_render_fill(r,0,0,1,1,0x80332211));unsigned before=attempts,tc=transfer_calls;
    struct ps2_pipe_copy_box box={0,0,1,1,10,10};
    if(noop){assert(!ps2_pipe_rop_copy_boxes(ctx,source_handle,0,&box,1,0,6));assert(!ps2_pipe_rop_copy_boxes(ctx,source_handle,0,&box,1,0xffffff,5));}
    else{box.width=INT32_MAX;assert(ps2_pipe_rop_copy_boxes(ctx,source_handle,0,&box,1,0xffffff,6)<0 && errno==EINVAL);}
    assert(r->count==1 && attempts==before && transfer_calls==tc);finish_rop();
}
static void rop_failure(void)
{
    setup_rop(32);struct ps2_pipe_copy_box box={0,0,1,1,10,10};transfer_failure=1;
    assert(ps2_pipe_rop_copy_boxes(ctx,source_handle,0,&box,1,0xffffff,6)<0 && errno==EIO);
    unsigned tc=transfer_calls;transfer_failure=0;
    assert(ps2_pipe_rop_copy_boxes(ctx,source_handle,0,&box,1,0xffffff,6)<0 && transfer_calls==tc);
    polygon_finish();
}
static void exhaustive(void)
{
    setup_rop(32);struct pixels *s=model(source_handle),*d=model(destination_handle());
    struct ps2_pipe_copy_box box={0,0,0,0,64,32};
    for(unsigned alu=0;alu<16;++alu)for(unsigned page=0;page<32;++page){
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x){unsigned pair=page*2048+y*64+x,sv=pair>>8,dv=pair&255;
            s->data[y*s->width+x]=sv*0x010101U;d->data[y*d->width+x]=0x55000000|dv*0x010101U;}
        check(alu,0xffffff,source_handle,destination_handle(),&box,1);}
    finish_rop();
}
static unsigned modulo(int value,unsigned n){int v=value%(int)n;return (unsigned)(v<0?v+(int)n:v);}
static void tiled_rop(unsigned bpp)
{
    setup_rop(bpp);struct pixels *s=model(source_handle),*d=model(destination_handle());
    for(unsigned i=0;i<s->width*s->height;++i)s->data[i]=(i*913+0x318155)&(bpp==16?0x7fff:0xffffff);
    struct ps2_pipe_solid_box box={5,4,81,47};uint32_t mask=bpp==16?0x5269:0x819135;
    size_t bytes=d->width*d->height*4;uint32_t *expected=malloc(bytes);assert(expected);
    for(unsigned alu=0;alu<16;++alu){seed(d);memcpy(expected,d->data,bytes);
        for(int y=box.y1;y<box.y2;++y)for(int x=box.x1;x<box.x2;++x){unsigned si=modulo(y-17,s->height)*s->width+modulo(x+19,s->width),di=y*d->width+x;
            expected[di]=(expected[di]&~mask)|(oracle(alu,s->data[si],expected[di])&mask);}
        assert(!ps2_pipe_tile_logic_geometry(ctx,source_handle,destination_handle(),&box,1,-19,17,mask,alu));
        assert(!memcmp(expected,d->data,bytes));}
    free(expected);finish_rop();
}
static void downloaded(unsigned bpp)
{
    setup_rop(bpp);struct pixels *s=model(source_handle);for(unsigned i=0;i<s->width*s->height;++i)s->data[i]=(i*8191+0x818141)&(bpp==16?0x7fff:0xffffff);
    unsigned cpp=bpp/8,w=71,h=43,stride=w*cpp+13;size_t bytes=(h-1)*stride+w*cpp;
    unsigned char *out=malloc(bytes+32);assert(out);memset(out,0xa7,bytes+32);
    uint32_t mask=bpp==16?0x5269:0x819135;
    assert(!ps2_pipe_download(ctx,source_handle,5,7,w,h,mask,out,bytes,stride));
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){uint32_t v=0;memcpy(&v,out+y*stride+x*cpp,cpp);assert(v==(s->data[(y+7)*s->width+x+5]&mask));}
    for(unsigned y=0;y<h-1;++y)for(unsigned x=w*cpp;x<stride;++x)assert(out[y*stride+x]==0xa7);
    for(unsigned i=0;i<32;++i)assert(out[bytes+i]==0xa7);
    free(out);finish_rop();
}
static void readback_records(unsigned bpp)
{
    for(unsigned owned=0;owned<2;++owned){polygon_setup(owned,bpp);struct pixels *p=model(destination_handle());
        for(unsigned i=0;i<p->width*p->height;++i)p->data[i]=(i*8191+0x818141)&(bpp==16?0x7fff:0xffffff);
        unsigned char out[512];unsigned w=17,h=3,cpp=bpp/8,stride=(w*cpp+3)&~3U;
        uint32_t mask=bpp==16?0x5269:0x819135;
        int sx=window.drawable.x+3-xp.screen_x,sy=window.drawable.y+2-xp.screen_y;
        memset(out,0xa7,sizeof(out));ps2_readback_image(ctx,&window.drawable,3,2,w,h,ZPixmap,mask,(char*)out);
        for(unsigned y=0;y<h;++y){for(unsigned x=0;x<w;++x){uint32_t v=0;memcpy(&v,out+y*stride+x*cpp,cpp);assert(v==(p->data[(sy+y)*p->width+sx+x]&mask));}
            for(unsigned x=w*cpp;x<stride;++x)assert(!out[y*stride+x]);}
        for(unsigned i=stride*h;i<sizeof(out);++i)assert(out[i]==0xa7);
        for(unsigned bit=0;bit<window.drawable.depth;++bit){memset(out,0xa7,sizeof(out));
            ps2_readback_image(ctx,&window.drawable,3,2,w,h,XYPixmap,1UL<<bit,(char*)out);
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<32;++x){unsigned actual=!!(out[y*4+x/8]&(BITMAP_BIT_ORDER==MSBFirst?0x80U>>(x%8):1U<<(x%8)));
                unsigned expected=x<w?!!(p->data[(sy+y)*p->width+sx+x]&(1U<<bit)):0;assert(actual==expected);}
            for(unsigned i=h*4;i<sizeof(out);++i)assert(out[i]==0xa7);}
        DDXPointRec points[]={{sx+xp.screen_x,sy+xp.screen_y},{sx+xp.screen_x+7,sy+xp.screen_y+1}};int widths[]={17,5};
        memset(out,0xa7,sizeof(out));ps2_readback_spans(ctx,&window.drawable,17,points,widths,2,(char*)out);
        unsigned offset=0;for(unsigned i=0;i<2;++i){for(int x=0;x<widths[i];++x){uint32_t v=0;memcpy(&v,out+offset+x*cpp,cpp);assert(v==p->data[(sy+i)*p->width+sx+(i?7:0)+x]);}
            unsigned row=(widths[i]*cpp+3)&~3U;for(unsigned x=widths[i]*cpp;x<row;++x)assert(!out[offset+x]);offset+=row;}
        for(unsigned i=offset;i<sizeof(out);++i)assert(out[i]==0xa7);
        ps2_readback_image(ctx,&window.drawable,0,0,0,0,ZPixmap,0,NULL);
        ps2_readback_spans(ctx,&window.drawable,0,NULL,NULL,0,NULL);
        polygon_finish();}
}
static void small_sources(void)
{
    for(unsigned bpp=16;bpp<=32;bpp+=16){setup_rop(bpp);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
        unsigned widths[]={1,2,3,5,7,9,19,63};uint32_t mask=bpp==16?0x7fff:0xffffff;
        for(unsigned i=0;i<sizeof(widths)/sizeof(*widths);++i){uint32_t source;assert(!ps2_surface_create(gs,widths[i],11,bpp,&source));
            struct pixels *s=model(source),*d=model(destination_handle());for(unsigned p=0;p<s->width*s->height;++p)s->data[p]=(p*8191+0x815171)&mask;
            struct ps2_pipe_copy_box box={0,0,5,4,widths[i],11};
            for(unsigned alu=0;alu<16;++alu){seed(d);check(alu,mask&0x518135,source,destination_handle(),&box,1);}
            unsigned char out[8192];unsigned stride=widths[i]*(bpp/8);
            assert(!ps2_pipe_download(ctx,source,0,0,widths[i],11,mask,out,stride*11,stride));
            for(unsigned p=0;p<widths[i]*11;++p){uint32_t v=0;memcpy(&v,out+p*(bpp/8),bpp/8);assert(v==s->data[p]);}
            struct ps2_pipe_solid_box tiled={3,2,38,29};seed(d);
            assert(!ps2_pipe_tile_logic_geometry(ctx,source,destination_handle(),&tiled,1,-3,2,mask&0x518135,GXxor));
            assert(!ps2_surface_destroy(gs,source));}
        finish_rop();}
}
static void rgba_copy(const char *name)
{
    rgba_enabled=1;setup_rop(32);
    struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    assert(!ps2_surface_set_depth(gs,source_handle,32));assert(!ps2_surface_set_depth(gs,destination_handle(),32));
    struct pixels *s=model(source_handle),*d=model(destination_handle());
    uint32_t masks[]={UINT32_MAX,0x5731a5cf,0xff000000,0x80000000,0xffffff};
    if(!strcmp(name,"rgba-overlap")){
        for(unsigned alu=0;alu<16;++alu)for(unsigned direction=0;direction<4;++direction){
            for(unsigned i=0;i<d->width*d->height;++i)d->data[i]=i*0x03050917U+0x5781a5cf;
            int sx=direction&1?3:9,sy=direction&2?2:7,dx=direction&1?9:3,dy=direction&2?7:2;
            struct ps2_pipe_copy_box box={sx,sy,dx,dy,70,39};
            check(alu,0x5731a5cf,destination_handle(),destination_handle(),&box,1);
        }
    }else if(!strcmp(name,"rgba-alpha-pairs")){
        uint32_t a=0,b=0;assert(!ps2_surface_create(gs,256,256,32,&a));assert(!ps2_surface_create(gs,256,256,32,&b));
        assert(!ps2_surface_set_depth(gs,a,32));assert(!ps2_surface_set_depth(gs,b,32));
        struct pixels *sa=model(a),*da=model(b);struct ps2_pipe_copy_box box={0,0,0,0,256,256};
        for(unsigned alu=0;alu<16;++alu){for(unsigned i=0;i<65536;++i){sa->data[i]=(i>>8)<<24|0x5173a5;
            da->data[i]=(i&255)<<24|0x739151;}check(alu,0xff000000,a,b,&box,1);}
        assert(!ps2_surface_destroy(gs,a));assert(!ps2_surface_destroy(gs,b));
    }else{
        for(unsigned alu=0;alu<16;++alu)for(unsigned mask=0;mask<sizeof(masks)/sizeof(*masks);++mask){
            for(unsigned i=0;i<s->width*s->height;++i)s->data[i]=i*0x03050917U+0x5781a5cf;
            for(unsigned i=0;i<d->width*d->height;++i)d->data[i]=i*0x05071923U+0xa5cf5713;
            struct ps2_pipe_copy_box box={2,1,7,5,71,43};check(alu,masks[mask],source_handle,destination_handle(),&box,1);
        }
        uint32_t tiny=0;assert(!ps2_surface_create(gs,1,1,32,&tiny));assert(!ps2_surface_set_depth(gs,tiny,32));
        model(tiny)->data[0]=0x5173a5cf;struct ps2_pipe_copy_box box={0,0,3,7,1,1};
        for(unsigned alu=0;alu<16;++alu)check(alu,UINT32_MAX,tiny,destination_handle(),&box,1);
        assert(!ps2_surface_destroy(gs,tiny));
    }
    finish_rop();
}
static void rgba_download(void)
{
    rgba_enabled=1;setup_rop(32);
    struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    const unsigned widths[]={1,2,3,63,64,65,71,127};
    for(unsigned wi=0;wi<sizeof(widths)/sizeof(*widths);++wi){uint32_t handle=0;
        unsigned width=widths[wi],height=43;
        assert(!ps2_surface_create(gs,width,height,32,&handle));assert(!ps2_surface_set_depth(gs,handle,32));
        struct pixels *s=model(handle);
        for(unsigned i=0;i<width*height;++i)s->data[i]=0xa58fc329U^(i*0x01030507U);
        for(unsigned edge=0;edge<2;++edge){unsigned x=edge?width-1:0,w=edge?1:width;
            size_t stride=w*4+13,bytes=(height-1)*stride+w*4;
            unsigned char *out=malloc(bytes+32);assert(out);memset(out,0xa7,bytes+32);
            assert(!ps2_pipe_download(ctx,handle,x,0,w,height,UINT32_MAX,out,bytes,stride));
            for(unsigned y=0;y<height;++y){for(unsigned xx=0;xx<w;++xx){uint32_t v;
                memcpy(&v,out+y*stride+xx*4,4);assert(v==s->data[y*width+x+xx]);}
                if(y+1<height)for(unsigned xx=w*4;xx<stride;++xx)assert(out[y*stride+xx]==0xa7);}
            for(unsigned i=0;i<32;++i)assert(out[bytes+i]==0xa7);
            free(out);
        }
        assert(!ps2_surface_destroy(gs,handle));
    }
    finish_rop();
}
static void rgba_solids(void)
{
    rgba_enabled=1;setup_rop(32);struct ps2_surface_client *gs=ps2_pipe_context_render(ctx)->gs;
    uint32_t handle=destination_handle();assert(!ps2_surface_set_depth(gs,handle,32));struct pixels *d=model(handle);
    uint32_t masks[]={UINT32_MAX,0x5731a5cf,0xff000000,0x80000000,0xffffff};
    for(unsigned alu=0;alu<16;++alu)for(unsigned m=0;m<sizeof(masks)/sizeof(*masks);++m){
        uint32_t *wanted=malloc(d->width*d->height*4);assert(wanted);
        for(unsigned i=0;i<d->width*d->height;++i)d->data[i]=wanted[i]=i*0x05071923U+0xa5cf5713;
        for(unsigned y=7;y<54;++y)for(unsigned x=3;x<80;++x){unsigned i=y*d->width+x;
            wanted[i]=(wanted[i]&~masks[m])|(oracle(alu,0x5173a5cf,wanted[i])&masks[m]);}
        struct ps2_pipe_solid_box box={3,7,80,54};
        assert(!ps2_pipe_solid_rectangles(ctx,handle,&box,1,0x5173a5cf,masks[m],alu));
        for(unsigned i=0;i<d->width*d->height;++i)assert(d->data[i]==wanted[i]);
        free(wanted);
    }
    finish_rop();
}
static void nearest_edges(unsigned bpp)
{
    rop_edge_rounding=1;setup_rop(bpp);
    struct pixels *s=model(source_handle),*d=model(destination_handle());
    for(unsigned i=0;i<s->width*s->height;++i)s->data[i]=(i*8191+0x814181)&(bpp==16?0x7fff:0xffffff);
    const unsigned widths[]={1,2,3,4,7,8,9,10,11,15,16,17,19,31,32,33,39,40,41,63,64,65};
    for(unsigned wi=0;wi<sizeof(widths)/sizeof(*widths);++wi)
    for(unsigned offset=0;offset<2;++offset)for(unsigned alu=0;alu<16;++alu){
        struct ps2_pipe_copy_box box={offset?3:0,offset?2:0,7,5,widths[wi],offset?11:3};
        seed(d);check(alu,bpp==16?0x5249:0x81a153,source_handle,destination_handle(),&box,1);
    }
    finish_rop();downloaded(bpp);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"nearest-edge-rgbx"))nearest_edges(32);
    else if(!strcmp(argv[1],"nearest-edge-rgb555"))nearest_edges(16);
    else if(!strcmp(argv[1],"all-rgbx"))all(32,0);
    else if(!strcmp(argv[1],"all-rgb555"))all(16,0);
    else if(!strcmp(argv[1],"masks")){all(32,1);all(16,1);}
    else if(!strcmp(argv[1],"overlap"))rop_overlap(0);
    else if(!strcmp(argv[1],"bands"))rop_overlap(1);
    else if(!strcmp(argv[1],"preflight"))rop_preflight(0);
    else if(!strcmp(argv[1],"noop"))rop_preflight(1);
    else if(!strcmp(argv[1],"failure"))rop_failure();
    else if(!strcmp(argv[1],"exhaustive"))exhaustive();
    else if(!strcmp(argv[1],"tiles-rgbx"))tiled_rop(32);
    else if(!strcmp(argv[1],"tiles-rgb555"))tiled_rop(16);
    else if(!strcmp(argv[1],"download-rgbx"))downloaded(32);
    else if(!strcmp(argv[1],"download-rgb555"))downloaded(16);
    else if(!strcmp(argv[1],"readback-records-rgbx"))readback_records(32);
    else if(!strcmp(argv[1],"readback-records-rgb555"))readback_records(16);
    else if(!strcmp(argv[1],"small-sources"))small_sources();
    else if(!strcmp(argv[1],"rgba-download"))rgba_download();
    else if(!strcmp(argv[1],"rgba-solids"))rgba_solids();
    else if(!strncmp(argv[1],"rgba-",5))rgba_copy(argv[1]);
    else return 2;
    puts("PS2 GPU-only Boolean copies PASS");return 0;
}
