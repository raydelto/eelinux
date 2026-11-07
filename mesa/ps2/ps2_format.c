/* SPDX-License-Identifier: MIT */
#include "ps2_format.h"
#include "ps2_coverage.h"
#include "ps2_gs_lut.h"
#include "ps2_gs_logic.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
int ps2_format_release(struct ps2_format_surface *f)
{
    int status=0,error=errno;
    uint32_t handles[]={f->rgba,f->packed,f->palette,f->scratch,f->words,f->surface};
    for(unsigned i=0;i<sizeof(handles)/sizeof(*handles);++i)
        if(handles[i] && !f->gs->error && ps2_surface_destroy(f->gs,handles[i])){
            if(!status)error=errno;
            status=-1;
        }
    memset(f,0,sizeof(*f));errno=error;return status;
}
int ps2_format_decode444(struct ps2_format_surface *f,struct ps2_surface_client *gs,uint32_t source,unsigned swap)
{
    if(!f || !gs){errno=EINVAL;return -1;}
    *f=(struct ps2_format_surface){.gs=gs,.swap=swap};
    const struct ps2_surface *s=ps2_surface_lookup(gs,source);
    if(!s || s->bits_per_pixel!=16 || ps2_surface_depth(s)!=15 || swap>1 || s->width>1024 || s->height>1024){
        errno=EINVAL;return -1;
    }
    if(!(gs->caps.features&PS2GS_HW_CHANNEL_MAP_BLIT) || !(gs->caps.features&PS2GS_HW_TEXTURE_TRANSFER)){
        errno=ENOTSUP;return -1;
    }
    f->original=*s;
    if(ps2_surface_create(gs,s->width,s->height,32,&f->surface) ||
        ps2_surface_create(gs,64,32,32,&f->words) || ps2_surface_create(gs,64,32,32,&f->scratch) ||
        ps2_surface_create(gs,16,16,32,&f->palette))goto fail;
    uint32_t table[256];
    for(unsigned y=0;y<s->height;y+=32)for(unsigned x=0;x<s->width;x+=64){
        unsigned w=s->width-x,h=s->height-y;if(w>64)w=64;if(h>32)h=32;
        if(ps2_rop_sample(gs,source,f->scratch,x,y,0,0,w,h,0xffffff,0,128))goto fail;
        for(unsigned ch=0;ch<3;++ch){
            for(unsigned i=0;i<256;++i)table[i]=(i>>3)<<(ch*5);
            if(ps2_surface_map_palette(gs,f->palette,table) ||
                ps2_surface_channel_map_region(gs,f->scratch,f->words,f->palette,0,0,0,0,w,h,31U<<(ch*5),ch))goto fail;
        }
        for(unsigned ch=0;ch<3;++ch){unsigned shift=(swap?2-ch:ch)*4;
            for(unsigned i=0;i<256;++i)table[i]=((i>>(shift%8))&15)*17U<<(ch*8);
            if(ps2_surface_map_palette(gs,f->palette,table) ||
                ps2_surface_channel_map_region(gs,f->words,f->surface,f->palette,0,0,x,y,w,h,255U<<(ch*8),shift/8))goto fail;
        }
    }
    return 0;
fail:
    {int error=errno;ps2_format_release(f);errno=error;return -1;}
}
int ps2_format_encode444(struct ps2_format_surface *f)
{
    if(!f || !f->gs || !f->surface || !ps2_surface_lookup(f->gs,f->original.handle)){
        errno=EINVAL;return -1;
    }
    struct ps2_surface_client *gs=f->gs;uint32_t table[256];
    for(unsigned y=0;y<f->original.height;y+=32)for(unsigned x=0;x<f->original.width;x+=64){
        unsigned w=f->original.width-x,h=f->original.height-y;if(w>64)w=64;if(h>32)h=32;
        for(unsigned ch=0;ch<3;++ch){unsigned shift=(f->swap?2-ch:ch)*4;
            for(unsigned i=0;i<256;++i)table[i]=(i>>4)<<shift;
            if(ps2_surface_map_palette(gs,f->palette,table) ||
                ps2_surface_channel_map_region(gs,f->surface,f->words,f->palette,x,y,0,0,w,h,15U<<shift,ch))return -1;
        }
        /* Repack the raw word into CT16's five-bit RGB texture lanes. The
         * transfer preserves the destination's unused high three core bits. */
        for(unsigned i=0;i<256;++i)table[i]=((i&31)<<3)|((i>>5)<<11);
        if(ps2_surface_map_palette(gs,f->palette,table) ||
            ps2_surface_channel_map_region(gs,f->words,f->scratch,f->palette,0,0,0,0,w,h,0x38f8,0))return -1;
        for(unsigned i=0;i<256;++i)table[i]=((i&3)<<14)|(((i>>2)&31)<<19);
        if(ps2_surface_map_palette(gs,f->palette,table) ||
            ps2_surface_channel_map_region(gs,f->words,f->scratch,f->palette,0,0,0,0,w,h,0xf8c000,1) ||
            ps2_rop_sample(gs,f->scratch,f->original.handle,0,0,x,y,w,h,0xfff,0,128))return -1;
    }
    return 0;
}
static void byte_layout(unsigned format,unsigned bits[4],unsigned shifts[4])
{
    if(format>=PS2_WORD_RGB565){
        unsigned swap=format==PS2_WORD_BGR565 || format==PS2_WORD_ABGR1555 ||
            format==PS2_WORD_ABGR4444 || format==PS2_WORD_BGR555 || format==PS2_WORD_BGR444;
        unsigned n=format>=PS2_WORD_RGB444 || format==PS2_WORD_ARGB4444 || format==PS2_WORD_ABGR4444?4:5;
        bits[0]=bits[2]=n;bits[1]=format<=PS2_WORD_BGR565?6:n;
        bits[3]=format==PS2_WORD_ARGB1555 || format==PS2_WORD_ABGR1555?1:
            format==PS2_WORD_ARGB4444 || format==PS2_WORD_ABGR4444?4:0;
        shifts[0]=swap?0:n+bits[1];shifts[1]=n;shifts[2]=swap?n+bits[1]:0;
        shifts[3]=2*n+bits[1];
    }else if(format<PS2_BYTE_ARGB2222){
        bits[0]=bits[1]=3;bits[2]=2;bits[3]=0;
        if(format==PS2_BYTE_RGB332){shifts[0]=5;shifts[1]=2;shifts[2]=0;}
        else{shifts[0]=0;shifts[1]=3;shifts[2]=6;}
        shifts[3]=0;
    }else{
        for(unsigned c=0;c<4;++c)bits[c]=2;
        shifts[0]=format==PS2_BYTE_ARGB2222?4:0;shifts[1]=2;
        shifts[2]=format==PS2_BYTE_ARGB2222?0:4;shifts[3]=6;
    }
}
static unsigned byte_expand(unsigned value,unsigned bits)
{
    unsigned n=value,used=bits;
    while(used<8){n=(n<<bits)|value;used+=bits;}
    return n>>(used-8);
}
int ps2_format_decode8(struct ps2_format_surface *f,struct ps2_surface_client *gs,
    const struct ps2_picture_bitmap *bytes,unsigned format)
{
    if(!f || !gs || !bytes){errno=EINVAL;return -1;}
    *f=(struct ps2_format_surface){.gs=gs,.byte_format=format,.original_bytes=*bytes};
    if(format>PS2_BYTE_ABGR2222 || !bytes->width || !bytes->height || bytes->width>1024 || bytes->height>1024 ||
        !bytes->bits || bytes->bits_per_pixel!=8 || bytes->stride<bytes->width || bytes->bytes<bytes->width ||
        bytes->bytes>1024*1024 || bytes->height-1>(bytes->bytes-bytes->width)/bytes->stride ||
        (uintptr_t)bytes->bits>UINTPTR_MAX-(bytes->bytes-1)){errno=EINVAL;return -1;}
    unsigned required=PS2GS_HW_CHANNEL_MAP_BLIT|PS2GS_HW_RGBA_TRANSFER;
    if((gs->caps.features&required)!=required){errno=ENOTSUP;return -1;}
    unsigned bits[4],shifts[4];byte_layout(format,bits,shifts);
    unsigned pw=(bytes->width+3)/4;
    if(ps2_surface_create(gs,bytes->width,bytes->height,32,&f->surface) ||
        (bits[3] && ps2_surface_set_depth(gs,f->surface,32)) || ps2_surface_create(gs,pw,bytes->height,32,&f->packed) ||
        ps2_surface_set_depth(gs,f->packed,32) || ps2_surface_create(gs,64,32,32,&f->words) ||
        ps2_surface_create(gs,64,32,32,&f->scratch) || ps2_surface_create(gs,64,32,32,&f->rgba) ||
        ps2_surface_create(gs,16,16,32,&f->palette))goto fail;
    uint32_t row[256];
    for(unsigned y=0;y<bytes->height;++y){
        memset(row,0,pw*4);memcpy(row,bytes->bits+y*bytes->stride,bytes->width);
        if(ps2_surface_upload(gs,f->packed,0,y,pw,1,row,pw*4,pw*4))goto fail;
    }
    for(unsigned y=0;y<bytes->height;++y)if(ps2_format_refresh_row(f,y))goto fail;
    return 0;
fail:
    {int error=errno;ps2_format_release(f);errno=error;return -1;}
}
int ps2_format_encode8(struct ps2_format_surface *f,struct pipe_context *ctx,unsigned char *bytes,size_t length,size_t stride)
{
    if(!f || !f->gs || !f->packed || f->byte_format>PS2_BYTE_ABGR2222 || !ctx || !bytes || stride<f->original_bytes.width || length<f->original_bytes.width ||
        f->original_bytes.height-1>(length-f->original_bytes.width)/stride ||
        (uintptr_t)bytes>UINTPTR_MAX-(length-1)){errno=EINVAL;return -1;}
    unsigned width=f->original_bytes.width,height=f->original_bytes.height,pw=(width+3)/4;
    for(unsigned y=0;y<height;++y)if(ps2_format_pack_region(f,0,y,width))return -1;
    unsigned char *reply=malloc((size_t)pw*4*height);if(!reply){errno=ENOMEM;return -1;}
    int status=ps2_pipe_download(ctx,f->packed,0,0,pw,height,UINT32_MAX,reply,(size_t)pw*4*height,pw*4),error=errno;
    if(!status)for(unsigned y=0;y<height;++y)memcpy(bytes+y*stride,reply+y*pw*4,width);
    free(reply);errno=error;return status;
}
int ps2_format_decode16(struct ps2_format_surface *f,struct ps2_surface_client *gs,
    const struct ps2_picture_bitmap *bytes,unsigned format)
{
    if(!f || !gs || !bytes){errno=EINVAL;return -1;}
    *f=(struct ps2_format_surface){.gs=gs,.byte_format=format,.original_bytes=*bytes};
    size_t row=(size_t)bytes->width*2;
    if(format<PS2_WORD_RGB565 || format>PS2_WORD_BGR444 || !bytes->width || !bytes->height ||
        bytes->width>1024 || bytes->height>1024 || !bytes->bits || bytes->bits_per_pixel!=16 ||
        bytes->stride<row || bytes->bytes<row || bytes->bytes>1024*1024 ||
        bytes->height-1>(bytes->bytes-row)/bytes->stride ||
        (uintptr_t)bytes->bits>UINTPTR_MAX-(bytes->bytes-1)){errno=EINVAL;return -1;}
    unsigned required=PS2GS_HW_CHANNEL_MAP_BLIT|PS2GS_HW_RGBA_TRANSFER;
    if((gs->caps.features&required)!=required){errno=ENOTSUP;return -1;}
    unsigned bits[4],shifts[4];byte_layout(format,bits,shifts);
    unsigned pw=(bytes->width+1)/2;
    if(ps2_surface_create(gs,bytes->width,bytes->height,32,&f->surface) ||
        (bits[3] && ps2_surface_set_depth(gs,f->surface,32)) || ps2_surface_create(gs,pw,bytes->height,32,&f->packed) ||
        ps2_surface_set_depth(gs,f->packed,32) || ps2_surface_create(gs,64,32,32,&f->words) ||
        ps2_surface_create(gs,64,32,32,&f->scratch) || ps2_surface_create(gs,64,32,32,&f->rgba) ||
        ps2_surface_create(gs,16,16,32,&f->palette))goto fail;
    uint32_t packed[512];
    for(unsigned y=0;y<bytes->height;++y){
        memset(packed,0,pw*4);memcpy(packed,bytes->bits+y*bytes->stride,row);
        if(ps2_surface_upload(gs,f->packed,0,y,pw,1,packed,pw*4,pw*4))goto fail;
    }
    for(unsigned y=0;y<bytes->height;++y)if(ps2_format_refresh_row(f,y))goto fail;
    return 0;
fail:
    {int error=errno;ps2_format_release(f);errno=error;return -1;}
}
int ps2_format_encode16(struct ps2_format_surface *f,struct pipe_context *ctx,
    unsigned char *bytes,size_t length,size_t stride)
{
    if(!f || !f->gs || !f->packed || f->byte_format<PS2_WORD_RGB565 || f->byte_format>PS2_WORD_BGR444 ||
        !ctx || !bytes){errno=EINVAL;return -1;}
    size_t row=(size_t)f->original_bytes.width*2;
    if(stride<row || length<row || f->original_bytes.height-1>(length-row)/stride ||
        (uintptr_t)bytes>UINTPTR_MAX-(length-1)){errno=EINVAL;return -1;}
    unsigned width=f->original_bytes.width,height=f->original_bytes.height,pw=(width+1)/2;
    for(unsigned y=0;y<height;++y)if(ps2_format_pack_region(f,0,y,width))return -1;
    unsigned char *reply=malloc((size_t)pw*4*height);if(!reply){errno=ENOMEM;return -1;}
    int status=ps2_pipe_download(ctx,f->packed,0,0,pw,height,UINT32_MAX,reply,(size_t)pw*4*height,pw*4),error=errno;
    if(!status)for(unsigned y=0;y<height;++y)memcpy(bytes+y*stride,reply+y*pw*4,row);
    free(reply);errno=error;return status;
}

static int refresh8(struct ps2_format_surface *f,unsigned y)
{
    struct ps2_surface_client *gs=f->gs;uint32_t table[256];
    unsigned bits[4],shifts[4];byte_layout(f->byte_format,bits,shifts);
    for(unsigned i=0;i<256;++i){table[i]=0;
        for(unsigned c=0;c<3;++c)table[i]|=byte_expand((i>>shifts[c])&((1U<<bits[c])-1),bits[c])<<(c*8);}
    if(ps2_surface_map_palette(gs,f->palette,table))return -1;
    for(unsigned x=0;x<f->original_bytes.width;++x)
        if(ps2_surface_channel_map_region(gs,f->packed,f->surface,f->palette,x/4,y,x,y,1,1,0xffffff,x%4))return -1;
    if(bits[3])for(unsigned x=0;x<f->original_bytes.width;){
        unsigned w=f->original_bytes.width-x;if(w>64)w=64;
        for(unsigned i=0;i<256;++i)table[i]=byte_expand((i>>shifts[3])&((1U<<bits[3])-1),bits[3])*0x010101U;
        if(ps2_surface_map_palette(gs,f->palette,table))return -1;
        for(unsigned k=0;k<w;++k)
            if(ps2_surface_channel_map_region(gs,f->packed,f->scratch,f->palette,(x+k)/4,y,k,0,1,1,0xffffff,(x+k)%4))return -1;
        if(ps2_surface_alpha_write(gs,f->scratch,f->rgba,f->surface,f->palette,x,y,w,1,0xff000000))return -1;
        x+=w;
    }
    return 0;
}
static int refresh16(struct ps2_format_surface *f,unsigned y)
{
    struct ps2_surface_client *gs=f->gs;uint32_t table[256];
    unsigned bits[4],shifts[4];byte_layout(f->byte_format,bits,shifts);
    for(unsigned x=0;x<f->original_bytes.width;){
        unsigned w=f->original_bytes.width-x;if(w>64)w=64;
        for(unsigned part=0;part<2;++part){
            for(unsigned i=0;i<256;++i)table[i]=i<<(part*8);
            if(ps2_surface_map_palette(gs,f->palette,table))return -1;
            for(unsigned k=0;k<w;++k)
                if(ps2_surface_channel_map_region(gs,f->packed,f->words,f->palette,(x+k)/2,y,k,0,1,1,
                    255U<<(part*8),((x+k)%2)*2+part))return -1;
        }
        for(unsigned c=0;c<4;++c)if(bits[c]){
            unsigned value_mask=(1U<<bits[c])-1;
            for(unsigned part=0;part<2;++part){
                unsigned contribution=((255U<<(part*8))>>shifts[c])&value_mask;
                if(!contribution)continue;
                uint32_t mask=byte_expand(contribution,bits[c])*(c==3?0x010101U:1U<<(c*8));
                for(unsigned i=0;i<256;++i)table[i]=
                    byte_expand(((i<<(part*8))>>shifts[c])&value_mask,bits[c])*(c==3?0x010101U:1U<<(c*8));
                if(ps2_surface_map_palette(gs,f->palette,table) ||
                    ps2_surface_channel_map_region(gs,f->words,c==3?f->scratch:f->surface,f->palette,
                        0,0,c==3?0:x,c==3?0:y,w,1,mask,part))return -1;
            }
            if(c==3 && ps2_surface_alpha_write(gs,f->scratch,f->rgba,f->surface,f->palette,x,y,w,1,0xff000000))return -1;
        }
        x+=w;
    }
    return 0;
}
static int pack8(struct ps2_format_surface *f,unsigned left,unsigned y,unsigned width)
{
    unsigned bits[4],shifts[4];byte_layout(f->byte_format,bits,shifts);uint32_t table[256];
    struct ps2_surface_client *gs=f->gs;
    for(unsigned x=left;x<left+width;){unsigned w=left+width-x;if(w>64)w=64;
        for(unsigned c=0;c<4;++c)if(bits[c]){
            for(unsigned i=0;i<256;++i)table[i]=(i>>(8-bits[c]))<<shifts[c];
            if(ps2_surface_map_palette(gs,f->palette,table) ||
                ps2_surface_channel_map_region(gs,f->surface,f->words,f->palette,x,y,0,0,w,1,
                    ((1U<<bits[c])-1)<<shifts[c],c))return -1;
        }
        for(unsigned phase=0;phase<4;++phase){
            for(unsigned i=0;i<256;++i)table[i]=i<<(phase*8);
            if(ps2_surface_map_palette(gs,f->palette,table))return -1;
            for(unsigned k=0;k<w;++k)if((x+k)%4==phase)
                if(ps2_surface_channel_map_region(gs,f->words,f->packed,f->palette,k,0,(x+k)/4,y,1,1,255U<<(phase*8),0))return -1;
        }
        x+=w;
    }
    return 0;
}
static int pack16(struct ps2_format_surface *f,unsigned left,unsigned y,unsigned width)
{
    unsigned bits[4],shifts[4];byte_layout(f->byte_format,bits,shifts);uint32_t table[256];
    struct ps2_surface_client *gs=f->gs;
    uint32_t word_mask=0;
    for(unsigned c=0;c<4;++c)if(bits[c])word_mask|=((1U<<bits[c])-1)<<shifts[c];
    for(unsigned x=left;x<left+width;){unsigned w=left+width-x;if(w>64)w=64;
        for(unsigned c=0;c<4;++c)if(bits[c]){
            for(unsigned i=0;i<256;++i)table[i]=(i>>(8-bits[c]))<<shifts[c];
            if(ps2_surface_map_palette(gs,f->palette,table) ||
                ps2_surface_channel_map_region(gs,f->surface,f->words,f->palette,x,y,0,0,w,1,
                    ((1U<<bits[c])-1)<<shifts[c],c))return -1;
        }
        for(unsigned phase=0;phase<2;++phase)for(unsigned part=0;part<2;++part){
            unsigned shift=(phase*2+part)*8;uint32_t mask=((word_mask>>(part*8))&255)<<shift;
            for(unsigned i=0;i<256;++i)table[i]=i<<shift;
            if(ps2_surface_map_palette(gs,f->palette,table))return -1;
            for(unsigned k=0;k<w;++k)if((x+k)%2==phase)
                if(ps2_surface_channel_map_region(gs,f->words,f->packed,f->palette,k,0,(x+k)/2,y,1,1,mask,part))return -1;
        }
        x+=w;
    }
    return 0;
}

static int packed_valid(struct ps2_format_surface *f)
{
    return f && f->gs && f->packed && f->surface && f->byte_format<=PS2_WORD_BGR444 &&
        ps2_surface_lookup(f->gs,f->surface) && ps2_surface_lookup(f->gs,f->packed);
}
int ps2_format_pack_region(struct ps2_format_surface *f,unsigned x,unsigned y,unsigned width)
{
    if(!packed_valid(f) || y>=f->original_bytes.height || !width || x>=f->original_bytes.width ||
        width>f->original_bytes.width-x){errno=EINVAL;return -1;}
    return f->byte_format<PS2_WORD_RGB565?pack8(f,x,y,width):pack16(f,x,y,width);
}
int ps2_format_refresh_row(struct ps2_format_surface *f,unsigned y)
{
    if(!packed_valid(f) || y>=f->original_bytes.height){errno=EINVAL;return -1;}
    return f->byte_format<PS2_WORD_RGB565?refresh8(f,y):refresh16(f,y);
}

static int native15_valid(struct ps2_format_surface *f)
{
    if(!packed_valid(f) || f->byte_format<PS2_WORD_RGB555 || f->byte_format>PS2_WORD_BGR444)return 0;
    const struct ps2_surface *s=ps2_surface_lookup(f->gs,f->original.handle);
    return s && f->original.bits_per_pixel==16 && s->bits_per_pixel==16 && ps2_surface_depth(s)==15 &&
        s->width==f->original_bytes.width && s->height==f->original_bytes.height &&
        s->width==f->original.width && s->height==f->original.height && s->bytes==f->original.bytes &&
        s->buffer_width==f->original.buffer_width && s->pixel_depth==f->original.pixel_depth;
}
int ps2_format_decode_native15(struct ps2_format_surface *f,struct ps2_surface_client *gs,uint32_t source,unsigned format)
{
    if(!f || !gs){errno=EINVAL;return -1;}
    *f=(struct ps2_format_surface){.gs=gs,.byte_format=format};
    const struct ps2_surface *s=ps2_surface_lookup(gs,source);
    if(!s || s->bits_per_pixel!=16 || ps2_surface_depth(s)!=15 || !s->width || !s->height ||
        s->width>1024 || s->height>1024 || format<PS2_WORD_RGB555 || format>PS2_WORD_BGR444){errno=EINVAL;return -1;}
    unsigned required=PS2GS_HW_CHANNEL_MAP_BLIT|PS2GS_HW_TEXTURE_TRANSFER|PS2GS_HW_RGBA_TRANSFER|PS2GS_HW_SOLID_LOGIC;
    if((gs->caps.features&required)!=required){errno=ENOTSUP;return -1;}
    f->original=*s;unsigned width=s->width,height=s->height,pw=(width+1)/2;
    f->original_bytes=(struct ps2_picture_bitmap){.width=width,.height=height,.bits_per_pixel=16,
        .stride=pw*4,.bytes=(size_t)pw*4*height};
    if(ps2_surface_create(gs,width,height,32,&f->surface) || ps2_surface_create(gs,pw,height,32,&f->packed) ||
        ps2_surface_set_depth(gs,f->packed,32) || ps2_surface_create(gs,64,32,32,&f->words) ||
        ps2_surface_create(gs,64,32,32,&f->scratch) || ps2_surface_create(gs,64,32,32,&f->rgba) ||
        ps2_surface_create(gs,16,16,32,&f->palette))goto fail;
    if(ps2_surface_solid_logic(gs,f->packed,0,0,pw,height,0,UINT32_MAX,3))goto fail;
    uint32_t table[256];
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;){unsigned w=width-x;if(w>64)w=64;
        if(ps2_rop_sample(gs,source,f->scratch,x,y,0,0,w,1,0xffffff,0,128))goto fail;
        for(unsigned ch=0;ch<3;++ch){
            for(unsigned i=0;i<256;++i)table[i]=(i>>3)<<(ch*5);
            if(ps2_surface_map_palette(gs,f->palette,table) ||
                ps2_surface_channel_map_region(gs,f->scratch,f->words,f->palette,0,0,0,0,w,1,31U<<(ch*5),ch))goto fail;
        }
        for(unsigned phase=0;phase<2;++phase)for(unsigned part=0;part<2;++part){
            unsigned shift=(phase*2+part)*8;uint32_t mask=(part?127U:255U)<<shift;
            for(unsigned i=0;i<256;++i)table[i]=i<<shift;
            if(ps2_surface_map_palette(gs,f->palette,table))goto fail;
            for(unsigned k=0;k<w;++k)if((x+k)%2==phase)
                if(ps2_surface_channel_map_region(gs,f->words,f->packed,f->palette,k,0,(x+k)/2,y,1,1,mask,part))goto fail;
        }
        x+=w;
    }
    for(unsigned y=0;y<height;++y)if(ps2_format_refresh_row(f,y))goto fail;
    return 0;
fail:
    {int error=errno;ps2_format_release(f);errno=error;return -1;}
}
int ps2_format_store_native15(struct ps2_format_surface *f)
{
    if(!native15_valid(f)){errno=EINVAL;return -1;}
    struct ps2_surface_client *gs=f->gs;uint32_t table[256];
    for(unsigned y=0;y<f->original.height;++y)for(unsigned x=0;x<f->original.width;){
        unsigned w=f->original.width-x;if(w>64)w=64;
        for(unsigned part=0;part<2;++part){
            for(unsigned i=0;i<256;++i)table[i]=i<<(part*8);
            if(ps2_surface_map_palette(gs,f->palette,table))return -1;
            for(unsigned k=0;k<w;++k)
                if(ps2_surface_channel_map_region(gs,f->packed,f->words,f->palette,(x+k)/2,y,k,0,1,1,
                    (part?127U:255U)<<(part*8),((x+k)%2)*2+part))return -1;
        }
        for(unsigned i=0;i<256;++i)table[i]=((i&31)<<3)|((i>>5)<<11);
        if(ps2_surface_map_palette(gs,f->palette,table) ||
            ps2_surface_channel_map_region(gs,f->words,f->scratch,f->palette,0,0,0,0,w,1,0x38f8,0))return -1;
        for(unsigned i=0;i<256;++i)table[i]=((i&3)<<14)|(((i>>2)&31)<<19);
        if(ps2_surface_map_palette(gs,f->palette,table) ||
            ps2_surface_channel_map_region(gs,f->words,f->scratch,f->palette,0,0,0,0,w,1,0xf8c000,1) ||
            ps2_rop_sample(gs,f->scratch,f->original.handle,0,0,x,y,w,1,0x7fff,0,128))return -1;
        x+=w;
    }
    return 0;
}
static uint32_t alias_packed(const struct ps2_format_alias *a,unsigned i)
{return a->format[i]?a->format[i]->packed:a->coverage[i]->packed;}
static const struct ps2_picture_bitmap *alias_bytes(const struct ps2_format_alias *a,unsigned i)
{return a->format[i]?&a->format[i]->original_bytes:&a->coverage[i]->original_bytes;}
int ps2_format_alias_valid(const struct ps2_format_alias *a,struct ps2_surface_client *gs,uint32_t parent,uint32_t map)
{
    if(!a || !gs || parent==map)return 0;
    unsigned native=a->format[0] && a->format[0]->original.bits_per_pixel==16;
    if(native && (!a->format[1] || !native15_valid(a->format[0]) || !native15_valid(a->format[1]) ||
        a->format[0]->original.handle!=a->format[1]->original.handle))return 0;
    if(!native && a->format[1] && a->format[1]->original.bits_per_pixel)return 0;
    uint32_t surfaces[]={parent,map};
    for(unsigned i=0;i<2;++i){
        if(!!a->format[i]==!!a->coverage[i])return 0;
        struct ps2_format_surface *f=a->format[i];struct ps2_coverage_surface *c=a->coverage[i];
        if(f?(!packed_valid(f) || f->gs!=gs || f->surface!=surfaces[i]):
            (c->gs!=gs || c->core || c->bpp!=8 || (c->alpha_bits!=4 && c->alpha_bits!=8) || c->surface!=surfaces[i]))return 0;
        const struct ps2_picture_bitmap *b=alias_bytes(a,i);
        const struct ps2_surface *s=ps2_surface_lookup(gs,surfaces[i]),*p=ps2_surface_lookup(gs,alias_packed(a,i));
        if(!s || !p || (!native && !b->bits) || (b->bits_per_pixel!=8 && b->bits_per_pixel!=16) || !b->width || !b->height ||
            b->width>1024 || b->height>1024 || b->stride<(size_t)b->width*(b->bits_per_pixel/8) ||
            b->bytes<b->stride || b->height-1>(b->bytes-b->width*(b->bits_per_pixel/8))/b->stride ||
            s->bits_per_pixel!=32 || s->width!=b->width || s->height!=b->height ||
            p->bits_per_pixel!=32 || p->pixel_depth!=32 || p->height!=b->height ||
            p->width!=(b->width*(b->bits_per_pixel/8)+3)/4 ||
            (f && ((f->byte_format<PS2_WORD_RGB565?8U:16U)!=b->bits_per_pixel)))return 0;
    }
    const struct ps2_picture_bitmap *p=alias_bytes(a,0),*m=alias_bytes(a,1);
    return p->bits==m->bits && p->bytes==m->bytes && p->stride==m->stride && p->width==m->width &&
        p->height==m->height && p->bits_per_pixel==m->bits_per_pixel && alias_packed(a,0)!=alias_packed(a,1);
}
static int alias_pack(struct ps2_format_alias *a,unsigned i,unsigned x,unsigned y,unsigned width)
{
    struct ps2_format_surface *f=a->format[i];struct ps2_coverage_surface *c=a->coverage[i];
    if(f?ps2_format_pack_region(f,x,y,width):ps2_coverage_pack_region(c,x,y,width))return -1;
    unsigned bpp=alias_bytes(a,i)->bits_per_pixel;uint32_t used=0;
    if(f){unsigned bits[4],shifts[4];byte_layout(f->byte_format,bits,shifts);
        for(unsigned ch=0;ch<4;++ch)if(bits[ch])used|=((1U<<bits[ch])-1)<<shifts[ch];
    }else used=(1U<<c->alpha_bits)-1;
    uint32_t padding=((1U<<bpp)-1)&~used;
    /* Pixman stores the entire byte/word, zeroing X/padding bits. They are
     * observable through a different alias even when absent in this view. */
    if(padding){unsigned phases=32/bpp;struct ps2_surface_client *gs=f?f->gs:c->gs;
        for(unsigned k=x;k<x+width;++k)
            if(ps2_surface_solid_logic(gs,alias_packed(a,i),k/phases,y,1,1,0,padding<<((k%phases)*bpp),3))return -1;
    }
    return 0;
}
static int alias_refresh(struct ps2_format_alias *a,unsigned i,unsigned y)
{return a->format[i]?ps2_format_refresh_row(a->format[i],y):ps2_coverage_refresh_row(a->coverage[i],y);}
static int alias_copy(struct ps2_format_alias *a,unsigned from,unsigned y)
{
    struct ps2_surface_client *gs=a->format[from]?a->format[from]->gs:a->coverage[from]->gs;
    const struct ps2_surface *p=ps2_surface_lookup(gs,alias_packed(a,from));
    return ps2_surface_rgba_rectangle(gs,p->handle,alias_packed(a,1-from),0,y,0,y,p->width,1,UINT32_MAX,PS2GS_RGBA_REPLACE);
}
int ps2_format_alias_parent(struct ps2_format_alias *a,unsigned x,unsigned y,unsigned width)
{
    /* Parent storage happens first. Copy complete packed words so the map
     * store retains unused fields and neighbouring pixels at ROI edges. */
    return alias_pack(a,0,x,y,width) || alias_copy(a,0,y)?-1:0;
}
int ps2_format_alias_map(struct ps2_format_alias *a,unsigned x,unsigned y,unsigned width,unsigned parent_y)
{
    if(alias_pack(a,1,x,y,width) || alias_copy(a,1,y))return -1;
    for(unsigned i=0;i<2;++i)
        if(alias_refresh(a,i,y) || (parent_y!=y && alias_refresh(a,i,parent_y)))return -1;
    return 0;
}
