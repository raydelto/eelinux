/* SPDX-License-Identifier: MIT */
/* Client-side oracle only: production rendering remains entirely on the GS. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xrender.h>
#include <pixman.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define W 19
#define H 11
#define PS2_GPU2D_PIXEL_CHECKS 19307
#define PS2_GPU2D_ALPHA_MAP_CHECKS 981
#define PS2_GPU2D_ALPHA_WINDOW_CHECKS 24
#define PS2_GPU2D_ALPHA_ALIAS_CHECKS 114
#define PS2_GPU2D_SATURATE_CHECKS 126
#define PS2_GPU2D_PACKED_SATURATE_CHECKS 56
#define PS2_GPU2D_BILINEAR_SATURATE_CHECKS 12
#define PS2_GPU2D_COVERAGE_SATURATE_CHECKS 24
#define PS2_GPU2D_SOLID_SATURATE_CHECKS 24
#define PS2_GPU2D_MAP_SATURATE_CHECKS 48
#define PS2_GPU2D_SHIFTED_MAP_CHECKS 12
#define PS2_GPU2D_CONVOLUTION_SATURATE_CHECKS 12
#define PS2_GPU2D_FORMAT_ALIAS_CHECKS 32
#define PS2_GPU2D_NATIVE15_ALIAS_CHECKS 24
#define PS2_GPU2D_SCREEN_CHECKS 198
#define PS2_GPU2D_EXCLUSION_CHECKS 198
#define PS2_GPU2D_MULTIPLY_CHECKS 198
#define PS2_GPU2D_ORDER_BLENDS_CHECKS 594
#define PS2_GPU2D_PIECEWISE_CHECKS 396
#define PS2_GPU2D_PD_FAMILIES_CHECKS 1412
#define PS2_GPU2D_PD_ALIASES_CHECKS 1188
#define PS2_GPU2D_DISJOINT_OVER_CHECKS 224
#define PS2_GPU2D_PD_RATIOS_CHECKS 104
#define PS2_GPU2D_DODGE_BURN_CHECKS 30
#define PS2_GPU2D_SOFTLIGHT_CHECKS 20
#define PS2_GPU2D_HSL_CHECKS 66
#define PS2_GPU2D_GRADIENT_CHECKS 72
#define PS2_GPU2D_PACKED10_CHECKS 72
#define PS2_GPU2D_PACKED10_OPS_CHECKS 68
#define PS2_GPU2D_PACKED10_CONVOLUTION_CHECKS 64
#define PS2_GPU2D_PACKED10_ALPHA_READ_CHECKS 32
#define PS2_GPU2D_PACKED10_ALPHA_WRITE_CHECKS 64
#define PS2_GPU2D_PACKED10_MIXED_CHECKS 126
#define PS2_GPU2D_DEPTH30_CHECKS 64
#define PS2_GPU2D_FORMAT_FILTER_CHECKS 684
#define PS2_GPU2D_FRONT_FORMAT_CHECKS 36
#define PS2_GPU2D_RESOURCE_CHECKS 8
#define PS2_GPU2D_CURSOR_ARGB_CHECKS 20
#define PS2_GPU2D_RENDER_GEOMETRY_CHECKS 68
#define PS2_GPU2D_WINDOW_SAMPLE_CHECKS 144
#define PS2_GPU2D_RENDER_GLYPH_FORMATS 28
#define PS2_GPU2D_RENDER_GLYPH_CHECKS 224
static Display *display;
static GC gc;
static unsigned depth,errors,checks,skipped_a4_checks,skipped_byte_checks;
static unsigned long bits;
static unsigned shard_index,shard_count=1,shard_requests;
static unsigned disjoint_request(void)
{
    if(shard_count==1)return 1;
    unsigned request=shard_requests++;
    if(request%shard_count!=shard_index)return 0;
    printf("ps2-gpu2d: shard request=%u index=%u count=%u\n",request,shard_index,shard_count);
    return 1;
}
static unsigned shard_number(const char *name,unsigned fallback)
{
    const char *s=getenv(name);if(!s)return fallback;
    char *end;unsigned long n=strtoul(s,&end,10);
    if(!*s || *end || n>16){fprintf(stderr,"ps2-gpu2d: invalid %s\n",name);exit(2);}
    return (unsigned)n;
}
static int xerror(Display *d,XErrorEvent *e)
{char s[128];XGetErrorText(d,e->error_code,s,sizeof(s));fprintf(stderr,"ps2-gpu2d: X error %s request=%u\n",s,e->request_code);++errors;return 0;}
static unsigned long op(unsigned a,unsigned long s,unsigned long d)
{
    switch(a){case 0:return 0;case 1:return s&d;case 2:return s&~d;case 3:return s;
    case 4:return ~s&d;case 5:return d;case 6:return s^d;case 7:return s|d;
    case 8:return ~(s|d);case 9:return ~(s^d);case 10:return ~d;case 11:return s|~d;
    case 12:return ~s;case 13:return ~s|d;case 14:return ~(s&d);default:return ~0UL;}
}
static void values(unsigned long *p,unsigned width,unsigned height,unsigned seed)
{for(unsigned i=0;i<width*height;++i)p[i]=((i*8191U+seed)^(i*0x10103U))&bits;}
static void upload(Drawable p,unsigned width,unsigned height,const unsigned long *pixels)
{
    XImage *image=XCreateImage(display,DefaultVisual(display,DefaultScreen(display)),depth,ZPixmap,0,NULL,width,height,32,0);
    if(!image)exit(2);
    image->data=calloc(height,image->bytes_per_line);if(!image->data)exit(2);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)XPutPixel(image,x,y,pixels[y*width+x]);
    XSetFunction(display,gc,GXcopy);XSetPlaneMask(display,gc,bits);XSetFillStyle(display,gc,FillSolid);
    XPutImage(display,p,gc,image,0,0,0,0,width,height);XDestroyImage(image);
}
static void verify(Drawable p,unsigned width,unsigned height,const unsigned long *expected,
    unsigned long mask,int format,const char *label,unsigned alu)
{
    XImage *image=XGetImage(display,p,0,0,width,height,mask,format);
    if(!image){fprintf(stderr,"ps2-gpu2d: no reply for %s\n",label);exit(2);}
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        unsigned long actual=XGetPixel(image,x,y),wanted=expected[y*width+x]&mask;
        if(format==XYPixmap){unsigned long packed=0;unsigned selected=0;
            for(unsigned bit=0;bit<32;++bit)if(mask&(1UL<<bit)){
                if(wanted&(1UL<<bit))packed|=1UL<<selected;
                ++selected;
            }
            wanted=packed;
        }
        if(actual!=wanted){fprintf(stderr,"ps2-gpu2d: mismatch %s alu=%u x=%u y=%u expected=%lx actual=%lx\n",label,alu,x,y,wanted,actual);XDestroyImage(image);exit(1);}}
    XDestroyImage(image);++checks;
}
static unsigned modulo(int v,unsigned n){int r=v%(int)n;return r<0?(unsigned)(r+n):(unsigned)r;}
static void plane_checks(Window root,Pixmap source,Pixmap dest,const unsigned long *s,const unsigned long *initial)
{
    unsigned long expected[W*H];
    const unsigned planes[]={0,depth/2,depth-1};
    for(unsigned p=0;p<3;++p)for(unsigned full=0;full<2;++full)for(unsigned alu=0;alu<16;++alu){
        unsigned long plane=1UL<<planes[p],mask=full?bits:bits&0x51a135;
        upload(dest,W,H,initial);memcpy(expected,initial,sizeof(expected));
        XSetForeground(display,gc,bits&0x719185);XSetBackground(display,gc,bits&0x035142);
        XSetFunction(display,gc,alu);XSetPlaneMask(display,gc,mask);
        XCopyPlane(display,source,dest,gc,2,1,13,7,3,2,plane);
        for(unsigned y=0;y<7;++y)for(unsigned x=0;x<13;++x){unsigned i=(y+2)*W+x+3;
            unsigned long v=s[(y+1)*W+x+2]&plane?bits&0x719185:bits&0x035142;
            expected[i]=(expected[i]&~mask)|(op(alu,v,expected[i])&mask);}
        verify(dest,W,H,expected,bits,ZPixmap,"colour-source CopyPlane",alu);
    }
    Pixmap mono=XCreatePixmap(display,root,W,H,1);GC mg=XCreateGC(display,mono,0,NULL);
    XSetGraphicsExposures(display,mg,False);
    for(unsigned p=0;p<3;++p)for(unsigned alu=0;alu<16;++alu){
        XSetFunction(display,mg,GXcopy);XSetForeground(display,mg,1);XFillRectangle(display,mono,mg,0,0,W,H);
        for(unsigned i=0;i<W*H;++i)expected[i]=1;
        XSetForeground(display,mg,1);XSetBackground(display,mg,0);XSetFunction(display,mg,alu);
        XCopyPlane(display,source,mono,mg,2,1,13,7,3,2,1UL<<planes[p]);
        for(unsigned y=0;y<7;++y)for(unsigned x=0;x<13;++x){unsigned i=(y+2)*W+x+3;
            expected[i]=op(alu,!!(s[(y+1)*W+x+2]&(1UL<<planes[p])),1)&1;}
        verify(mono,W,H,expected,1,ZPixmap,"colour-to-bitmap CopyPlane",alu);
    }
    XFreeGC(display,mg);XFreePixmap(display,mono);
    printf("ps2-gpu2d: colour-source CopyPlane to colour and bitmap passed\n");
}
static pixman_format_code_t reference_format(XRenderPictFormat *f)
{
    if(f->depth==32){
        if(f->direct.red==8)return f->direct.alphaMask?PIXMAN_b8g8r8a8:PIXMAN_b8g8r8x8;
        return f->direct.alphaMask?(f->direct.red==0?PIXMAN_a8b8g8r8:PIXMAN_a8r8g8b8):
            (f->direct.red==0?PIXMAN_x8b8g8r8:PIXMAN_x8r8g8b8);
    }
    if(f->depth==24)return f->direct.red==0?PIXMAN_x8b8g8r8:PIXMAN_x8r8g8b8;
    if(f->direct.redMask==15)return f->direct.red==0?PIXMAN_x4b4g4r4:PIXMAN_x4r4g4b4;
    return f->direct.red==0?PIXMAN_x1b5g5r5:PIXMAN_x1r5g5b5;
}
static pixman_image_t *reference_pixels(const unsigned long *pixels,XRenderPictFormat *f,void **allocation)
{
    unsigned cpp=f->depth==15?2:4,stride=(W*cpp+3)&~3U;
    unsigned char *data=calloc(H,stride);if(!data)exit(2);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){uint32_t v=pixels[y*W+x];memcpy(data+y*stride+x*cpp,&v,cpp);}
    *allocation=data;pixman_image_t *image=pixman_image_create_bits(reference_format(f),W,H,(uint32_t*)data,stride);
    if(!image)exit(2);
    return image;
}
static void render_checks(Window root,Pixmap source,Pixmap dest,const unsigned long *s,const unsigned long *initial,XRenderPictFormat *f)
{
    Picture ps=XRenderCreatePicture(display,source,f,0,NULL),pd=XRenderCreatePicture(display,dest,f,0,NULL);
    Pixmap mono=XCreatePixmap(display,root,7,5,1);GC mg=XCreateGC(display,mono,0,NULL);
    uint32_t mbits[5]={0};
    XSetForeground(display,mg,0);XFillRectangle(display,mono,mg,0,0,7,5);XSetForeground(display,mg,1);
    for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x)if((x/2+y)%3){mbits[y]|=1U<<x;XDrawPoint(display,mono,mg,x,y);}
    XRenderPictFormat *mf=XRenderFindStandardFormat(display,PictStandardA1);if(!mf)exit(2);
    XRenderPictureAttributes attr={.repeat=RepeatReflect};Picture pm=XRenderCreatePicture(display,mono,mf,CPRepeat,&attr);
    Pixmap alpha=XCreatePixmap(display,root,7,5,8);GC ag=XCreateGC(display,alpha,0,NULL);
    uint32_t abits[10]={0};
    for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x){
        unsigned a=(y*7+x)*73&255;((unsigned char*)abits)[y*8+x]=a;
        XSetForeground(display,ag,a);XDrawPoint(display,alpha,ag,x,y);}
    XRenderPictFormat *af=XRenderFindStandardFormat(display,PictStandardA8);if(!af)exit(2);
    Picture pa=XRenderCreatePicture(display,alpha,af,CPRepeat,&attr);
    XRenderSetPictureFilter(display,ps,FilterNearest,NULL,0);XRenderSetPictureFilter(display,pm,FilterNearest,NULL,0);
    for(unsigned transform=0;transform<4;++transform)for(unsigned repeat=0;repeat<4;++repeat)
    for(unsigned masked=0;masked<3;++masked)for(unsigned over=0;over<2;++over)
    for(unsigned linear=0;linear<(masked?2U:1U);++linear){
        printf("ps2-gpu2d: Render transform=%u repeat=%u mask=%s mask_filter=%s op=%s\n",transform,repeat,
            masked==2?"A8":masked?"A1":"none",linear?"bilinear":"nearest",over?"Over":"Src");
        pixman_transform_t t={{{65536,0,0},{0,65536,0},{0,0,65536}}};
        if(transform==1){t.matrix[0][0]=98304;t.matrix[1][1]=32768;t.matrix[0][2]=-21845;}
        if(transform==2){t.matrix[0][0]=0;t.matrix[0][1]=-65536;t.matrix[0][2]=10*65536;t.matrix[1][0]=65536;t.matrix[1][1]=0;}
        if(transform==3){t.matrix[2][0]=65536/15;t.matrix[2][1]=65536/19;}
        XTransform xt;memcpy(xt.matrix,t.matrix,sizeof(xt.matrix));XRenderSetPictureTransform(display,ps,&xt);
        attr.repeat=repeat;XRenderChangePicture(display,ps,CPRepeat,&attr);
        upload(dest,W,H,initial);
        void *sb,*db;pixman_image_t *si=reference_pixels(s,f,&sb),*di=reference_pixels(initial,f,&db);
        pixman_image_t *mi=masked?pixman_image_create_bits(masked==2?PIXMAN_a8:PIXMAN_a1,7,5,
            masked==2?abits:mbits,masked==2?8:4):NULL;
        if(masked&&!mi)exit(2);
        if(!pixman_image_set_transform(si,&t)||!pixman_image_set_filter(si,PIXMAN_FILTER_NEAREST,NULL,0))exit(2);
        pixman_image_set_repeat(si,(pixman_repeat_t)repeat);
        if(mi){pixman_transform_t mt={{{65536,0,0},{0,65536,0},{0,0,65536}}};
            if(linear){mt.matrix[0][0]=98304;mt.matrix[1][1]=32768;mt.matrix[1][0]=21845;mt.matrix[0][2]=-21845;}
            XTransform mx;memcpy(mx.matrix,mt.matrix,sizeof(mx.matrix));
            XRenderSetPictureTransform(display,masked==2?pa:pm,&mx);
            XRenderSetPictureFilter(display,masked==2?pa:pm,linear?FilterBilinear:FilterNearest,NULL,0);
            if(!pixman_image_set_transform(mi,&mt) ||
                !pixman_image_set_filter(mi,linear?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,NULL,0))exit(2);
            pixman_image_set_repeat(mi,PIXMAN_REPEAT_REFLECT);}
        pixman_image_composite32(over?PIXMAN_OP_OVER:PIXMAN_OP_SRC,si,mi,di,-3,-2,-1,1,2,1,15,9);
        XRenderComposite(display,over?PictOpOver:PictOpSrc,ps,masked==2?pa:masked?pm:None,pd,-3,-2,-1,1,2,1,15,9);
        unsigned long expected[W*H];unsigned cpp=depth==15?2:4,stride=(W*cpp+3)&~3U;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){uint32_t v=0;memcpy(&v,(unsigned char*)db+y*stride+x*cpp,cpp);expected[y*W+x]=v&bits;}
        verify(dest,W,H,expected,bits,ZPixmap,"nearest XRender against pixman",over?PictOpOver:PictOpSrc);
        if(mi)pixman_image_unref(mi);
        pixman_image_unref(si);pixman_image_unref(di);free(sb);free(db);
    }
    XRenderFreePicture(display,pm);XRenderFreePicture(display,ps);XRenderFreePicture(display,pd);
    XRenderFreePicture(display,pa);XFreeGC(display,ag);XFreePixmap(display,alpha);
    XFreeGC(display,mg);XFreePixmap(display,mono);
    printf("ps2-gpu2d: Src/Over affine/projective transforms, repeats and A1/A8 masks match pixman\n");
}
static void filter_render_checks(Window root,Pixmap source,Pixmap dest,const unsigned long *s,const unsigned long *initial,XRenderPictFormat *f)
{
    Picture ps=XRenderCreatePicture(display,source,f,0,NULL),pd=XRenderCreatePicture(display,dest,f,0,NULL);
    uint32_t abits[10]={0};Pixmap alpha=XCreatePixmap(display,root,7,5,8);GC ag=XCreateGC(display,alpha,0,NULL);
    for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x){unsigned a=(y*7+x)*73&255;
        ((unsigned char*)abits)[y*8+x]=a;XSetForeground(display,ag,a);XDrawPoint(display,alpha,ag,x,y);}
    XRenderPictureAttributes attr={.repeat=RepeatReflect};
    Picture pm=XRenderCreatePicture(display,alpha,XRenderFindStandardFormat(display,PictStandardA8),CPRepeat,&attr);
    XFixed kernels[][6]={{2*65536,2*65536,16384,16384,16384,16384},{3*65536,65536,-32768,131072,-32768}};
    for(unsigned kind=0;kind<3;++kind)for(unsigned projective=0;projective<2;++projective)
    for(unsigned repeat=0;repeat<4;++repeat)for(unsigned over=0;over<2;++over)for(unsigned masked=0;masked<2;++masked){
        pixman_transform_t t={{{98304,21845,-21845},{0,32768,21845},{0,0,65536}}};
        if(projective){t.matrix[2][0]=65536/17;t.matrix[2][1]=65536/19;}
        XTransform xt;memcpy(xt.matrix,t.matrix,sizeof(xt.matrix));XRenderSetPictureTransform(display,ps,&xt);
        attr.repeat=repeat;XRenderChangePicture(display,ps,CPRepeat,&attr);upload(dest,W,H,initial);
        XFixed *params=kind?kernels[kind-1]:NULL;unsigned nparams=kind==1?6:kind==2?5:0;
        XRenderSetPictureFilter(display,ps,kind?FilterConvolution:FilterBilinear,params,nparams);
        void *sb,*db;pixman_image_t *si=reference_pixels(s,f,&sb),*di=reference_pixels(initial,f,&db);
        pixman_image_t *mi=masked?pixman_image_create_bits(PIXMAN_a8,7,5,abits,8):NULL;
        if(!pixman_image_set_transform(si,&t) || !pixman_image_set_filter(si,kind?PIXMAN_FILTER_CONVOLUTION:PIXMAN_FILTER_BILINEAR,params,nparams))exit(2);
        pixman_image_set_repeat(si,(pixman_repeat_t)repeat);
        if(mi)pixman_image_set_repeat(mi,PIXMAN_REPEAT_REFLECT);
        pixman_image_composite32(over?PIXMAN_OP_OVER:PIXMAN_OP_SRC,si,mi,di,-1,-1,-1,1,2,1,3,3);
        printf("ps2-gpu2d: Render colour_filter=%s kernel=%u projective=%u repeat=%u A8=%u op=%s\n",
            kind?"convolution":"bilinear",kind,projective,repeat,masked,over?"Over":"Src");
        XRenderComposite(display,over?PictOpOver:PictOpSrc,ps,masked?pm:None,pd,-1,-1,-1,1,2,1,3,3);
        unsigned long expected[W*H];unsigned cpp=depth==15?2:4,stride=(W*cpp+3)&~3U;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){uint32_t v=0;memcpy(&v,(unsigned char*)db+y*stride+x*cpp,cpp);expected[y*W+x]=v&bits;}
        verify(dest,W,H,expected,bits,ZPixmap,"colour filter XRender against pixman",over?PictOpOver:PictOpSrc);
        if(mi)pixman_image_unref(mi);
        pixman_image_unref(si);pixman_image_unref(di);free(sb);free(db);
    }
    const char *aliases[]={FilterFast,FilterGood,FilterBest};
    for(unsigned alias=0;alias<3;++alias)for(unsigned repeat=0;repeat<4;++repeat){
        pixman_transform_t t={{{65536,0,21845},{0,65536,-21845},{0,0,65536}}};XTransform xt;
        memcpy(xt.matrix,t.matrix,sizeof(xt.matrix));XRenderSetPictureTransform(display,ps,&xt);
        attr.repeat=repeat;XRenderChangePicture(display,ps,CPRepeat,&attr);
        XRenderSetPictureFilter(display,ps,aliases[alias],NULL,0);upload(dest,W,H,initial);
        void *sb,*db;pixman_image_t *si=reference_pixels(s,f,&sb),*di=reference_pixels(initial,f,&db);
        if(!pixman_image_set_transform(si,&t) || !pixman_image_set_filter(si,alias?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,NULL,0))exit(2);
        pixman_image_set_repeat(si,(pixman_repeat_t)repeat);
        pixman_image_composite32(PIXMAN_OP_OVER,si,NULL,di,-1,-1,0,0,2,1,3,3);
        printf("ps2-gpu2d: Render colour_filter_alias=%s repeat=%u\n",aliases[alias],repeat);
        XRenderComposite(display,PictOpOver,ps,None,pd,-1,-1,0,0,2,1,3,3);
        unsigned long expected[W*H];unsigned cpp=depth==15?2:4,stride=(W*cpp+3)&~3U;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){uint32_t v=0;memcpy(&v,(unsigned char*)db+y*stride+x*cpp,cpp);expected[y*W+x]=v&bits;}
        verify(dest,W,H,expected,bits,ZPixmap,"filter alias XRender against pixman",PictOpOver);
        pixman_image_unref(si);pixman_image_unref(di);free(sb);free(db);
    }
    XRenderFreePicture(display,pm);XRenderFreePicture(display,ps);XRenderFreePicture(display,pd);
    XFreeGC(display,ag);XFreePixmap(display,alpha);
    printf("ps2-gpu2d: native bilinear, signed convolution and fast/good/best aliases match pixman\n");
}
static void solid_render_checks(Window root,Pixmap dest,const unsigned long *initial,XRenderPictFormat *f)
{
    unsigned weights[]={0,1,127,128,129,191,254,255};uint32_t abits[10]={0};
    Pixmap alpha=XCreatePixmap(display,root,7,5,8);GC ag=XCreateGC(display,alpha,0,NULL);
    for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x){unsigned a=(y*7+x)*73&255;
        ((unsigned char*)abits)[y*8+x]=a;XSetForeground(display,ag,a);XDrawPoint(display,alpha,ag,x,y);}
    XRenderPictureAttributes attr={.repeat=RepeatReflect};
    Picture pm=XRenderCreatePicture(display,alpha,XRenderFindStandardFormat(display,PictStandardA8),CPRepeat,&attr);
    Picture pd=XRenderCreatePicture(display,dest,f,0,NULL);
    for(unsigned wi=0;wi<sizeof(weights)/sizeof(*weights);++wi)for(unsigned op=0;op<=PictOpSaturate;++op)for(unsigned masked=0;masked<2;++masked){
        unsigned a=weights[wi];XRenderColor color={.red=(a*73/100)*257,.green=(a*31/100)*257,.blue=(a*51/100)*257,.alpha=a*257};
        Picture ps=XRenderCreateSolidFill(display,&color);upload(dest,W,H,initial);
        pixman_color_t c={.red=color.red,.green=color.green,.blue=color.blue,.alpha=color.alpha};
        pixman_image_t *si=pixman_image_create_solid_fill(&c),*mi=masked?pixman_image_create_bits(PIXMAN_a8,7,5,abits,8):NULL;
        void *db;pixman_image_t *di=reference_pixels(initial,f,&db);if(!si || (masked && !mi))exit(2);
        if(mi)pixman_image_set_repeat(mi,PIXMAN_REPEAT_REFLECT);
        pixman_image_composite32((pixman_op_t)op,si,mi,di,-3,-2,-1,1,2,1,15,9);
        printf("ps2-gpu2d: solid source alpha=%u A8=%u op=%u\n",a,masked,op);
        XRenderComposite(display,op,ps,masked?pm:None,pd,-3,-2,-1,1,2,1,15,9);
        unsigned long expected[W*H];unsigned cpp=depth==15?2:4,stride=(W*cpp+3)&~3U;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){uint32_t v=0;memcpy(&v,(unsigned char*)db+y*stride+x*cpp,cpp);expected[y*W+x]=v&bits;}
        verify(dest,W,H,expected,bits,ZPixmap,"solid alpha XRender against pixman",op);
        if(mi)pixman_image_unref(mi);
        pixman_image_unref(si);pixman_image_unref(di);free(db);XRenderFreePicture(display,ps);
    }
    XRenderFreePicture(display,pm);XRenderFreePicture(display,pd);XFreeGC(display,ag);XFreePixmap(display,alpha);
    printf("ps2-gpu2d: translucent solid sources with A8 match pixman\n");
}
static void format_upload(Pixmap p,GC g,XRenderPictFormat *f,const unsigned long *pixels)
{
    XImage *image=XCreateImage(display,DefaultVisual(display,DefaultScreen(display)),f->depth,
        ZPixmap,0,NULL,W,H,32,0);if(!image)exit(2);
    image->data=calloc(H,image->bytes_per_line);if(!image->data)exit(2);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)XPutPixel(image,x,y,pixels[y*W+x]);
    XPutImage(display,p,g,image,0,0,0,0,W,H);XDestroyImage(image);
}
static void format_verify(Pixmap p,XRenderPictFormat *f,const void *data,const char *label,unsigned operation)
{
    unsigned long expected[W*H],mask=((uint32_t)f->direct.redMask<<f->direct.red)|
        ((uint32_t)f->direct.greenMask<<f->direct.green)|((uint32_t)f->direct.blueMask<<f->direct.blue)|
        ((uint32_t)f->direct.alphaMask<<f->direct.alpha);
    unsigned cpp=f->depth==15?2:4,stride=(W*cpp+3)&~3U;
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){uint32_t v=0;
        memcpy(&v,(const unsigned char*)data+y*stride+x*cpp,cpp);expected[y*W+x]=v&mask;}
    verify(p,W,H,expected,mask,ZPixmap,label,operation);
}
static pixman_image_t *saturate_reference(const unsigned long *pixels,XRenderPictFormat *f,void **allocation)
{
    if(f->depth!=1 && f->depth!=8)return reference_pixels(pixels,f,allocation);
    unsigned stride=f->depth==1?((W+31)/32)*4:(W+3)&~3U;
    unsigned char *data=calloc(H,stride);if(!data)exit(2);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
        if(f->depth==1)data[y*stride+x/8]|=(pixels[y*W+x]&1)<<(x&7);
        else data[y*stride+x]=pixels[y*W+x];
    *allocation=data;
    pixman_image_t *image=pixman_image_create_bits(f->depth==1?PIXMAN_a1:
        f->direct.alphaMask==15?PIXMAN_x4a4:PIXMAN_a8,W,H,(uint32_t*)data,stride);
    if(!image)exit(2);
    return image;
}
static unsigned long saturate_pixel(uint32_t rgba,XRenderPictFormat *f)
{
    if(f->depth==1)return rgba>>31;
    if(f->depth==8)return rgba>>24;
    return ((rgba&255UL)<<f->direct.red)|(((rgba>>8)&255UL)<<f->direct.green)|
        (((rgba>>16)&255UL)<<f->direct.blue)|((rgba>>24UL & f->direct.alphaMask)<<f->direct.alpha);
}
static void saturate_checks(Window root,unsigned filtered)
{
    XRenderPictFormat *formats[8];
    for(unsigned i=0;i<6;++i){unsigned alpha=i<3,rotate=i==2 || i==5,swap=i==1 || i==4;
        XRenderPictFormat wanted={.type=PictTypeDirect,.depth=alpha || rotate?32:24,
            .direct={.red=rotate?8:swap?16:0,.redMask=255,.green=rotate?16:8,.greenMask=255,
                .blue=rotate?24:swap?0:16,.blueMask=255,.alpha=alpha && !rotate?24:0,.alphaMask=alpha?255:0}};
        formats[i]=XRenderFindFormat(display,PictFormatType|PictFormatDepth|PictFormatRed|PictFormatRedMask|
            PictFormatGreen|PictFormatGreenMask|PictFormatBlue|PictFormatBlueMask|PictFormatAlpha|PictFormatAlphaMask,&wanted,0);
        if(!formats[i]){fprintf(stderr,"ps2-gpu2d: missing wide Saturate format %u\n",i);exit(2);}
    }
    formats[6]=XRenderFindStandardFormat(display,PictStandardA1);formats[7]=XRenderFindStandardFormat(display,PictStandardA8);
    if(!formats[6] || !formats[7])exit(2);
    pixman_transform_t transform={{{98304,21845,-21845},{0,32768,21845},{65536/17,0,65536}}};
    XTransform xt;memcpy(xt.matrix,transform.matrix,sizeof(xt.matrix));
    for(unsigned alias=0;alias<2;++alias)for(unsigned si=0;si<8;++si)
    for(unsigned di=0;di<3;++di)for(unsigned mask_kind=0;mask_kind<5;++mask_kind){
        if(alias && (si!=di || (mask_kind!=0 && mask_kind!=4)))continue;
        if(filtered && !(alias?(si==0 && di==0 && mask_kind==4):
            (si==0 && di==0) || (si==1 && di==1 && mask_kind==4) || (si==2 && di==2 && mask_kind==4) ||
            (si>=3 && si!=4 && di==0 && mask_kind==4)))continue;
        XRenderPictFormat *sf=formats[si],*df=formats[di],*mf=mask_kind==1?formats[6]:mask_kind==2?formats[7]:formats[0];
        Pixmap dp=XCreatePixmap(display,root,W,H,df->depth),sp=alias?dp:XCreatePixmap(display,root,W,H,sf->depth),
            mp=mask_kind?XCreatePixmap(display,root,W,H,mf->depth):None;
        GC dg=XCreateGC(display,dp,0,NULL),sg=alias?dg:XCreateGC(display,sp,0,NULL),mg=mask_kind?XCreateGC(display,mp,0,NULL):NULL;
        unsigned long source[W*H],original[W*H],mask[W*H];
        for(unsigned i=0;i<W*H;++i){unsigned a=(i*73+29)&255,da=(i*37+131)&255;
            uint32_t src=(a<<24)|((i*17+191)&255)*a/255|(((i*37+13)&255)*a/255<<8)|(((i*53+127)&255)*a/255<<16),
                dst=(da<<24)|((i*43+127)&255)*da/255|(((i*19+73)&255)*da/255<<8)|(((i*59+17)&255)*da/255<<16),
                m=((i*17+59)&255)<<24|((i*73)&255)|(((i*37+127)&255)<<8)|(((i*19+73)&255)<<16);
            if(i%17==0)src&=0xffffff;
            if(i%19==0)dst|=0xff000000;
            source[i]=saturate_pixel(src,sf);original[i]=saturate_pixel(dst,df);mask[i]=saturate_pixel(m,mf);
        }
        if(alias)memcpy(source,original,sizeof(source));
        else format_upload(sp,sg,sf,source);
        format_upload(dp,dg,df,original);if(mask_kind)format_upload(mp,mg,mf,mask);
        XRenderPictureAttributes attr={.repeat=(si+di+mask_kind)%4,.component_alpha=mask_kind==4};
        Picture ps=XRenderCreatePicture(display,sp,sf,CPRepeat,&attr),pd=XRenderCreatePicture(display,dp,df,0,NULL),
            pm=mask_kind?XRenderCreatePicture(display,mp,mf,CPRepeat|CPComponentAlpha,&attr):None;
        XRenderSetPictureTransform(display,ps,&xt);
        unsigned source_linear=filtered && mask_kind%3!=2,mask_linear=filtered && mask_kind%3!=0;
        XRenderSetPictureFilter(display,ps,source_linear?FilterBilinear:FilterNearest,NULL,0);
        if(pm)XRenderSetPictureFilter(display,pm,mask_linear?FilterBilinear:FilterNearest,NULL,0);
        void *sb,*db,*mb=NULL;pixman_image_t *sref=saturate_reference(source,sf,&sb),
            *dref=reference_pixels(original,df,&db),*mref=mask_kind?saturate_reference(mask,mf,&mb):NULL;
        if(!pixman_image_set_transform(sref,&transform))exit(2);
        if(!pixman_image_set_filter(sref,source_linear?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,NULL,0))exit(2);
        pixman_image_set_repeat(sref,(pixman_repeat_t)attr.repeat);
        if(mref){pixman_image_set_repeat(mref,(pixman_repeat_t)attr.repeat);pixman_image_set_component_alpha(mref,attr.component_alpha);
            if(!pixman_image_set_filter(mref,mask_linear?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,NULL,0))exit(2);}
        unsigned size=filtered?1:2;
        pixman_image_composite32(PIXMAN_OP_SATURATE,sref,mref,dref,-1,-1,-1,1,2,1,size,size);
        printf("ps2-gpu2d: wide Saturate source=%u destination=%u mask=%u alias=%u source_linear=%u mask_linear=%u\n",
            si,di,mask_kind,alias,source_linear,mask_linear);
        XRenderComposite(display,PictOpSaturate,ps,pm,pd,-1,-1,-1,1,2,1,size,size);
        format_verify(dp,df,db,"wide Saturate against pixman",PictOpSaturate);
        pixman_image_unref(sref);pixman_image_unref(dref);if(mref)pixman_image_unref(mref);free(sb);free(db);free(mb);
        XRenderFreePicture(display,ps);XRenderFreePicture(display,pd);if(pm)XRenderFreePicture(display,pm);
        if(mask_kind){XFreeGC(display,mg);XFreePixmap(display,mp);}
        if(!alias){XFreeGC(display,sg);XFreePixmap(display,sp);}XFreeGC(display,dg);XFreePixmap(display,dp);
    }
}
static void coverage_saturate_checks(Window root)
{
    XRenderPictFormat wanted={.type=PictTypeDirect,.depth=8,.direct={.alphaMask=15}};
    XRenderPictFormat *formats[]={XRenderFindStandardFormat(display,PictStandardARGB32),
        XRenderFindStandardFormat(display,PictStandardA1),
        XRenderFindFormat(display,PictFormatType|PictFormatDepth|PictFormatRedMask|PictFormatGreenMask|
            PictFormatBlueMask|PictFormatAlpha|PictFormatAlphaMask,&wanted,0),
        XRenderFindStandardFormat(display,PictStandardA8)};
    for(unsigned i=0;i<4;++i)if(!formats[i]){
        fprintf(stderr,"ps2-gpu2d: missing coverage Saturate format %u\n",i);exit(2);}
    pixman_transform_t st={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}},
        mt={{{98304,0,257},{0,32768,21845},{0,0,65536}}};
    XTransform xs,xm;memcpy(xs.matrix,st.matrix,sizeof(xs.matrix));memcpy(xm.matrix,mt.matrix,sizeof(xm.matrix));
    const unsigned sources[]={0,0,0,1,2,3,0,0},masks[]={0,3,0,2,0,1,0,0};
    for(unsigned kind=1;kind<4;++kind)for(unsigned mode=0;mode<8;++mode){
        unsigned alias_source=mode==6,alias_mask=mode==7,masked=mode!=0 && mode!=6;
        unsigned component=mode==2 || mode==4 || mode==5;
        unsigned source_linear=mode!=1 && mode!=4 && mode!=7,mask_linear=mode==1 || mode==3 || mode==4 || mode==7;
        XRenderPictFormat *df=formats[kind],*sf=alias_source?df:formats[sources[mode]],*mf=alias_mask?df:formats[masks[mode]];
        Pixmap dp=XCreatePixmap(display,root,W,H,df->depth),sp=alias_source?dp:XCreatePixmap(display,root,W,H,sf->depth),
            mp=masked?(alias_mask?dp:XCreatePixmap(display,root,W,H,mf->depth)):None;
        GC dg=XCreateGC(display,dp,0,NULL),sg=alias_source?dg:XCreateGC(display,sp,0,NULL),
            mg=masked?(alias_mask?dg:XCreateGC(display,mp,0,NULL)):NULL;
        unsigned long source[W*H],original[W*H],mask[W*H];
        for(unsigned i=0;i<W*H;++i){unsigned a=(i*73+29)&255,da=(i*37+131)&255,ma=(i*17+59)&255;
            source[i]=sf==formats[0]?(a<<24)|((i*17+191)&255)*a/255|
                (((i*37+13)&255)*a/255<<8)|(((i*53+127)&255)*a/255<<16):
                sf->depth==1?a>>7:sf->direct.alphaMask==15?a>>4:a;
            original[i]=kind==1?da>>7:kind==2?da>>4:da;
            mask[i]=mf==formats[0]?(ma<<24)|((i*73)&255)|(((i*37+127)&255)<<8)|(((i*19+73)&255)<<16):
                mf->depth==1?ma>>7:mf->direct.alphaMask==15?ma>>4:ma;
        }
        if(alias_source)memcpy(source,original,sizeof(source));else format_upload(sp,sg,sf,source);
        if(alias_mask)memcpy(mask,original,sizeof(mask));else if(masked)format_upload(mp,mg,mf,mask);
        format_upload(dp,dg,df,original);
        XRenderPictureAttributes attr={.repeat=mode%4,.component_alpha=component};
        Picture ps=XRenderCreatePicture(display,sp,sf,CPRepeat,&attr),pd=XRenderCreatePicture(display,dp,df,0,NULL),
            pm=masked?XRenderCreatePicture(display,mp,mf,CPRepeat|CPComponentAlpha,&attr):None;
        XRenderSetPictureTransform(display,ps,&xs);
        XRenderSetPictureFilter(display,ps,source_linear?FilterBilinear:FilterNearest,NULL,0);
        if(pm){XRenderSetPictureTransform(display,pm,&xm);
            XRenderSetPictureFilter(display,pm,mask_linear?FilterBilinear:FilterNearest,NULL,0);}
        void *sb,*db,*mb=NULL;pixman_image_t *si=saturate_reference(source,sf,&sb),
            *di=saturate_reference(original,df,&db),*mi=masked?saturate_reference(mask,mf,&mb):NULL;
        if(!pixman_image_set_transform(si,&st) || !pixman_image_set_filter(si,
            source_linear?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,NULL,0))exit(2);
        pixman_image_set_repeat(si,(pixman_repeat_t)attr.repeat);
        if(mi){pixman_image_set_component_alpha(mi,component);pixman_image_set_repeat(mi,(pixman_repeat_t)attr.repeat);
            if(!pixman_image_set_transform(mi,&mt) || !pixman_image_set_filter(mi,
                mask_linear?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,NULL,0))exit(2);}
        pixman_image_composite32(PIXMAN_OP_SATURATE,si,mi,di,1,1,-1,1,3,2,1,1);
        printf("ps2-gpu2d: Render coverage Saturate destination=A%u mode=%u source_linear=%u mask_linear=%u\n",
            kind==1?1:kind==2?4:8,mode,source_linear,mask_linear);
        XRenderComposite(display,PictOpSaturate,ps,pm,pd,1,1,-1,1,3,2,1,1);
        unsigned long expected[W*H];unsigned stride=kind==1?((W+31)/32)*4:(W+3)&~3U;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){const unsigned char *row=(const unsigned char*)db+y*stride;
            expected[y*W+x]=kind==1?(row[x/8]>>(x&7))&1:row[x]&(kind==2?15:255);}
        verify(dp,W,H,expected,kind==1?1:kind==2?15:255,ZPixmap,"coverage Saturate against pixman",PictOpSaturate);
        pixman_image_unref(si);pixman_image_unref(di);if(mi)pixman_image_unref(mi);free(sb);free(db);free(mb);
        XRenderFreePicture(display,ps);XRenderFreePicture(display,pd);if(pm)XRenderFreePicture(display,pm);
        if(masked && !alias_mask){XFreeGC(display,mg);XFreePixmap(display,mp);}
        if(!alias_source){XFreeGC(display,sg);XFreePixmap(display,sp);}XFreeGC(display,dg);XFreePixmap(display,dp);
    }
}
static void format_render_checks(Window root)
{
    XRenderPictFormat *f[4];Pixmap p[4],src[4];GC g[4];Picture pd[4],ps[4];
    unsigned long initial[4][W*H],pixels[4][W*H];
    for(unsigned i=0;i<4;++i){unsigned five=i<2,swap=i&1;
        XRenderPictFormat wanted={.type=PictTypeDirect,.depth=five?15:24,
            .direct={.red=swap?(five?10:16):0,.redMask=five?31:255,
                .green=five?5:8,.greenMask=five?31:255,
                .blue=swap?0:(five?10:16),.blueMask=five?31:255,.alphaMask=0}};
        f[i]=XRenderFindFormat(display,PictFormatType|PictFormatDepth|PictFormatRed|PictFormatRedMask|
            PictFormatGreen|PictFormatGreenMask|PictFormatBlue|PictFormatBlueMask|PictFormatAlphaMask,&wanted,0);
        if(!f[i]){fprintf(stderr,"ps2-gpu2d: missing RGB format depth=%u swapped=%u\n",five?15:24,swap);exit(2);}
        p[i]=XCreatePixmap(display,root,W,H,f[i]->depth);src[i]=XCreatePixmap(display,root,W,H,f[i]->depth);
        g[i]=XCreateGC(display,p[i],0,NULL);XSetGraphicsExposures(display,g[i],False);
        for(unsigned k=0;k<W*H;++k){unsigned mask=five?0x7fff:0xffffff;
            initial[i][k]=((k*8191U+0x312915)^(k*0x10103U))&mask;
            pixels[i][k]=((k*6173U+0x7181a5)^(k*0x30307U))&mask;}
        format_upload(src[i],g[i],f[i],pixels[i]);
        pd[i]=XRenderCreatePicture(display,p[i],f[i],0,NULL);ps[i]=XRenderCreatePicture(display,src[i],f[i],0,NULL);
    }
    XFixed coefficients[]={2*65536,65536,2*65536,-65536};
    pixman_transform_t t={{{98304,0,-21845},{0,32768,21845},{65536/15,65536/19,65536}}};
    XTransform xt;memcpy(xt.matrix,t.matrix,sizeof(xt.matrix));XRenderPictureAttributes attr={.repeat=RepeatReflect};
    for(unsigned s=0;s<4;++s)for(unsigned d=0;d<4;++d)for(unsigned filter=0;filter<3;++filter)for(unsigned over=0;over<2;++over){
        XRenderSetPictureTransform(display,ps[s],&xt);XRenderChangePicture(display,ps[s],CPRepeat,&attr);
        XRenderSetPictureFilter(display,ps[s],filter==2?FilterConvolution:filter==1?FilterBilinear:FilterNearest,
            filter==2?coefficients:NULL,filter==2?4:0);format_upload(p[d],g[d],f[d],initial[d]);
        void *sb,*db;pixman_image_t *si=reference_pixels(pixels[s],f[s],&sb),*di=reference_pixels(initial[d],f[d],&db);
        if(!pixman_image_set_transform(si,&t) || !pixman_image_set_filter(si,
            filter==2?PIXMAN_FILTER_CONVOLUTION:filter==1?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
            filter==2?coefficients:NULL,filter==2?4:0))exit(2);
        pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);
        pixman_image_composite32(over?PIXMAN_OP_OVER:PIXMAN_OP_SRC,si,NULL,di,-1,-1,0,0,2,1,2,2);
        printf("ps2-gpu2d: Render RGB format source=%u destination=%u filter=%u op=%s\n",s,d,filter,over?"Over":"Src");
        XRenderComposite(display,over?PictOpOver:PictOpSrc,ps[s],None,pd[d],-1,-1,0,0,2,1,2,2);
        format_verify(p[d],f[d],db,"RGB format conversion against pixman",over?PictOpOver:PictOpSrc);
        pixman_image_unref(si);pixman_image_unref(di);free(sb);free(db);
    }
    XRenderColor color={.red=73*257,.green=31*257,.blue=51*257,.alpha=129*257};
    Picture solid=XRenderCreateSolidFill(display,&color);pixman_color_t pc={.red=color.red,.green=color.green,.blue=color.blue,.alpha=color.alpha};
    for(unsigned d=0;d<4;++d)for(unsigned operation=0;operation<=PictOpSaturate;++operation){
        format_upload(p[d],g[d],f[d],initial[d]);void *db;
        pixman_image_t *di=reference_pixels(initial[d],f[d],&db),*si=pixman_image_create_solid_fill(&pc);if(!si)exit(2);
        pixman_image_composite32((pixman_op_t)operation,si,NULL,di,0,0,0,0,2,1,3,2);
        printf("ps2-gpu2d: Render RGB format solid destination=%u op=%u\n",d,operation);
        XRenderComposite(display,operation,solid,None,pd[d],0,0,0,0,2,1,3,2);
        format_verify(p[d],f[d],db,"solid RGB format against pixman",operation);
        pixman_image_unref(si);pixman_image_unref(di);free(db);
    }
    for(unsigned d=0;d<4;++d)for(unsigned filter=0;filter<3;++filter){
        XRenderPictFormat *other=f[d^1];Picture alias=XRenderCreatePicture(display,p[d],other,0,NULL);
        XRenderSetPictureTransform(display,alias,&xt);XRenderChangePicture(display,alias,CPRepeat,&attr);
        XRenderSetPictureFilter(display,alias,filter==2?FilterConvolution:filter==1?FilterBilinear:FilterNearest,
            filter==2?coefficients:NULL,filter==2?4:0);format_upload(p[d],g[d],f[d],initial[d]);
        void *sb,*db;pixman_image_t *si=reference_pixels(initial[d],other,&sb),*di=reference_pixels(initial[d],f[d],&db);
        if(!pixman_image_set_transform(si,&t) || !pixman_image_set_filter(si,
            filter==2?PIXMAN_FILTER_CONVOLUTION:filter==1?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
            filter==2?coefficients:NULL,filter==2?4:0))exit(2);
        pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);
        pixman_image_composite32(PIXMAN_OP_OVER,si,NULL,di,-1,-1,0,0,2,1,2,2);
        printf("ps2-gpu2d: Render RGB format alias destination=%u filter=%u\n",d,filter);
        XRenderComposite(display,PictOpOver,alias,None,pd[d],-1,-1,0,0,2,1,2,2);
        format_verify(p[d],f[d],db,"aliased RGB format against pixman",PictOpOver);
        pixman_image_unref(si);pixman_image_unref(di);free(sb);free(db);XRenderFreePicture(display,alias);
    }
    XRenderFreePicture(display,solid);
    for(unsigned i=0;i<4;++i){XRenderFreePicture(display,ps[i]);XRenderFreePicture(display,pd[i]);
        XFreeGC(display,g[i]);XFreePixmap(display,p[i]);XFreePixmap(display,src[i]);}
    printf("ps2-gpu2d: RGBX/RGB555 colour orders, conversion, filters, solids and aliases match pixman\n");
}
static void rgba_checks(Window root)
{
    unsigned saved_depth=depth;unsigned long saved_bits=bits;GC saved_gc=gc;
    depth=32;bits=UINT32_MAX;
    Pixmap source=XCreatePixmap(display,root,W,H,32),dest=XCreatePixmap(display,root,W,H,32);
    gc=XCreateGC(display,dest,0,NULL);XSetGraphicsExposures(display,gc,False);
    unsigned long s[W*H],initial[W*H],expected[W*H];values(s,W,H,0x817f3b19);values(initial,W,H,0xf1392b47);
    upload(source,W,H,s);
    const uint32_t masks[]={UINT32_MAX,0xff000000,0x5731a5cf};
    for(unsigned alu=0;alu<16;++alu)for(unsigned m=0;m<3;++m)for(unsigned solid=0;solid<2;++solid){
        unsigned long mask=masks[m];upload(dest,W,H,initial);memcpy(expected,initial,sizeof(expected));
        XSetPlaneMask(display,gc,mask);XSetFunction(display,gc,alu);XSetForeground(display,gc,0x817f3b19);
        if(solid)XFillRectangle(display,dest,gc,3,2,13,7);else XCopyArea(display,source,dest,gc,2,1,13,7,3,2);
        for(unsigned y=0;y<7;++y)for(unsigned x=0;x<13;++x){unsigned i=(y+2)*W+x+3;
            unsigned long v=solid?0x817f3b19:s[(y+1)*W+x+2];expected[i]=(expected[i]&~mask)|(op(alu,v,expected[i])&mask);}
        verify(dest,W,H,expected,bits,ZPixmap,"depth32 Boolean copy/fill",alu);
    }
    /* All alpha bits remain meaningful in the core CopyPlane protocol. */
    Pixmap mono=XCreatePixmap(display,root,W,H,1);GC mg=XCreateGC(display,mono,0,NULL);
    for(unsigned alu=0;alu<16;++alu){
        upload(dest,W,H,initial);memcpy(expected,initial,sizeof(expected));
        XSetPlaneMask(display,gc,bits);XSetFunction(display,gc,alu);
        XSetForeground(display,gc,0x817f3b19);XSetBackground(display,gc,0xf1392b47);
        XCopyPlane(display,source,dest,gc,2,1,13,7,3,2,0x80000000);
        for(unsigned y=0;y<7;++y)for(unsigned x=0;x<13;++x){unsigned i=(y+2)*W+x+3;
            expected[i]=op(alu,s[(y+1)*W+x+2]&0x80000000?0x817f3b19:0xf1392b47,expected[i])&bits;}
        verify(dest,W,H,expected,bits,ZPixmap,"depth32 alpha CopyPlane",alu);
        XSetFunction(display,mg,GXcopy);XSetForeground(display,mg,1);XFillRectangle(display,mono,mg,0,0,W,H);
        for(unsigned i=0;i<W*H;++i)expected[i]=1;
        XSetFunction(display,mg,alu);XSetForeground(display,mg,1);XSetBackground(display,mg,0);
        XCopyPlane(display,source,mono,mg,2,1,13,7,3,2,0x80000000);
        for(unsigned y=0;y<7;++y)for(unsigned x=0;x<13;++x)expected[(y+2)*W+x+3]=op(alu,!!(s[(y+1)*W+x+2]&0x80000000),1)&1;
        verify(mono,W,H,expected,1,ZPixmap,"depth32 alpha to bitmap",alu);
        upload(dest,W,H,initial);memcpy(expected,initial,sizeof(expected));XSetFunction(display,gc,alu);
        XCopyArea(display,dest,dest,gc,2,1,13,7,3,2);
        for(unsigned y=0;y<7;++y)for(unsigned x=0;x<13;++x){unsigned i=(y+2)*W+x+3;
            expected[i]=op(alu,initial[(y+1)*W+x+2],initial[i])&bits;}
        verify(dest,W,H,expected,bits,ZPixmap,"depth32 overlapping copy",alu);
        upload(dest,W,H,initial);verify(dest,W,H,initial,0x80000000,XYPixmap,"depth32 planar reply",alu);
    }
    XFreeGC(display,mg);XFreePixmap(display,mono);XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
    gc=saved_gc;depth=saved_depth;bits=saved_bits;

    XRenderPictFormat *f[12];Pixmap p[12],src[12];GC g[12];Picture pd[12],ps[12];
    unsigned long pixels[12][W*H],original[12][W*H];
    for(unsigned i=0;i<12;++i){unsigned five=i<2,four=i>=10,alpha=i>=4 && i<=6,swap=i&1,rotate=i==6 || i==7;
        XRenderPictFormat wanted={.type=PictTypeDirect,.depth=five || four?15:i>=4?32:24,
            .direct={.red=rotate?8:swap?(four?8:five?10:16):0,.redMask=four?15:five?31:255,
                .green=rotate?16:four?4:five?5:8,.greenMask=four?15:five?31:255,
                .blue=rotate?24:swap?0:(four?8:five?10:16),.blueMask=four?15:five?31:255,.alpha=alpha && !rotate?24:0,.alphaMask=alpha?255:0}};
        f[i]=XRenderFindFormat(display,PictFormatType|PictFormatDepth|PictFormatRed|PictFormatRedMask|
            PictFormatGreen|PictFormatGreenMask|PictFormatBlue|PictFormatBlueMask|PictFormatAlpha|PictFormatAlphaMask,&wanted,0);
        if(!f[i] && i==4)f[i]=XRenderFindStandardFormat(display,PictStandardARGB32);
        if(!f[i] && i==8)f[i]=XRenderFindFormat(display,PictFormatType|PictFormatDepth|PictFormatRedMask|
            PictFormatGreenMask|PictFormatBlueMask|PictFormatAlphaMask,&wanted,0);
        if(!f[i] && i==9)f[i]=f[8];
        if(!f[i]){fprintf(stderr,"ps2-gpu2d: missing RGBA format %u\n",i);exit(2);}
        if(i>=4)printf("ps2-gpu2d: ARGB format index=%u red=%d green=%d blue=%d alpha=%d\n",
            i,f[i]->direct.red,f[i]->direct.green,f[i]->direct.blue,f[i]->direct.alpha);
        p[i]=XCreatePixmap(display,root,W,H,f[i]->depth);src[i]=XCreatePixmap(display,root,W,H,f[i]->depth);
        g[i]=XCreateGC(display,p[i],0,NULL);XSetGraphicsExposures(display,g[i],False);
        for(unsigned k=0;k<W*H;++k){unsigned a=(k*73+29)&255,da=(k*37+131)&255;
            unsigned mask=four?0xfff:five?0x7fff:0xffffff;
            pixels[i][k]=alpha?((uint32_t)a<<24)|(((k*17+191)&255)*a+127)/255|
                ((((k*37+13)&255)*a+127)/255<<8)|((((k*53+127)&255)*a+127)/255<<16):((k*6173+0x7181a5)^(k*0x30307))&mask;
            original[i][k]=alpha?((uint32_t)da<<24)|(((k*43+127)&255)*da+127)/255|
                ((((k*19+73)&255)*da+127)/255<<8)|((((k*59+17)&255)*da+127)/255<<16):((k*8191+0x312915)^(k*0x10103))&mask;
            if(rotate){pixels[i][k]=(pixels[i][k]<<8)|(pixels[i][k]>>24);original[i][k]=(original[i][k]<<8)|(original[i][k]>>24);}
        }
        format_upload(src[i],g[i],f[i],pixels[i]);pd[i]=XRenderCreatePicture(display,p[i],f[i],0,NULL);
        ps[i]=XRenderCreatePicture(display,src[i],f[i],0,NULL);
    }
    Pixmap mask=XCreatePixmap(display,root,7,5,8);GC mg8=XCreateGC(display,mask,0,NULL);uint32_t mbits[12]={0};
    for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x){unsigned a=(y*7+x)*73&255;
        ((unsigned char*)mbits)[y*8+x]=a;XSetForeground(display,mg8,a);XDrawPoint(display,mask,mg8,x,y);}
    XRenderPictureAttributes attr={.repeat=RepeatReflect};Picture pm=XRenderCreatePicture(display,mask,
        XRenderFindStandardFormat(display,PictStandardA8),CPRepeat,&attr);
    XFixed coefficients[]={2*65536,65536,2*65536,-65536};
    pixman_transform_t transform={{{98304,0,-21845},{0,32768,21845},{65536/15,65536/19,65536}}};
    XTransform xt;memcpy(xt.matrix,transform.matrix,sizeof(xt.matrix));
    for(unsigned si=0;si<12;++si)for(unsigned di=0;di<12;++di){
        if(si<4 && di<4)continue;
        /* Qualify all basic operators on four ARGB pairs, and Src/Over for
         * each opaque layout in both conversion directions. */
        unsigned operations=si>=4 && di>=4?13:2;
        for(unsigned filter=0;filter<3;++filter)for(unsigned j=0;j<operations;++j){unsigned operation=operations==2?(j?PictOpOver:PictOpSrc):j;
            XRenderSetPictureTransform(display,ps[si],&xt);XRenderChangePicture(display,ps[si],CPRepeat,&attr);
            XRenderSetPictureFilter(display,ps[si],filter==2?FilterConvolution:filter==1?FilterBilinear:FilterNearest,
                filter==2?coefficients:NULL,filter==2?4:0);format_upload(p[di],g[di],f[di],original[di]);
            void *sb,*db;pixman_image_t *sref=reference_pixels(pixels[si],f[si],&sb),*dref=reference_pixels(original[di],f[di],&db);
            pixman_image_t *mref=pixman_image_create_bits(PIXMAN_a8,7,5,mbits,8);if(!mref)exit(2);
            pixman_image_set_repeat(mref,PIXMAN_REPEAT_REFLECT);
            if(!pixman_image_set_transform(sref,&transform) || !pixman_image_set_filter(sref,
                filter==2?PIXMAN_FILTER_CONVOLUTION:filter==1?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
                filter==2?coefficients:NULL,filter==2?4:0))exit(2);
            pixman_image_set_repeat(sref,PIXMAN_REPEAT_REFLECT);
            pixman_image_composite32((pixman_op_t)operation,sref,mref,dref,-1,-1,-1,1,2,1,2,2);
            printf("ps2-gpu2d: Render ARGB source=%u destination=%u filter=%u op=%u\n",si,di,filter,operation);
            XRenderComposite(display,operation,ps[si],pm,pd[di],-1,-1,-1,1,2,1,2,2);
            format_verify(p[di],f[di],db,"ARGB Render against pixman",operation);
            pixman_image_unref(sref);pixman_image_unref(dref);pixman_image_unref(mref);free(sb);free(db);
        }
    }
    XRenderColor color={.red=73*257,.green=31*257,.blue=51*257,.alpha=129*257};Picture solid=XRenderCreateSolidFill(display,&color);
    pixman_color_t pc={.red=color.red,.green=color.green,.blue=color.blue,.alpha=color.alpha};
    for(unsigned di=4;di<12;++di)for(unsigned operation=0;operation<13;++operation){
        format_upload(p[di],g[di],f[di],original[di]);void *db;
        pixman_image_t *dref=reference_pixels(original[di],f[di],&db),*sref=pixman_image_create_solid_fill(&pc);if(!sref)exit(2);
        pixman_image_composite32((pixman_op_t)operation,sref,NULL,dref,0,0,0,0,2,1,3,2);
        printf("ps2-gpu2d: Render ARGB solid destination=%u op=%u\n",di,operation);
        XRenderComposite(display,operation,solid,None,pd[di],0,0,0,0,2,1,3,2);
        format_verify(p[di],f[di],db,"ARGB solid against pixman",operation);
        pixman_image_unref(sref);pixman_image_unref(dref);free(db);
    }
    for(unsigned di=4;di<12;++di)for(unsigned filter=0;filter<3;++filter){
        Picture alias=XRenderCreatePicture(display,p[di],f[di^1],0,NULL);
        XRenderSetPictureTransform(display,alias,&xt);XRenderChangePicture(display,alias,CPRepeat,&attr);
        XRenderSetPictureFilter(display,alias,filter==2?FilterConvolution:filter==1?FilterBilinear:FilterNearest,
            filter==2?coefficients:NULL,filter==2?4:0);format_upload(p[di],g[di],f[di],original[di]);
        void *sb,*db;pixman_image_t *sref=reference_pixels(original[di],f[di^1],&sb),*dref=reference_pixels(original[di],f[di],&db);
        if(!pixman_image_set_transform(sref,&transform) || !pixman_image_set_filter(sref,
            filter==2?PIXMAN_FILTER_CONVOLUTION:filter==1?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
            filter==2?coefficients:NULL,filter==2?4:0))exit(2);
        pixman_image_set_repeat(sref,PIXMAN_REPEAT_REFLECT);
        pixman_image_composite32(PIXMAN_OP_OVER,sref,NULL,dref,W-2,-1,0,0,2,1,2,2);
        printf("ps2-gpu2d: Render ARGB alias destination=%u filter=%u\n",di,filter);
        XRenderComposite(display,PictOpOver,alias,None,pd[di],W-2,-1,0,0,2,1,2,2);
        format_verify(p[di],f[di],db,"ARGB alias against pixman",PictOpOver);
        pixman_image_unref(sref);pixman_image_unref(dref);free(sb);free(db);XRenderFreePicture(display,alias);
    }
    XRenderPictFormat *a4_format=XRenderFindStandardFormat(display,PictStandardA4);
    if(!a4_format){
        if(!getenv("PS2_GS_ORACLE")){fprintf(stderr,"ps2-gpu2d: required A4 format missing\n");exit(2);}
        skipped_a4_checks=1416;
        printf("ps2-gpu2d: oracle_A4_Render_section_skipped=1416\n");
    }
    Pixmap ca4_bitmap=None;GC ca4_gc=NULL;uint32_t ca4_bits[10]={0};
    if(a4_format){
        ca4_bitmap=XCreatePixmap(display,root,7,5,4);ca4_gc=XCreateGC(display,ca4_bitmap,0,NULL);
        for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x){unsigned value=((y*7+x)*73+29)&15;
            ((unsigned char*)ca4_bits)[y*8+x]=value;
            XSetForeground(display,ca4_gc,value);XDrawPoint(display,ca4_bitmap,ca4_gc,x,y);
        }
    }
    Pixmap ca_bitmap=XCreatePixmap(display,root,7,5,1);GC ca_gc=XCreateGC(display,ca_bitmap,0,NULL);
    uint32_t ca_bits[5]={0};
    for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x){unsigned value=((x/2+y)%3)!=0;
        if(value)ca_bits[y]|=1U<<x;
        XSetForeground(display,ca_gc,value);XDrawPoint(display,ca_bitmap,ca_gc,x,y);
    }
    for(unsigned mask_layout=0;mask_layout<15;++mask_layout){
        if(mask_layout==13 && !a4_format)continue;
        XRenderPictureAttributes ca={.repeat=RepeatReflect,.component_alpha=True};
        XRenderPictFormat *cm_format=mask_layout<12?f[mask_layout]:
            mask_layout==13?a4_format:XRenderFindStandardFormat(display,mask_layout==12?PictStandardA1:PictStandardA8);
        if(!cm_format)exit(2);
        Picture cm=XRenderCreatePicture(display,mask_layout<12?src[mask_layout]:mask_layout==12?ca_bitmap:mask_layout==13?ca4_bitmap:mask,
            cm_format,CPRepeat|CPComponentAlpha,&ca);
        XRenderSetPictureTransform(display,cm,&xt);
        for(unsigned di=0;di<12;++di)for(unsigned filter=0;filter<3;++filter)
        for(unsigned operation=0;operation<13;++operation){
            format_upload(p[di],g[di],f[di],original[di]);
            void *sb,*mb=NULL,*db;
            pixman_image_t *si=reference_pixels(pixels[4],f[4],&sb),
                *mi=mask_layout<12?reference_pixels(pixels[mask_layout],f[mask_layout],&mb):
                    pixman_image_create_bits(mask_layout==12?PIXMAN_a1:mask_layout==13?PIXMAN_x4a4:PIXMAN_a8,7,5,
                        mask_layout==12?ca_bits:mask_layout==13?ca4_bits:mbits,mask_layout==12?4:8),
                *di_ref=reference_pixels(original[di],f[di],&db);
            if(!mi)exit(2);
            pixman_filter_t pf=filter==2?PIXMAN_FILTER_CONVOLUTION:filter==1?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST;
            pixman_image_set_component_alpha(mi,True);
            pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);pixman_image_set_repeat(mi,PIXMAN_REPEAT_REFLECT);
            if(!pixman_image_set_transform(si,&transform) || !pixman_image_set_transform(mi,&transform) ||
                !pixman_image_set_filter(si,pf,filter==2?coefficients:NULL,filter==2?4:0) ||
                !pixman_image_set_filter(mi,pf,filter==2?coefficients:NULL,filter==2?4:0))exit(2);
            XRenderSetPictureTransform(display,ps[4],&xt);XRenderChangePicture(display,ps[4],CPRepeat,&ca);
            const char *xf=filter==2?FilterConvolution:filter==1?FilterBilinear:FilterNearest;
            XRenderSetPictureFilter(display,ps[4],xf,filter==2?coefficients:NULL,filter==2?4:0);
            XRenderSetPictureFilter(display,cm,xf,filter==2?coefficients:NULL,filter==2?4:0);
            pixman_image_composite32((pixman_op_t)operation,si,mi,di_ref,-1,-1,2,-1,2,1,3,2);
            printf("ps2-gpu2d: Render component-alpha mask=%u destination=%u filter=%u op=%u\n",mask_layout,di,filter,operation);
            XRenderComposite(display,operation,ps[4],cm,pd[di],-1,-1,2,-1,2,1,3,2);
            format_verify(p[di],f[di],db,"component alpha against pixman",operation);
            pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di_ref);free(sb);free(mb);free(db);
        }
        XRenderFreePicture(display,cm);
    }
    XFreeGC(display,ca_gc);XFreePixmap(display,ca_bitmap);
    if(ca4_gc){XFreeGC(display,ca4_gc);XFreePixmap(display,ca4_bitmap);}
    Pixmap coverage_source[3],coverage_destination[3];GC coverage_gc[3];Picture coverage_ps[3],coverage_pd[3];
    uint32_t coverage_bits[3][12]={{0}},coverage_initial[3][12]={{0}};
    for(unsigned kind=0;kind<3;++kind){unsigned cdepth=kind==0?1:kind==1?4:8;
        if(kind==1 && !a4_format)continue;
        XRenderPictFormat *cf=XRenderFindStandardFormat(display,kind==0?PictStandardA1:kind==1?PictStandardA4:PictStandardA8);
        if(!cf)exit(2);
        coverage_source[kind]=XCreatePixmap(display,root,7,5,cdepth);
        coverage_destination[kind]=XCreatePixmap(display,root,7,5,cdepth);
        coverage_gc[kind]=XCreateGC(display,coverage_source[kind],0,NULL);
        for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x){unsigned a=kind?((y*7+x)*73+29)&(kind==1?15:255):((x/2+y)%3)!=0;
            unsigned d=kind?((y*7+x)*37+131)&(kind==1?15:255):((x+y)%3)!=0;
            if(kind){((unsigned char*)coverage_bits[kind])[y*8+x]=a;((unsigned char*)coverage_initial[kind])[y*8+x]=d;}
            else{coverage_bits[kind][y]|=a<<x;coverage_initial[kind][y]|=d<<x;}
            XSetForeground(display,coverage_gc[kind],a);XDrawPoint(display,coverage_source[kind],coverage_gc[kind],x,y);
        }
        coverage_ps[kind]=XRenderCreatePicture(display,coverage_source[kind],cf,CPRepeat,&attr);
        coverage_pd[kind]=XRenderCreatePicture(display,coverage_destination[kind],cf,0,NULL);
    }
    for(unsigned direction=0;direction<2;++direction)for(unsigned kind=0;kind<3;++kind)
    for(unsigned colour=0;colour<12;++colour)for(unsigned filter=0;filter<3;++filter)
    for(unsigned operation=0;operation<(direction?14U:13U);++operation){
        if(kind==1 && !a4_format)continue;
        if(direction && filter && operation==PictOpSaturate)continue;
        unsigned stride=kind?8:4;uint32_t cbits[12];memcpy(cbits,coverage_initial[kind],sizeof(cbits));
        void *sb=NULL,*db=NULL;pixman_image_t *ci,*di,*si;
        if(direction){
            for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x){unsigned d=kind?((unsigned char*)cbits)[y*8+x]:(cbits[y]>>x)&1;
                XSetForeground(display,coverage_gc[kind],d);XDrawPoint(display,coverage_destination[kind],coverage_gc[kind],x,y);}
            ci=pixman_image_create_bits(kind==0?PIXMAN_a1:kind==1?PIXMAN_x4a4:PIXMAN_a8,7,5,cbits,stride);
            si=reference_pixels(pixels[colour],f[colour],&sb);di=ci;
        }else{
            format_upload(p[colour],g[colour],f[colour],original[colour]);
            ci=pixman_image_create_bits(kind==0?PIXMAN_a1:kind==1?PIXMAN_x4a4:PIXMAN_a8,7,5,coverage_bits[kind],stride);
            si=ci;di=reference_pixels(original[colour],f[colour],&db);
        }
        pixman_image_t *mi=pixman_image_create_bits(PIXMAN_a8,7,5,mbits,8);if(!ci || !mi)exit(2);
        pixman_image_set_repeat(mi,PIXMAN_REPEAT_REFLECT);pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);
        if(!pixman_image_set_transform(si,&transform) || !pixman_image_set_filter(si,
            filter==2?PIXMAN_FILTER_CONVOLUTION:filter==1?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
            filter==2?coefficients:NULL,filter==2?4:0))exit(2);
        Picture xs=direction?ps[colour]:coverage_ps[kind],xd=direction?coverage_pd[kind]:pd[colour];
        XRenderSetPictureTransform(display,xs,&xt);XRenderChangePicture(display,xs,CPRepeat,&attr);
        XRenderSetPictureFilter(display,xs,filter==2?FilterConvolution:filter==1?FilterBilinear:FilterNearest,
            filter==2?coefficients:NULL,filter==2?4:0);
        pixman_image_composite32((pixman_op_t)operation,si,mi,di,-1,-1,-1,1,2,1,3,2);
        printf("ps2-gpu2d: Render coverage direction=%s A%u colour=%u filter=%u op=%u\n",
            direction?"colour-to-alpha":"alpha-to-colour",kind==0?1:kind==1?4:8,colour,filter,operation);
        XRenderComposite(display,operation,xs,pm,xd,-1,-1,-1,1,2,1,3,2);
        if(direction){unsigned long expected[35];for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x){
            expected[y*7+x]=kind?((unsigned char*)cbits)[y*8+x]:(cbits[y]>>x)&1;}
            verify(coverage_destination[kind],7,5,expected,kind==0?1:kind==1?15:255,ZPixmap,"colour-to-alpha Render",operation);
        }else format_verify(p[colour],f[colour],db,"alpha-to-colour Render",operation);
        pixman_image_unref(si);pixman_image_unref(di);pixman_image_unref(mi);free(sb);free(db);
    }
    for(unsigned kind=0;kind<3;++kind){if(kind==1 && !a4_format)continue;XRenderFreePicture(display,coverage_ps[kind]);XRenderFreePicture(display,coverage_pd[kind]);
        XFreeGC(display,coverage_gc[kind]);XFreePixmap(display,coverage_source[kind]);XFreePixmap(display,coverage_destination[kind]);}
    XRenderFreePicture(display,solid);XRenderFreePicture(display,pm);XFreeGC(display,mg8);XFreePixmap(display,mask);
    for(unsigned i=0;i<12;++i){XRenderFreePicture(display,ps[i]);XRenderFreePicture(display,pd[i]);
        XFreeGC(display,g[i]);XFreePixmap(display,p[i]);XFreePixmap(display,src[i]);}
    printf("ps2-gpu2d: depth32 core and ARGB colour/alpha match independent oracle\n");
}
static void byte_colour_checks(Window root,unsigned word,unsigned saturate)
{
    const pixman_format_code_t byte_codes[]={PIXMAN_r3g3b2,PIXMAN_b2g3r3,PIXMAN_a2r2g2b2,PIXMAN_a2b2g2r2};
    const pixman_format_code_t word_codes[]={PIXMAN_r5g6b5,PIXMAN_b5g6r5,PIXMAN_a1r5g5b5,PIXMAN_a1b5g5r5,
        PIXMAN_a4r4g4b4,PIXMAN_a4b4g4r4,PIXMAN_x1r5g5b5,PIXMAN_x1b5g5r5,PIXMAN_x4r4g4b4,PIXMAN_x4b4g4r4};
    const pixman_format_code_t *codes=word?word_codes:byte_codes;
    unsigned bpp=word?16:8,stride=(W*(bpp/8)+3)&~3U;
    XRenderPictFormat *af=XRenderFindStandardFormat(display,PictStandardARGB32);if(!af)exit(2);
    Pixmap np=XCreatePixmap(display,root,W,H,32);GC ng=XCreateGC(display,np,0,NULL);
    Picture ns=XRenderCreatePicture(display,np,af,0,NULL);
    unsigned long rgba[W*H];
    for(unsigned i=0;i<W*H;++i){unsigned a=(i*73+29)&255;
        rgba[i]=((uint32_t)a<<24)|((((i*17+191)&255)*a+127)/255<<16)|
            ((((i*37+13)&255)*a+127)/255<<8)|(((i*53+127)&255)*a+127)/255;
    }
    uint32_t raw[H*stride/4];
    for(unsigned i=0;i<sizeof(raw);++i)((unsigned char*)raw)[i]=i*73+19;
    XFixed coefficients[]={2*65536,65536,2*65536,-65536};
    pixman_transform_t transform={{{98304,0,-21845},{0,32768,21845},{65536/15,65536/19,65536}}};
    XTransform xt;memcpy(xt.matrix,transform.matrix,sizeof(xt.matrix));
    for(unsigned kind=0;kind<(word?10U:4U);++kind){
        XRenderPictFormat wanted={.type=PictTypeDirect,.depth=8,.direct={.red=kind<2?(kind?0:5):(kind==2?4:0),
            .green=kind<2?(kind?3:2):2,.blue=kind<2?(kind?6:0):(kind==2?0:4),
            .redMask=kind<2?7:3,.greenMask=kind<2?7:3,.blueMask=3,.alpha=kind<2?0:6,.alphaMask=kind<2?0:3}};
        if(word){
            unsigned n=kind==4 || kind==5 || kind>=8?4:5,green=kind<2?6:n,swap=kind&1;
            unsigned alpha=kind==2 || kind==3?1:kind==4 || kind==5?4:0;
            wanted=(XRenderPictFormat){.type=PictTypeDirect,.depth=16,.direct={
                .red=swap?0:n+green,.green=n,.blue=swap?n+green:0,.alpha=2*n+green,
                .redMask=(1U<<n)-1,.greenMask=(1U<<green)-1,.blueMask=(1U<<n)-1,.alphaMask=(1U<<alpha)-1}};
            if(!alpha)wanted.direct.alpha=0;
        }
        XRenderPictFormat *f=XRenderFindFormat(display,PictFormatType|PictFormatDepth|PictFormatRed|PictFormatRedMask|
            PictFormatGreen|PictFormatGreenMask|PictFormatBlue|PictFormatBlueMask|PictFormatAlpha|PictFormatAlphaMask,&wanted,0);
        if(!f){
            if(word || !getenv("PS2_GS_ORACLE")){fprintf(stderr,"ps2-gpu2d: required raw colour format depth=%u kind=%u missing\n",bpp,kind);exit(2);}
            unsigned skipped=saturate?4:156;
            skipped_byte_checks+=skipped;printf("ps2-gpu2d: oracle_byte_colour_skipped=%u format=%u\n",skipped,kind);continue;
        }
        Pixmap bp=XCreatePixmap(display,root,W,H,bpp);GC bg=XCreateGC(display,bp,0,NULL);
        Pixmap mp=XCreatePixmap(display,root,W,H,bpp);GC mg=XCreateGC(display,mp,0,NULL);
        Picture bs=XRenderCreatePicture(display,bp,f,0,NULL);
        XRenderPictureAttributes ma={.repeat=RepeatNormal,.component_alpha=True};
        Picture bm=XRenderCreatePicture(display,mp,f,CPRepeat|CPComponentAlpha,&ma);
        for(unsigned direction=0;direction<2;++direction)for(unsigned filter=0;filter<(saturate?1U:3U);++filter)
        for(unsigned masked=0;masked<2;++masked)
        for(unsigned operation=saturate?PictOpSaturate:0;operation<=(saturate?PictOpSaturate:PictOpAdd);++operation){
            printf("ps2-gpu2d: raw colour depth=%u kind=%u direction=%u filter=%u masked=%u op=%u\n",bpp,kind,direction,filter,masked,operation);
            fflush(stdout);
            XImage *image=XCreateImage(display,DefaultVisual(display,DefaultScreen(display)),bpp,ZPixmap,0,NULL,W,H,32,stride);
            if(!image)exit(2);
            image->data=malloc(sizeof(raw));if(!image->data)exit(2);memcpy(image->data,raw,sizeof(raw));
            XPutImage(display,bp,bg,image,0,0,0,0,W,H);XPutImage(display,mp,mg,image,0,0,0,0,W,H);
            XDestroyImage(image);format_upload(np,ng,af,rgba);
            Picture ps=direction?bs:ns,pd=direction?ns:bs;
            XRenderPictureAttributes repeat={.repeat=RepeatReflect};
            XRenderSetPictureTransform(display,ps,&xt);XRenderChangePicture(display,ps,CPRepeat,&repeat);
            XTransform identity={{{65536,0,0},{0,65536,0},{0,0,65536}}};repeat.repeat=RepeatNone;
            XRenderSetPictureTransform(display,pd,&identity);XRenderChangePicture(display,pd,CPRepeat,&repeat);
            XRenderSetPictureFilter(display,pd,FilterNearest,NULL,0);
            const char *xf=filter==2?FilterConvolution:filter==1?FilterBilinear:FilterNearest;
            XRenderSetPictureFilter(display,ps,xf,filter==2?coefficients:NULL,filter==2?4:0);
            XRenderSetPictureFilter(display,bm,xf,filter==2?coefficients:NULL,filter==2?4:0);
            uint32_t br[H*stride/4];memcpy(br,raw,sizeof(br));void *nr;
            pixman_image_t *ni=reference_pixels(rgba,af,&nr),*bi=pixman_image_create_bits(codes[kind],W,H,br,stride),
                *mi=pixman_image_create_bits(codes[kind],W,H,raw,stride);if(!bi || !mi)exit(2);
            pixman_image_t *si=direction?bi:ni,*di=direction?ni:bi;
            pixman_filter_t pf=filter==2?PIXMAN_FILTER_CONVOLUTION:filter==1?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST;
            if(!pixman_image_set_transform(si,&transform) || !pixman_image_set_filter(si,pf,filter==2?coefficients:NULL,filter==2?4:0) ||
                !pixman_image_set_filter(mi,pf,filter==2?coefficients:NULL,filter==2?4:0))exit(2);
            pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);pixman_image_set_repeat(mi,PIXMAN_REPEAT_NORMAL);pixman_image_set_component_alpha(mi,True);
            unsigned height=saturate?1:3;
            pixman_image_composite32((pixman_op_t)operation,si,masked?mi:NULL,di,-1,-1,-2,1,3,2,7,height);
            XRenderComposite(display,operation,ps,masked?bm:None,pd,-1,-1,-2,1,3,2,7,height);
            if(direction)format_verify(np,af,nr,"byte colour to ARGB",operation);
            else{unsigned long expected[W*H];for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
                uint32_t value=0;memcpy(&value,(unsigned char*)br+y*stride+x*(bpp/8),bpp/8);expected[y*W+x]=value;}
                unsigned mask=word?(kind>=8?0xfff:kind>=6?0x7fff:0xffff):255;
                verify(bp,W,H,expected,mask,ZPixmap,"ARGB to raw colour",operation);}
            pixman_image_unref(ni);pixman_image_unref(bi);pixman_image_unref(mi);free(nr);
        }
        XRenderFreePicture(display,bm);XRenderFreePicture(display,bs);XFreeGC(display,bg);XFreePixmap(display,bp);
        XFreeGC(display,mg);XFreePixmap(display,mp);
    }
    XRenderFreePicture(display,ns);XFreeGC(display,ng);XFreePixmap(display,np);
}
static void coverage_core_checks(Window root)
{
    enum { CW=41,CH=11,SW=33,SH=7 };
    const unsigned depths[]={1,4,8,16};
    for(unsigned k=0;k<4;++k){unsigned bpp=depths[k];
        unsigned long limit=(1UL<<bpp)-1,initial[CW*CH],expected[CW*CH],values[SW*SH];
        Pixmap target=XCreatePixmap(display,root,CW,CH,bpp),source=XCreatePixmap(display,root,SW,SH,bpp);
        GC g=XCreateGC(display,target,0,NULL);XSetGraphicsExposures(display,g,False);
        XImage *base=XCreateImage(display,DefaultVisual(display,DefaultScreen(display)),bpp,ZPixmap,0,NULL,CW,CH,32,0);
        XImage *input=XCreateImage(display,DefaultVisual(display,DefaultScreen(display)),bpp,ZPixmap,0,NULL,SW,SH,32,0);
        if(!base || !input)exit(2);
        base->data=calloc(CH,base->bytes_per_line);input->data=calloc(SH,input->bytes_per_line);
        if(!base->data || !input->data)exit(2);
        for(unsigned y=0;y<CH;++y)for(unsigned x=0;x<CW;++x){
            initial[y*CW+x]=((y*CW+x)*37+11)&limit;XPutPixel(base,x,y,initial[y*CW+x]);}
        for(unsigned y=0;y<SH;++y)for(unsigned x=0;x<SW;++x){
            values[y*SW+x]=((y*SW+x)*73+19)&limit;XPutPixel(input,x,y,values[y*SW+x]);}
        XPutImage(display,source,g,input,0,0,0,0,SW,SH);
        for(unsigned alu=0;alu<16;++alu)for(unsigned kind=0;kind<5;++kind){
            unsigned long mask=bpp==1?1:limit&0xa55a;memcpy(expected,initial,sizeof(expected));
            XSetFunction(display,g,GXcopy);XSetPlaneMask(display,g,limit);XPutImage(display,target,g,base,0,0,0,0,CW,CH);
            XSetFunction(display,g,alu);XSetPlaneMask(display,g,mask);XSetForeground(display,g,0x69c3&limit);XSetBackground(display,g,0x963c&limit);
            for(unsigned y=2;y<9;++y)for(unsigned x=5;x<38;++x){unsigned i=y*CW+x;
                unsigned long s=kind==0?0x69c3&limit:kind==4?(values[(y-2)*SW+x-5]&1?0x69c3:0x963c)&limit:values[(y-2)*SW+x-5];
                expected[i]=(expected[i]&~mask)|(op(alu,s,expected[i])&mask);}
            if(kind==0)XFillRectangle(display,target,g,5,2,SW,SH);
            else if(kind==1)XCopyArea(display,source,target,g,0,0,SW,SH,5,2);
            else if(kind==4)XCopyPlane(display,source,target,g,0,0,SW,SH,5,2,1);
            else{
                XImage *image=XCreateImage(display,DefaultVisual(display,DefaultScreen(display)),bpp,
                    kind==2?ZPixmap:XYPixmap,0,NULL,SW,SH,32,0);
                if(!image)exit(2);
                image->data=calloc(SH*(kind==2?1:bpp),image->bytes_per_line);if(!image->data)exit(2);
                for(unsigned y=0;y<SH;++y)for(unsigned x=0;x<SW;++x)XPutPixel(image,x,y,values[y*SW+x]);
                XPutImage(display,target,g,image,0,0,5,2,SW,SH);XDestroyImage(image);
            }
            verify(target,CW,CH,expected,limit,ZPixmap,"A1/A8 core Boolean drawing",alu);
        }
        if(bpp!=1)for(unsigned raw32=0;raw32<2;++raw32){
            unsigned cd=raw32?32:depth;unsigned long cmask=cd==32?UINT32_MAX:(1UL<<cd)-1;
            Pixmap colour=XCreatePixmap(display,root,CW,CH,cd);GC cg=XCreateGC(display,colour,0,NULL);
            XSetGraphicsExposures(display,cg,False);
            XImage *image=XCreateImage(display,DefaultVisual(display,DefaultScreen(display)),cd,ZPixmap,0,NULL,CW,CH,32,0);
            if(!image)exit(2);
            image->data=calloc(CH,image->bytes_per_line);if(!image->data)exit(2);
            for(unsigned i=0;i<CW*CH;++i)initial[i]=((i*8191+0x817f3b19)^(i*0x03010507))&cmask;
            for(unsigned y=0;y<CH;++y)for(unsigned x=0;x<CW;++x)XPutPixel(image,x,y,initial[y*CW+x]);
            for(unsigned plane=0;plane<bpp;++plane)for(unsigned alu=0;alu<16;++alu)for(unsigned full=0;full<2;++full){
                unsigned long mask=full?cmask:cmask&0x5731a5cf;
                memcpy(expected,initial,sizeof(expected));XSetFunction(display,cg,GXcopy);XSetPlaneMask(display,cg,cmask);
                XPutImage(display,colour,cg,image,0,0,0,0,CW,CH);
                XSetFunction(display,cg,alu);XSetPlaneMask(display,cg,mask);
                XSetForeground(display,cg,cmask&0x817f3b19);XSetBackground(display,cg,cmask&0xf1392b47);
                for(unsigned y=2;y<9;++y)for(unsigned x=5;x<38;++x){unsigned i=y*CW+x;
                    unsigned long v=values[(y-2)*SW+x-5]&(1U<<plane)?cmask&0x817f3b19:cmask&0xf1392b47;
                    expected[i]=(expected[i]&~mask)|(op(alu,v,expected[i])&mask);}
                XCopyPlane(display,source,colour,cg,0,0,SW,SH,5,2,1U<<plane);
                verify(colour,CW,CH,expected,cmask,ZPixmap,"A8 CopyPlane to colour",alu);
            }
            XDestroyImage(image);XFreeGC(display,cg);XFreePixmap(display,colour);
        }
        XDestroyImage(input);XDestroyImage(base);XFreeGC(display,g);XFreePixmap(display,source);XFreePixmap(display,target);
    }
}
static void font_checks(Window root)
{
    enum { FW=240,FH=32 };unsigned long expected[FW*FH];
    const char *text="Native GS / Xorg / Mesa integration";
    XFontStruct *font=XLoadQueryFont(display,"6x13");if(!font)exit(2);
    Pixmap mono=XCreatePixmap(display,root,FW,FH,1),colour=XCreatePixmap(display,root,FW,FH,depth);
    GC mg=XCreateGC(display,mono,0,NULL);XSetFont(display,mg,font->fid);XSetForeground(display,mg,0);
    XFillRectangle(display,mono,mg,0,0,FW,FH);XSetForeground(display,mg,1);XDrawString(display,mono,mg,12,20,text,strlen(text));
    XImage *reference=XGetImage(display,mono,0,0,FW,FH,1,ZPixmap);if(!reference)exit(2);
    for(unsigned y=0;y<FH;++y)for(unsigned x=0;x<FW;++x)expected[y*FW+x]=XGetPixel(reference,x,y)?bits:bits&0x041821;
    XSetFont(display,gc,font->fid);XSetFillStyle(display,gc,FillSolid);XSetFunction(display,gc,GXcopy);XSetPlaneMask(display,gc,bits);
    XSetForeground(display,gc,bits&0x041821);XFillRectangle(display,colour,gc,0,0,FW,FH);XSetForeground(display,gc,bits);
    XDrawString(display,colour,gc,12,20,text,strlen(text));verify(colour,FW,FH,expected,bits,ZPixmap,"core font in owned pixmap",GXcopy);
    for(unsigned full=0;full<2;++full)for(unsigned alu=0;alu<16;++alu){
        unsigned long initial=bits&0x041821,ink=bits&0x713185,mask=full?bits:bits&0x51a153;
        XSetFunction(display,gc,GXcopy);XSetPlaneMask(display,gc,bits);XSetForeground(display,gc,initial);
        XFillRectangle(display,colour,gc,0,0,FW,FH);XSetForeground(display,gc,ink);
        XSetFunction(display,gc,alu);XSetPlaneMask(display,gc,mask);XDrawString(display,colour,gc,12,20,text,strlen(text));
        for(unsigned y=0;y<FH;++y)for(unsigned x=0;x<FW;++x)expected[y*FW+x]=XGetPixel(reference,x,y)?
            (initial&~mask)|(op(alu,ink,initial)&mask):initial;
        verify(colour,FW,FH,expected,bits,ZPixmap,"core text ALUs and plane masks",alu);
    }
    unsigned long tp[6];values(tp,3,2,0x135721);Pixmap tile=XCreatePixmap(display,root,3,2,depth);upload(tile,3,2,tp);
    XSetTile(display,gc,tile);XSetTSOrigin(display,gc,1,-2);
    for(unsigned alu=0;alu<16;++alu){
        unsigned long initial=bits&0x041821;XSetFunction(display,gc,GXcopy);XSetPlaneMask(display,gc,bits);
        XSetFillStyle(display,gc,FillSolid);XSetForeground(display,gc,initial);XFillRectangle(display,colour,gc,0,0,FW,FH);
        XSetFillStyle(display,gc,FillTiled);XSetFunction(display,gc,alu);XDrawString(display,colour,gc,12,20,text,strlen(text));
        for(unsigned y=0;y<FH;++y)for(unsigned x=0;x<FW;++x)expected[y*FW+x]=XGetPixel(reference,x,y)?
            op(alu,tp[modulo((int)y+2,2)*3+modulo((int)x-1,3)],initial)&bits:initial;
        verify(colour,FW,FH,expected,bits,ZPixmap,"tiled core text",alu);
    }
    XFreePixmap(display,tile);XSetFillStyle(display,gc,FillSolid);XSetFunction(display,gc,GXcopy);XSetPlaneMask(display,gc,bits);
    unsigned long initial=bits&0x041821,ink=bits&0x713185,bg=bits&0x025031,mask=bits&0x51a153;
    XSetForeground(display,gc,initial);XFillRectangle(display,colour,gc,0,0,FW,FH);
    XSetFunction(display,gc,GXxor);XSetPlaneMask(display,gc,mask);XSetForeground(display,gc,ink);XSetBackground(display,gc,bg);
    XDrawImageString(display,colour,gc,12,20,text,strlen(text));int advance=XTextWidth(font,text,strlen(text));
    for(unsigned y=0;y<FH;++y)for(unsigned x=0;x<FW;++x){unsigned long v=initial;
        if((int)x>=12 && (int)x<12+advance && (int)y>=20-font->ascent && (int)y<20+font->descent)v=(v&~mask)|(bg&mask);
        if(XGetPixel(reference,x,y))v=(v&~mask)|(ink&mask);
        expected[y*FW+x]=v;}
    verify(colour,FW,FH,expected,bits,ZPixmap,"masked image text forces GXcopy",GXcopy);
    XSetFunction(display,gc,GXcopy);XSetPlaneMask(display,gc,bits);XSetForeground(display,gc,bits);
    for(unsigned y=0;y<FH;++y)for(unsigned x=0;x<FW;++x)expected[y*FW+x]=XGetPixel(reference,x,y)?bits:bits&0x041821;
    Window w=XCreateSimpleWindow(display,root,100,100,FW,FH,0,0,0);XSetWindowAttributes a={.override_redirect=True};
    XChangeWindowAttributes(display,w,CWOverrideRedirect,&a);XMapWindow(display,w);XSync(display,False);
    XSetForeground(display,gc,bits&0x041821);XFillRectangle(display,w,gc,0,0,FW,FH);XSetForeground(display,gc,bits);
    XDrawString(display,w,gc,12,20,text,strlen(text));verify(w,FW,FH,expected,bits,ZPixmap,"core font in visible window",GXcopy);
    XSetForeground(display,gc,bits&0x080431);XFillRectangle(display,w,gc,0,0,FW,FH);
    for(unsigned i=0;i<FW*FH;++i)expected[i]=bits&0x080431;
    verify(w,FW,FH,expected,bits,ZPixmap,"visible clear after text",GXcopy);
    XDestroyWindow(display,w);XFreeGC(display,mg);XFreePixmap(display,mono);XFreePixmap(display,colour);XDestroyImage(reference);XFreeFont(display,font);
    printf("ps2-gpu2d: core text matches monochrome font reference in pixmap and window\n");
}
static void alpha_glyph_checks(Pixmap dest,const unsigned long *initial,XRenderPictFormat *f)
{
    XRenderPictFormat *af=XRenderFindStandardFormat(display,PictStandardA8);if(!af)exit(2);
    GlyphSet set=XRenderCreateGlyphSet(display,af);Glyph glyph=65;XGlyphInfo info={.width=7,.height=5,.xOff=7};
    uint32_t abits[10]={0};for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x)((unsigned char*)abits)[y*8+x]=(y*7+x)*73&255;
    XRenderAddGlyphs(display,set,&glyph,&info,1,(const char*)abits,sizeof(abits));
    Picture pd=XRenderCreatePicture(display,dest,f,0,NULL);
    for(unsigned overlap=0;overlap<2;++overlap)for(unsigned opaque=0;opaque<2;++opaque)for(unsigned aggregate=0;aggregate<2;++aggregate){
        printf("ps2-gpu2d: glyph opaque=%u aggregate=%u overlap=%u\n",opaque,aggregate,overlap);
        unsigned advance=overlap?4:7;info.xOff=advance;
        XRenderAddGlyphs(display,set,&glyph,&info,1,(const char*)abits,sizeof(abits));
        unsigned a=opaque?255:129;XRenderColor color={.red=(a*73/100)*257,.green=(a*31/100)*257,.blue=(a*51/100)*257,.alpha=a*257};
        Picture ps=XRenderCreateSolidFill(display,&color);upload(dest,W,H,initial);
        pixman_color_t c={.red=color.red,.green=color.green,.blue=color.blue,.alpha=color.alpha};
        uint32_t accumulated[20]={0};
        if(aggregate)for(unsigned i=0;i<2;++i)for(unsigned y=0;y<5;++y)for(unsigned x=0;x<7;++x){
            unsigned char *v=(unsigned char*)accumulated+y*16+x+i*advance;
            unsigned sum=*v+((unsigned char*)abits)[y*8+x];*v=sum>255?255:sum;}
        pixman_image_t *si=pixman_image_create_solid_fill(&c),*mi=pixman_image_create_bits(PIXMAN_a8,
            aggregate?advance+7:7,5,aggregate?accumulated:abits,aggregate?16:8);
        void *db;pixman_image_t *di=reference_pixels(initial,f,&db);if(!si || !mi)exit(2);
        for(unsigned i=0;i<(aggregate?1U:2U);++i)pixman_image_composite32(PIXMAN_OP_OVER,si,mi,di,0,0,0,0,2+i*advance,3,aggregate?advance+7:7,5);
        XRenderCompositeString8(display,PictOpOver,ps,pd,aggregate?af:NULL,set,0,0,2,3,"AA",2);
        unsigned long expected[W*H];unsigned cpp=depth==15?2:4,stride=(W*cpp+3)&~3U;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){uint32_t v=0;memcpy(&v,(unsigned char*)db+y*stride+x*cpp,cpp);expected[y*W+x]=v&bits;}
        verify(dest,W,H,expected,bits,ZPixmap,"A8 Render glyphs against pixman",PictOpOver);
        pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);free(db);XRenderFreePicture(display,ps);
    }
    XRenderFreePicture(display,pd);XRenderFreeGlyphSet(display,set);
    printf("ps2-gpu2d: A8 glyphs match pixman with direct and accumulated masks\n");
}
static void alpha_map_checks(Window root)
{
    XRenderPictFormat *format=XRenderFindVisualFormat(display,DefaultVisual(display,DefaultScreen(display)));
    if(!format)exit(2);
    Pixmap colour[3];GC colour_gc[3];Picture picture[3];unsigned long values_[3][W*H];
    for(unsigned i=0;i<3;++i){
        colour[i]=XCreatePixmap(display,root,W,H,depth);colour_gc[i]=XCreateGC(display,colour[i],0,NULL);
        XSetGraphicsExposures(display,colour_gc[i],False);values(values_[i],W,H,0x318181+i*0x714329);
        picture[i]=XRenderCreatePicture(display,colour[i],format,0,NULL);
    }
    XFixed coefficients[]={2*65536,65536,2*65536,-65536};
    pixman_transform_t transform={{{98304,0,-21845},{0,32768,21845},{65536/15,65536/19,65536}}};
    XTransform xt;memcpy(xt.matrix,transform.matrix,sizeof(xt.matrix));
    const int standard[]={PictStandardA1,PictStandardA4,PictStandardA8};
    XTransform unit={.matrix={{65536,0,0},{0,65536,0},{0,0,65536}}};
    for(unsigned kind=0;kind<3;++kind){
        XRenderPictFormat *af=XRenderFindStandardFormat(display,standard[kind]);
        if(!af){
            if(kind!=1 || !getenv("PS2_GS_ORACLE")){fprintf(stderr,"ps2-gpu2d: required alpha map format missing\n");exit(2);}
            skipped_a4_checks+=327;continue;
        }
        Pixmap map[3];GC map_gc[3];Picture alpha[3];unsigned long original[3][W*H];
        unsigned alpha_bits=kind==0?1:kind==1?4:8;
        unsigned stride=kind==0?((W+31)/32)*4:(W+3)&~3U;
        for(unsigned i=0;i<3;++i){
            map[i]=XCreatePixmap(display,root,W,H,af->depth);map_gc[i]=XCreateGC(display,map[i],0,NULL);
            XSetGraphicsExposures(display,map_gc[i],False);alpha[i]=XRenderCreatePicture(display,map[i],af,0,NULL);
            for(unsigned j=0;j<W*H;++j)original[i][j]=(j*73+29+i*37)&((1U<<alpha_bits)-1);
        }
        for(unsigned role=0;role<8;++role)for(unsigned filter=0;filter<(role<5?3U:1U);++filter)for(unsigned op_=0;op_<=PictOpAdd;++op_){
            if(role>=5 && op_!=PictOpClear && op_!=PictOpSrc && op_!=PictOpOver)continue;
            pixman_image_t *ref[3],*am[3];void *colour_bytes[3];unsigned char *map_bytes[3];
            for(unsigned i=0;i<3;++i){
                format_upload(colour[i],colour_gc[i],format,values_[i]);
                format_upload(map[i],map_gc[i],af,original[i]);
                ref[i]=reference_pixels(values_[i],format,colour_bytes+i);
                map_bytes[i]=calloc(stride,H);if(!map_bytes[i])exit(2);
                for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){unsigned value=original[i][y*W+x];
                    if(kind==0)map_bytes[i][y*stride+x/8]|=value<<(x&7);
                    else map_bytes[i][y*stride+x]=value;
                }
                am[i]=pixman_image_create_bits(kind==0?PIXMAN_a1:kind==1?PIXMAN_x4a4:PIXMAN_a8,
                    W,H,(uint32_t*)map_bytes[i],stride);if(!am[i])exit(2);
                Bool mapped=role<5?role>=3 || role==i:(i==0 && role!=6) || (i==2 && role!=5);
                XRenderPictureAttributes attributes={.alpha_map=mapped?alpha[i]:None,
                    .alpha_x_origin=i==0?-1:1,.alpha_y_origin=i==1?0:-1,.component_alpha=i==1 && role==4};
                XRenderChangePicture(display,picture[i],CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin|CPComponentAlpha,&attributes);
                if(mapped)pixman_image_set_alpha_map(ref[i],am[i],attributes.alpha_x_origin,attributes.alpha_y_origin);
                if(i==1)pixman_image_set_component_alpha(ref[i],attributes.component_alpha);
                if(i<2){
                    XRenderSetPictureTransform(display,picture[i],role<5?&xt:&unit);
                    XRenderPictureAttributes repeat={.repeat=role<5?RepeatReflect:RepeatNone};
                    XRenderChangePicture(display,picture[i],CPRepeat,&repeat);
                    XRenderSetPictureFilter(display,picture[i],filter==2?FilterConvolution:filter==1?FilterBilinear:FilterNearest,
                        filter==2?coefficients:NULL,filter==2?4:0);
                    pixman_transform_t identity={{{65536,0,0},{0,65536,0},{0,0,65536}}};
                    if(!pixman_image_set_transform(ref[i],role<5?&transform:&identity) || !pixman_image_set_filter(ref[i],
                        filter==2?PIXMAN_FILTER_CONVOLUTION:filter==1?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
                        filter==2?coefficients:NULL,filter==2?4:0))exit(2);
                    pixman_image_set_repeat(ref[i],role<5?PIXMAN_REPEAT_REFLECT:PIXMAN_REPEAT_NONE);
                }
            }
            pixman_image_composite32(op_,ref[0],role<5?ref[1]:NULL,ref[2],role<5?-1:2,1,2,1,3,2,5,3);
            printf("ps2-gpu2d: alpha maps depth=%u role=%u filter=%u op=%u\n",alpha_bits,role,filter,op_);
            XRenderComposite(display,op_,picture[0],role<5?picture[1]:None,picture[2],role<5?-1:2,1,2,1,3,2,5,3);
            format_verify(colour[2],format,colour_bytes[2],"alpha map colour against pixman",op_);
            if((role>=2 && role<5) || role==6 || role==7){
                unsigned long wanted[W*H];
                for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
                    wanted[y*W+x]=kind==0?(map_bytes[2][y*stride+x/8]>>(x&7))&1:
                        map_bytes[2][y*stride+x]&((1U<<alpha_bits)-1);
                verify(map[2],W,H,wanted,(1U<<alpha_bits)-1,ZPixmap,"alpha map reply against pixman",op_);
            }
            for(unsigned i=0;i<3;++i){pixman_image_unref(ref[i]);pixman_image_unref(am[i]);free(colour_bytes[i]);free(map_bytes[i]);}
        }
        for(unsigned i=0;i<3;++i){XRenderPictureAttributes none={.alpha_map=None};
            XRenderChangePicture(display,picture[i],CPAlphaMap,&none);
            XRenderFreePicture(display,alpha[i]);XFreeGC(display,map_gc[i]);XFreePixmap(display,map[i]);}
    }
    for(unsigned i=0;i<3;++i){XRenderFreePicture(display,picture[i]);XFreeGC(display,colour_gc[i]);XFreePixmap(display,colour[i]);}
    printf("ps2-gpu2d: GS alpha maps source/mask/destination and component alpha match pixman\n");
}
static void alpha_raw_alias_checks(Window root)
{
    const pixman_format_code_t codes[]={PIXMAN_a1,PIXMAN_x4a4,PIXMAN_a8,PIXMAN_r3g3b2,PIXMAN_b2g3r3,
        PIXMAN_a2r2g2b2,PIXMAN_a2b2g2r2,PIXMAN_r5g6b5,PIXMAN_b5g6r5,PIXMAN_a1r5g5b5,PIXMAN_a1b5g5r5,
        PIXMAN_a4r4g4b4,PIXMAN_a4b4g4r4,PIXMAN_x1r5g5b5,PIXMAN_x1b5g5r5,PIXMAN_x4r4g4b4,PIXMAN_x4b4g4r4};
    XRenderPictFormat *sf=XRenderFindStandardFormat(display,PictStandardARGB32);if(!sf)exit(2);
    Pixmap source=XCreatePixmap(display,root,W,H,32);GC sg=XCreateGC(display,source,0,NULL);
    XSetGraphicsExposures(display,sg,False);Picture ps=XRenderCreatePicture(display,source,sf,0,NULL);
    unsigned long rgba[W*H];for(unsigned i=0;i<W*H;++i){unsigned a=(i*73+29)&255;
        rgba[i]=((uint32_t)a<<24)|((((i*17+191)&255)*a+127)/255<<16)|
            ((((i*37+13)&255)*a+127)/255<<8)|(((i*53+127)&255)*a+127)/255;}
    pixman_transform_t transform={{{98304,0,-21845},{0,32768,21845},{0,0,65536}}};
    XTransform xt;memcpy(xt.matrix,transform.matrix,sizeof(xt.matrix));XFixed coefficients[]={2*65536,65536,2*65536,-65536};
    XRenderPictureAttributes repeat={.repeat=RepeatReflect};XRenderChangePicture(display,ps,CPRepeat,&repeat);
    XRenderSetPictureTransform(display,ps,&xt);
    for(unsigned layout=0;layout<17;++layout){
        unsigned bpp=layout==0?1:layout==1?4:layout<7?8:16;
        XRenderPictFormat *f;
        if(layout<3)f=XRenderFindStandardFormat(display,layout==0?PictStandardA1:layout==1?PictStandardA4:PictStandardA8);
        else{
            unsigned word=layout>=7,kind=layout-(word?7:3);
            XRenderPictFormat wanted={.type=PictTypeDirect,.depth=8,.direct={.red=kind<2?(kind?0:5):(kind==2?4:0),
                .green=kind<2?(kind?3:2):2,.blue=kind<2?(kind?6:0):(kind==2?0:4),
                .redMask=kind<2?7:3,.greenMask=kind<2?7:3,.blueMask=3,.alpha=kind<2?0:6,.alphaMask=kind<2?0:3}};
            if(word){unsigned n=kind==4 || kind==5 || kind>=8?4:5,green=kind<2?6:n,swap=kind&1;
                unsigned alpha=kind==2 || kind==3?1:kind==4 || kind==5?4:0;
                wanted=(XRenderPictFormat){.type=PictTypeDirect,.depth=16,.direct={.red=swap?0:n+green,
                    .green=n,.blue=swap?n+green:0,.alpha=alpha?2*n+green:0,
                    .redMask=(1U<<n)-1,.greenMask=(1U<<green)-1,.blueMask=(1U<<n)-1,.alphaMask=(1U<<alpha)-1}};
            }
            f=XRenderFindFormat(display,PictFormatType|PictFormatDepth|PictFormatRed|PictFormatRedMask|
                PictFormatGreen|PictFormatGreenMask|PictFormatBlue|PictFormatBlueMask|PictFormatAlpha|PictFormatAlphaMask,&wanted,0);
        }
        if(!f){fprintf(stderr,"ps2-gpu2d: required alpha alias layout=%u missing\n",layout);exit(2);}
        unsigned storage=bpp==4?8:bpp,stride=((W*storage+31)/32)*4;
        unsigned char initial[W*2*H+H*4];unsigned long original[W*H];
        memset(initial,0,sizeof(initial));
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){unsigned v=(y*W+x)*73+29;
            original[y*W+x]=v&((1U<<bpp)-1);
            if(bpp==1)initial[y*stride+x/8]|=(v&1)<<(x&7);
            else if(storage==8)initial[y*stride+x]=v;
            else{uint16_t value=v;memcpy(initial+y*stride+x*2,&value,2);}
        }
        Pixmap target=XCreatePixmap(display,root,W,H,f->depth);GC tg=XCreateGC(display,target,0,NULL);
        XSetGraphicsExposures(display,tg,False);Picture pd=XRenderCreatePicture(display,target,f,0,NULL),
            pm=XRenderCreatePicture(display,target,f,0,NULL);
        for(unsigned origin=0;origin<(layout==2?3U:1U);++origin)for(unsigned filter=0;filter<3;++filter)for(unsigned operation=0;operation<2;++operation){
            int shift=origin==1?1:origin==2?-1:0;
            XRenderPictureAttributes attrs={.alpha_map=pm,.alpha_x_origin=shift,.alpha_y_origin=shift};
            XRenderChangePicture(display,pd,CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin,&attrs);
            format_upload(source,sg,sf,rgba);format_upload(target,tg,f,original);
            unsigned char expected[sizeof(initial)];memcpy(expected,initial,sizeof(initial));
            void *source_bytes;pixman_image_t *si=reference_pixels(rgba,sf,&source_bytes),
                *di=pixman_image_create_bits(codes[layout],W,H,(uint32_t*)expected,stride),
                *ai=pixman_image_create_bits(codes[layout],W,H,(uint32_t*)expected,stride);if(!di || !ai)exit(2);
            pixman_image_set_alpha_map(di,ai,shift,shift);pixman_image_set_repeat(si,PIXMAN_REPEAT_REFLECT);
            pixman_filter_t pf=filter==2?PIXMAN_FILTER_CONVOLUTION:filter==1?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST;
            if(!pixman_image_set_transform(si,&transform) || !pixman_image_set_filter(si,pf,filter==2?coefficients:NULL,filter==2?4:0))exit(2);
            XRenderSetPictureFilter(display,ps,filter==2?FilterConvolution:filter==1?FilterBilinear:FilterNearest,
                filter==2?coefficients:NULL,filter==2?4:0);
            unsigned op_=operation?PictOpOver:PictOpSrc;
            pixman_image_composite32(op_,si,NULL,di,2,1,0,0,3,3,7,3);
            printf("ps2-gpu2d: alpha raw alias layout=%u origin=%d filter=%u op=%u\n",layout,shift,filter,op_);
            XRenderComposite(display,op_,ps,None,pd,2,1,0,0,3,3,7,3);
            unsigned mask=layout==0?1:layout==1?15:layout>=15?0xfff:layout>=13?0x7fff:bpp==16?0xffff:0xff;
            unsigned long wanted[W*H];for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
                if(bpp==1)wanted[y*W+x]=(expected[y*stride+x/8]>>(x&7))&1;
                else if(storage==8)wanted[y*W+x]=expected[y*stride+x];
                else{uint16_t value;memcpy(&value,expected+y*stride+x*2,2);wanted[y*W+x]=value;}
            }
            verify(target,W,H,wanted,mask,ZPixmap,"packed alpha map alias against pixman",op_);
            pixman_image_unref(si);pixman_image_unref(di);pixman_image_unref(ai);free(source_bytes);
        }
        XRenderFreePicture(display,pd);XRenderFreePicture(display,pm);XFreeGC(display,tg);XFreePixmap(display,target);
    }
    XRenderFreePicture(display,ps);XFreeGC(display,sg);XFreePixmap(display,source);
}
static void alpha_window_checks(Window root)
{
    XRenderPictFormat *format=XRenderFindVisualFormat(display,DefaultVisual(display,DefaultScreen(display)));
    if(!format)exit(2);
    Window windows[3];GC cgc[3];unsigned long original[3][W*H];
    Pixmap alternate[2];GC agc[2];
    for(unsigned i=0;i<3;++i){
        windows[i]=XCreateSimpleWindow(display,root,40+i*40,80,W,H,0,0,0);
        XSetWindowAttributes attr={.override_redirect=True};XChangeWindowAttributes(display,windows[i],CWOverrideRedirect,&attr);
        XMapWindow(display,windows[i]);cgc[i]=XCreateGC(display,windows[i],0,NULL);
        XSetGraphicsExposures(display,cgc[i],False);values(original[i],W,H,0x318181+i*0x714329);
        if(i<2){alternate[i]=XCreatePixmap(display,root,W,H,depth);agc[i]=XCreateGC(display,alternate[i],0,NULL);
            XSetGraphicsExposures(display,agc[i],False);}
    }
    XSync(display,False);
    const int standard[]={PictStandardA1,PictStandardA4,PictStandardA8};
    for(unsigned kind=0;kind<3;++kind){
        XRenderPictFormat *af=XRenderFindStandardFormat(display,standard[kind]);
        if(!af){
            if(kind!=1 || !getenv("PS2_GS_ORACLE"))exit(2);
            skipped_a4_checks+=8;continue;
        }
        unsigned alpha_bits=kind==0?1:kind==1?4:8,stride=kind==0?((W+31)/32)*4:(W+3)&~3U;
        unsigned long map_values[W*H];unsigned char map_initial[((W+31)/32)*4*H+W*H];
        memset(map_initial,0,sizeof(map_initial));
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            unsigned v=(y*W+x)*73+29;v&=(1U<<alpha_bits)-1;map_values[y*W+x]=v;
            if(kind==0)map_initial[y*stride+x/8]|=v<<(x&7);else map_initial[y*stride+x]=v;
        }
        Pixmap map=XCreatePixmap(display,root,W,H,af->depth);GC mg=XCreateGC(display,map,0,NULL);
        XSetGraphicsExposures(display,mg,False);Picture am=XRenderCreatePicture(display,map,af,0,NULL);
        for(unsigned role=0;role<3;++role)for(unsigned op_=0;op_<2;++op_){
            Picture parent[3];pixman_image_t *ref[3];void *bytes[3];
            for(unsigned i=0;i<3;++i){
                Drawable drawable=i<2 && ((role==1 && i==0) || (role!=1 && i==1))?alternate[i]:windows[i];
                GC selected=i<2 && drawable==alternate[i]?agc[i]:cgc[i];
                format_upload(drawable,selected,format,original[i]);
                parent[i]=XRenderCreatePicture(display,drawable,format,0,NULL);ref[i]=reference_pixels(original[i],format,bytes+i);
            }
            format_upload(map,mg,af,map_values);
            unsigned char map_bytes[sizeof(map_initial)];memcpy(map_bytes,map_initial,sizeof(map_initial));
            pixman_image_t *ai=pixman_image_create_bits(kind==0?PIXMAN_a1:kind==1?PIXMAN_x4a4:PIXMAN_a8,
                W,H,(uint32_t*)map_bytes,stride);if(!ai)exit(2);
            XRenderPictureAttributes attrs={.alpha_map=am,.alpha_x_origin=role==2?3:role==1?2:-1,.alpha_y_origin=role==2?2:1};
            XRenderChangePicture(display,parent[role],CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin,&attrs);
            pixman_image_set_alpha_map(ref[role],ai,attrs.alpha_x_origin,attrs.alpha_y_origin);
            unsigned render_op=op_?PictOpOver:PictOpSrc;
            pixman_image_composite32(render_op,ref[0],ref[1],ref[2],0,0,0,0,0,0,W,H);
            printf("ps2-gpu2d: alpha window depth=%u role=%u op=%u\n",alpha_bits,role,render_op);
            XRenderComposite(display,render_op,parent[0],parent[1],parent[2],0,0,0,0,0,0,W,H);
            format_verify(windows[2],format,bytes[2],"alpha map window coordinates and clipping",render_op);
            if(role==2){unsigned long wanted[W*H];
                for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
                    wanted[y*W+x]=kind==0?(map_bytes[y*stride+x/8]>>(x&7))&1:map_bytes[y*stride+x]&((1U<<alpha_bits)-1);
                verify(map,W,H,wanted,(1U<<alpha_bits)-1,ZPixmap,"alpha map window reply",render_op);
            }
            for(unsigned i=0;i<3;++i){XRenderFreePicture(display,parent[i]);pixman_image_unref(ref[i]);free(bytes[i]);}
            pixman_image_unref(ai);
        }
        XRenderFreePicture(display,am);XFreeGC(display,mg);XFreePixmap(display,map);
    }
    for(unsigned i=0;i<3;++i){XFreeGC(display,cgc[i]);XDestroyWindow(display,windows[i]);
        if(i<2){XFreeGC(display,agc[i]);XFreePixmap(display,alternate[i]);}}
}
/* Client oracle only. Production solid arithmetic is performed on GS. */
static pixman_image_t *solid_client_bits(const unsigned long *values,pixman_format_code_t code,void **allocation)
{
    unsigned bpp=PIXMAN_FORMAT_BPP(code),stride=(W*bpp+31)/32*4;
    unsigned char *data=calloc(H,stride);if(!data)exit(2);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){unsigned long value=values[y*W+x];
        if(bpp==1)data[y*stride+x/8]|=(value&1)<<(x&7);
        else memcpy(data+y*stride+x*(bpp/8),&value,bpp/8);}
    *allocation=data;pixman_image_t *image=pixman_image_create_bits(code,W,H,(uint32_t*)data,stride);
    if(!image)exit(2);
    return image;
}
static void solid_saturate_checks(Window root)
{
    const pixman_format_code_t codes[]={PIXMAN_a8r8g8b8,PIXMAN_a8b8g8r8,PIXMAN_b8g8r8a8,
        PIXMAN_a2r2g2b2,PIXMAN_a4r4g4b4,PIXMAN_a1,PIXMAN_x4a4,PIXMAN_a8};
    XRenderPictFormat wanted[]={
        {.type=PictTypeDirect,.depth=32,.direct={.red=16,.redMask=255,.green=8,.greenMask=255,.blueMask=255,.alpha=24,.alphaMask=255}},
        {.type=PictTypeDirect,.depth=32,.direct={.redMask=255,.green=8,.greenMask=255,.blue=16,.blueMask=255,.alpha=24,.alphaMask=255}},
        {.type=PictTypeDirect,.depth=32,.direct={.red=8,.redMask=255,.green=16,.greenMask=255,.blue=24,.blueMask=255,.alphaMask=255}},
        {.type=PictTypeDirect,.depth=8,.direct={.red=4,.redMask=3,.green=2,.greenMask=3,.blueMask=3,.alpha=6,.alphaMask=3}},
        {.type=PictTypeDirect,.depth=16,.direct={.red=8,.redMask=15,.green=4,.greenMask=15,.blueMask=15,.alpha=12,.alphaMask=15}},
        {.type=PictTypeDirect,.depth=1,.direct={.alphaMask=1}},
        {.type=PictTypeDirect,.depth=8,.direct={.alphaMask=15}},
        {.type=PictTypeDirect,.depth=8,.direct={.alphaMask=255}}};
    XRenderPictFormat *formats[8];
    for(unsigned i=0;i<8;++i){formats[i]=XRenderFindFormat(display,PictFormatType|PictFormatDepth|PictFormatRed|PictFormatRedMask|
        PictFormatGreen|PictFormatGreenMask|PictFormatBlue|PictFormatBlueMask|PictFormatAlpha|PictFormatAlphaMask,wanted+i,0);
        if(!formats[i]){fprintf(stderr,"ps2-gpu2d: missing solid Saturate format %u\n",i);exit(2);}}
    const unsigned alphas[]={0,1,127,128,255,256,257,32767,32768,65280,65407,65534,65535};
    pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    XTransform xt;memcpy(xt.matrix,transform.matrix,sizeof(xt.matrix));
    XFixed coefficients[]={2*65536,65536,2*65536,-65536};
    unsigned changed[8]={0};
    for(unsigned layout=0;layout<8;++layout)for(unsigned mode=0;mode<3;++mode){
        unsigned ai=mode==0 && layout==0?12:mode==0 && layout==1?11:
            layout==5?(mode==0?8:mode==1?7:12):layout==6?7+mode:(layout*3+mode)%13,
            a=alphas[ai],ma=alphas[(ai*3+7)%13];
        XRenderColor sc={.red=a*73/100,.green=a*31/100,.blue=a*51/100,.alpha=a},
            mc={.red=ma*61/100,.green=ma*89/100,.blue=ma*37/100,.alpha=ma};
        unsigned source_image=mode==2 && layout%3==0,mask_image=mode==1 && layout%2==0,component=mode==2;
        XRenderPictFormat *df=formats[layout],*ifmt=formats[0];
        Pixmap dp=XCreatePixmap(display,root,W,H,df->depth),ip=XCreatePixmap(display,root,W,H,32);
        GC dg=XCreateGC(display,dp,0,NULL),ig=XCreateGC(display,ip,0,NULL);
        unsigned long original[W*H],image[W*H];unsigned logical=PIXMAN_FORMAT_DEPTH(codes[layout]);
        uint32_t mask=(uint32_t)((UINT64_C(1)<<logical)-1);
        for(unsigned i=0;i<W*H;++i){unsigned alpha=(i*73+29)&255;
            original[i]=((i*8191U+0x317915)^(i*0x10103U))&mask;
            image[i]=(alpha<<24)|((((i*17+191)&255)*alpha/255)<<16)|
                ((((i*37+13)&255)*alpha/255)<<8)|(((i*53+127)&255)*alpha/255);}
        if(layout>=5)original[2*W+3]=0;
        format_upload(dp,dg,df,original);format_upload(ip,ig,ifmt,image);
        XRenderPictureAttributes attr={.repeat=RepeatReflect,.component_alpha=component};
        Picture pi=XRenderCreatePicture(display,ip,ifmt,CPRepeat|CPComponentAlpha,&attr),
            ps=source_image?pi:XRenderCreateSolidFill(display,&sc),
            pm=!mode?None:mask_image?pi:XRenderCreateSolidFill(display,&mc),pd=XRenderCreatePicture(display,dp,df,0,NULL);
        XRenderSetPictureTransform(display,pi,&xt);XRenderSetPictureFilter(display,pi,FilterBilinear,NULL,0);
        if(pm && !mask_image)XRenderChangePicture(display,pm,CPComponentAlpha,&attr);
        if(layout==1 && mode==1)XRenderSetPictureFilter(display,ps,FilterConvolution,coefficients,4);
        if(layout==2 && mode==2)XRenderSetPictureFilter(display,pm,FilterConvolution,coefficients,4);
        void *db,*ib;pixman_image_t *di=solid_client_bits(original,codes[layout],&db),
            *ii=solid_client_bits(image,PIXMAN_a8r8g8b8,&ib);
        pixman_color_t s={.red=sc.red,.green=sc.green,.blue=sc.blue,.alpha=sc.alpha},
            m={.red=mc.red,.green=mc.green,.blue=mc.blue,.alpha=mc.alpha};
        pixman_image_t *si=source_image?ii:pixman_image_create_solid_fill(&s),
            *mi=!mode?NULL:mask_image?ii:pixman_image_create_solid_fill(&m);
        if(!si || (mode && !mi) || !pixman_image_set_transform(ii,&transform) ||
            !pixman_image_set_filter(ii,PIXMAN_FILTER_BILINEAR,NULL,0))exit(2);
        pixman_image_set_repeat(ii,PIXMAN_REPEAT_REFLECT);if(mi)pixman_image_set_component_alpha(mi,component);
        pixman_image_composite32(PIXMAN_OP_SATURATE,si,mi,di,-1,-1,-1,1,3,2,1,1);
        printf("ps2-gpu2d: Render solid Saturate layout=%u mode=%u alpha16=%u mask_alpha16=%u\n",layout,mode,a,ma);
        XRenderComposite(display,PictOpSaturate,ps,pm,pd,-1,-1,-1,1,3,2,1,1);
        unsigned long expected[W*H];unsigned bpp=PIXMAN_FORMAT_BPP(codes[layout]),stride=(W*bpp+31)/32*4;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){uint32_t value=0;const unsigned char *row=(const unsigned char*)db+y*stride;
            if(bpp==1)value=(row[x/8]>>(x&7))&1;else memcpy(&value,row+x*(bpp/8),bpp/8);
            expected[y*W+x]=value&mask;}
        verify(dp,W,H,expected,mask,ZPixmap,"solid RGBA16 Saturate against pixman",PictOpSaturate);
        if(expected[2*W+3]!=original[2*W+3])++changed[layout];
        if(!source_image)pixman_image_unref(si);
        if(mi && !mask_image)pixman_image_unref(mi);
        pixman_image_unref(ii);pixman_image_unref(di);free(ib);free(db);
        if(!source_image)XRenderFreePicture(display,ps);
        if(pm && !mask_image)XRenderFreePicture(display,pm);
        XRenderFreePicture(display,pi);XRenderFreePicture(display,pd);XFreeGC(display,dg);XFreeGC(display,ig);
        XFreePixmap(display,dp);XFreePixmap(display,ip);
    }
    for(unsigned layout=0;layout<8;++layout)if(!changed[layout]){
        fprintf(stderr,"ps2-gpu2d: solid Saturate fixture did not change layout %u\n",layout);++errors;}
}
static void solid_disjoint_checks(Window root)
{
    const pixman_format_code_t codes[]={PIXMAN_a8r8g8b8,PIXMAN_a8b8g8r8,PIXMAN_b8g8r8a8,
        PIXMAN_a2r2g2b2,PIXMAN_a4r4g4b4,PIXMAN_a1,PIXMAN_x4a4,PIXMAN_a8};
    XRenderPictFormat wanted[]={
        {.type=PictTypeDirect,.depth=32,.direct={.red=16,.redMask=255,.green=8,.greenMask=255,.blueMask=255,.alpha=24,.alphaMask=255}},
        {.type=PictTypeDirect,.depth=32,.direct={.redMask=255,.green=8,.greenMask=255,.blue=16,.blueMask=255,.alpha=24,.alphaMask=255}},
        {.type=PictTypeDirect,.depth=32,.direct={.red=8,.redMask=255,.green=16,.greenMask=255,.blue=24,.blueMask=255,.alphaMask=255}},
        {.type=PictTypeDirect,.depth=8,.direct={.red=4,.redMask=3,.green=2,.greenMask=3,.blueMask=3,.alpha=6,.alphaMask=3}},
        {.type=PictTypeDirect,.depth=16,.direct={.red=8,.redMask=15,.green=4,.greenMask=15,.blueMask=15,.alpha=12,.alphaMask=15}},
        {.type=PictTypeDirect,.depth=1,.direct={.alphaMask=1}},
        {.type=PictTypeDirect,.depth=8,.direct={.alphaMask=15}},
        {.type=PictTypeDirect,.depth=8,.direct={.alphaMask=255}}};
    XRenderPictFormat *formats[8];
    for(unsigned i=0;i<8;++i){formats[i]=XRenderFindFormat(display,PictFormatType|PictFormatDepth|PictFormatRed|PictFormatRedMask|
        PictFormatGreen|PictFormatGreenMask|PictFormatBlue|PictFormatBlueMask|PictFormatAlpha|PictFormatAlphaMask,wanted+i,0);
        if(!formats[i]){fprintf(stderr,"ps2-gpu2d: missing solid DisjointOverReverse format %u\n",i);exit(2);}}
    const unsigned alphas[]={0,1,127,128,255,256,257,32767,32768,65280,65407,65534,65535};
    pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    XTransform xt;memcpy(xt.matrix,transform.matrix,sizeof(xt.matrix));
    XFixed coefficients[]={2*65536,65536,2*65536,-65536};
    unsigned changed[8]={0};
    for(unsigned layout=0;layout<8;++layout)for(unsigned mode=0;mode<3;++mode){
        if(!disjoint_request())continue;
        unsigned ai=mode==0 && layout==0?12:mode==0 && layout==1?11:
            layout==5?(mode==0?8:mode==1?7:12):layout==6?7+mode:(layout*3+mode)%13,
            a=alphas[ai],ma=alphas[(ai*3+7)%13];
        XRenderColor sc={.red=a*73/100,.green=a*31/100,.blue=a*51/100,.alpha=a},
            mc={.red=ma*61/100,.green=ma*89/100,.blue=ma*37/100,.alpha=ma};
        unsigned source_image=mode==2 && layout%3==0,mask_image=mode==1 && layout%2==0,component=mode==2;
        XRenderPictFormat *df=formats[layout],*ifmt=formats[0];
        Pixmap dp=XCreatePixmap(display,root,W,H,df->depth),ip=XCreatePixmap(display,root,W,H,32);
        GC dg=XCreateGC(display,dp,0,NULL),ig=XCreateGC(display,ip,0,NULL);
        unsigned long original[W*H],image[W*H];unsigned logical=PIXMAN_FORMAT_DEPTH(codes[layout]);
        uint32_t mask=(uint32_t)((UINT64_C(1)<<logical)-1);
        for(unsigned i=0;i<W*H;++i){unsigned alpha=(i*73+29)&255;
            original[i]=((i*8191U+0x317915)^(i*0x10103U))&mask;
            image[i]=(alpha<<24)|((((i*17+191)&255)*alpha/255)<<16)|
                ((((i*37+13)&255)*alpha/255)<<8)|(((i*53+127)&255)*alpha/255);}
        if(layout>=5)original[2*W+3]=0;
        format_upload(dp,dg,df,original);format_upload(ip,ig,ifmt,image);
        XRenderPictureAttributes attr={.repeat=RepeatReflect,.component_alpha=component};
        Picture pi=XRenderCreatePicture(display,ip,ifmt,CPRepeat|CPComponentAlpha,&attr),
            ps=source_image?pi:XRenderCreateSolidFill(display,&sc),
            pm=!mode?None:mask_image?pi:XRenderCreateSolidFill(display,&mc),pd=XRenderCreatePicture(display,dp,df,0,NULL);
        XRenderSetPictureTransform(display,pi,&xt);XRenderSetPictureFilter(display,pi,FilterBilinear,NULL,0);
        if(pm && !mask_image)XRenderChangePicture(display,pm,CPComponentAlpha,&attr);
        if(layout==1 && mode==1)XRenderSetPictureFilter(display,ps,FilterConvolution,coefficients,4);
        if(layout==2 && mode==2)XRenderSetPictureFilter(display,pm,FilterConvolution,coefficients,4);
        void *db,*ib;pixman_image_t *di=solid_client_bits(original,codes[layout],&db),
            *ii=solid_client_bits(image,PIXMAN_a8r8g8b8,&ib);
        pixman_color_t s={.red=sc.red,.green=sc.green,.blue=sc.blue,.alpha=sc.alpha},
            m={.red=mc.red,.green=mc.green,.blue=mc.blue,.alpha=mc.alpha};
        pixman_image_t *si=source_image?ii:pixman_image_create_solid_fill(&s),
            *mi=!mode?NULL:mask_image?ii:pixman_image_create_solid_fill(&m);
        if(!si || (mode && !mi) || !pixman_image_set_transform(ii,&transform) ||
            !pixman_image_set_filter(ii,PIXMAN_FILTER_BILINEAR,NULL,0))exit(2);
        pixman_image_set_repeat(ii,PIXMAN_REPEAT_REFLECT);if(mi)pixman_image_set_component_alpha(mi,component);
        pixman_image_composite32(PIXMAN_OP_DISJOINT_OVER_REVERSE,si,mi,di,-1,-1,-1,1,3,2,1,1);
        printf("ps2-gpu2d: Render solid DisjointOverReverse layout=%u mode=%u alpha16=%u mask_alpha16=%u\n",layout,mode,a,ma);
        XRenderComposite(display,PictOpDisjointOverReverse,ps,pm,pd,-1,-1,-1,1,3,2,1,1);
        unsigned long expected[W*H];unsigned bpp=PIXMAN_FORMAT_BPP(codes[layout]),stride=(W*bpp+31)/32*4;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){uint32_t value=0;const unsigned char *row=(const unsigned char*)db+y*stride;
            if(bpp==1)value=(row[x/8]>>(x&7))&1;else memcpy(&value,row+x*(bpp/8),bpp/8);
            expected[y*W+x]=value&mask;}
        verify(dp,W,H,expected,mask,ZPixmap,"solid RGBA16 DisjointOverReverse against pixman",PictOpDisjointOverReverse);
        if(expected[2*W+3]!=original[2*W+3])++changed[layout];
        if(!source_image)pixman_image_unref(si);
        if(mi && !mask_image)pixman_image_unref(mi);
        pixman_image_unref(ii);pixman_image_unref(di);free(ib);free(db);
        if(!source_image)XRenderFreePicture(display,ps);
        if(pm && !mask_image)XRenderFreePicture(display,pm);
        XRenderFreePicture(display,pi);XRenderFreePicture(display,pd);XFreeGC(display,dg);XFreeGC(display,ig);
        XFreePixmap(display,dp);XFreePixmap(display,ip);
    }
    for(unsigned layout=0;layout<8;++layout){
        if(shard_count>1)printf("ps2-gpu2d: shard solid changed layout=%u modes_changed=%u\n",layout,changed[layout]);
        else if(!changed[layout]){
            fprintf(stderr,"ps2-gpu2d: solid DisjointOverReverse fixture did not change layout %u\n",layout);++errors;}}
}
static XRenderPictFormat *map_saturate_format_at_depth(pixman_format_code_t code,unsigned depth_override)
{
    unsigned r=PIXMAN_FORMAT_R(code),g=PIXMAN_FORMAT_G(code),b=PIXMAN_FORMAT_B(code),a=PIXMAN_FORMAT_A(code);
    XRenderPictFormat wanted={.type=PictTypeDirect,.depth=depth_override?depth_override:PIXMAN_FORMAT_BPP(code)<=16?
        PIXMAN_FORMAT_BPP(code):PIXMAN_FORMAT_DEPTH(code),.direct={.redMask=(1U<<r)-1,
        .greenMask=(1U<<g)-1,.blueMask=(1U<<b)-1,.alphaMask=(1U<<a)-1}};
    if(PIXMAN_FORMAT_TYPE(code)==PIXMAN_TYPE_BGRA){wanted.direct.red=8;wanted.direct.green=8+r;wanted.direct.blue=8+r+g;}
    else if(PIXMAN_FORMAT_TYPE(code)==PIXMAN_TYPE_ABGR){wanted.direct.green=r;wanted.direct.blue=r+g;wanted.direct.alpha=r+g+b;}
    else{wanted.direct.red=g+b;wanted.direct.green=b;wanted.direct.alpha=r+g+b;}
    if(!a)wanted.direct.alpha=0;
    XRenderPictFormat *f=XRenderFindFormat(display,PictFormatType|PictFormatDepth|PictFormatRed|PictFormatRedMask|
        PictFormatGreen|PictFormatGreenMask|PictFormatBlue|PictFormatBlueMask|PictFormatAlpha|PictFormatAlphaMask,&wanted,0);
    if(!f){fprintf(stderr,"ps2-gpu2d: required map Saturate format %08x missing\n",code);exit(2);}
    return f;
}
static XRenderPictFormat *map_saturate_format(pixman_format_code_t code)
{return map_saturate_format_at_depth(code,0);}
static void map_saturate_verify(Pixmap pixmap,pixman_format_code_t code,const void *data,const char *label)
{
    unsigned bpp=PIXMAN_FORMAT_BPP(code),stride=(W*bpp+31)/32*4;
    unsigned long expected[W*H],mask=PIXMAN_FORMAT_DEPTH(code)==32?UINT32_MAX:(1U<<PIXMAN_FORMAT_DEPTH(code))-1;
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){uint32_t v=0;const unsigned char *row=(const unsigned char*)data+y*stride;
        if(bpp==1)v=(row[x/8]>>(x&7))&1;else memcpy(&v,row+x*(bpp/8),bpp/8);
        expected[y*W+x]=v&mask;}
    verify(pixmap,W,H,expected,mask,ZPixmap,label,PictOpSaturate);
}
static void map_saturate_checks(Window root)
{
    const pixman_format_code_t destinations[]={PIXMAN_a8r8g8b8,PIXMAN_a8b8g8r8,PIXMAN_b8g8r8a8,
        PIXMAN_a2r2g2b2,PIXMAN_a4r4g4b4,PIXMAN_a1,PIXMAN_x4a4,PIXMAN_a8},
        maps[]={PIXMAN_a1,PIXMAN_x4a4,PIXMAN_a8,PIXMAN_a2r2g2b2,PIXMAN_a4r4g4b4,
            PIXMAN_b8g8r8a8,PIXMAN_r5g6b5,PIXMAN_x8r8g8b8};
    XRenderPictFormat *rgba=map_saturate_format(PIXMAN_a8r8g8b8);
    pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    XTransform xt;memcpy(xt.matrix,transform.matrix,sizeof(xt.matrix));
    for(unsigned layout=0;layout<8;++layout)for(unsigned mode=0;mode<3;++mode){
        pixman_format_code_t dc=destinations[layout],mc=maps[(layout+mode)%8];
        XRenderPictFormat *df=map_saturate_format(dc),*mf=map_saturate_format(mc);
        Pixmap parents[3],map_pixmaps[3];GC pg[3],mg[3];Picture pictures[3],map_pictures[3];
        pixman_image_t *ref[3],*map_ref[3];void *memory[3],*map_memory[3];
        for(unsigned i=0;i<3;++i){XRenderPictFormat *pf=i==2?df:rgba;
            pixman_format_code_t pc=i==2?dc:PIXMAN_a8r8g8b8;
            unsigned pmask=PIXMAN_FORMAT_DEPTH(pc)==32?UINT32_MAX:(1U<<PIXMAN_FORMAT_DEPTH(pc))-1,
                mmask=PIXMAN_FORMAT_DEPTH(mc)==32?UINT32_MAX:(1U<<PIXMAN_FORMAT_DEPTH(mc))-1;
            unsigned long initial[W*H],map_initial[W*H];
            for(unsigned n=0;n<W*H;++n){unsigned a=(n*73+29+i*37)&255;
                initial[n]=i==2?((n*8191U+0x317915)^(n*0x10103U))&pmask:
                    (a<<24)|((n*17%256)*a/255<<16)|((n*37%256)*a/255<<8)|(n*53%256)*a/255;
                map_initial[n]=((n*7919U+i*65537+0x196131)^(n*0x10103U))&mmask;
            }
            parents[i]=XCreatePixmap(display,root,W,H,pf->depth);pg[i]=XCreateGC(display,parents[i],0,NULL);
            map_pixmaps[i]=XCreatePixmap(display,root,W,H,mf->depth);mg[i]=XCreateGC(display,map_pixmaps[i],0,NULL);
            format_upload(parents[i],pg[i],pf,initial);format_upload(map_pixmaps[i],mg[i],mf,map_initial);
            pictures[i]=XRenderCreatePicture(display,parents[i],pf,0,NULL);
            map_pictures[i]=XRenderCreatePicture(display,map_pixmaps[i],mf,0,NULL);
            ref[i]=solid_client_bits(initial,pc,memory+i);map_ref[i]=solid_client_bits(map_initial,mc,map_memory+i);
            unsigned mapped=i==2 || (i==0 && mode!=1) || (i==1 && mode!=0);
            XRenderPictureAttributes attr={.alpha_map=mapped?map_pictures[i]:None,
                .alpha_x_origin=i==0?-1:1,.alpha_y_origin=i==1?0:-1,.component_alpha=i==1 && mode==2};
            XRenderChangePicture(display,pictures[i],CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin|CPComponentAlpha,&attr);
            if(mapped)pixman_image_set_alpha_map(ref[i],map_ref[i],attr.alpha_x_origin,attr.alpha_y_origin);
            if(i==1)pixman_image_set_component_alpha(ref[i],attr.component_alpha);
            if(i<2){XRenderPictureAttributes repeat={.repeat=RepeatReflect};
                XRenderChangePicture(display,pictures[i],CPRepeat,&repeat);XRenderSetPictureTransform(display,pictures[i],&xt);
                XRenderSetPictureFilter(display,pictures[i],mode?FilterBilinear:FilterNearest,NULL,0);
                pixman_image_set_repeat(ref[i],PIXMAN_REPEAT_REFLECT);
                if(!pixman_image_set_transform(ref[i],&transform) ||
                    !pixman_image_set_filter(ref[i],mode?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,NULL,0))exit(2);
            }
        }
        pixman_image_composite32(PIXMAN_OP_SATURATE,ref[0],mode?ref[1]:NULL,ref[2],-1,1,2,1,3,2,1,1);
        printf("ps2-gpu2d: Render alpha-map Saturate layout=%u mode=%u map=%08x\n",layout,mode,mc);
        XRenderComposite(display,PictOpSaturate,pictures[0],mode?pictures[1]:None,pictures[2],-1,1,2,1,3,2,1,1);
        map_saturate_verify(parents[2],dc,memory[2],"map Saturate parent against pixman");
        map_saturate_verify(map_pixmaps[2],mc,map_memory[2],"map Saturate store against pixman");
        for(unsigned i=0;i<3;++i){XRenderPictureAttributes clear={.alpha_map=None};
            XRenderChangePicture(display,pictures[i],CPAlphaMap,&clear);
            XRenderFreePicture(display,pictures[i]);XRenderFreePicture(display,map_pictures[i]);
            XFreeGC(display,pg[i]);XFreeGC(display,mg[i]);XFreePixmap(display,parents[i]);XFreePixmap(display,map_pixmaps[i]);
            pixman_image_unref(ref[i]);pixman_image_unref(map_ref[i]);free(memory[i]);free(map_memory[i]);}
    }
}
static void shifted_map_checks(Window root,unsigned convolution)
{
    const pixman_format_code_t formats[]={PIXMAN_a8r8g8b8,PIXMAN_a8b8g8r8,PIXMAN_b8g8r8a8,
        PIXMAN_a2r2g2b2,PIXMAN_a4r4g4b4,PIXMAN_a1,PIXMAN_x4a4,PIXMAN_a8};
    const int origins[4][2]={{1,0},{-1,0},{0,1},{1,-1}};
    XRenderPictFormat *rgba=map_saturate_format(PIXMAN_a8r8g8b8);
    pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    XTransform xt;memcpy(xt.matrix,transform.matrix,sizeof(xt.matrix));
    XFixed positive[]={2*65536,65536,49152,16384},signed_kernel[]={2*65536,2*65536,131072,-65536,-16384,16384};
    XFixed overflow_kernel[]={2*65536,2*65536,INT32_MAX,INT32_MAX,-INT32_MAX,-(INT32_MAX-65536)};
    for(unsigned layout=0;layout<8;++layout)for(unsigned basic=0;basic<(layout<4?2U:1U);++basic){
        pixman_format_code_t dc=formats[layout];XRenderPictFormat *df=map_saturate_format(dc);
        unsigned signed_filter=basic || layout>=4;
        XFixed *kernel=signed_filter?signed_kernel:positive;int nparams=signed_filter?6:4;
        if(convolution && layout==4)kernel=overflow_kernel;
        Pixmap pixmaps[3];GC local_gc[3];Picture pictures[3];pixman_image_t *ref[3];void *memory[3];
        for(unsigned i=0;i<3;++i){XRenderPictFormat *pf=i==2?df:rgba;
            pixman_format_code_t pc=i==2?dc:PIXMAN_a8r8g8b8;
            unsigned mask=PIXMAN_FORMAT_DEPTH(pc)==32?UINT32_MAX:(1U<<PIXMAN_FORMAT_DEPTH(pc))-1;
            unsigned long initial[W*H];
            for(unsigned n=0;n<W*H;++n){unsigned a=(n*73+29+i*37)&255;
                initial[n]=i==2?((n*8191U+0x317915)^(n*0x10103U))&mask:
                    (a<<24)|((n*17%256)*a/255<<16)|((n*37%256)*a/255<<8)|(n*53%256)*a/255;
                if(convolution && i==2 && (pc==PIXMAN_a8r8g8b8 || pc==PIXMAN_a8b8g8r8))initial[n]|=(unsigned long)a<<24;
                /* R5900: 2^31 + 2^31 clamps to UINT_MAX; subtracting 2^31
                 * and (2^31-65536) leaves 65536, which filters to one.
                 * Wrapping the intermediate unsigned overflow gives zero. */
                if(convolution && layout==4 && i<2)initial[n]=UINT32_MAX;
            }
            pixmaps[i]=XCreatePixmap(display,root,W,H,pf->depth);local_gc[i]=XCreateGC(display,pixmaps[i],0,NULL);
            format_upload(pixmaps[i],local_gc[i],pf,initial);pictures[i]=XRenderCreatePicture(display,pixmaps[i],pf,0,NULL);
            ref[i]=solid_client_bits(initial,pc,memory+i);
            if(i<2){XRenderPictureAttributes repeat={.repeat=RepeatReflect,.component_alpha=i==1 && (layout&1)};
                XRenderChangePicture(display,pictures[i],CPRepeat|CPComponentAlpha,&repeat);
                XRenderSetPictureTransform(display,pictures[i],&xt);
                unsigned convolved=convolution && (i==0 || basic || (layout&1));
                XRenderSetPictureFilter(display,pictures[i],convolved?FilterConvolution:layout&1?FilterBilinear:FilterNearest,
                    convolved?kernel:NULL,convolved?nparams:0);
                pixman_image_set_repeat(ref[i],PIXMAN_REPEAT_REFLECT);pixman_image_set_component_alpha(ref[i],repeat.component_alpha);
                if(!pixman_image_set_transform(ref[i],&transform) ||
                    !pixman_image_set_filter(ref[i],convolved?PIXMAN_FILTER_CONVOLUTION:layout&1?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
                        convolved?kernel:NULL,convolved?nparams:0))exit(2);
            }
        }
        Picture map=XRenderCreatePicture(display,pixmaps[2],df,0,NULL);
        pixman_image_t *ai=pixman_image_create_bits(dc,W,H,memory[2],(W*PIXMAN_FORMAT_BPP(dc)+31)/32*4);
        if(!ai)exit(2);
        unsigned mapped=!convolution || basic;
        XRenderPictureAttributes attr={.alpha_map=mapped?map:None,.alpha_x_origin=origins[layout%4][0],.alpha_y_origin=origins[layout%4][1]};
        XRenderChangePicture(display,pictures[2],CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin,&attr);
        if(mapped)pixman_image_set_alpha_map(ref[2],ai,attr.alpha_x_origin,attr.alpha_y_origin);
        unsigned operation=convolution?PictOpSaturate:basic?PictOpOver:PictOpSaturate,
            width=convolution?1:basic?13:2,height=convolution?1:2;
        pixman_image_composite32(operation,ref[0],ref[1],ref[2],-1,1,2,1,3,2,width,height);
        printf("ps2-gpu2d: Render %s layout=%u origin=%d,%d op=%u signed=%u mapped=%u\n",
            convolution?"convolution Saturate":"shifted map",layout,attr.alpha_x_origin,attr.alpha_y_origin,operation,signed_filter,mapped);
        XRenderComposite(display,operation,pictures[0],pictures[1],pictures[2],-1,1,2,1,3,2,width,height);
        map_saturate_verify(pixmaps[2],dc,memory[2],convolution?"convolution Saturate against pixman":"shifted writable map against pixman");
        attr.alpha_map=None;XRenderChangePicture(display,pictures[2],CPAlphaMap,&attr);XRenderFreePicture(display,map);
        for(unsigned i=0;i<3;++i){XRenderFreePicture(display,pictures[i]);XFreeGC(display,local_gc[i]);XFreePixmap(display,pixmaps[i]);
            pixman_image_unref(ref[i]);free(memory[i]);}
        pixman_image_unref(ai);
    }
}
static void format_alias_checks(Window root)
{
    const pixman_format_code_t formats[]={PIXMAN_x4a4,PIXMAN_a8,PIXMAN_r3g3b2,PIXMAN_b2g3r3,PIXMAN_a2r2g2b2,PIXMAN_a2b2g2r2,
        PIXMAN_r5g6b5,PIXMAN_b5g6r5,PIXMAN_a1r5g5b5,PIXMAN_a1b5g5r5,PIXMAN_a4r4g4b4,PIXMAN_a4b4g4r4,
        PIXMAN_x1r5g5b5,PIXMAN_x1b5g5r5,PIXMAN_x4r4g4b4,PIXMAN_x4b4g4r4},
        maps[]={PIXMAN_a2r2g2b2,PIXMAN_x4a4,PIXMAN_a8,PIXMAN_a2b2g2r2,PIXMAN_b2g3r3,PIXMAN_r3g3b2,
        PIXMAN_a4r4g4b4,PIXMAN_a1b5g5r5,PIXMAN_x1r5g5b5,PIXMAN_b5g6r5,PIXMAN_a4b4g4r4,PIXMAN_r5g6b5,
        PIXMAN_x4r4g4b4,PIXMAN_a4r4g4b4,PIXMAN_a1r5g5b5,PIXMAN_x1b5g5r5};
    const int origins[4][2]={{1,0},{-1,0},{0,1},{1,-1}};
    XRenderPictFormat *rgba=map_saturate_format(PIXMAN_a8r8g8b8);
    pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    XTransform xt;memcpy(xt.matrix,transform.matrix,sizeof(xt.matrix));
    XFixed kernel[]={2*65536,65536,49152,16384};
    for(unsigned layout=0;layout<16;++layout)for(unsigned basic=0;basic<2;++basic){
        pixman_format_code_t dc=formats[layout],mc=maps[layout];
        XRenderPictFormat *df=map_saturate_format(dc),*mf=map_saturate_format(mc);
        Pixmap pixmaps[3];GC local_gc[3];Picture pictures[3];pixman_image_t *ref[3];void *memory[3];
        for(unsigned i=0;i<3;++i){XRenderPictFormat *pf=i==2?df:rgba;
            pixman_format_code_t pc=i==2?dc:PIXMAN_a8r8g8b8;
            unsigned mask=PIXMAN_FORMAT_DEPTH(pc)==32?UINT32_MAX:(1U<<PIXMAN_FORMAT_DEPTH(pc))-1;
            unsigned long initial[W*H];
            for(unsigned n=0;n<W*H;++n){unsigned a=(n*73+29+i*37)&255;
                initial[n]=i==2?((n*8191U+0x317915)^(n*0x10103U))&mask:
                    (a<<24)|((n*17%256)*a/255<<16)|((n*37%256)*a/255<<8)|(n*53%256)*a/255;
            }
            pixmaps[i]=XCreatePixmap(display,root,W,H,pf->depth);local_gc[i]=XCreateGC(display,pixmaps[i],0,NULL);
            format_upload(pixmaps[i],local_gc[i],pf,initial);pictures[i]=XRenderCreatePicture(display,pixmaps[i],pf,0,NULL);
            ref[i]=solid_client_bits(initial,pc,memory+i);
            if(i<2){XRenderPictureAttributes repeat={.repeat=RepeatReflect,.component_alpha=i==1 && (layout&1)};
                XRenderChangePicture(display,pictures[i],CPRepeat|CPComponentAlpha,&repeat);
                XRenderSetPictureTransform(display,pictures[i],&xt);
                unsigned convolved=!basic && layout%3==2,linear=!basic && layout%3==1;
                XRenderSetPictureFilter(display,pictures[i],convolved?FilterConvolution:linear?FilterBilinear:FilterNearest,
                    convolved?kernel:NULL,convolved?4:0);
                pixman_image_set_repeat(ref[i],PIXMAN_REPEAT_REFLECT);pixman_image_set_component_alpha(ref[i],repeat.component_alpha);
                if(!pixman_image_set_transform(ref[i],&transform) ||
                    !pixman_image_set_filter(ref[i],convolved?PIXMAN_FILTER_CONVOLUTION:linear?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
                        convolved?kernel:NULL,convolved?4:0))exit(2);
            }
        }
        Picture map=XRenderCreatePicture(display,pixmaps[2],mf,0,NULL);
        pixman_image_t *ai=pixman_image_create_bits(mc,W,H,memory[2],(W*PIXMAN_FORMAT_BPP(dc)+31)/32*4);
        if(!ai)exit(2);
        XRenderPictureAttributes attr={.alpha_map=map,.alpha_x_origin=origins[layout%4][0],.alpha_y_origin=origins[layout%4][1]};
        XRenderChangePicture(display,pictures[2],CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin,&attr);
        pixman_image_set_alpha_map(ref[2],ai,attr.alpha_x_origin,attr.alpha_y_origin);
        unsigned operation=basic?PictOpOver:PictOpSaturate,width=basic?13:1,height=2;
        pixman_image_composite32(operation,ref[0],ref[1],ref[2],-1,1,2,1,3,2,width,height);
        printf("ps2-gpu2d: Render format alias layout=%u parent=%08x map=%08x origin=%d,%d op=%u filter=%u\n",
            layout,dc,mc,attr.alpha_x_origin,attr.alpha_y_origin,operation,basic?0:layout%3);
        XRenderComposite(display,operation,pictures[0],pictures[1],pictures[2],-1,1,2,1,3,2,width,height);
        map_saturate_verify(pixmaps[2],PIXMAN_FORMAT_DEPTH(dc)>=PIXMAN_FORMAT_DEPTH(mc)?dc:mc,memory[2],"different format writable map against pixman");
        attr.alpha_map=None;XRenderChangePicture(display,pictures[2],CPAlphaMap,&attr);XRenderFreePicture(display,map);
        for(unsigned i=0;i<3;++i){XRenderFreePicture(display,pictures[i]);XFreeGC(display,local_gc[i]);XFreePixmap(display,pixmaps[i]);
            pixman_image_unref(ref[i]);free(memory[i]);}
        pixman_image_unref(ai);
    }
}
static void pd_opaque_rounding_checks(Window root)
{
    XRenderPictFormat *format=map_saturate_format(PIXMAN_a8r8g8b8);
    Pixmap destination=XCreatePixmap(display,root,W,H,format->depth);
    GC draw=XCreateGC(display,destination,0,NULL);
    Picture target=XRenderCreatePicture(display,destination,format,0,NULL);
    XRenderColor colour={257,257,257,65535};Picture source=XRenderCreateSolidFill(display,&colour);
    unsigned long initial[W*H];for(unsigned i=0;i<W*H;++i)initial[i]=0x01000000;
    for(unsigned i=0;i<2;++i){void *memory;
        if(!disjoint_request())continue;
        format_upload(destination,draw,format,initial);
        pixman_color_t c={.red=257,.green=257,.blue=257,.alpha=65535};
        pixman_image_t *s=pixman_image_create_solid_fill(&c),*d=solid_client_bits(initial,PIXMAN_a8r8g8b8,&memory);
        if(!s || !d)exit(2);
        unsigned operation=i?PictOpDisjointOverReverse:PictOpSaturate;
        pixman_image_composite32(operation,s,NULL,d,0,0,0,0,3,2,1,1);
        uint32_t actual;memcpy(&actual,(unsigned char*)memory+(2*W+3)*4,4);
        if(actual!=(i?0xff000000U:0xff010101U)){
            fprintf(stderr,"ps2-gpu2d: opaque rounding oracle op=%u actual=%08x\n",operation,actual);exit(2);}
        printf("ps2-gpu2d: Render opaque-source rounding op=%u expected=%08x\n",operation,actual);
        XRenderComposite(display,operation,source,None,target,0,0,0,0,3,2,1,1);
        map_saturate_verify(destination,PIXMAN_a8r8g8b8,memory,"PD opaque-source rounding against pixman");
        pixman_image_unref(s);pixman_image_unref(d);free(memory);
    }
    XRenderFreePicture(display,source);XRenderFreePicture(display,target);XFreeGC(display,draw);XFreePixmap(display,destination);
}
static const char *pd_family_name(unsigned op)
{
    switch(op){
    case PictOpDisjointClear:return "DisjointClear";
    case PictOpDisjointSrc:return "DisjointSrc";
    case PictOpDisjointDst:return "DisjointDst";
    case PictOpConjointClear:return "ConjointClear";
    case PictOpConjointSrc:return "ConjointSrc";
    case PictOpConjointDst:return "ConjointDst";
    case PictOpDisjointOverReverse:return "DisjointOverReverse";
    default:return NULL;
    }
}
static void native15_alias_checks(Window root,unsigned blend)
{
    const pixman_format_code_t formats[]={PIXMAN_x1r5g5b5,PIXMAN_x1b5g5r5,PIXMAN_x4r4g4b4,PIXMAN_x4b4g4r4};
    const int origins[5][2]={{0,0},{1,0},{-1,0},{0,1},{1,-1}};
    XRenderPictFormat *rgba=map_saturate_format(PIXMAN_a8r8g8b8);
    pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    XTransform xt;memcpy(xt.matrix,transform.matrix,sizeof(xt.matrix));
    XFixed kernel[]={2*65536,65536,49152,16384};
    for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b)for(unsigned basic=0;basic<(blend?3U:2U);++basic){
        if(a==b)continue;
        if(!disjoint_request())continue;
        unsigned layout=a*4+b;
        pixman_format_code_t dc=formats[a],mc=formats[b];
        XRenderPictFormat *df=map_saturate_format_at_depth(dc,15),*mf=map_saturate_format_at_depth(mc,15);
        Pixmap pixmaps[3];GC local_gc[3];Picture pictures[3];pixman_image_t *ref[3];void *memory[3];
        for(unsigned i=0;i<3;++i){XRenderPictFormat *pf=i==2?df:rgba;
            pixman_format_code_t pc=i==2?dc:PIXMAN_a8r8g8b8;
            unsigned mask=i==2?0x7fff:UINT32_MAX;
            unsigned long initial[W*H];
            for(unsigned n=0;n<W*H;++n){unsigned a=(n*73+29+i*37)&255;
                initial[n]=i==2?((n*8191U+0x317915)^(n*0x10103U))&mask:
                    (a<<24)|((n*17%256)*a/255<<16)|((n*37%256)*a/255<<8)|(n*53%256)*a/255;
            }
            pixmaps[i]=XCreatePixmap(display,root,W,H,pf->depth);local_gc[i]=XCreateGC(display,pixmaps[i],0,NULL);
            format_upload(pixmaps[i],local_gc[i],pf,initial);pictures[i]=XRenderCreatePicture(display,pixmaps[i],pf,0,NULL);
            ref[i]=solid_client_bits(initial,pc,memory+i);
            if(i<2){XRenderPictureAttributes repeat={.repeat=RepeatReflect,.component_alpha=i==1 && (layout&1)};
                XRenderChangePicture(display,pictures[i],CPRepeat|CPComponentAlpha,&repeat);
                XRenderSetPictureTransform(display,pictures[i],&xt);
                unsigned filter=blend?basic:basic?0:layout%3;
                unsigned convolved=filter==2,linear=filter==1;
                XRenderSetPictureFilter(display,pictures[i],convolved?FilterConvolution:linear?FilterBilinear:FilterNearest,
                    convolved?kernel:NULL,convolved?4:0);
                pixman_image_set_repeat(ref[i],PIXMAN_REPEAT_REFLECT);pixman_image_set_component_alpha(ref[i],repeat.component_alpha);
                if(!pixman_image_set_transform(ref[i],&transform) ||
                    !pixman_image_set_filter(ref[i],convolved?PIXMAN_FILTER_CONVOLUTION:linear?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
                        convolved?kernel:NULL,convolved?4:0))exit(2);
            }
        }
        Picture map=XRenderCreatePicture(display,pixmaps[2],mf,0,NULL);
        pixman_image_t *ai=pixman_image_create_bits(mc,W,H,memory[2],(W*PIXMAN_FORMAT_BPP(dc)+31)/32*4);
        if(!ai)exit(2);
        XRenderPictureAttributes attr={.alpha_map=map,.alpha_x_origin=origins[layout%5][0],.alpha_y_origin=origins[layout%5][1]};
        XRenderChangePicture(display,pictures[2],CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin,&attr);
        pixman_image_set_alpha_map(ref[2],ai,attr.alpha_x_origin,attr.alpha_y_origin);
        unsigned operation=blend?blend:basic?PictOpOver:PictOpSaturate,width=pd_family_name(blend)?1:blend?3:basic?13:1,height=pd_family_name(blend)?1:2;
        pixman_image_composite32(operation,ref[0],ref[1],ref[2],-1,1,2,1,3,2,width,height);
        printf("ps2-gpu2d: Render native15 alias layout=%u parent=%08x map=%08x origin=%d,%d op=%u filter=%u\n",
            layout,dc,mc,attr.alpha_x_origin,attr.alpha_y_origin,operation,blend?basic:basic?0:layout%3);
        XRenderComposite(display,operation,pictures[0],pictures[1],pictures[2],-1,1,2,1,3,2,width,height);
        map_saturate_verify(pixmaps[2],PIXMAN_FORMAT_DEPTH(dc)>=PIXMAN_FORMAT_DEPTH(mc)?dc:mc,memory[2],"native depth-15 writable map against pixman");
        attr.alpha_map=None;XRenderChangePicture(display,pictures[2],CPAlphaMap,&attr);XRenderFreePicture(display,map);
        for(unsigned i=0;i<3;++i){XRenderFreePicture(display,pictures[i]);XFreeGC(display,local_gc[i]);XFreePixmap(display,pixmaps[i]);
            pixman_image_unref(ref[i]);free(memory[i]);}
        pixman_image_unref(ai);
    }
}
static void blend_checks(Window root,unsigned operation)
{
    const char *family=pd_family_name(operation);
    unsigned rw=family?1:3,rh=family?1:2;
    const char *name=family?family:operation==PictOpOverlay?"Overlay":operation==PictOpHardLight?"HardLight":operation==PictOpDarken?"Darken":operation==PictOpLighten?"Lighten":operation==PictOpDifference?"Difference":operation==PictOpMultiply?"Multiply":operation==PictOpExclusion?"Exclusion":"Screen";
    const pixman_format_code_t formats[]={
        PIXMAN_x1r5g5b5,PIXMAN_x1b5g5r5,PIXMAN_x4r4g4b4,PIXMAN_x4b4g4r4,
        PIXMAN_a8r8g8b8,PIXMAN_a8b8g8r8,PIXMAN_x8r8g8b8,PIXMAN_x8b8g8r8,PIXMAN_b8g8r8a8,PIXMAN_b8g8r8x8,
        PIXMAN_r3g3b2,PIXMAN_b2g3r3,PIXMAN_a2r2g2b2,PIXMAN_a2b2g2r2,
        PIXMAN_r5g6b5,PIXMAN_b5g6r5,PIXMAN_a1r5g5b5,PIXMAN_a1b5g5r5,PIXMAN_a4r4g4b4,PIXMAN_a4b4g4r4,
        PIXMAN_x1r5g5b5,PIXMAN_x1b5g5r5,PIXMAN_x4r4g4b4,PIXMAN_x4b4g4r4,PIXMAN_a1,PIXMAN_x4a4,PIXMAN_a8};
    XRenderPictFormat *sf=map_saturate_format(PIXMAN_a8r8g8b8);
    Pixmap inputs[2];GC input_gc[2];Picture pictures[2];unsigned long raw[2][W*H];
    for(unsigned i=0;i<2;++i){
        for(unsigned n=0;n<W*H;++n){unsigned a=(n*73+29+i*37)&255;
            raw[i][n]=(a<<24)|((n*17%256)*a/255<<16)|((n*37%256)*a/255<<8)|(n*53%256)*a/255;}
        inputs[i]=XCreatePixmap(display,root,W,H,sf->depth);input_gc[i]=XCreateGC(display,inputs[i],0,NULL);
        format_upload(inputs[i],input_gc[i],sf,raw[i]);pictures[i]=XRenderCreatePicture(display,inputs[i],sf,0,NULL);
        XRenderPictureAttributes attr={.repeat=i?RepeatPad:RepeatReflect};XRenderChangePicture(display,pictures[i],CPRepeat,&attr);
    }
    pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    XTransform xt;memcpy(xt.matrix,transform.matrix,sizeof(xt.matrix));
    XFixed kernel[]={2*65536,65536,2*65536,-65536};
    for(unsigned layout=0;layout<sizeof(formats)/sizeof(*formats);++layout){
        XRenderPictFormat *df=map_saturate_format_at_depth(formats[layout],layout<4?15:formats[layout]==PIXMAN_b8g8r8x8?32:0);
        Pixmap dp=XCreatePixmap(display,root,W,H,df->depth);GC dg=XCreateGC(display,dp,0,NULL);
        Picture pd=XRenderCreatePicture(display,dp,df,0,NULL);unsigned long initial[W*H];
        for(unsigned n=0;n<W*H;++n)initial[n]=((n*8191U+0x317915)^(n*0x10103U))&
            (df->depth==32?UINT32_MAX:(1U<<df->depth)-1);
        for(unsigned filter=0;filter<3;++filter)for(unsigned ca=0;ca<2;++ca){
            if(!disjoint_request())continue;
            format_upload(dp,dg,df,initial);void *sm,*mm,*dm;
            pixman_image_t *si=solid_client_bits(raw[0],PIXMAN_a8r8g8b8,&sm),
                *mi=solid_client_bits(raw[1],PIXMAN_a8r8g8b8,&mm),*di=solid_client_bits(initial,formats[layout],&dm);
            if(!si || !mi || !di)exit(2);
            for(unsigned i=0;i<2;++i){unsigned f=i?(ca?1:0):filter;
                XRenderSetPictureTransform(display,pictures[i],&xt);
                XRenderSetPictureFilter(display,pictures[i],f==2?FilterConvolution:f?FilterBilinear:FilterNearest,
                    f==2?kernel:NULL,f==2?4:0);
                pixman_image_t *im=i?mi:si;
                pixman_image_set_repeat(im,i?PIXMAN_REPEAT_PAD:PIXMAN_REPEAT_REFLECT);
                if(!pixman_image_set_transform(im,&transform) || !pixman_image_set_filter(im,
                    f==2?PIXMAN_FILTER_CONVOLUTION:f?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
                    f==2?kernel:NULL,f==2?4:0))exit(2);
            }
            XRenderPictureAttributes attr={.component_alpha=ca};XRenderChangePicture(display,pictures[1],CPComponentAlpha,&attr);
            pixman_image_set_component_alpha(mi,ca);
            pixman_image_composite32((pixman_op_t)operation,si,mi,di,-1,1,2,1,3,2,rw,rh);
            printf("ps2-gpu2d: Render %s layout=%u format=%08x filter=%u ca=%u\n",name,layout,formats[layout],filter,ca);
            XRenderComposite(display,operation,pictures[0],pictures[1],pd,-1,1,2,1,3,2,rw,rh);
            if(PIXMAN_FORMAT_TYPE(formats[layout])==PIXMAN_TYPE_BGRA)
                format_verify(dp,df,dm,name,operation);
            else map_saturate_verify(dp,formats[layout],dm,name);
            pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(di);free(sm);free(mm);free(dm);
        }
        XRenderFreePicture(display,pd);XFreeGC(display,dg);XFreePixmap(display,dp);
    }
    for(unsigned i=0;i<2;++i){XRenderFreePicture(display,pictures[i]);XFreeGC(display,input_gc[i]);XFreePixmap(display,inputs[i]);}
    native15_alias_checks(root,operation);
}
/* Six protocol profiles per operator: unmasked ARGB, bilinear BGRA, packed
 * convolution/component alpha, original RGBA16 solid coverage, shifted
 * packed map views, and native depth-15 differing map views. */
static void pd_ratio_checks(Window root)
{
    const unsigned operations[]={PictOpDisjointOver,PictOpDisjointIn,PictOpDisjointInReverse,
        PictOpDisjointOut,PictOpDisjointOutReverse,PictOpDisjointAtop,PictOpDisjointAtopReverse,PictOpDisjointXor,
        PictOpConjointOver,PictOpConjointOverReverse,PictOpConjointIn,PictOpConjointInReverse,
        PictOpConjointOut,PictOpConjointOutReverse,PictOpConjointAtop,PictOpConjointAtopReverse,PictOpConjointXor};
    const pixman_format_code_t codes[]={PIXMAN_a8r8g8b8,PIXMAN_b8g8r8a8,PIXMAN_a2r2g2b2,
        PIXMAN_x4a4,PIXMAN_a4r4g4b4,PIXMAN_x1r5g5b5};
    pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    XTransform xt;memcpy(xt.matrix,transform.matrix,sizeof(xt.matrix));
    XFixed kernel[]={2*65536,65536,2*65536,-65536};
    XRenderPictFormat *rgba=map_saturate_format(PIXMAN_a8r8g8b8);
    /* Opaque Saturate reduces to Dst before deciding whether a uniform mask
     * needs the wide solid planes. Guard both byte and word packed targets. */
    for(unsigned word=0;word<2;++word){
        if(!disjoint_request())continue;
        pixman_format_code_t code=word?PIXMAN_r5g6b5:PIXMAN_r3g3b2;
        XRenderPictFormat *df=map_saturate_format(code);
        Pixmap dp=XCreatePixmap(display,root,W,H,df->depth);GC dg=XCreateGC(display,dp,0,NULL);
        unsigned long initial[W*H];for(unsigned i=0;i<W*H;++i)initial[i]=(i*73+19)&(word?65535:255);
        format_upload(dp,dg,df,initial);Picture pd=XRenderCreatePicture(display,dp,df,0,NULL);
        XRenderColor sc={11113,7711,17131,32767},mc={17431,21431,8411,41311};
        Picture sp=XRenderCreateSolidFill(display,&sc),mp=XRenderCreateSolidFill(display,&mc);
        XRenderPictureAttributes attr={.component_alpha=word};XRenderChangePicture(display,mp,CPComponentAlpha,&attr);
        printf("ps2-gpu2d: Render PD opaque Saturate solid-mask regression word=%u\n",word);
        XRenderComposite(display,PictOpSaturate,sp,mp,pd,0,0,0,0,3,2,1,1);
        void *memory;pixman_image_t *im=solid_client_bits(initial,code,&memory);
        map_saturate_verify(dp,code,memory,"opaque Saturate solid mask preserves Dst");pixman_image_unref(im);free(memory);
        XRenderFreePicture(display,sp);XRenderFreePicture(display,mp);XRenderFreePicture(display,pd);XFreeGC(display,dg);XFreePixmap(display,dp);
    }
    for(unsigned op=0;op<17;++op)for(unsigned profile=0;profile<6;++profile){
        /* Rotate the profile order so expensive convolution/map requests
         * are distributed across shards while request IDs stay contiguous. */
        unsigned layout=(profile+op)%6;
        if(!disjoint_request())continue;
        unsigned operation=operations[op],filter=layout%3,component=layout>=2,
            mapped=layout>=4,solid=layout==3;
        pixman_format_code_t dc=codes[layout],mc=layout==5?PIXMAN_x4b4g4r4:PIXMAN_r5g6b5;
        XRenderPictFormat *df=map_saturate_format_at_depth(dc,layout==5?15:0);
        Pixmap pixmaps[3];GC draw[3];Picture pictures[3];pixman_image_t *ref[3];void *memory[3];
        for(unsigned i=0;i<3;++i){
            XRenderPictFormat *pf=i==2?df:rgba;
            pixman_format_code_t pc=i==2?dc:PIXMAN_a8r8g8b8;
            unsigned long initial[W*H];
            for(unsigned n=0;n<W*H;++n){unsigned a=(n*73+29+i*37+op*19)&255;
                initial[n]=i==2?((n*8191U+0x317915)^(n*0x10103U))&
                    (pf->depth==32?UINT32_MAX:(1U<<pf->depth)-1):
                    (a<<24)|((n*17%256)*a/255<<16)|((n*37%256)*a/255<<8)|(n*53%256)*a/255;
            }
            pixmaps[i]=XCreatePixmap(display,root,W,H,pf->depth);draw[i]=XCreateGC(display,pixmaps[i],0,NULL);
            format_upload(pixmaps[i],draw[i],pf,initial);
            pictures[i]=XRenderCreatePicture(display,pixmaps[i],pf,0,NULL);
            ref[i]=solid_client_bits(initial,pc,memory+i);
            if(i<2){
                XRenderPictureAttributes attr={.repeat=RepeatReflect,.component_alpha=i==1 && component};
                XRenderChangePicture(display,pictures[i],CPRepeat|CPComponentAlpha,&attr);
                XRenderSetPictureTransform(display,pictures[i],&xt);
                XRenderSetPictureFilter(display,pictures[i],filter==2?FilterConvolution:filter?FilterBilinear:FilterNearest,
                    filter==2?kernel:NULL,filter==2?4:0);
                pixman_image_set_repeat(ref[i],PIXMAN_REPEAT_REFLECT);pixman_image_set_component_alpha(ref[i],i==1 && component);
                if(!pixman_image_set_transform(ref[i],&transform) || !pixman_image_set_filter(ref[i],
                    filter==2?PIXMAN_FILTER_CONVOLUTION:filter?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
                    filter==2?kernel:NULL,filter==2?4:0))exit(2);
            }
        }
        if(solid)for(unsigned i=0;i<2;++i){
            XRenderColor colour={.red=11113+i*1321,.green=7711+i*1921,.blue=17131+i*2131,.alpha=32767+i*4099};
            pixman_color_t c={.red=colour.red,.green=colour.green,.blue=colour.blue,.alpha=colour.alpha};
            XRenderFreePicture(display,pictures[i]);pixman_image_unref(ref[i]);
            pictures[i]=XRenderCreateSolidFill(display,&colour);ref[i]=pixman_image_create_solid_fill(&c);if(!ref[i])exit(2);
            if(i){XRenderPictureAttributes attr={.component_alpha=component};XRenderChangePicture(display,pictures[i],CPComponentAlpha,&attr);
                pixman_image_set_component_alpha(ref[i],component);}
        }
        Picture map=None;pixman_image_t *ai=NULL;
        if(mapped){
            XRenderPictFormat *mf=map_saturate_format_at_depth(mc,layout==5?15:0);
            map=XRenderCreatePicture(display,pixmaps[2],mf,0,NULL);
            ai=pixman_image_create_bits(mc,W,H,memory[2],(W*PIXMAN_FORMAT_BPP(dc)+31)/32*4);if(!ai)exit(2);
            XRenderPictureAttributes attr={.alpha_map=map,.alpha_x_origin=1,.alpha_y_origin=0};
            XRenderChangePicture(display,pictures[2],CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin,&attr);
            pixman_image_set_alpha_map(ref[2],ai,1,0);
        }
        pixman_image_composite32(operation,ref[0],layout?ref[1]:NULL,ref[2],-1,1,2,1,3,2,1,1);
        printf("ps2-gpu2d: Render PD ratio op=%u layout=%u filter=%u ca=%u map=%u solid=%u\n",
            operation,layout,filter,component,mapped,solid);
        XRenderComposite(display,operation,pictures[0],layout?pictures[1]:None,pictures[2],-1,1,2,1,3,2,1,1);
        if(PIXMAN_FORMAT_TYPE(dc)==PIXMAN_TYPE_BGRA)format_verify(pixmaps[2],df,memory[2],"PD ratio BGRA",operation);
        else map_saturate_verify(pixmaps[2],mapped && PIXMAN_FORMAT_DEPTH(mc)>PIXMAN_FORMAT_DEPTH(dc)?mc:dc,
            memory[2],"PD ratio against pixman");
        if(mapped){XRenderPictureAttributes attr={.alpha_map=None};XRenderChangePicture(display,pictures[2],CPAlphaMap,&attr);
            XRenderFreePicture(display,map);pixman_image_unref(ai);}
        for(unsigned i=0;i<3;++i){XRenderFreePicture(display,pictures[i]);XFreeGC(display,draw[i]);XFreePixmap(display,pixmaps[i]);
            pixman_image_unref(ref[i]);free(memory[i]);}
    }
}
static void dodge_burn_boundary_checks(Window root)
{
    const unsigned alpha[]={0,1,127,257,32767,32768,65534,65535};
    const uint32_t dest[]={0,0x01000001,0x7f003f7f,0x80004080,0x8001407f,0xff0001ff,0x01010101,0xff80807f};
    XRenderPictFormat *format=map_saturate_format(PIXMAN_a8r8g8b8);
    Pixmap dp=XCreatePixmap(display,root,W,H,format->depth);GC draw=XCreateGC(display,dp,0,NULL);
    Picture target=XRenderCreatePicture(display,dp,format,0,NULL);
    for(unsigned op=0;op<2;++op)for(unsigned vector=0;vector<8;++vector){
        if(!disjoint_request())continue;
        unsigned a=alpha[vector],ma=alpha[(vector*3+5)%8],mode=vector%3;
        XRenderColor colour={.red=a,.green=a?a-1:0,.blue=vector==3?32768:0,.alpha=a},
            mask_colour={.red=ma,.green=ma/2,.blue=ma/3,.alpha=ma};
        Picture source=XRenderCreateSolidFill(display,&colour),mask=mode?XRenderCreateSolidFill(display,&mask_colour):None;
        Pixmap raw_source=None;GC raw_draw=NULL;unsigned long raw[W*H];
        if(vector==3){
            /* Nonpremultiplied solid colours are deliberately rejected.
             * Raw ARGB image storage can contain these channel values. */
            for(unsigned i=0;i<W*H;++i)raw[i]=((unsigned)(colour.alpha>>8)<<24)|
                ((unsigned)(colour.red>>8)<<16)|((unsigned)(colour.green>>8)<<8)|(colour.blue>>8);
            raw_source=XCreatePixmap(display,root,W,H,32);raw_draw=XCreateGC(display,raw_source,0,NULL);
            format_upload(raw_source,raw_draw,format,raw);XRenderFreePicture(display,source);
            XRenderPictureAttributes attr={.repeat=RepeatNormal};source=XRenderCreatePicture(display,raw_source,format,CPRepeat,&attr);
        }
        if(mask){XRenderPictureAttributes attr={.component_alpha=mode==2};XRenderChangePicture(display,mask,CPComponentAlpha,&attr);}
        unsigned long initial[W*H];for(unsigned i=0;i<W*H;++i)initial[i]=0x317915U^(i*0x10103U);
        initial[2*W+3]=dest[vector];format_upload(dp,draw,format,initial);
        void *memory,*raw_memory=NULL;pixman_color_t sc={.red=colour.red,.green=colour.green,.blue=colour.blue,.alpha=colour.alpha},
            mc={.red=mask_colour.red,.green=mask_colour.green,.blue=mask_colour.blue,.alpha=mask_colour.alpha};
        pixman_image_t *si=pixman_image_create_solid_fill(&sc),*mi=mode?pixman_image_create_solid_fill(&mc):NULL,
            *di=solid_client_bits(initial,PIXMAN_a8r8g8b8,&memory);if(!si || (mode && !mi))exit(2);
        if(raw_source){pixman_image_unref(si);si=solid_client_bits(raw,PIXMAN_a8r8g8b8,&raw_memory);pixman_image_set_repeat(si,PIXMAN_REPEAT_NORMAL);}
        if(mi)pixman_image_set_component_alpha(mi,mode==2);
        unsigned operation=op?PictOpColorBurn:PictOpColorDodge;
        pixman_image_composite32(operation,si,mi,di,0,0,0,0,3,2,1,1);
        printf("ps2-gpu2d: Render dodge/burn boundary op=%u vector=%u alpha16=%u mask=%u raw_source=%u\n",operation,vector,a,mode,!!raw_source);
        XRenderComposite(display,operation,source,mask,target,0,0,0,0,3,2,1,1);
        map_saturate_verify(dp,PIXMAN_a8r8g8b8,memory,"dodge/burn zero and branch boundaries against pixman");
        XRenderFreePicture(display,source);if(mask)XRenderFreePicture(display,mask);
        if(raw_source){XFreeGC(display,raw_draw);XFreePixmap(display,raw_source);}
        pixman_image_unref(si);if(mi)pixman_image_unref(mi);pixman_image_unref(di);free(memory);free(raw_memory);
    }
    XRenderFreePicture(display,target);XFreeGC(display,draw);XFreePixmap(display,dp);
}
static void dodge_burn_checks(Window root)
{
    const unsigned operations[]={PictOpColorDodge,PictOpColorBurn};
    const pixman_format_code_t codes[]={PIXMAN_a8r8g8b8,PIXMAN_b8g8r8a8,PIXMAN_a2r2g2b2,
        PIXMAN_x4a4,PIXMAN_a4r4g4b4,PIXMAN_x1r5g5b5};
    pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    XTransform xt;memcpy(xt.matrix,transform.matrix,sizeof(xt.matrix));
    XFixed kernel[]={2*65536,65536,2*65536,-65536};
    XRenderPictFormat *rgba=map_saturate_format(PIXMAN_a8r8g8b8);
    /* Opaque Saturate reduces to Dst before deciding whether a uniform mask
     * needs the wide solid planes. Guard both byte and word packed targets. */
    for(unsigned word=0;word<2;++word){
        if(!disjoint_request())continue;
        pixman_format_code_t code=word?PIXMAN_r5g6b5:PIXMAN_r3g3b2;
        XRenderPictFormat *df=map_saturate_format(code);
        Pixmap dp=XCreatePixmap(display,root,W,H,df->depth);GC dg=XCreateGC(display,dp,0,NULL);
        unsigned long initial[W*H];for(unsigned i=0;i<W*H;++i)initial[i]=(i*73+19)&(word?65535:255);
        format_upload(dp,dg,df,initial);Picture pd=XRenderCreatePicture(display,dp,df,0,NULL);
        XRenderColor sc={11113,7711,17131,32767},mc={17431,21431,8411,41311};
        Picture sp=XRenderCreateSolidFill(display,&sc),mp=XRenderCreateSolidFill(display,&mc);
        XRenderPictureAttributes attr={.component_alpha=word};XRenderChangePicture(display,mp,CPComponentAlpha,&attr);
        printf("ps2-gpu2d: Render PD opaque Saturate solid-mask regression word=%u\n",word);
        XRenderComposite(display,PictOpSaturate,sp,mp,pd,0,0,0,0,3,2,1,1);
        void *memory;pixman_image_t *im=solid_client_bits(initial,code,&memory);
        map_saturate_verify(dp,code,memory,"opaque Saturate solid mask preserves Dst");pixman_image_unref(im);free(memory);
        XRenderFreePicture(display,sp);XRenderFreePicture(display,mp);XRenderFreePicture(display,pd);XFreeGC(display,dg);XFreePixmap(display,dp);
    }
    for(unsigned op=0;op<2;++op)for(unsigned profile=0;profile<6;++profile){
        /* Rotate the profile order so expensive convolution/map requests
         * are distributed across shards while request IDs stay contiguous. */
        unsigned layout=(profile+op)%6;
        if(!disjoint_request())continue;
        unsigned operation=operations[op],filter=layout%3,component=layout>=2,
            mapped=layout>=4,solid=layout==3;
        pixman_format_code_t dc=codes[layout],mc=layout==5?PIXMAN_x4b4g4r4:PIXMAN_r5g6b5;
        XRenderPictFormat *df=map_saturate_format_at_depth(dc,layout==5?15:0);
        Pixmap pixmaps[3];GC draw[3];Picture pictures[3];pixman_image_t *ref[3];void *memory[3];
        for(unsigned i=0;i<3;++i){
            XRenderPictFormat *pf=i==2?df:rgba;
            pixman_format_code_t pc=i==2?dc:PIXMAN_a8r8g8b8;
            unsigned long initial[W*H];
            for(unsigned n=0;n<W*H;++n){unsigned a=(n*73+29+i*37+op*19)&255;
                initial[n]=i==2?((n*8191U+0x317915)^(n*0x10103U))&
                    (pf->depth==32?UINT32_MAX:(1U<<pf->depth)-1):
                    (a<<24)|((n*17%256)*a/255<<16)|((n*37%256)*a/255<<8)|(n*53%256)*a/255;
            }
            pixmaps[i]=XCreatePixmap(display,root,W,H,pf->depth);draw[i]=XCreateGC(display,pixmaps[i],0,NULL);
            format_upload(pixmaps[i],draw[i],pf,initial);
            pictures[i]=XRenderCreatePicture(display,pixmaps[i],pf,0,NULL);
            ref[i]=solid_client_bits(initial,pc,memory+i);
            if(i<2){
                XRenderPictureAttributes attr={.repeat=RepeatReflect,.component_alpha=i==1 && component};
                XRenderChangePicture(display,pictures[i],CPRepeat|CPComponentAlpha,&attr);
                XRenderSetPictureTransform(display,pictures[i],&xt);
                XRenderSetPictureFilter(display,pictures[i],filter==2?FilterConvolution:filter?FilterBilinear:FilterNearest,
                    filter==2?kernel:NULL,filter==2?4:0);
                pixman_image_set_repeat(ref[i],PIXMAN_REPEAT_REFLECT);pixman_image_set_component_alpha(ref[i],i==1 && component);
                if(!pixman_image_set_transform(ref[i],&transform) || !pixman_image_set_filter(ref[i],
                    filter==2?PIXMAN_FILTER_CONVOLUTION:filter?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
                    filter==2?kernel:NULL,filter==2?4:0))exit(2);
            }
        }
        if(solid)for(unsigned i=0;i<2;++i){
            XRenderColor colour={.red=11113+i*1321,.green=7711+i*1921,.blue=17131+i*2131,.alpha=32767+i*4099};
            pixman_color_t c={.red=colour.red,.green=colour.green,.blue=colour.blue,.alpha=colour.alpha};
            XRenderFreePicture(display,pictures[i]);pixman_image_unref(ref[i]);
            pictures[i]=XRenderCreateSolidFill(display,&colour);ref[i]=pixman_image_create_solid_fill(&c);if(!ref[i])exit(2);
            if(i){XRenderPictureAttributes attr={.component_alpha=component};XRenderChangePicture(display,pictures[i],CPComponentAlpha,&attr);
                pixman_image_set_component_alpha(ref[i],component);}
        }
        Picture map=None;pixman_image_t *ai=NULL;
        if(mapped){
            XRenderPictFormat *mf=map_saturate_format_at_depth(mc,layout==5?15:0);
            map=XRenderCreatePicture(display,pixmaps[2],mf,0,NULL);
            ai=pixman_image_create_bits(mc,W,H,memory[2],(W*PIXMAN_FORMAT_BPP(dc)+31)/32*4);if(!ai)exit(2);
            XRenderPictureAttributes attr={.alpha_map=map,.alpha_x_origin=1,.alpha_y_origin=0};
            XRenderChangePicture(display,pictures[2],CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin,&attr);
            pixman_image_set_alpha_map(ref[2],ai,1,0);
        }
        pixman_image_composite32(operation,ref[0],layout?ref[1]:NULL,ref[2],-1,1,2,1,3,2,1,1);
        printf("ps2-gpu2d: Render dodge/burn op=%u layout=%u filter=%u ca=%u map=%u solid=%u\n",
            operation,layout,filter,component,mapped,solid);
        XRenderComposite(display,operation,pictures[0],layout?pictures[1]:None,pictures[2],-1,1,2,1,3,2,1,1);
        if(PIXMAN_FORMAT_TYPE(dc)==PIXMAN_TYPE_BGRA)format_verify(pixmaps[2],df,memory[2],"PD ratio BGRA",operation);
        else map_saturate_verify(pixmaps[2],mapped && PIXMAN_FORMAT_DEPTH(mc)>PIXMAN_FORMAT_DEPTH(dc)?mc:dc,
            memory[2],"PD ratio against pixman");
        if(mapped){XRenderPictureAttributes attr={.alpha_map=None};XRenderChangePicture(display,pictures[2],CPAlphaMap,&attr);
            XRenderFreePicture(display,map);pixman_image_unref(ai);}
        for(unsigned i=0;i<3;++i){XRenderFreePicture(display,pictures[i]);XFreeGC(display,draw[i]);XFreePixmap(display,pixmaps[i]);
            pixman_image_unref(ref[i]);free(memory[i]);}
    }
    dodge_burn_boundary_checks(root);
}
static void softlight_boundary_checks(Window root)
{
    const unsigned alpha[]={0,65535,65534,65534,65534,65534,65534,65534,257,257,0,1};
    const unsigned red[]={0,0,32766,32767,32768,65534,65534,65534,257,257,65535,65535};
    const uint32_t dest[]={0,0x00004080,0xfc3e3f40,0xfc3e3f40,0xfc3e3f40,0xfc3e3f40,
        0x01020100,0xff00ffff,0xff3e3f40,0x003e3f40,0xfc3e3f40,0x01020100};
    XRenderPictFormat *format=map_saturate_format(PIXMAN_a8r8g8b8);
    Pixmap dp=XCreatePixmap(display,root,W,H,format->depth);GC draw=XCreateGC(display,dp,0,NULL);
    Picture target=XRenderCreatePicture(display,dp,format,0,NULL);
    for(unsigned vector=0;vector<12;++vector){
        if(!disjoint_request())continue;
        unsigned a=alpha[vector],mode=vector<8?0:vector%3,ma=vector==8?32767:65535;
        XRenderColor colour={.red=red[vector],.green=a/2,.blue=a,.alpha=a},
            mask_colour={.red=ma,.green=ma/2,.blue=ma/3,.alpha=ma};
        Picture source=None,mask=mode?XRenderCreateSolidFill(display,&mask_colour):None;
        Pixmap raw=None;GC raw_gc=NULL;void *source_memory=NULL;
        pixman_image_t *si=NULL;
        if(vector>=10){
            unsigned long pixels[W*H];uint32_t value=((a>>8)<<24)|0xff807f;
            for(unsigned i=0;i<W*H;++i)pixels[i]=value;
            raw=XCreatePixmap(display,root,W,H,32);raw_gc=XCreateGC(display,raw,0,NULL);
            format_upload(raw,raw_gc,format,pixels);source=XRenderCreatePicture(display,raw,format,0,NULL);
            si=solid_client_bits(pixels,PIXMAN_a8r8g8b8,&source_memory);
        }else{
            source=XRenderCreateSolidFill(display,&colour);
            pixman_color_t sc={.red=colour.red,.green=colour.green,.blue=colour.blue,.alpha=colour.alpha};
            si=pixman_image_create_solid_fill(&sc);
        }
        if(mask){XRenderPictureAttributes attr={.component_alpha=mode==2};XRenderChangePicture(display,mask,CPComponentAlpha,&attr);}
        unsigned long initial[W*H];for(unsigned i=0;i<W*H;++i)initial[i]=0x317915U^(i*0x10103U);
        initial[2*W+3]=dest[vector];format_upload(dp,draw,format,initial);
        void *memory;pixman_color_t mc={.red=mask_colour.red,.green=mask_colour.green,.blue=mask_colour.blue,.alpha=mask_colour.alpha};
        pixman_image_t *mi=mode?pixman_image_create_solid_fill(&mc):NULL,
            *di=solid_client_bits(initial,PIXMAN_a8r8g8b8,&memory);if(!si || (mode && !mi))exit(2);
        if(mi)pixman_image_set_component_alpha(mi,mode==2);
        pixman_image_composite32(PIXMAN_OP_SOFT_LIGHT,si,mi,di,0,0,0,0,3,2,1,1);
        printf("ps2-gpu2d: Render SoftLight boundary vector=%u alpha16=%u mask=%u\n",vector,a,mode);
        XRenderComposite(display,PictOpSoftLight,source,mask,target,0,0,0,0,3,2,1,1);
        map_saturate_verify(dp,PIXMAN_a8r8g8b8,memory,"SoftLight threshold/zero/raw boundary against pixman");
        XRenderFreePicture(display,source);if(mask)XRenderFreePicture(display,mask);
        pixman_image_unref(si);if(mi)pixman_image_unref(mi);pixman_image_unref(di);free(memory);free(source_memory);
        if(raw){XFreeGC(display,raw_gc);XFreePixmap(display,raw);}
    }
    XRenderFreePicture(display,target);XFreeGC(display,draw);XFreePixmap(display,dp);
}
static void softlight_checks(Window root)
{
    const unsigned operations[]={PictOpSoftLight};
    const pixman_format_code_t codes[]={PIXMAN_a8r8g8b8,PIXMAN_b8g8r8a8,PIXMAN_a2r2g2b2,
        PIXMAN_x4a4,PIXMAN_a4r4g4b4,PIXMAN_x1r5g5b5};
    pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    XTransform xt;memcpy(xt.matrix,transform.matrix,sizeof(xt.matrix));
    XFixed kernel[]={2*65536,65536,2*65536,-65536};
    XRenderPictFormat *rgba=map_saturate_format(PIXMAN_a8r8g8b8);
    /* Opaque Saturate reduces to Dst before deciding whether a uniform mask
     * needs the wide solid planes. Guard both byte and word packed targets. */
    for(unsigned word=0;word<2;++word){
        if(!disjoint_request())continue;
        pixman_format_code_t code=word?PIXMAN_r5g6b5:PIXMAN_r3g3b2;
        XRenderPictFormat *df=map_saturate_format(code);
        Pixmap dp=XCreatePixmap(display,root,W,H,df->depth);GC dg=XCreateGC(display,dp,0,NULL);
        unsigned long initial[W*H];for(unsigned i=0;i<W*H;++i)initial[i]=(i*73+19)&(word?65535:255);
        format_upload(dp,dg,df,initial);Picture pd=XRenderCreatePicture(display,dp,df,0,NULL);
        XRenderColor sc={11113,7711,17131,32767},mc={17431,21431,8411,41311};
        Picture sp=XRenderCreateSolidFill(display,&sc),mp=XRenderCreateSolidFill(display,&mc);
        XRenderPictureAttributes attr={.component_alpha=word};XRenderChangePicture(display,mp,CPComponentAlpha,&attr);
        printf("ps2-gpu2d: Render PD opaque Saturate solid-mask regression word=%u\n",word);
        XRenderComposite(display,PictOpSaturate,sp,mp,pd,0,0,0,0,3,2,1,1);
        void *memory;pixman_image_t *im=solid_client_bits(initial,code,&memory);
        map_saturate_verify(dp,code,memory,"opaque Saturate solid mask preserves Dst");pixman_image_unref(im);free(memory);
        XRenderFreePicture(display,sp);XRenderFreePicture(display,mp);XRenderFreePicture(display,pd);XFreeGC(display,dg);XFreePixmap(display,dp);
    }
    for(unsigned op=0;op<1;++op)for(unsigned profile=0;profile<6;++profile){
        /* Rotate the profile order so expensive convolution/map requests
         * are distributed across shards while request IDs stay contiguous. */
        unsigned layout=(profile+op)%6;
        if(!disjoint_request())continue;
        unsigned operation=operations[op],filter=layout%3,component=layout>=2,
            mapped=layout>=4,solid=layout==3;
        pixman_format_code_t dc=codes[layout],mc=layout==5?PIXMAN_x4b4g4r4:PIXMAN_r5g6b5;
        XRenderPictFormat *df=map_saturate_format_at_depth(dc,layout==5?15:0);
        Pixmap pixmaps[3];GC draw[3];Picture pictures[3];pixman_image_t *ref[3];void *memory[3];
        for(unsigned i=0;i<3;++i){
            XRenderPictFormat *pf=i==2?df:rgba;
            pixman_format_code_t pc=i==2?dc:PIXMAN_a8r8g8b8;
            unsigned long initial[W*H];
            for(unsigned n=0;n<W*H;++n){unsigned a=(n*73+29+i*37+op*19)&255;
                initial[n]=i==2?((n*8191U+0x317915)^(n*0x10103U))&
                    (pf->depth==32?UINT32_MAX:(1U<<pf->depth)-1):
                    (a<<24)|((n*17%256)*a/255<<16)|((n*37%256)*a/255<<8)|(n*53%256)*a/255;
            }
            pixmaps[i]=XCreatePixmap(display,root,W,H,pf->depth);draw[i]=XCreateGC(display,pixmaps[i],0,NULL);
            format_upload(pixmaps[i],draw[i],pf,initial);
            pictures[i]=XRenderCreatePicture(display,pixmaps[i],pf,0,NULL);
            ref[i]=solid_client_bits(initial,pc,memory+i);
            if(i<2){
                XRenderPictureAttributes attr={.repeat=RepeatReflect,.component_alpha=i==1 && component};
                XRenderChangePicture(display,pictures[i],CPRepeat|CPComponentAlpha,&attr);
                XRenderSetPictureTransform(display,pictures[i],&xt);
                XRenderSetPictureFilter(display,pictures[i],filter==2?FilterConvolution:filter?FilterBilinear:FilterNearest,
                    filter==2?kernel:NULL,filter==2?4:0);
                pixman_image_set_repeat(ref[i],PIXMAN_REPEAT_REFLECT);pixman_image_set_component_alpha(ref[i],i==1 && component);
                if(!pixman_image_set_transform(ref[i],&transform) || !pixman_image_set_filter(ref[i],
                    filter==2?PIXMAN_FILTER_CONVOLUTION:filter?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
                    filter==2?kernel:NULL,filter==2?4:0))exit(2);
            }
        }
        if(solid)for(unsigned i=0;i<2;++i){
            XRenderColor colour={.red=11113+i*1321,.green=7711+i*1921,.blue=17131+i*2131,.alpha=32767+i*4099};
            pixman_color_t c={.red=colour.red,.green=colour.green,.blue=colour.blue,.alpha=colour.alpha};
            XRenderFreePicture(display,pictures[i]);pixman_image_unref(ref[i]);
            pictures[i]=XRenderCreateSolidFill(display,&colour);ref[i]=pixman_image_create_solid_fill(&c);if(!ref[i])exit(2);
            if(i){XRenderPictureAttributes attr={.component_alpha=component};XRenderChangePicture(display,pictures[i],CPComponentAlpha,&attr);
                pixman_image_set_component_alpha(ref[i],component);}
        }
        Picture map=None;pixman_image_t *ai=NULL;
        if(mapped){
            XRenderPictFormat *mf=map_saturate_format_at_depth(mc,layout==5?15:0);
            map=XRenderCreatePicture(display,pixmaps[2],mf,0,NULL);
            ai=pixman_image_create_bits(mc,W,H,memory[2],(W*PIXMAN_FORMAT_BPP(dc)+31)/32*4);if(!ai)exit(2);
            XRenderPictureAttributes attr={.alpha_map=map,.alpha_x_origin=1,.alpha_y_origin=0};
            XRenderChangePicture(display,pictures[2],CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin,&attr);
            pixman_image_set_alpha_map(ref[2],ai,1,0);
        }
        pixman_image_composite32(operation,ref[0],layout?ref[1]:NULL,ref[2],-1,1,2,1,3,2,1,1);
        printf("ps2-gpu2d: Render SoftLight op=%u layout=%u filter=%u ca=%u map=%u solid=%u\n",
            operation,layout,filter,component,mapped,solid);
        XRenderComposite(display,operation,pictures[0],layout?pictures[1]:None,pictures[2],-1,1,2,1,3,2,1,1);
        if(PIXMAN_FORMAT_TYPE(dc)==PIXMAN_TYPE_BGRA)format_verify(pixmaps[2],df,memory[2],"PD ratio BGRA",operation);
        else map_saturate_verify(pixmaps[2],mapped && PIXMAN_FORMAT_DEPTH(mc)>PIXMAN_FORMAT_DEPTH(dc)?mc:dc,
            memory[2],"PD ratio against pixman");
        if(mapped){XRenderPictureAttributes attr={.alpha_map=None};XRenderChangePicture(display,pictures[2],CPAlphaMap,&attr);
            XRenderFreePicture(display,map);pixman_image_unref(ai);}
        for(unsigned i=0;i<3;++i){XRenderFreePicture(display,pictures[i]);XFreeGC(display,draw[i]);XFreePixmap(display,pixmaps[i]);
            pixman_image_unref(ref[i]);free(memory[i]);}
    }
    softlight_boundary_checks(root);
}
static void hsl_boundary_checks(Window root)
{
    static const unsigned permutations[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    const uint32_t dest[]={0,0x01ff8040,0x80ff0040,0xffffffff,0x80404040,0x00a02090,0xff807f80,0x8000ff40};
    XRenderPictFormat *format=map_saturate_format(PIXMAN_a8r8g8b8);
    Pixmap dp=XCreatePixmap(display,root,W,H,32);GC draw=XCreateGC(display,dp,0,NULL);
    Picture target=XRenderCreatePicture(display,dp,format,0,NULL);
    for(unsigned op=0;op<4;++op)for(unsigned vector=0;vector<8;++vector){
        if(!disjoint_request())continue;
        unsigned a=vector==6?65534:32767,v[]={173,11113,32767};
        XRenderColor colour={.red=v[permutations[vector%6][0]],.green=v[permutations[vector%6][1]],
            .blue=v[permutations[vector%6][2]],.alpha=a};
        if(vector==6)colour.red=colour.green=colour.blue=32767;
        XRenderColor mc={.red=17431,.green=21431,.blue=8411,.alpha=41311};
        Picture mask=vector%2?XRenderCreateSolidFill(display,&mc):None,source=None;
        Pixmap raw=None;GC raw_gc=NULL;void *source_memory=NULL;pixman_image_t *si=NULL;
        if(vector==7){
            unsigned long pixels[W*H];for(unsigned i=0;i<W*H;++i)pixels[i]=0x00ff807f;
            raw=XCreatePixmap(display,root,W,H,32);raw_gc=XCreateGC(display,raw,0,NULL);
            format_upload(raw,raw_gc,format,pixels);source=XRenderCreatePicture(display,raw,format,0,NULL);
            si=solid_client_bits(pixels,PIXMAN_a8r8g8b8,&source_memory);
        }else{
            source=XRenderCreateSolidFill(display,&colour);
            pixman_color_t sc={.red=colour.red,.green=colour.green,.blue=colour.blue,.alpha=colour.alpha};
            si=pixman_image_create_solid_fill(&sc);
        }
        unsigned long initial[W*H];for(unsigned i=0;i<W*H;++i)initial[i]=0x317915U^(i*0x10103U);
        initial[2*W+3]=dest[vector];format_upload(dp,draw,format,initial);
        void *memory;pixman_color_t mpc={.red=mc.red,.green=mc.green,.blue=mc.blue,.alpha=mc.alpha};
        pixman_image_t *mi=mask?pixman_image_create_solid_fill(&mpc):NULL,
            *di=solid_client_bits(initial,PIXMAN_a8r8g8b8,&memory);if(!si || (mask && !mi))exit(2);
        unsigned operation=PictOpHSLHue+op;
        pixman_image_composite32(operation,si,mi,di,0,0,0,0,3,2,1,1);
        printf("ps2-gpu2d: Render HSL boundary op=%u vector=%u alpha16=%u mask=%u\n",operation,vector,a,!!mask);
        XRenderComposite(display,operation,source,mask,target,0,0,0,0,3,2,1,1);
        map_saturate_verify(dp,PIXMAN_a8r8g8b8,memory,"HSL order/tie/clipping/raw boundary against pixman");
        XRenderFreePicture(display,source);if(mask)XRenderFreePicture(display,mask);
        pixman_image_unref(si);if(mi)pixman_image_unref(mi);pixman_image_unref(di);free(memory);free(source_memory);
        if(raw){XFreeGC(display,raw_gc);XFreePixmap(display,raw);}
    }
    XRenderFreePicture(display,target);XFreeGC(display,draw);XFreePixmap(display,dp);
}
static void hsl_checks(Window root)
{
    const unsigned operations[]={PictOpHSLHue,PictOpHSLSaturation,PictOpHSLColor,PictOpHSLLuminosity};
    const pixman_format_code_t codes[]={PIXMAN_a8r8g8b8,PIXMAN_b8g8r8a8,PIXMAN_a2r2g2b2,
        PIXMAN_x4a4,PIXMAN_a4r4g4b4,PIXMAN_x1r5g5b5};
    pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    XTransform xt;memcpy(xt.matrix,transform.matrix,sizeof(xt.matrix));
    XFixed kernel[]={2*65536,65536,2*65536,-65536};
    XRenderPictFormat *rgba=map_saturate_format(PIXMAN_a8r8g8b8);
    /* Opaque Saturate reduces to Dst before deciding whether a uniform mask
     * needs the wide solid planes. Guard both byte and word packed targets. */
    for(unsigned word=0;word<2;++word){
        if(!disjoint_request())continue;
        pixman_format_code_t code=word?PIXMAN_r5g6b5:PIXMAN_r3g3b2;
        XRenderPictFormat *df=map_saturate_format(code);
        Pixmap dp=XCreatePixmap(display,root,W,H,df->depth);GC dg=XCreateGC(display,dp,0,NULL);
        unsigned long initial[W*H];for(unsigned i=0;i<W*H;++i)initial[i]=(i*73+19)&(word?65535:255);
        format_upload(dp,dg,df,initial);Picture pd=XRenderCreatePicture(display,dp,df,0,NULL);
        XRenderColor sc={11113,7711,17131,32767},mc={17431,21431,8411,41311};
        Picture sp=XRenderCreateSolidFill(display,&sc),mp=XRenderCreateSolidFill(display,&mc);
        XRenderPictureAttributes attr={.component_alpha=word};XRenderChangePicture(display,mp,CPComponentAlpha,&attr);
        printf("ps2-gpu2d: Render PD opaque Saturate solid-mask regression word=%u\n",word);
        XRenderComposite(display,PictOpSaturate,sp,mp,pd,0,0,0,0,3,2,1,1);
        void *memory;pixman_image_t *im=solid_client_bits(initial,code,&memory);
        map_saturate_verify(dp,code,memory,"opaque Saturate solid mask preserves Dst");pixman_image_unref(im);free(memory);
        XRenderFreePicture(display,sp);XRenderFreePicture(display,mp);XRenderFreePicture(display,pd);XFreeGC(display,dg);XFreePixmap(display,dp);
    }
    for(unsigned op=0;op<4;++op)for(unsigned profile=0;profile<6;++profile){
        /* Rotate the profile order so expensive convolution/map requests
         * are distributed across shards while request IDs stay contiguous. */
        unsigned layout=(profile+op)%6;
        if(!disjoint_request())continue;
        unsigned operation=operations[op],filter=layout%3,component=0,
            mapped=layout>=4,solid=layout==3;
        pixman_format_code_t dc=codes[layout],mc=layout==5?PIXMAN_x4b4g4r4:PIXMAN_r5g6b5;
        XRenderPictFormat *df=map_saturate_format_at_depth(dc,layout==5?15:0);
        Pixmap pixmaps[3];GC draw[3];Picture pictures[3];pixman_image_t *ref[3];void *memory[3];
        for(unsigned i=0;i<3;++i){
            XRenderPictFormat *pf=i==2?df:rgba;
            pixman_format_code_t pc=i==2?dc:PIXMAN_a8r8g8b8;
            unsigned long initial[W*H];
            for(unsigned n=0;n<W*H;++n){unsigned a=(n*73+29+i*37+op*19)&255;
                initial[n]=i==2?((n*8191U+0x317915)^(n*0x10103U))&
                    (pf->depth==32?UINT32_MAX:(1U<<pf->depth)-1):
                    (a<<24)|((n*17%256)*a/255<<16)|((n*37%256)*a/255<<8)|(n*53%256)*a/255;
            }
            pixmaps[i]=XCreatePixmap(display,root,W,H,pf->depth);draw[i]=XCreateGC(display,pixmaps[i],0,NULL);
            format_upload(pixmaps[i],draw[i],pf,initial);
            pictures[i]=XRenderCreatePicture(display,pixmaps[i],pf,0,NULL);
            ref[i]=solid_client_bits(initial,pc,memory+i);
            if(i<2){
                XRenderPictureAttributes attr={.repeat=RepeatReflect,.component_alpha=i==1 && component};
                XRenderChangePicture(display,pictures[i],CPRepeat|CPComponentAlpha,&attr);
                XRenderSetPictureTransform(display,pictures[i],&xt);
                XRenderSetPictureFilter(display,pictures[i],filter==2?FilterConvolution:filter?FilterBilinear:FilterNearest,
                    filter==2?kernel:NULL,filter==2?4:0);
                pixman_image_set_repeat(ref[i],PIXMAN_REPEAT_REFLECT);pixman_image_set_component_alpha(ref[i],i==1 && component);
                if(!pixman_image_set_transform(ref[i],&transform) || !pixman_image_set_filter(ref[i],
                    filter==2?PIXMAN_FILTER_CONVOLUTION:filter?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
                    filter==2?kernel:NULL,filter==2?4:0))exit(2);
            }
        }
        if(solid)for(unsigned i=0;i<2;++i){
            XRenderColor colour={.red=11113+i*1321,.green=7711+i*1921,.blue=17131+i*2131,.alpha=32767+i*4099};
            pixman_color_t c={.red=colour.red,.green=colour.green,.blue=colour.blue,.alpha=colour.alpha};
            XRenderFreePicture(display,pictures[i]);pixman_image_unref(ref[i]);
            pictures[i]=XRenderCreateSolidFill(display,&colour);ref[i]=pixman_image_create_solid_fill(&c);if(!ref[i])exit(2);
            if(i){XRenderPictureAttributes attr={.component_alpha=component};XRenderChangePicture(display,pictures[i],CPComponentAlpha,&attr);
                pixman_image_set_component_alpha(ref[i],component);}
        }
        Picture map=None;pixman_image_t *ai=NULL;
        if(mapped){
            XRenderPictFormat *mf=map_saturate_format_at_depth(mc,layout==5?15:0);
            map=XRenderCreatePicture(display,pixmaps[2],mf,0,NULL);
            ai=pixman_image_create_bits(mc,W,H,memory[2],(W*PIXMAN_FORMAT_BPP(dc)+31)/32*4);if(!ai)exit(2);
            XRenderPictureAttributes attr={.alpha_map=map,.alpha_x_origin=1,.alpha_y_origin=0};
            XRenderChangePicture(display,pictures[2],CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin,&attr);
            pixman_image_set_alpha_map(ref[2],ai,1,0);
        }
        pixman_image_composite32(operation,ref[0],layout?ref[1]:NULL,ref[2],-1,1,2,1,3,2,1,1);
        printf("ps2-gpu2d: Render HSL op=%u layout=%u filter=%u ca=%u map=%u solid=%u\n",
            operation,layout,filter,component,mapped,solid);
        XRenderComposite(display,operation,pictures[0],layout?pictures[1]:None,pictures[2],-1,1,2,1,3,2,1,1);
        if(PIXMAN_FORMAT_TYPE(dc)==PIXMAN_TYPE_BGRA)format_verify(pixmaps[2],df,memory[2],"PD ratio BGRA",operation);
        else map_saturate_verify(pixmaps[2],mapped && PIXMAN_FORMAT_DEPTH(mc)>PIXMAN_FORMAT_DEPTH(dc)?mc:dc,
            memory[2],"PD ratio against pixman");
        if(mapped){XRenderPictureAttributes attr={.alpha_map=None};XRenderChangePicture(display,pictures[2],CPAlphaMap,&attr);
            XRenderFreePicture(display,map);pixman_image_unref(ai);}
        for(unsigned i=0;i<3;++i){XRenderFreePicture(display,pictures[i]);XFreeGC(display,draw[i]);XFreePixmap(display,pixmaps[i]);
            pixman_image_unref(ref[i]);free(memory[i]);}
    }
    for(unsigned op=0;op<4;++op)for(unsigned packed=0;packed<2;++packed){
        if(!disjoint_request())continue;
        pixman_format_code_t code=packed?PIXMAN_a4r4g4b4:PIXMAN_a8r8g8b8;
        XRenderPictFormat *df=map_saturate_format(code);
        Pixmap dp=XCreatePixmap(display,root,W,H,df->depth);GC dg=XCreateGC(display,dp,0,NULL);
        unsigned long initial[W*H];for(unsigned i=0;i<W*H;++i)initial[i]=(i*8191U+0x317915)&(packed?65535:UINT32_MAX);
        format_upload(dp,dg,df,initial);Picture pd=XRenderCreatePicture(display,dp,df,0,NULL);
        XRenderColor sc={11113,7711,17131,32767},mc={17431,21431,8411,41311};
        Picture sp=XRenderCreateSolidFill(display,&sc),mp=XRenderCreateSolidFill(display,&mc);
        XRenderPictureAttributes attr={.component_alpha=True};XRenderChangePicture(display,mp,CPComponentAlpha,&attr);
        void *memory;pixman_image_t *im=solid_client_bits(initial,code,&memory);
        pixman_color_t cs={sc.red,sc.green,sc.blue,sc.alpha},cm={mc.red,mc.green,mc.blue,mc.alpha};
        pixman_image_t *si=pixman_image_create_solid_fill(&cs),*mi=pixman_image_create_solid_fill(&cm);
        if(!si || !mi)exit(2);
        pixman_image_set_component_alpha(mi,True);pixman_image_composite32(operations[op],si,mi,im,0,0,0,0,3,2,7,5);
        printf("ps2-gpu2d: Render HSL component-alpha Dst op=%u packed=%u\n",operations[op],packed);
        XRenderComposite(display,operations[op],sp,mp,pd,0,0,0,0,3,2,7,5);
        map_saturate_verify(dp,code,memory,"HSL component-alpha preserves destination");
        pixman_image_unref(si);pixman_image_unref(mi);pixman_image_unref(im);free(memory);
        XRenderFreePicture(display,sp);XRenderFreePicture(display,mp);XRenderFreePicture(display,pd);XFreeGC(display,dg);XFreePixmap(display,dp);
    }
    hsl_boundary_checks(root);
}

static Picture gradient_client(unsigned kind,unsigned variant,unsigned opaque,pixman_image_t **reference)
{
    XFixed positions[]={8192,32768,32768,57344};
    XRenderColor colours[]={{65535,11413,8317,32767},{9113,65535,17831,65535},
        {61731,2931,63813,0},{8171,19131,65535,41311}};
    pixman_gradient_stop_t stops[4];
    for(unsigned i=0;i<4;++i){if(opaque)colours[i].alpha=65535;
        stops[i]=(pixman_gradient_stop_t){.x=positions[i],
            .color={colours[i].red,colours[i].green,colours[i].blue,colours[i].alpha}};}
    XPointFixed first={-65536,32768},last={3*65536,2*65536};
    XFixed small=32768,large=3*65536,angle=-37*65536;
    if(opaque){last.x=first.x+65536;last.y=first.y+65536;large=5*65536;}
    if(variant){last=first;if(kind==2){last.x+=65536;large=small+65536;}
        if(kind==3){first.x=first.y=32768;angle=397*65536;}}
    pixman_point_fixed_t a={first.x,first.y},b={last.x,last.y};Picture out;
    if(kind==1){XLinearGradient g={first,last};out=XRenderCreateLinearGradient(display,&g,positions,colours,4);
        *reference=pixman_image_create_linear_gradient(&a,&b,stops,4);}
    else if(kind==2){XRadialGradient g={{first.x,first.y,small},{last.x,last.y,large}};
        out=XRenderCreateRadialGradient(display,&g,positions,colours,4);
        *reference=pixman_image_create_radial_gradient(&a,&b,small,large,stops,4);}
    else{XConicalGradient g={first,angle};out=XRenderCreateConicalGradient(display,&g,positions,colours,4);
        *reference=pixman_image_create_conical_gradient(&a,angle,stops,4);}
    if(!out || !*reference)exit(2);
    return out;
}
static void gradient_client_state(Picture picture,pixman_image_t *ref,unsigned repeat,unsigned filter,
    unsigned component,const pixman_transform_t *transform)
{
    XRenderPictureAttributes attr={.repeat=repeat,.component_alpha=component};
    XRenderChangePicture(display,picture,CPRepeat|CPComponentAlpha,&attr);
    pixman_image_set_repeat(ref,repeat);pixman_image_set_component_alpha(ref,component);
    if(transform){XTransform xt;memcpy(xt.matrix,transform->matrix,sizeof(xt.matrix));
        XRenderSetPictureTransform(display,picture,&xt);if(!pixman_image_set_transform(ref,transform))exit(2);}
    XFixed kernel[]={2*65536,65536,2*65536,-65536};
    XRenderSetPictureFilter(display,picture,filter==2?FilterConvolution:filter?FilterBilinear:FilterNearest,
        filter==2?kernel:NULL,filter==2?4:0);
    if(!pixman_image_set_filter(ref,filter==2?PIXMAN_FILTER_CONVOLUTION:filter?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,
        filter==2?kernel:NULL,filter==2?4:0))exit(2);
}
static void gradient_checks(Window root)
{
    const pixman_format_code_t codes[]={PIXMAN_a8r8g8b8,PIXMAN_b8g8r8a8,PIXMAN_a2r2g2b2,
        PIXMAN_x4a4,PIXMAN_a4r4g4b4,PIXMAN_x1r5g5b5};
    const unsigned ops[]={PictOpSrc,PictOpOver,PictOpSoftLight,PictOpSaturate,PictOpHSLColor,PictOpDisjointOver};
    const pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    XRenderPictFormat *rgba=map_saturate_format(PIXMAN_a8r8g8b8);
    for(unsigned kind=1;kind<=3;++kind)for(unsigned layout=0;layout<6;++layout)for(unsigned role=0;role<3;++role){
        if(!disjoint_request())continue;
        unsigned filter=layout%3,mapped=layout>=4,component=role && (layout&1);
        pixman_format_code_t dc=codes[layout],mc=layout==5?PIXMAN_x4b4g4r4:PIXMAN_r5g6b5;
        XRenderPictFormat *df=map_saturate_format_at_depth(dc,layout==5?15:0);
        Pixmap pixmaps[3]={0};GC draw[3]={0};Picture pictures[3];pixman_image_t *ref[3];void *memory[3]={0};
        for(unsigned i=0;i<3;++i){
            if(i<2 && (i==0?role!=1:role!=0)){
                pictures[i]=gradient_client(kind,0,0,ref+i);
            }else{
                XRenderPictFormat *pf=i==2?df:rgba;
                pixman_format_code_t pc=i==2?dc:PIXMAN_a8r8g8b8;
                unsigned long initial[W*H];
                for(unsigned n=0;n<W*H;++n){unsigned a=(n*73+29+i*37+kind*19)&255;
                    initial[n]=i==2?((n*8191U+0x317915)^(n*0x10103U))&
                        (pf->depth==32?UINT32_MAX:(1U<<pf->depth)-1):
                        (a<<24)|((n*17%256)*a/255<<16)|((n*37%256)*a/255<<8)|(n*53%256)*a/255;
                }
                pixmaps[i]=XCreatePixmap(display,root,W,H,pf->depth);draw[i]=XCreateGC(display,pixmaps[i],0,NULL);
                format_upload(pixmaps[i],draw[i],pf,initial);pictures[i]=XRenderCreatePicture(display,pixmaps[i],pf,0,NULL);
                ref[i]=solid_client_bits(initial,pc,memory+i);
            }
            if(i<2)gradient_client_state(pictures[i],ref[i],layout%4,filter,i==1 && component,&transform);
        }
        Picture map=None;pixman_image_t *ai=NULL;
        if(mapped){XRenderPictFormat *mf=map_saturate_format_at_depth(mc,layout==5?15:0);
            map=XRenderCreatePicture(display,pixmaps[2],mf,0,NULL);
            ai=pixman_image_create_bits(mc,W,H,memory[2],(W*PIXMAN_FORMAT_BPP(dc)+31)/32*4);if(!ai)exit(2);
            XRenderPictureAttributes attr={.alpha_map=map,.alpha_x_origin=1};
            XRenderChangePicture(display,pictures[2],CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin,&attr);
            pixman_image_set_alpha_map(ref[2],ai,1,0);
        }
        pixman_image_composite32(ops[layout],ref[0],role?ref[1]:NULL,ref[2],-1,1,2,1,3,2,1,1);
        printf("ps2-gpu2d: Render gradient kind=%u layout=%u role=%u filter=%u ca=%u map=%u\n",kind,layout,role,filter,component,mapped);
        XRenderComposite(display,ops[layout],pictures[0],role?pictures[1]:None,pictures[2],-1,1,2,1,3,2,1,1);
        if(PIXMAN_FORMAT_TYPE(dc)==PIXMAN_TYPE_BGRA)format_verify(pixmaps[2],df,memory[2],"gradient BGRA",ops[layout]);
        else map_saturate_verify(pixmaps[2],mapped && PIXMAN_FORMAT_DEPTH(mc)>PIXMAN_FORMAT_DEPTH(dc)?mc:dc,memory[2],"gradient against pixman");
        if(mapped){XRenderPictureAttributes attr={.alpha_map=None};XRenderChangePicture(display,pictures[2],CPAlphaMap,&attr);
            XRenderFreePicture(display,map);pixman_image_unref(ai);}
        for(unsigned i=0;i<3;++i){XRenderFreePicture(display,pictures[i]);pixman_image_unref(ref[i]);free(memory[i]);
            if(draw[i])XFreeGC(display,draw[i]);
            if(pixmaps[i])XFreePixmap(display,pixmaps[i]);}
    }
    for(unsigned kind=1;kind<=3;++kind)for(unsigned vector=0;vector<6;++vector){
        if(!disjoint_request())continue;
        Pixmap dp=XCreatePixmap(display,root,W,H,32);GC draw=XCreateGC(display,dp,0,NULL);
        unsigned long initial[W*H];for(unsigned i=0;i<W*H;++i)initial[i]=(i*8191U+0x317915)^(i*0x10103U);
        format_upload(dp,draw,rgba,initial);Picture pd=XRenderCreatePicture(display,dp,rgba,0,NULL),source,mask=None;
        void *memory;pixman_image_t *di=solid_client_bits(initial,PIXMAN_a8r8g8b8,&memory),*si,*mi=NULL;
        source=gradient_client(kind,vector==5,vector<2,&si);
        pixman_transform_t pole={{{65536,8192,-21845},{4096,49152,21845},{65536,0,vector==2?98304:32768}}};
        unsigned operation=vector<2?PictOpSaturate:PictOpSrc,repeat=vector==4?RepeatNone:RepeatReflect;
        gradient_client_state(source,si,repeat,vector==1?2:0,0,vector==2 || vector==3?&pole:NULL);
        if(vector<2){mask=gradient_client(kind,0,1,&mi);gradient_client_state(mask,mi,RepeatReflect,vector==1?2:0,0,NULL);}
        int sx=vector==4?21:vector==5?0:-2,sy=vector==5?0:-1;unsigned width=vector==2 || vector==3?4:1;
        pixman_image_composite32(operation,si,mi,di,sx,sy,0,0,3,2,width,1);
        printf("ps2-gpu2d: Render gradient boundary kind=%u vector=%u width=%u\n",kind,vector,width);
        XRenderComposite(display,operation,source,mask,pd,sx,sy,0,0,3,2,width,1);
        map_saturate_verify(dp,PIXMAN_a8r8g8b8,memory,"gradient opacity/pole/none/degenerate against pixman");
        XRenderFreePicture(display,source);pixman_image_unref(si);
        if(mask){XRenderFreePicture(display,mask);pixman_image_unref(mi);}
        XRenderFreePicture(display,pd);pixman_image_unref(di);free(memory);XFreeGC(display,draw);XFreePixmap(display,dp);
    }
}

static void packed10_checks(Window root,unsigned extended)
{
    const pixman_format_code_t codes[]={PIXMAN_a2r10g10b10,PIXMAN_a2b10g10r10,PIXMAN_x2r10g10b10,PIXMAN_x2b10g10r10};
    const pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    const unsigned ops[]={PictOpSaturate,PictOpDisjointOver,PictOpDisjointOverReverse,PictOpDisjointIn,PictOpDisjointInReverse,
        PictOpDisjointOut,PictOpDisjointOutReverse,PictOpDisjointAtop,PictOpDisjointAtopReverse,PictOpDisjointXor,
        PictOpConjointOver,PictOpConjointOverReverse,PictOpConjointIn,PictOpConjointInReverse,PictOpConjointOut,PictOpConjointOutReverse,
        PictOpConjointAtop,PictOpConjointAtopReverse,PictOpConjointXor,
        PictOpMultiply,PictOpScreen,PictOpOverlay,PictOpDarken,PictOpLighten,PictOpHardLight,PictOpDifference,PictOpExclusion,
        PictOpColorDodge,PictOpColorBurn,PictOpSoftLight,PictOpHSLHue,PictOpHSLSaturation,PictOpHSLColor,PictOpHSLLuminosity};
    for(unsigned index=0;index<(extended?34U:4U);++index)for(unsigned variant=0;variant<(extended?2U:18U);++variant){
        unsigned layout=extended?index%4:index,profile=extended?(variant?(index%8==7?17:15):0):variant;
        unsigned procedural=extended?index/8%4:layout;
        if(!disjoint_request())continue;
        unsigned operation=profile<13?profile:profile<15?(profile==13?PictOpSrc:PictOpOver):profile==15?PictOpOver:profile==16?PictOpXor:PictOpOver;
        if(extended)operation=ops[index];
        unsigned masked=profile>=15,component=extended?(variant && operation<PictOpHSLHue && (index&1)):(profile==15 || profile==16),alias=profile==11;
        pixman_format_code_t formats[]={codes[layout],codes[(layout+2)%4],codes[(layout+1)%4]};
        if(profile==13)formats[0]=PIXMAN_a8b8g8r8;
        if(profile==14)formats[2]=PIXMAN_a8b8g8r8;
        Pixmap pixmaps[3]={0};GC draw[3]={0};Picture pictures[3];pixman_image_t *ref[3];void *memory[3]={0};
        for(unsigned i=0;i<3;++i){
            if(i<2 && profile==17){
                if(procedural<3)pictures[i]=gradient_client(procedural+1,0,0,ref+i);
                else{XRenderColor colour={11113+i*1321,7711+i*1921,17131+i*2131,32767+i*4099};
                    pixman_color_t c={colour.red,colour.green,colour.blue,colour.alpha};
                    pictures[i]=XRenderCreateSolidFill(display,&colour);ref[i]=pixman_image_create_solid_fill(&c);if(!ref[i])exit(2);}
            }else{
                XRenderPictFormat *pf=map_saturate_format_at_depth(formats[i],32);
                unsigned long initial[W*H];for(unsigned n=0;n<W*H;++n)
                    initial[n]=((n*8191U+0x317915+i*0x1f3d731+layout*0x1779131)^(n*0x10103U))|((n+i+layout)%4U<<30);
                pixmaps[i]=XCreatePixmap(display,root,W,H,32);draw[i]=XCreateGC(display,pixmaps[i],0,NULL);
                format_upload(pixmaps[i],draw[i],pf,initial);pictures[i]=XRenderCreatePicture(display,pixmaps[i],pf,0,NULL);
                ref[i]=solid_client_bits(initial,formats[i],memory+i);
            }
            if(i<2)gradient_client_state(pictures[i],ref[i],RepeatReflect,profile>=13 && (extended || layout<3)?1:0,i==1 && component,profile>=13?&transform:NULL);
        }
        if(alias){XRenderFreePicture(display,pictures[0]);pixman_image_unref(ref[0]);free(memory[0]);memory[0]=NULL;
            XFreeGC(display,draw[0]);XFreePixmap(display,pixmaps[0]);draw[0]=0;pixmaps[0]=0;
            XRenderPictFormat *sf=map_saturate_format_at_depth(formats[0],32);
            pictures[0]=XRenderCreatePicture(display,pixmaps[2],sf,0,NULL);
            size_t bytes=W*H*4;memory[0]=malloc(bytes);if(!memory[0])exit(2);memcpy(memory[0],memory[2],bytes);
            ref[0]=pixman_image_create_bits(formats[0],W,H,memory[0],W*4);if(!ref[0])exit(2);}
        unsigned width=alias?4:1;
        pixman_image_composite32(operation,ref[0],masked?ref[1]:NULL,ref[2],1,1,2,1,3,2,width,1);
        printf("ps2-gpu2d: Render packed10 layout=%u profile=%u op=%u ca=%u alias=%u extended=%u\n",layout,profile,operation,component,alias,extended);
        XRenderComposite(display,operation,pictures[0],masked?pictures[1]:None,pictures[2],1,1,2,1,3,2,width,1);
        map_saturate_verify(pixmaps[2],formats[2],memory[2],"packed10 against pixman");
        for(unsigned i=0;i<3;++i){XRenderFreePicture(display,pictures[i]);pixman_image_unref(ref[i]);free(memory[i]);
            if(draw[i])XFreeGC(display,draw[i]);
            if(pixmaps[i])XFreePixmap(display,pixmaps[i]);}
    }
}

static void packed10_convolution_checks(Window root)
{
    const pixman_format_code_t codes[]={PIXMAN_a2r10g10b10,PIXMAN_a2b10g10r10,PIXMAN_x2r10g10b10,PIXMAN_x2b10g10r10};
    const pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    const unsigned ops[]={PictOpSrc,PictOpOver,PictOpSaturate,PictOpDisjointAtop,PictOpDifference,PictOpColorBurn,PictOpSoftLight,PictOpHSLColor};
    XFixed kernels[][6]={{131072,65536,98303,-32767},{131072,131072,INT32_MAX,INT32_MIN,257,65535}};
    for(unsigned layout=0;layout<4;++layout)for(unsigned oi=0;oi<8;++oi)for(unsigned profile=0;profile<2;++profile){
        if(!disjoint_request())continue;
        unsigned operation=ops[oi],component=profile && (layout&1) && operation<PictOpHSLHue;
        unsigned alias=profile?(layout&1?2:1):0;
        pixman_format_code_t formats[]={codes[layout],codes[(layout+2)%4],codes[(layout+1)%4]};
        if(profile && layout==1)formats[1]=PIXMAN_a8b8g8r8;
        Pixmap pixmaps[3];GC draw[3];Picture pictures[3];pixman_image_t *ref[3];void *memory[3]={0};
        for(unsigned i=0;i<3;++i){
            XRenderPictFormat *pf=map_saturate_format_at_depth(formats[i],32);unsigned long initial[W*H];
            for(unsigned n=0;n<W*H;++n)initial[n]=((n*8191U+0x317915+i*0x1f3d731+layout*0x1779131)^(n*0x10103U))|((n+i+layout)%4U<<30);
            pixmaps[i]=XCreatePixmap(display,root,W,H,32);draw[i]=XCreateGC(display,pixmaps[i],0,NULL);
            format_upload(pixmaps[i],draw[i],pf,initial);pictures[i]=XRenderCreatePicture(display,pixmaps[i],pf,0,NULL);
            ref[i]=solid_client_bits(initial,formats[i],memory+i);
        }
        if(alias){unsigned i=alias-1;
            XRenderFreePicture(display,pictures[i]);pixman_image_unref(ref[i]);free(memory[i]);memory[i]=malloc(W*H*4);if(!memory[i])exit(2);
            memcpy(memory[i],memory[2],W*H*4);pictures[i]=XRenderCreatePicture(display,pixmaps[2],map_saturate_format_at_depth(formats[i],32),0,NULL);
            ref[i]=pixman_image_create_bits(formats[i],W,H,memory[i],W*4);if(!ref[i])exit(2);
        }
        for(unsigned i=0;i<2;++i){
            gradient_client_state(pictures[i],ref[i],profile?RepeatReflect:layout,0,i==1 && component,profile?NULL:&transform);
            unsigned kernel=i?1:profile;unsigned count=kernel?6:4;
            XRenderSetPictureFilter(display,pictures[i],FilterConvolution,kernels[kernel],count);
            if(!pixman_image_set_filter(ref[i],PIXMAN_FILTER_CONVOLUTION,kernels[kernel],count))exit(2);
        }
        unsigned width=profile?2:1;int sx=profile?3:1,sy=profile?2:1,mx=3,my=2;
        pixman_image_composite32(operation,ref[0],profile?ref[1]:NULL,ref[2],sx,sy,mx,my,3,2,width,1);
        printf("ps2-gpu2d: Render packed10-convolution layout=%u profile=%u op=%u ca=%u alias=%u\n",layout,profile,operation,component,alias);
        XRenderComposite(display,operation,pictures[0],profile?pictures[1]:None,pictures[2],sx,sy,mx,my,3,2,width,1);
        map_saturate_verify(pixmaps[2],formats[2],memory[2],"packed10 convolution against pixman");
        for(unsigned i=0;i<3;++i){XRenderFreePicture(display,pictures[i]);pixman_image_unref(ref[i]);free(memory[i]);XFreeGC(display,draw[i]);XFreePixmap(display,pixmaps[i]);}
    }
}
static void packed10_alpha_read_checks(Window root)
{
    const pixman_format_code_t codes[]={PIXMAN_a2r10g10b10,PIXMAN_a2b10g10r10,PIXMAN_x2r10g10b10,PIXMAN_x2b10g10r10};
    const unsigned ops[]={PictOpSrc,PictOpOver,PictOpSaturate,PictOpHSLColor,PictOpOver,PictOpSaturate,PictOpSrc,PictOpSoftLight};
    const pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}},pole={{{65536,0,0},{0,65536,0},{0,0,0}}};
    XFixed kernel[]={131072,65536,98303,-32767};
    for(unsigned layout=0;layout<4;++layout)for(unsigned step=0;step<8;++step){
        unsigned profile=(step+layout*3)%8;if(!disjoint_request())continue;
        unsigned role=profile==0 || profile==3?1:profile==1 || profile==4 || profile==5?2:3,
            filter=profile==3 || profile==6?0:profile%3,ca=profile==1 || profile==5,alias=profile==3 || profile==6;
        pixman_format_code_t formats[]={codes[layout],codes[(layout+2)%4],codes[(layout+1)%4]},map_codes[2];
        if(profile==4)formats[0]=formats[1]=formats[2]=PIXMAN_a8r8g8b8;
        Pixmap pixmaps[3],maps[2]={0};GC draw[3],map_gc[2]={0};Picture pictures[3],map_pictures[2];
        pixman_image_t *ref[3],*map_ref[2];void *memory[3]={0},*map_memory[2]={0};
        for(unsigned i=0;i<3;++i){unsigned long initial[W*H];
            for(unsigned n=0;n<W*H;++n)initial[n]=((n*8191U+0x317915+i*0x1f3d731+layout*0x1779131)^(n*0x10103U))|((n+i+layout)%4U<<30);
            XRenderPictFormat *pf=map_saturate_format_at_depth(formats[i],32);
            pixmaps[i]=XCreatePixmap(display,root,W,H,32);draw[i]=XCreateGC(display,pixmaps[i],0,NULL);
            format_upload(pixmaps[i],draw[i],pf,initial);pictures[i]=XRenderCreatePicture(display,pixmaps[i],pf,0,NULL);
            ref[i]=solid_client_bits(initial,formats[i],memory+i);
        }
        if(profile==6)for(unsigned i=0;i<2;++i){
            XRenderFreePicture(display,pictures[i]);pixman_image_unref(ref[i]);free(memory[i]);memory[i]=malloc(W*H*4);if(!memory[i])exit(2);
            memcpy(memory[i],memory[2],W*H*4);pictures[i]=XRenderCreatePicture(display,pixmaps[2],map_saturate_format_at_depth(formats[i],32),0,NULL);
            ref[i]=pixman_image_create_bits(formats[i],W,H,memory[i],W*4);if(!ref[i])exit(2);
        }
        if(profile==4){XRenderColor colour={11113,7711,17131,32767};pixman_color_t c={colour.red,colour.green,colour.blue,colour.alpha};
            XRenderFreePicture(display,pictures[0]);pixman_image_unref(ref[0]);free(memory[0]);memory[0]=NULL;
            pictures[0]=XRenderCreateSolidFill(display,&colour);ref[0]=pixman_image_create_solid_fill(&c);if(!ref[0])exit(2);
        }
        for(unsigned i=0;i<2;++i){
            gradient_client_state(pictures[i],ref[i],RepeatReflect,filter==1,i==1 && ca,alias || (profile==4 && !i)?NULL:&transform);
            if(filter==2){XRenderSetPictureFilter(display,pictures[i],FilterConvolution,kernel,4);
                if(!pixman_image_set_filter(ref[i],PIXMAN_FILTER_CONVOLUTION,kernel,4))exit(2);}
            map_codes[i]=profile==0?PIXMAN_a1:profile==1?PIXMAN_x4a4:profile==2?PIXMAN_a8:
                profile==7 && !i?PIXMAN_b8g8r8a8:codes[(layout+i)%4];
            unsigned packed=PIXMAN_FORMAT_R(map_codes[i])==10;
            XRenderPictFormat *pf=map_saturate_format_at_depth(map_codes[i],packed?32:0);
            if(alias){map_pictures[i]=XRenderCreatePicture(display,pixmaps[2],pf,0,NULL);
                map_memory[i]=malloc(W*H*4);if(!map_memory[i])exit(2);memcpy(map_memory[i],memory[2],W*H*4);
                map_ref[i]=pixman_image_create_bits(map_codes[i],W,H,map_memory[i],W*4);if(!map_ref[i])exit(2);
            }else{unsigned long initial[W*H];
                for(unsigned n=0;n<W*H;++n)initial[n]=((n*7919+0x196131+i*0x13711)^(n*0x10103U))|((n+profile+i)%4U<<30);
                maps[i]=XCreatePixmap(display,root,W,H,pf->depth);map_gc[i]=XCreateGC(display,maps[i],0,NULL);
                format_upload(maps[i],map_gc[i],pf,initial);map_pictures[i]=XRenderCreatePicture(display,maps[i],pf,0,NULL);
                map_ref[i]=solid_client_bits(initial,map_codes[i],map_memory+i);
            }
            gradient_client_state(map_pictures[i],map_ref[i],RepeatReflect,1,0,&pole);
            if(role&(1U<<i)){XRenderPictureAttributes attr={.alpha_map=map_pictures[i],.alpha_x_origin=1,.alpha_y_origin=0};
                XRenderChangePicture(display,pictures[i],CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin,&attr);
                pixman_image_set_alpha_map(ref[i],map_ref[i],1,0);}
        }
        unsigned width=alias?2:1;int sx=alias?3:2,sy=alias?2:1;
        pixman_image_composite32(ops[profile],ref[0],role&2?ref[1]:NULL,ref[2],sx,sy,3,2,3,2,width,1);
        printf("ps2-gpu2d: Render packed10-alpha-read layout=%u profile=%u op=%u role=%u filter=%u ca=%u alias=%u\n",layout,profile,ops[profile],role,filter,ca,alias);
        XRenderComposite(display,ops[profile],pictures[0],role&2?pictures[1]:None,pictures[2],sx,sy,3,2,3,2,width,1);
        map_saturate_verify(pixmaps[2],formats[2],memory[2],"packed10 alpha reads against pixman");
        for(unsigned i=0;i<3;++i){XRenderFreePicture(display,pictures[i]);pixman_image_unref(ref[i]);free(memory[i]);XFreeGC(display,draw[i]);XFreePixmap(display,pixmaps[i]);}
        for(unsigned i=0;i<2;++i){XRenderFreePicture(display,map_pictures[i]);pixman_image_unref(map_ref[i]);free(map_memory[i]);
            if(maps[i]){XFreeGC(display,map_gc[i]);XFreePixmap(display,maps[i]);}}
    }
}

static void packed10_alpha_write_checks(Window root)
{
    const pixman_format_code_t codes[]={PIXMAN_a2r10g10b10,PIXMAN_a2b10g10r10,PIXMAN_x2r10g10b10,PIXMAN_x2b10g10r10};
    const unsigned ops[]={PictOpSrc,PictOpOver,PictOpSaturate,PictOpHSLColor,PictOpOver,PictOpSoftLight,PictOpHSLColor,PictOpClear};
    const pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}},pole={{{65536,0,0},{0,65536,0},{0,0,0}}};
    XFixed kernel[]={131072,65536,98303,-32767};
    for(unsigned layout=0;layout<4;++layout)for(unsigned step=0;step<8;++step){
        unsigned profile=(step+layout*3)%8;if(!disjoint_request())continue;
        unsigned alias=profile==5 || profile==6,inputs_alias=profile==6,filter=alias?0:profile%3,masked=profile==2 || profile==3 || profile==5 || profile==6,ca=profile==2 || profile==3 || profile==5 || profile==6;
        pixman_format_code_t formats[]={codes[layout],codes[(layout+2)%4],codes[(layout+1)%4]},
            map_code=profile==0?PIXMAN_a1:profile==1?PIXMAN_x4a4:profile==2?PIXMAN_a8:profile==3?PIXMAN_b8g8r8a8:
                profile==7?layout&1?PIXMAN_x1b5g5r5:PIXMAN_r5g6b5:codes[layout];
        if(profile==4)formats[0]=formats[1]=formats[2]=PIXMAN_a8r8g8b8;
        Pixmap pixmaps[3],map_pixmap=0;GC draw[3],map_gc=0;Picture pictures[3],map_picture;
        pixman_image_t *ref[3],*map_ref;void *memory[3]={0},*map_memory=NULL;
        for(unsigned i=0;i<3;++i){unsigned long initial[W*H];
            for(unsigned n=0;n<W*H;++n)initial[n]=((n*8191U+0x317915+i*0x1f3d731+layout*0x1779131)^(n*0x10103U))|((n+i+layout)%4U<<30);
            XRenderPictFormat *pf=map_saturate_format_at_depth(formats[i],32);
            pixmaps[i]=XCreatePixmap(display,root,W,H,32);draw[i]=XCreateGC(display,pixmaps[i],0,NULL);
            format_upload(pixmaps[i],draw[i],pf,initial);pictures[i]=XRenderCreatePicture(display,pixmaps[i],pf,0,NULL);ref[i]=solid_client_bits(initial,formats[i],memory+i);
        }
        if(inputs_alias)for(unsigned i=0;i<2;++i){
            XRenderFreePicture(display,pictures[i]);pixman_image_unref(ref[i]);free(memory[i]);memory[i]=malloc(W*H*4);if(!memory[i])exit(2);
            memcpy(memory[i],memory[2],W*H*4);pictures[i]=XRenderCreatePicture(display,pixmaps[2],map_saturate_format_at_depth(formats[i],32),0,NULL);
            ref[i]=pixman_image_create_bits(formats[i],W,H,memory[i],W*4);if(!ref[i])exit(2);
        }
        if(profile==4){XRenderColor colour={11113,7711,17131,32767};pixman_color_t c={colour.red,colour.green,colour.blue,colour.alpha};
            XRenderFreePicture(display,pictures[0]);pixman_image_unref(ref[0]);free(memory[0]);memory[0]=NULL;
            pictures[0]=XRenderCreateSolidFill(display,&colour);ref[0]=pixman_image_create_solid_fill(&c);if(!ref[0])exit(2);
        }
        for(unsigned i=0;i<2;++i){
            gradient_client_state(pictures[i],ref[i],RepeatReflect,filter==1,i==1 && ca,alias || (profile==4 && !i)?NULL:&transform);
            if(filter==2){XRenderSetPictureFilter(display,pictures[i],FilterConvolution,kernel,4);if(!pixman_image_set_filter(ref[i],PIXMAN_FILTER_CONVOLUTION,kernel,4))exit(2);}
        }
        unsigned packed=PIXMAN_FORMAT_R(map_code)==10;
        XRenderPictFormat *pf=map_saturate_format_at_depth(map_code,packed?32:profile==7?layout&1?15:16:0);
        if(alias){map_picture=XRenderCreatePicture(display,pixmaps[2],pf,0,NULL);map_memory=memory[2];map_ref=pixman_image_create_bits(map_code,W,H,map_memory,W*4);if(!map_ref)exit(2);}
        else{unsigned long initial[W*H];
            for(unsigned n=0;n<W*H;++n)initial[n]=((n*7919+0x196131+profile*0x13711)^(n*0x10103U))|((n+profile)%4U<<30);
            map_pixmap=XCreatePixmap(display,root,W,H,pf->depth);map_gc=XCreateGC(display,map_pixmap,0,NULL);format_upload(map_pixmap,map_gc,pf,initial);
            map_picture=XRenderCreatePicture(display,map_pixmap,pf,0,NULL);map_ref=solid_client_bits(initial,map_code,&map_memory);
        }
        gradient_client_state(map_picture,map_ref,RepeatReflect,1,0,&pole);
        int ax=alias && profile==6?-1:1,ay=alias && profile==6?-1:1;
        XRenderPictureAttributes attr={.alpha_map=map_picture,.alpha_x_origin=ax,.alpha_y_origin=ay};
        XRenderChangePicture(display,pictures[2],CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin,&attr);pixman_image_set_alpha_map(ref[2],map_ref,ax,ay);
        unsigned width=alias?2:1,height=alias && !inputs_alias?2:1;
        pixman_image_composite32(ops[profile],ref[0],masked?ref[1]:NULL,ref[2],2,1,3,2,3,2,width,height);
        printf("ps2-gpu2d: Render packed10-alpha-write layout=%u profile=%u op=%u filter=%u ca=%u alias=%u\n",layout,profile,ops[profile],filter,ca,alias);
        XRenderComposite(display,ops[profile],pictures[0],masked?pictures[1]:None,pictures[2],2,1,3,2,3,2,width,height);
        map_saturate_verify(pixmaps[2],formats[2],memory[2],"packed10 writable map parent against pixman");
        map_saturate_verify(alias?pixmaps[2]:map_pixmap,map_code,map_memory,"packed10 writable map store against pixman");
        XRenderFreePicture(display,map_picture);pixman_image_unref(map_ref);
        if(!alias){free(map_memory);XFreeGC(display,map_gc);XFreePixmap(display,map_pixmap);}
        for(unsigned i=0;i<3;++i){XRenderFreePicture(display,pictures[i]);pixman_image_unref(ref[i]);free(memory[i]);XFreeGC(display,draw[i]);XFreePixmap(display,pixmaps[i]);}
    }
}

static void packed10_mixed_checks(Window root)
{
    const pixman_format_code_t low[]={PIXMAN_a1,PIXMAN_x4a4,PIXMAN_a8,
        PIXMAN_r3g3b2,PIXMAN_b2g3r3,PIXMAN_a2r2g2b2,PIXMAN_a2b2g2r2,
        PIXMAN_r5g6b5,PIXMAN_b5g6r5,PIXMAN_a1r5g5b5,PIXMAN_a1b5g5r5,PIXMAN_a4r4g4b4,PIXMAN_a4b4g4r4,
        PIXMAN_x1r5g5b5,PIXMAN_x1b5g5r5,PIXMAN_x4r4g4b4,PIXMAN_x4b4g4r4,
        PIXMAN_x1r5g5b5,PIXMAN_x1b5g5r5,PIXMAN_x4r4g4b4,PIXMAN_x4b4g4r4},
        codes[]={PIXMAN_a2r10g10b10,PIXMAN_a2b10g10r10,PIXMAN_x2r10g10b10,PIXMAN_x2b10g10r10};
    const unsigned ops[]={PictOpSrc,PictOpOver,PictOpSaturate,PictOpDisjointOver,PictOpConjointXor,
        PictOpMultiply,PictOpColorDodge,PictOpSoftLight,PictOpHSLColor,PictOpClear,PictOpInReverse};
    const pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    XFixed kernel[]={131072,65536,98303,-32767};
    for(unsigned format=0;format<21;++format)for(unsigned role=0;role<3;++role){
        if(!disjoint_request())continue;
        unsigned layout=(format+role)%4,filter=(format+role)%3,ca=(format+role)&1,op=ops[(format*3+role)%11];
        pixman_format_code_t fc[4];Pixmap pixels[4];GC draw[4];Picture pictures[4];pixman_image_t *ref[4];void *memory[4];
        for(unsigned i=0;i<4;++i){
            fc[i]=i==role?low[format]:codes[(layout+i)%4];
            XRenderPictFormat *pf=map_saturate_format_at_depth(fc[i],i==role?format>=17?15:0:32);
            unsigned long initial[W*H];
            for(unsigned n=0;n<W*H;++n)initial[n]=(n*7919+0x196131+format*0x13711+i*0x197113)^(n*0x10103U);
            pixels[i]=XCreatePixmap(display,root,W,H,pf->depth);draw[i]=XCreateGC(display,pixels[i],0,NULL);
            format_upload(pixels[i],draw[i],pf,initial);pictures[i]=XRenderCreatePicture(display,pixels[i],pf,0,NULL);
            ref[i]=solid_client_bits(initial,fc[i],memory+i);
        }
        for(unsigned i=0;i<2;++i){
            gradient_client_state(pictures[i],ref[i],RepeatReflect,filter==1,i==1 && ca,filter?&transform:NULL);
            if(filter==2){XRenderSetPictureFilter(display,pictures[i],FilterConvolution,kernel,4);
                if(!pixman_image_set_filter(ref[i],PIXMAN_FILTER_CONVOLUTION,kernel,4))exit(2);}
        }
        XRenderPictureAttributes attr={.alpha_map=pictures[3],.alpha_x_origin=1,.alpha_y_origin=1};
        XRenderChangePicture(display,pictures[2],CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin,&attr);
        pixman_image_set_alpha_map(ref[2],ref[3],1,1);
        pixman_image_composite32(op,ref[0],ref[1],ref[2],2,1,3,2,3,2,1,1);
        printf("ps2-gpu2d: Render packed10-mixed format=%u role=%u filter=%u op=%u ca=%u\n",format,role,filter,op,ca);
        XRenderComposite(display,op,pictures[0],pictures[1],pictures[2],2,1,3,2,3,2,1,1);
        map_saturate_verify(pixels[2],fc[2],memory[2],"packed10 mixed parent against pixman");
        map_saturate_verify(pixels[3],fc[3],memory[3],"packed10 mixed writable map against pixman");
        for(unsigned i=0;i<4;++i){XRenderFreePicture(display,pictures[i]);pixman_image_unref(ref[i]);free(memory[i]);XFreeGC(display,draw[i]);XFreePixmap(display,pixels[i]);}
    }
}

static void depth30_checks(Window root)
{
    const pixman_format_code_t rgb[]={PIXMAN_x2r10g10b10,PIXMAN_x2b10g10r10},argb[]={PIXMAN_a2r10g10b10,PIXMAN_a2b10g10r10};
    const unsigned ops[]={PictOpSrc,PictOpOver,PictOpSaturate,PictOpDisjointOver,PictOpConjointXor,PictOpSoftLight,PictOpHSLColor,
        PictOpOver,PictOpSrc,PictOpSaturate,PictOpSoftLight,PictOpHSLColor,PictOpSrc,PictOpOver,PictOpSrc,PictOpSaturate};
    const pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    XFixed kernel[]={131072,65536,98303,-32767};
    for(unsigned layout=0;layout<2;++layout)for(unsigned step=0;step<16;++step){
        unsigned profile=(step+layout*3)%16;if(!disjoint_request())continue;
        unsigned alias=profile>=8 && profile<12,inputs_alias=profile==11,filter=alias?0:profile%3,ca=(profile+layout)&1;
        pixman_format_code_t fc[4];unsigned depth[4];Pixmap pixels[4];GC draw[4];Picture pictures[4];pixman_image_t *ref[4];void *memory[4];
        for(unsigned i=0;i<4;++i){
            unsigned raw30=i==3 || (profile<7?((profile+1)&(1U<<i)):profile<12);
            fc[i]=raw30?rgb[(layout+i)%2]:argb[(layout+i)%2];depth[i]=raw30?30:32;
            if(profile==7 && i==3){fc[i]=PIXMAN_a8;depth[i]=8;}
            if(profile>=12 && i<3){fc[i]=PIXMAN_a8r8g8b8;depth[i]=32;}
            if(profile==13 && i<3){fc[i]=i==0?PIXMAN_r5g6b5:i==1?PIXMAN_x4a4:PIXMAN_x4r4g4b4;depth[i]=i==0?16:i==1?8:15;}
            if(profile==14 && i<3){fc[i]=i==0?PIXMAN_a8:i==1?PIXMAN_a1:PIXMAN_x4a4;depth[i]=i==1?1:8;}
            XRenderPictFormat *pf=map_saturate_format_at_depth(fc[i],depth[i]);unsigned long initial[W*H];
            for(unsigned n=0;n<W*H;++n)initial[n]=(n*7919U+0x196131U+profile*0x13711U+i*0x197113U)^(n*0x10103U);
            pixels[i]=XCreatePixmap(display,root,W,H,pf->depth);draw[i]=XCreateGC(display,pixels[i],0,NULL);
            format_upload(pixels[i],draw[i],pf,initial);pictures[i]=XRenderCreatePicture(display,pixels[i],pf,0,NULL);
            ref[i]=solid_client_bits(initial,fc[i],memory+i);
        }
        if(alias){XRenderFreePicture(display,pictures[3]);pixman_image_unref(ref[3]);free(memory[3]);
            pictures[3]=XRenderCreatePicture(display,pixels[2],map_saturate_format_at_depth(fc[3],30),0,NULL);memory[3]=memory[2];
            ref[3]=pixman_image_create_bits(fc[3],W,H,memory[3],W*4);if(!ref[3])exit(2);
        }
        if(inputs_alias)for(unsigned i=0;i<2;++i){XRenderFreePicture(display,pictures[i]);pixman_image_unref(ref[i]);free(memory[i]);
            memory[i]=malloc(W*H*4);if(!memory[i])exit(2);memcpy(memory[i],memory[2],W*H*4);
            pictures[i]=XRenderCreatePicture(display,pixels[2],map_saturate_format_at_depth(fc[i],30),0,NULL);
            ref[i]=pixman_image_create_bits(fc[i],W,H,memory[i],W*4);if(!ref[i])exit(2);
        }
        if(profile==15){XRenderColor colour={11113,7711,17131,32767};pixman_color_t c={colour.red,colour.green,colour.blue,colour.alpha};
            XRenderFreePicture(display,pictures[0]);pixman_image_unref(ref[0]);free(memory[0]);memory[0]=NULL;
            pictures[0]=XRenderCreateSolidFill(display,&colour);ref[0]=pixman_image_create_solid_fill(&c);if(!ref[0])exit(2);
        }
        for(unsigned i=0;i<2;++i){gradient_client_state(pictures[i],ref[i],RepeatReflect,filter==1,i==1 && ca,alias || (profile==15 && !i)?NULL:&transform);
            if(filter==2){XRenderSetPictureFilter(display,pictures[i],FilterConvolution,kernel,4);if(!pixman_image_set_filter(ref[i],PIXMAN_FILTER_CONVOLUTION,kernel,4))exit(2);}
        }
        int ax=alias && (profile&1)?-1:1,ay=alias && (profile&2)?-1:1;
        XRenderPictureAttributes attr={.alpha_map=pictures[3],.alpha_x_origin=ax,.alpha_y_origin=ay};
        XRenderChangePicture(display,pictures[2],CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin,&attr);pixman_image_set_alpha_map(ref[2],ref[3],ax,ay);
        unsigned width=alias?2:1,height=alias && !inputs_alias?2:1;
        pixman_image_composite32(ops[profile],ref[0],ref[1],ref[2],2,1,3,2,3,2,width,height);
        printf("ps2-gpu2d: Render depth30 layout=%u profile=%u filter=%u op=%u ca=%u alias=%u\n",layout,profile,filter,ops[profile],ca,alias);
        XRenderComposite(display,ops[profile],pictures[0],pictures[1],pictures[2],2,1,3,2,3,2,width,height);
        map_saturate_verify(pixels[2],fc[2],memory[2],"depth30 parent against pixman");
        map_saturate_verify(alias?pixels[2]:pixels[3],fc[3],memory[3],"depth30 writable map against pixman");
        for(unsigned i=0;i<4;++i){XRenderFreePicture(display,pictures[i]);pixman_image_unref(ref[i]);if(!alias || i!=3)free(memory[i]);XFreeGC(display,draw[i]);XFreePixmap(display,pixels[i]);}
    }
}


/* Discover actual server formats; no advertised format may be silently skipped.
 * This reference conversion is confined to the diagnostic client. */
static pixman_format_code_t public_format_code(const XRenderPictFormat *pf)
{
    unsigned bpp=pf->depth==1?1:pf->depth<=8?8:pf->depth<=16?16:32,
        r=__builtin_popcount((unsigned short)pf->direct.redMask),g=__builtin_popcount((unsigned short)pf->direct.greenMask),
        b=__builtin_popcount((unsigned short)pf->direct.blueMask),a=__builtin_popcount((unsigned short)pf->direct.alphaMask),type;
    if(pf->type!=PictTypeDirect){fprintf(stderr,"ps2-gpu2d: unhandled advertised indexed format id=%lu\n",pf->id);exit(2);}
    if(!r && !g && !b && pf->direct.alpha==0)type=PIXMAN_TYPE_A;
    else if(pf->direct.blue==0 && pf->direct.green==(int)b && pf->direct.red==(int)(g+b) && (!a || pf->direct.alpha==(int)(r+g+b)))type=PIXMAN_TYPE_ARGB;
    else if(pf->direct.red==0 && pf->direct.green==(int)r && pf->direct.blue==(int)(r+g) && (!a || pf->direct.alpha==(int)(r+g+b)))type=PIXMAN_TYPE_ABGR;
    else if(pf->direct.red==(int)(bpp-b-g-r) && pf->direct.green==(int)(bpp-b-g) && pf->direct.blue==(int)(bpp-b) && (!a || pf->direct.alpha==0))type=PIXMAN_TYPE_BGRA;
    else{fprintf(stderr,"ps2-gpu2d: unhandled advertised direct format id=%lu depth=%d\n",pf->id,pf->depth);exit(2);}
    return (pixman_format_code_t)PIXMAN_FORMAT(bpp,type,a,r,g,b);
}

static void public_format_verify(Pixmap pixmap,const XRenderPictFormat *pf,pixman_format_code_t code,const void *data,unsigned op)
{
    unsigned bpp=PIXMAN_FORMAT_BPP(code),stride=(W*bpp+31)/32*4;
    unsigned long expected[W*H],mask=((uint32_t)pf->direct.redMask<<pf->direct.red)|
        ((uint32_t)pf->direct.greenMask<<pf->direct.green)|((uint32_t)pf->direct.blueMask<<pf->direct.blue)|
        ((uint32_t)pf->direct.alphaMask<<pf->direct.alpha);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){uint32_t value=0;const unsigned char *row=(const unsigned char*)data+y*stride;
        if(bpp==1)value=(row[x/8]>>(x&7))&1;else memcpy(&value,row+x*(bpp/8),bpp/8);
        expected[y*W+x]=value&mask;
    }
    verify(pixmap,W,H,expected,mask,ZPixmap,"every public format/filter against pixman",op);
}

static void public_format_filter_checks(Window root)
{
    const char *names[]={FilterNearest,FilterBilinear,FilterConvolution,FilterFast,FilterGood,FilterBest};
    XFilters *filters=XRenderQueryFilters(display,root);if(!filters || filters->nfilter!=6){fprintf(stderr,"ps2-gpu2d: expected six public filters\n");exit(2);}
    for(unsigned f=0;f<6;++f){unsigned found=0;for(int n=0;n<filters->nfilter;++n)found|=!strcmp(names[f],filters->filter[n]);
        if(!found){fprintf(stderr,"ps2-gpu2d: public filter missing: %s\n",names[f]);exit(2);}}
    XFree(filters);
    const pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    XFixed kernel[]={131072,65536,98303,-32767};XRenderPictFormat any={0},*pf;unsigned count=0;
    while((pf=XRenderFindFormat(display,0,&any,count))){
        pixman_format_code_t code=public_format_code(pf);unsigned format=count++;
        printf("ps2-gpu2d: advertised format=%u id=%lu depth=%d code=%08x\n",format,pf->id,pf->depth,code);
        for(unsigned filter=0;filter<6;++filter)for(unsigned role=0;role<3;++role){
            if(!disjoint_request())continue;
            unsigned linear=filter==1 || filter>=4,convolution=filter==2,ca=(format+filter)&1;
            Pixmap pixels[3];GC draw[3];Picture pictures[3];pixman_image_t *ref[3];void *memory[3];pixman_format_code_t fc[3];
            for(unsigned i=0;i<3;++i){XRenderPictFormat *f=i==role?pf:map_saturate_format_at_depth(PIXMAN_a8r8g8b8,32);
                fc[i]=i==role?code:PIXMAN_a8r8g8b8;unsigned long initial[W*H];
                for(unsigned n=0;n<W*H;++n)initial[n]=(n*7919U+0x196131U+format*0x13711U+i*0x197113U)^(n*0x10103U);
                pixels[i]=XCreatePixmap(display,root,W,H,f->depth);draw[i]=XCreateGC(display,pixels[i],0,NULL);
                format_upload(pixels[i],draw[i],f,initial);pictures[i]=XRenderCreatePicture(display,pixels[i],f,0,NULL);
                ref[i]=solid_client_bits(initial,fc[i],memory+i);
            }
            for(unsigned i=0;i<2;++i){gradient_client_state(pictures[i],ref[i],RepeatReflect,linear,i==1 && ca,&transform);
                XRenderSetPictureFilter(display,pictures[i],names[filter],convolution?kernel:NULL,convolution?4:0);
                if(!pixman_image_set_filter(ref[i],convolution?PIXMAN_FILTER_CONVOLUTION:linear?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,convolution?kernel:NULL,convolution?4:0))exit(2);
            }
            unsigned masked=role==1,op=role==0?PictOpSrc:PictOpOver;
            pixman_image_composite32(op,ref[0],masked?ref[1]:NULL,ref[2],2,1,3,2,3,2,1,1);
            printf("ps2-gpu2d: Render public-format-filter format=%u depth=%d code=%08x role=%u filter=%s op=%u ca=%u\n",format,pf->depth,code,role,names[filter],op,masked && ca);
            XRenderComposite(display,op,pictures[0],masked?pictures[1]:None,pictures[2],2,1,3,2,3,2,1,1);
            public_format_verify(pixels[2],role==2?pf:map_saturate_format_at_depth(PIXMAN_a8r8g8b8,32),fc[2],memory[2],op);
            for(unsigned i=0;i<3;++i){XRenderFreePicture(display,pictures[i]);pixman_image_unref(ref[i]);free(memory[i]);XFreeGC(display,draw[i]);XFreePixmap(display,pixels[i]);}
        }
    }
    if(count!=38){fprintf(stderr,"ps2-gpu2d: expected38 public formats, found%u\n",count);exit(2);}
    printf("ps2-gpu2d: public formats=%u filters=6 roles=3 requests=684\n",count);
}


static void front_format_checks(Window root)
{
    XRenderPictFormat *target=XRenderFindVisualFormat(display,DefaultVisual(display,DefaultScreen(display)));
    if(!target || target->depth!=15)exit(2);
    pixman_format_code_t dc=public_format_code(target);
    const pixman_format_code_t codes[]={PIXMAN_a2r10g10b10,PIXMAN_a2b10g10r10,PIXMAN_x2r10g10b10,PIXMAN_x2b10g10r10,PIXMAN_x2r10g10b10,PIXMAN_x2b10g10r10};
    XFixed kernel[]={131072,65536,98303,-32767};
    const char *filters[]={FilterNearest,FilterBilinear,FilterConvolution};
    const pixman_transform_t transform={{{49152,21845,173},{0,98304,-21845},{65536/19,0,65536}}};
    GC fill=XCreateGC(display,root,0,NULL);
    for(unsigned layout=0;layout<6;++layout)for(unsigned filter=0;filter<3;++filter)for(unsigned over=0;over<2;++over){
        if(!disjoint_request())continue;
        XRenderPictFormat *sf=map_saturate_format_at_depth(codes[layout],layout>=4?30:32);
        Pixmap source=XCreatePixmap(display,root,W,H,sf->depth);GC draw=XCreateGC(display,source,0,NULL);
        unsigned long initial[W*H],dest[W*H];for(unsigned n=0;n<W*H;++n){initial[n]=(n*7919U+0x196131U)^(n*0x10103U);dest[n]=0x4413;}
        format_upload(source,draw,sf,initial);XSetForeground(display,fill,0x4413);XFillRectangle(display,root,fill,0,0,W,H);
        Picture sp=XRenderCreatePicture(display,source,sf,0,NULL),dp=XRenderCreatePicture(display,root,target,0,NULL);
        void *sm,*dm;pixman_image_t *sr=solid_client_bits(initial,codes[layout],&sm),*dr=solid_client_bits(dest,dc,&dm);
        gradient_client_state(sp,sr,RepeatReflect,filter==1,0,&transform);
        XRenderSetPictureFilter(display,sp,filters[filter],filter==2?kernel:NULL,filter==2?4:0);
        if(!pixman_image_set_filter(sr,filter==2?PIXMAN_FILTER_CONVOLUTION:filter?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,filter==2?kernel:NULL,filter==2?4:0))exit(2);
        unsigned op=over?PictOpOver:PictOpSrc;pixman_image_composite32(op,sr,NULL,dr,2,1,0,0,3,2,1,1);
        printf("ps2-gpu2d: Render front-format layout=%u depth=%d filter=%s op=%u\n",layout,sf->depth,filters[filter],op);
        XRenderComposite(display,op,sp,None,dp,2,1,0,0,3,2,1,1);public_format_verify(root,target,dc,dm,op);
        XRenderFreePicture(display,sp);XRenderFreePicture(display,dp);XFreeGC(display,draw);XFreePixmap(display,source);
        pixman_image_unref(sr);pixman_image_unref(dr);free(sm);free(dm);
    }
    XFreeGC(display,fill);
}


static unsigned resource_errors;
static int resource_error(Display *d,XErrorEvent *event)
{
    (void)d;++resource_errors;
    if(event->error_code!=BadAlloc){fprintf(stderr,"ps2-gpu2d: unexpected resource X error=%u request=%u\n",event->error_code,event->request_code);++errors;}
    return 0;
}
static void resource_checks(Window root)
{
    if(depth!=15)exit(2);
    unsigned rejected=0;
    for(unsigned phase=0;phase<2;++phase){
        XSync(display,False);resource_errors=0;XErrorHandler previous=XSetErrorHandler(resource_error);
        Pixmap held[160];unsigned count=0;
        if(!phase){XCreatePixmap(display,root,1024,1024,32);XSync(display,False);}
        else while(count<160){
            Pixmap next=XCreatePixmap(display,root,1,1,15);XSync(display,False);
            if(resource_errors)break;
            held[count++]=next;
        }
        XSetErrorHandler(previous);
        if(resource_errors!=1 || (phase && (!count || count>=128))){fprintf(stderr,"ps2-gpu2d: resource rejection phase=%u errors=%u held=%u\n",phase,resource_errors,count);exit(2);}
        ++rejected;printf("ps2-gpu2d: resource rejected phase=%u BadAlloc=1 held=%u; checking GS recovery\n",phase,count);
        while(count)XFreePixmap(display,held[--count]);
        XSync(display,False);
        Pixmap p=XCreatePixmap(display,root,W,H,depth);GC draw=XCreateGC(display,p,0,NULL);
        unsigned long expected[W*H]={0};
        XSetForeground(display,draw,0x4413);XFillRectangle(display,p,draw,3,2,1,1);expected[2*W+3]=0x4413;
        verify(p,W,H,expected,0x7fff,ZPixmap,"GS drawing after BadAlloc",GXcopy);
        XCopyArea(display,p,root,draw,0,0,W,H,0,0);verify(root,W,H,expected,0x7fff,ZPixmap,"display after BadAlloc",GXcopy);
        XSetFunction(display,draw,GXxor);XSetForeground(display,draw,0x2222);XFillRectangle(display,p,draw,3,2,1,1);expected[2*W+3]^=0x2222;
        verify(p,W,H,expected,0x7fff,ZPixmap,"GS XOR after BadAlloc",GXxor);
        XSetFunction(display,draw,GXcopy);XCopyArea(display,p,root,draw,0,0,W,H,0,0);
        verify(root,W,H,expected,0x7fff,ZPixmap,"display XOR after BadAlloc",GXcopy);
        XFreeGC(display,draw);XFreePixmap(display,p);
    }
    if(rejected!=2)exit(2);
    puts("ps2-gpu2d: VRAM and handle exhaustion recover without CPU raster fallback");
}

static void cursor_argb_checks(Window root)
{
    Window window=XCreateSimpleWindow(display,root,40,80,W,H,0,0,0);
    XSetWindowAttributes wa={.override_redirect=True};XChangeWindowAttributes(display,window,CWOverrideRedirect,&wa);
    XMapWindow(display,window);XSync(display,False);
    unsigned long initial[W*H];values(initial,W,H,0x318195);upload(window,W,H,initial);
    Cursor frames[2];
    for(unsigned kind=0;kind<2;++kind){
        XRenderPictFormat *format=XRenderFindStandardFormat(display,kind?PictStandardRGB24:PictStandardARGB32);
        if(!format)exit(2);
        Pixmap pixmap=XCreatePixmap(display,root,W,H,format->depth);GC draw=XCreateGC(display,pixmap,0,NULL);
        unsigned long raw[W*H];for(unsigned i=0;i<W*H;++i){unsigned a=(i*73+29)&255;
            raw[i]=((unsigned long)a<<24)|((((i*17)&255)*a+127)/255)|
                (((((i*31)&255)*a+127)/255)<<8)|(((((i*43)&255)*a+127)/255)<<16);}
        format_upload(pixmap,draw,format,raw);Picture picture=XRenderCreatePicture(display,pixmap,format,0,NULL);
        frames[kind]=XRenderCreateCursor(display,picture,3,2);XDefineCursor(display,window,frames[kind]);
        XRenderFreePicture(display,picture);XFreeGC(display,draw);XFreePixmap(display,pixmap);
        for(unsigned i=0;i<6;++i){XWarpPointer(display,None,window,0,0,0,0,i%3==0?0:i%3==1?7:W-1,i%3==0?0:i%3==1?4:H-1);
            XSync(display,False);verify(window,W,H,initial,bits,ZPixmap,"ARGB cursor save-under and movement",kind);}
    }
    XSetForeground(display,gc,bits&0x13571);XFillRectangle(display,window,gc,0,0,W,H);
    for(unsigned i=0;i<W*H;++i)initial[i]=bits&0x13571;
    verify(window,W,H,initial,bits,ZPixmap,"ARGB cursor under drawing",0);
    Cursor mono=XCreateFontCursor(display,XC_crosshair);XDefineCursor(display,window,mono);XSync(display,False);
    verify(window,W,H,initial,bits,ZPixmap,"ARGB to monochrome cursor",0);XFreeCursor(display,mono);
    Pixmap empty=XCreatePixmap(display,root,W,H,32);GC eg=XCreateGC(display,empty,0,NULL);
    XSetForeground(display,eg,0);XFillRectangle(display,empty,eg,0,0,W,H);
    Picture ep=XRenderCreatePicture(display,empty,XRenderFindStandardFormat(display,PictStandardARGB32),0,NULL);
    Cursor transparent=XRenderCreateCursor(display,ep,0,0);XDefineCursor(display,window,transparent);XSync(display,False);
    verify(window,W,H,initial,bits,ZPixmap,"transparent Render cursor",0);
    XFreeCursor(display,transparent);XRenderFreePicture(display,ep);XFreeGC(display,eg);XFreePixmap(display,empty);
    XUndefineCursor(display,window);XSync(display,False);verify(window,W,H,initial,bits,ZPixmap,"cursor undefine",0);
    XAnimCursor animation[2]={{frames[0],40},{frames[1],40}};Cursor animated=XRenderCreateAnimCursor(display,2,animation);
    XDefineCursor(display,window,animated);XFreeCursor(display,frames[0]);XFreeCursor(display,frames[1]);
    for(unsigned i=0;i<3;++i){XWarpPointer(display,None,window,0,0,0,0,4+i,3+i);XSync(display,False);
        verify(window,W,H,initial,bits,ZPixmap,"animated ARGB cursor",i);}
    XFreeCursor(display,animated);XUndefineCursor(display,window);XSync(display,False);
    verify(window,W,H,initial,bits,ZPixmap,"released ARGB cursor",0);XDestroyWindow(display,window);
    puts("ps2-gpu2d: ARGB, converted, transparent and animated cursors passed");
}

#define GEOM_FIXED(v) ((XFixed)((v)*65536))
static XRenderPictFormat *geometry_format(unsigned precision)
{
    if(precision!=4)return XRenderFindStandardFormat(display,precision==1?PictStandardA1:PictStandardA8);
    XRenderPictFormat match={.type=PictTypeDirect,.depth=4,.direct={.alpha=0,.alphaMask=15}};
    return XRenderFindFormat(display,PictFormatType|PictFormatDepth|PictFormatAlpha|PictFormatAlphaMask,&match,0);
}
static void render_geometry_checks(Window root)
{
    const XTrapezoid traps[]={
        {GEOM_FIXED(-1.25),GEOM_FIXED(10.6875),{{GEOM_FIXED(-2.625),GEOM_FIXED(-2)},{GEOM_FIXED(7.125),GEOM_FIXED(12)}},
            {{GEOM_FIXED(17.875),GEOM_FIXED(-2)},{GEOM_FIXED(12.625),GEOM_FIXED(12)}}},
        {GEOM_FIXED(2.1875),GEOM_FIXED(8.875),{{GEOM_FIXED(6.125),GEOM_FIXED(2.1875)},{GEOM_FIXED(7.875),GEOM_FIXED(8.875)}},
            {{GEOM_FIXED(14.125),GEOM_FIXED(2.1875)},{GEOM_FIXED(12.25),GEOM_FIXED(8.875)}}}
    };
    const XTriangle tris[]={
        {{GEOM_FIXED(-2.375),GEOM_FIXED(2.125)},{GEOM_FIXED(16.875),GEOM_FIXED(-1.625)},{GEOM_FIXED(12.125),GEOM_FIXED(10.875)}},
        {{GEOM_FIXED(13.125),GEOM_FIXED(3.5)},{GEOM_FIXED(4.25),GEOM_FIXED(8.75)},{GEOM_FIXED(17.75),GEOM_FIXED(8.75)}}
    };
    const XPointFixed points[]={{GEOM_FIXED(1.125),GEOM_FIXED(1.375)},{GEOM_FIXED(16.625),GEOM_FIXED(2.125)},
        {GEOM_FIXED(3.875),GEOM_FIXED(9.25)},{GEOM_FIXED(14.375),GEOM_FIXED(8.625)}};
    const XTrap direct[]={
        {.top={.left=GEOM_FIXED(-2.625),.right=GEOM_FIXED(17.875),.y=GEOM_FIXED(-1.25)},
         .bottom={.left=GEOM_FIXED(7.125),.right=GEOM_FIXED(12.625),.y=GEOM_FIXED(10.6875)}},
        {.top={.left=GEOM_FIXED(6.125),.right=GEOM_FIXED(14.125),.y=GEOM_FIXED(2.1875)},
         .bottom={.left=GEOM_FIXED(7.875),.right=GEOM_FIXED(12.25),.y=GEOM_FIXED(8.875)}}};
    XRenderPictFormat *rgba=XRenderFindStandardFormat(display,PictStandardARGB32);if(!rgba)exit(2);
    unsigned long source[W*H],initial[W*H];
    for(unsigned i=0;i<W*H;++i){unsigned a=(i*73+29)&255;
        source[i]=(a<<24)|((i*17%256)*a/255<<16)|((i*37%256)*a/255<<8)|(i*53%256)*a/255;
        a=(i*41+97)&255;initial[i]=(a<<24)|((i*13%256)*a/255<<16)|((i*23%256)*a/255<<8)|(i*47%256)*a/255;}
    unsigned precisions[]={1,4,8},operations[]={PictOpClear,PictOpSrc,PictOpOver,PictOpAdd};
    unsigned skipped=0;
    for(unsigned k=0;k<3;++k){
        unsigned precision=precisions[k];XRenderPictFormat *mf=geometry_format(precision);
        if(!mf){if(getenv("PS2_GS_ORACLE") && precision==4){skipped+=18;continue;}exit(2);}
        /* Direct AddTraps follows storage bpp, including byte-stored A4.
         * Its geometry does not apply the picture's composite clip. */
        Pixmap pixmap=XCreatePixmap(display,root,W,H,mf->depth);GC draw=XCreateGC(display,pixmap,0,NULL);
        Picture picture=XRenderCreatePicture(display,pixmap,mf,0,NULL);unsigned long seeded[W*H];
        for(unsigned i=0;i<W*H;++i)seeded[i]=(i*7+3)&((1U<<precision)-1);
        format_upload(pixmap,draw,mf,seeded);
        void *memory;pixman_image_t *ref=solid_client_bits(seeded,precision==1?PIXMAN_a1:precision==4?PIXMAN_x4a4:PIXMAN_a8,&memory);
        XRectangle clip={4,2,7,5};XRenderSetPictureClipRectangles(display,picture,0,0,&clip,1);
        for(unsigned phase=0;phase<2;++phase){
            pixman_add_traps(ref,-1,1,2,(const pixman_trap_t*)direct);
            XRenderAddTraps(display,picture,-1,1,direct,2);
            unsigned stride=precision==1?4:20;unsigned long expected[W*H];
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
                const unsigned char *row=(const unsigned char*)memory+y*stride;
                expected[y*W+x]=precision==1?(row[x/8]>>(x&7))&1:row[x]&((1U<<precision)-1);
            }
            verify(pixmap,W,H,expected,(1U<<precision)-1,ZPixmap,"direct GS AddTraps",phase);
        }
        pixman_image_unref(ref);free(memory);XRenderFreePicture(display,picture);XFreeGC(display,draw);XFreePixmap(display,pixmap);
        for(unsigned kind=0;kind<4;++kind)for(unsigned operation=0;operation<4;++operation){
            Pixmap pixels[2];GC draws[2];Picture pictures[2];pixman_image_t *refs[2];void *bytes[2];
            for(unsigned i=0;i<2;++i){pixels[i]=XCreatePixmap(display,root,W,H,32);draws[i]=XCreateGC(display,pixels[i],0,NULL);
                format_upload(pixels[i],draws[i],rgba,i?initial:source);pictures[i]=XRenderCreatePicture(display,pixels[i],rgba,0,NULL);
                refs[i]=reference_pixels(i?initial:source,rgba,bytes+i);}
            if(operation==2){XRectangle r={2,1,13,8};XRenderSetPictureClipRectangles(display,pictures[1],0,0,&r,1);
                pixman_region32_t region;pixman_region32_init_rect(&region,r.x,r.y,r.width,r.height);
                pixman_image_set_clip_region32(refs[1],&region);pixman_region32_fini(&region);}
            unsigned op=operations[operation];pixman_format_code_t code=precision==1?PIXMAN_a1:precision==4?PIXMAN_a4:PIXMAN_a8;
            int sx=3,sy=2;
            printf("ps2-gpu2d: Render geometry kind=%u precision=%u op=%u\n",kind,precision,op);
            if(!kind){
                pixman_composite_trapezoids(op,refs[0],refs[1],code,sx-(traps[0].left.p1.x>>16),sy-(traps[0].left.p1.y>>16),0,0,2,(const pixman_trapezoid_t*)traps);
                XRenderCompositeTrapezoids(display,op,pictures[0],pictures[1],mf,sx,sy,traps,2);
            }else{
                pixman_triangle_t native[2];
                if(kind==1)memcpy(native,tris,sizeof(native));
                else for(unsigned i=0;i<2;++i){
                    native[i].p1=*(const pixman_point_fixed_t*)(points+(kind==3?0:i));
                    native[i].p2=*(const pixman_point_fixed_t*)(points+i+1);
                    native[i].p3=*(const pixman_point_fixed_t*)(points+i+2);}
                pixman_composite_triangles(op,refs[0],refs[1],code,sx-(native[0].p1.x>>16),sy-(native[0].p1.y>>16),0,0,2,native);
                if(kind==1)XRenderCompositeTriangles(display,op,pictures[0],pictures[1],mf,sx,sy,tris,2);
                else if(kind==2)XRenderCompositeTriStrip(display,op,pictures[0],pictures[1],mf,sx,sy,points,4);
                else XRenderCompositeTriFan(display,op,pictures[0],pictures[1],mf,sx,sy,points,4);
            }
            format_verify(pixels[1],rgba,bytes[1],"GS Render geometry against pixman",op);
            for(unsigned i=0;i<2;++i){pixman_image_unref(refs[i]);free(bytes[i]);XRenderFreePicture(display,pictures[i]);XFreeGC(display,draws[i]);XFreePixmap(display,pixels[i]);}
        }
    }
    for(unsigned mode=0;mode<10;++mode){
        printf("ps2-gpu2d: Render geometry special mode=%u\n",mode);
        Pixmap pixels[2];GC draws[2];Picture pictures[2];pixman_image_t *refs[2];void *bytes[2];
        for(unsigned i=0;i<2;++i){pixels[i]=XCreatePixmap(display,root,W,H,32);draws[i]=XCreateGC(display,pixels[i],0,NULL);
            format_upload(pixels[i],draws[i],rgba,i?initial:source);pictures[i]=XRenderCreatePicture(display,pixels[i],rgba,0,NULL);
            refs[i]=reference_pixels(i?initial:source,rgba,bytes+i);}
        /* mode4 checks a source alias. mode5 isolates ignored destination
         * sampling state with an independent source; filtered in-place pixman
         * iteration is not the driver's frozen-input oracle. */
        unsigned alias=mode==4,source_id=alias?1:0,op=mode==9?PictOpClear:mode==8?PictOpSrc:PictOpOver;
        Picture mp=None;Pixmap map=None;GC mg=None;pixman_image_t *mi=NULL;void *mb=NULL;
        if(mode==6 || mode==7){
            XRenderPictFormat *af=geometry_format(8);unsigned long alpha[W*H];for(unsigned i=0;i<W*H;++i)alpha[i]=(i*31+57)&255;
            map=XCreatePixmap(display,root,W,H,8);mg=XCreateGC(display,map,0,NULL);format_upload(map,mg,af,alpha);
            mp=XRenderCreatePicture(display,map,af,0,NULL);mi=saturate_reference(alpha,af,&mb);
            unsigned id=mode==6?0:1;XRenderPictureAttributes a={.alpha_map=mp,.alpha_x_origin=1,.alpha_y_origin=-1};
            XRenderChangePicture(display,pictures[id],CPAlphaMap|CPAlphaXOrigin|CPAlphaYOrigin,&a);
            pixman_image_set_alpha_map(refs[id],mi,1,-1);
        }
        if(mode<4){
            unsigned sharp=mode&1;XRenderPictureAttributes a={.poly_edge=sharp?PolyEdgeSharp:PolyEdgeSmooth};
            XRenderChangePicture(display,pictures[1],CPPolyEdge,&a);
            pixman_format_code_t code=sharp?PIXMAN_a1:PIXMAN_a8;
            if(mode<2){
                for(unsigned i=0;i<2;++i)pixman_composite_trapezoids(op,refs[0],refs[1],code,3-(traps[0].left.p1.x>>16),2-(traps[0].left.p1.y>>16),0,0,1,(const pixman_trapezoid_t*)(traps+i));
                XRenderCompositeTrapezoids(display,op,pictures[0],pictures[1],NULL,3,2,traps,2);
            }else{
                for(unsigned i=0;i<2;++i)pixman_composite_triangles(op,refs[0],refs[1],code,3-(tris[0].p1.x>>16),2-(tris[0].p1.y>>16),0,0,1,(const pixman_triangle_t*)(tris+i));
                XRenderCompositeTriangles(display,op,pictures[0],pictures[1],NULL,3,2,tris,2);
            }
        }else{
            XTrapezoid shape=traps[0];if(mode>=8)shape.bottom=shape.top;
            if(mode==5){XTransform t={{{65536,0,-65536},{0,65536,0},{0,0,65536}}};XRenderSetPictureTransform(display,pictures[1],&t);
                pixman_image_set_transform(refs[1],(const pixman_transform_t*)&t);
                XRenderPictureAttributes a={.repeat=RepeatReflect};XRenderChangePicture(display,pictures[1],CPRepeat,&a);
                XRenderSetPictureFilter(display,pictures[1],FilterBilinear,NULL,0);
                pixman_image_set_repeat(refs[1],PIXMAN_REPEAT_REFLECT);pixman_image_set_filter(refs[1],PIXMAN_FILTER_BILINEAR,NULL,0);}
            pixman_composite_trapezoids(op,refs[source_id],refs[1],PIXMAN_a8,3-(shape.left.p1.x>>16),2-(shape.left.p1.y>>16),0,0,1,(const pixman_trapezoid_t*)&shape);
            XRenderCompositeTrapezoids(display,op,pictures[source_id],pictures[1],geometry_format(8),3,2,&shape,1);
        }
        format_verify(pixels[1],rgba,bytes[1],"GS geometry null masks, aliases, alpha maps and empty shapes",mode);
        if(mode==6 || mode==7)map_saturate_verify(map,PIXMAN_a8,mb,"geometry alpha map");
        if(mp){XRenderFreePicture(display,mp);XFreeGC(display,mg);XFreePixmap(display,map);pixman_image_unref(mi);free(mb);}
        for(unsigned i=0;i<2;++i){pixman_image_unref(refs[i]);free(bytes[i]);XRenderFreePicture(display,pictures[i]);XFreeGC(display,draws[i]);XFreePixmap(display,pixels[i]);}
    }
    XRenderPictFormat *rf=XRenderFindVisualFormat(display,DefaultVisual(display,DefaultScreen(display)));
    Picture rp=XRenderCreatePicture(display,root,rf,0,NULL);
    XRenderColor colour={0x4000,0x2000,0x1000,0x8000};Picture solid=XRenderCreateSolidFill(display,&colour);
    unsigned width=DisplayWidth(display,DefaultScreen(display)),height=DisplayHeight(display,DefaultScreen(display));
    unsigned long seed=0x4413&bits;XSetForeground(display,gc,seed);XFillRectangle(display,root,gc,0,0,width,height);
    pixman_color_t pc={colour.red,colour.green,colour.blue,colour.alpha};pixman_image_t *si=pixman_image_create_solid_fill(&pc);
    for(unsigned mode=0;mode<2;++mode){
        unsigned op=mode?PictOpOver:PictOpSrc,l=mode?2:0,t=mode?4:0,r=width-(mode?3:0),b=height-(mode?2:0);
        XTrapezoid shape={(XFixed)(t*65536),(XFixed)(b*65536),
            {{(XFixed)(l*65536),0},{(XFixed)(l*65536),(XFixed)(height*65536)}},
            {{(XFixed)(r*65536),0},{(XFixed)(r*65536),(XFixed)(height*65536)}}};
        printf("ps2-gpu2d: Render geometry display rectangle mode=%u size=%ux%u\n",mode,width,height);
        XRenderCompositeTrapezoids(display,op,solid,rp,geometry_format(8),0,0,&shape,1);
        uint32_t one=seed;pixman_image_t *di=pixman_image_create_bits(reference_format(rf),1,1,&one,4);if(!di || !si)exit(2);
        pixman_image_composite32(op,si,NULL,di,0,0,0,0,0,0,1,1);pixman_image_unref(di);
        for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x){
            unsigned xx=x==0?0:x==1?width/2:width-1,yy=y==0?0:y==1?height/2:height-1;
            XImage *image=XGetImage(display,root,xx,yy,1,1,bits,ZPixmap);if(!image)exit(2);
            unsigned long wanted=mode && (xx<l || xx>=r || yy<t || yy>=b)?seed:one&bits;
            if(XGetPixel(image,0,0)!=wanted){fprintf(stderr,"ps2-gpu2d: display rectangle mismatch mode=%u x=%u y=%u\n",mode,xx,yy);exit(1);}
            XDestroyImage(image);
        }
        seed=one&bits;++checks;
    }
    pixman_image_unref(si);XRenderFreePicture(display,solid);XRenderFreePicture(display,rp);
    if(skipped){skipped_a4_checks+=skipped;printf("ps2-gpu2d: oracle_A4_Render_skipped=%u\n",skipped);}
    puts("ps2-gpu2d: GS trapezoids, triangles, strips, fans and direct traps passed");
}
static void window_sampling_checks(Window root)
{
    unsigned rw=DisplayWidth(display,DefaultScreen(display)),rh=DisplayHeight(display,DefaultScreen(display)),cpp=depth==15?2:4;
    XRenderPictFormat *wf=XRenderFindVisualFormat(display,DefaultVisual(display,DefaultScreen(display))),
        *rgba=XRenderFindStandardFormat(display,PictStandardARGB32);if(!wf || !rgba)exit(2);
    unsigned long background=0x0211&bits;XSetFunction(display,gc,GXcopy);XSetPlaneMask(display,gc,bits);
    XSetFillStyle(display,gc,FillSolid);XSetForeground(display,gc,background);XFillRectangle(display,root,gc,0,0,rw,rh);
    unsigned char *canvas=calloc(rh,rw*cpp);if(!canvas)exit(2);
    for(unsigned i=0;i<rw*rh;++i)memcpy(canvas+i*cpp,&background,cpp);
    Window windows[2];unsigned ox[]={37,89},oy[]={71,73};
    for(unsigned i=0;i<2;++i){
        windows[i]=XCreateSimpleWindow(display,root,ox[i],oy[i],W,H,0,0,0);
        XSetWindowAttributes a={.override_redirect=True};XChangeWindowAttributes(display,windows[i],CWOverrideRedirect,&a);
        XMapWindow(display,windows[i]);XSync(display,False);
        unsigned long values_[W*H];values(values_,W,H,0x193181+i*0x1971);upload(windows[i],W,H,values_);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)memcpy(canvas+((oy[i]+y)*rw+ox[i]+x)*cpp,values_+y*W+x,cpp);
    }
    unsigned long initial[W*H],source_pixels[W*H];
    for(unsigned i=0;i<W*H;++i){unsigned a=(i*41+97)&255;
        initial[i]=(a<<24)|((i*13%256)*a/255<<16)|((i*23%256)*a/255<<8)|(i*47%256)*a/255;
        a=(i*73+29)&255;source_pixels[i]=(a<<24)|((i*17%256)*a/255<<16)|((i*37%256)*a/255<<8)|(i*53%256)*a/255;}
    const char *filters[]={FilterNearest,FilterBilinear,FilterConvolution,FilterFast,FilterGood,FilterBest};
    const pixman_transform_t transforms[2][2]={
        {{{{49152,16384,-32768},{8192,98304,6553},{0,0,65536}}},{{{65536,0,-21845},{0,-65536,9*65536},{0,0,65536}}}},
        {{{{49152,21845,173},{0,65536,-21845},{65536/37,65536/41,65536}}},{{{65536,8192,32768},{16384,65536,0},{-65536/55,65536/43,65536}}}}
    };
    XFixed kernel[]={131072,65536,98303,-32767};
    for(unsigned shape=0;shape<2;++shape)for(unsigned role=0;role<3;++role)
    for(unsigned repeat=0;repeat<4;++repeat)for(unsigned filter=0;filter<6;++filter){
        Pixmap destination=XCreatePixmap(display,root,W,H,32),source=XCreatePixmap(display,root,W,H,32);
        GC dg=XCreateGC(display,destination,0,NULL),sg=XCreateGC(display,source,0,NULL);
        format_upload(destination,dg,rgba,initial);format_upload(source,sg,rgba,source_pixels);
        Picture dst=XRenderCreatePicture(display,destination,rgba,0,NULL),src,mask=None;
        pixman_image_t *ref[3];void *memory[2];
        ref[2]=reference_pixels(initial,rgba,memory+1);
        unsigned source_window=role!=1,masked=role!=0,ca=(shape+repeat+filter)&1;
        for(unsigned slot=0;slot<(masked?2U:1U);++slot){
            unsigned window=slot || source_window,r=slot?(repeat+1)%4:repeat;
            Picture *picture=slot?&mask:&src;
            if(window){
                XRenderPictureAttributes a={.repeat=r,.component_alpha=slot && ca};
                *picture=XRenderCreatePicture(display,windows[slot],wf,CPRepeat|CPComponentAlpha,&a);
                ref[slot]=pixman_image_create_bits(reference_format(wf),rw,rh,(uint32_t*)canvas,rw*cpp);if(!ref[slot])exit(2);
                XTransform t;memcpy(t.matrix,transforms[shape][slot].matrix,sizeof(t.matrix));
                XRenderSetPictureTransform(display,*picture,&t);
                pixman_transform_t adjusted=transforms[shape][slot];
                if(!pixman_transform_translate(&adjusted,NULL,ox[slot]*65536,oy[slot]*65536) ||
                    !pixman_image_set_transform(ref[slot],&adjusted))exit(2);
                XRenderSetPictureFilter(display,*picture,filters[filter],filter==2?kernel:NULL,filter==2?4:0);
                if(!pixman_image_set_filter(ref[slot],filter==2?PIXMAN_FILTER_CONVOLUTION:
                    filter==1 || filter>=4?PIXMAN_FILTER_BILINEAR:PIXMAN_FILTER_NEAREST,filter==2?kernel:NULL,filter==2?4:0))exit(2);
                pixman_image_set_repeat(ref[slot],r);pixman_image_set_component_alpha(ref[slot],slot && ca);
            }else{
                *picture=XRenderCreatePicture(display,source,rgba,0,NULL);
                ref[slot]=reference_pixels(source_pixels,rgba,memory);
            }
        }
        int sx=repeat==3?0:2,sy=1,mx=repeat==3?0:3,my=2,dx=3,dy=2;
        pixman_image_composite32(PIXMAN_OP_OVER,ref[0],masked?ref[1]:NULL,ref[2],sx,sy,mx,my,dx,dy,1,1);
        printf("ps2-gpu2d: Render window-sampling transform=%u role=%u repeat=%u filter=%s ca=%u\n",shape,role,repeat,filters[filter],ca);
        XRenderComposite(display,PictOpOver,src,mask,dst,sx,sy,mx,my,dx,dy,1,1);
        format_verify(destination,rgba,memory[1],"window sampling against backing-image pixman",filter);
        pixman_image_unref(ref[0]);pixman_image_unref(ref[2]);if(masked)pixman_image_unref(ref[1]);
        if(!source_window)free(memory[0]);
        free(memory[1]);XRenderFreePicture(display,src);if(masked)XRenderFreePicture(display,mask);XRenderFreePicture(display,dst);
        XFreeGC(display,dg);XFreeGC(display,sg);XFreePixmap(display,destination);XFreePixmap(display,source);
    }
    XDestroyWindow(display,windows[0]);XDestroyWindow(display,windows[1]);free(canvas);
    puts("ps2-gpu2d: affine/projective Window sources and masks, repeat modes and six filters passed");
}
static unsigned glyph_format_count;
static void render_glyph_format_checks(Window root,Pixmap dest,const unsigned long *initial,XRenderPictFormat *format)
{
    (void)root;
    XRenderPictFormat any={0},*af;unsigned layout=0,enumerated=0;
    while((af=XRenderFindFormat(display,0,&any,enumerated++))){
        if(af->depth!=1 && af->depth!=4 && af->depth!=8 && af->depth!=16 && af->depth!=32)continue;
        pixman_format_code_t code=public_format_code(af);unsigned bpp=PIXMAN_FORMAT_BPP(code);
        /* Packed10 blending is independently qualified by the public-format and
         * operator matrices. Keep this protocol/glyph integration probe small. */
        unsigned gw=PIXMAN_FORMAT_R(code)==10?2:7,gh=PIXMAN_FORMAT_R(code)==10?1:5;
        unsigned stride=((gw*bpp+31)/32)*4,component=af->direct.redMask!=0 && af->direct.alphaMask!=0;
        XImage *image=XCreateImage(display,DefaultVisual(display,DefaultScreen(display)),af->depth,ZPixmap,
            0,NULL,gw,gh,32,stride);if(!image || (unsigned)image->bits_per_pixel!=bpp)exit(2);
        image->data=calloc(gh,stride);if(!image->data)exit(2);
        for(unsigned y=0;y<gh;++y)for(unsigned x=0;x<gw;++x){
            unsigned a=((y*gw+x)*73+(PIXMAN_FORMAT_R(code)==10?91:29))&255,am=(unsigned short)af->direct.alphaMask;
            unsigned aq=(a*am+127)/255,level=am?aq*255/am:a;
            unsigned long value=(unsigned long)aq<<af->direct.alpha;
            value|=(unsigned long)(((level*73/100)*(unsigned short)af->direct.redMask+127)/255)<<af->direct.red;
            value|=(unsigned long)(((level*31/100)*(unsigned short)af->direct.greenMask+127)/255)<<af->direct.green;
            value|=(unsigned long)(((level*51/100)*(unsigned short)af->direct.blueMask+127)/255)<<af->direct.blue;
            XPutPixel(image,x,y,value);
        }
        printf("ps2-gpu2d: glyph-advertised layout=%u depth=%d code=%08x size=%ux%u\n",layout,af->depth,code,gw,gh);
        GlyphSet set=XRenderCreateGlyphSet(display,af);Glyph glyph=65;
        Picture pd=XRenderCreatePicture(display,dest,format,0,NULL);
        for(unsigned overlap=0;overlap<2;++overlap)for(unsigned opaque=0;opaque<2;++opaque)for(unsigned aggregate=0;aggregate<2;++aggregate){
            unsigned advance=overlap?(gw==2?1:4):gw;XGlyphInfo info={.width=gw,.height=gh,.xOff=advance};
            XRenderAddGlyphs(display,set,&glyph,&info,1,image->data,stride*gh);
            unsigned a=opaque?255:129;XRenderColor colour={.red=(a*73/100)*257,.green=(a*31/100)*257,.blue=(a*51/100)*257,.alpha=a*257};
            Picture ps=XRenderCreateSolidFill(display,&colour);upload(dest,W,H,initial);
            pixman_color_t c={.red=colour.red,.green=colour.green,.blue=colour.blue,.alpha=colour.alpha};
            pixman_image_t *solid=pixman_image_create_solid_fill(&c),*glyph_image=pixman_image_create_bits(code,gw,gh,(uint32_t*)image->data,stride),*mask=glyph_image;
            void *bytes;pixman_image_t *reference=reference_pixels(initial,format,&bytes);
            if(!solid || !glyph_image || !reference)exit(2);
            if(aggregate){
                mask=pixman_image_create_bits(code,advance+gw,gh,NULL,0);if(!mask)exit(2);
                for(unsigned i=0;i<2;++i)pixman_image_composite32(PIXMAN_OP_ADD,glyph_image,NULL,mask,0,0,0,0,i*advance,0,gw,gh);
            }
            pixman_image_set_component_alpha(mask,component);
            for(unsigned i=0;i<(aggregate?1U:2U);++i)
                pixman_image_composite32(PIXMAN_OP_OVER,solid,mask,reference,0,0,0,0,2+i*advance,3,aggregate?advance+gw:gw,gh);
            printf("ps2-gpu2d: Render glyph-format layout=%u depth=%d bpp=%u opaque=%u aggregate=%u overlap=%u\n",layout,af->depth,bpp,opaque,aggregate,overlap);
            XRenderCompositeString8(display,PictOpOver,ps,pd,aggregate?af:NULL,set,0,0,2,3,"AA",2);
            format_verify(dest,format,bytes,"Render glyph formats against pixman",layout);
            if(aggregate)pixman_image_unref(mask);
            pixman_image_unref(glyph_image);pixman_image_unref(solid);pixman_image_unref(reference);free(bytes);XRenderFreePicture(display,ps);
        }
        XRenderFreePicture(display,pd);XRenderFreeGlyphSet(display,set);XDestroyImage(image);++layout;
    }
    glyph_format_count=layout;
    if(getenv("PS2_GS_ORACLE"))printf("ps2-gpu2d: oracle_glyph_formats=%u native_expected=28\n",layout);
}

int main(void)
{
    setbuf(stdout,NULL);display=XOpenDisplay(NULL);if(!display)return 2;XSetErrorHandler(xerror);
    int screen=DefaultScreen(display);Window root=RootWindow(display,screen);depth=DefaultDepth(display,screen);
    if(depth!=15 && depth!=24)return 2;
    bits=(1UL<<depth)-1;
    Pixmap source=XCreatePixmap(display,root,W,H,depth),dest=XCreatePixmap(display,root,W,H,depth);
    gc=XCreateGC(display,dest,0,NULL);XSetGraphicsExposures(display,gc,False);
    const char *selected=getenv("PS2_GS_CASE");
    if(selected){
        if(!strcmp(selected,"render-glyphs")){
            unsigned long initial[W*H];values(initial,W,H,0x136731);
            XRenderPictFormat *format=XRenderFindVisualFormat(display,DefaultVisual(display,screen));if(!format)return 2;
            render_glyph_format_checks(root,dest,initial,format);XSync(display,False);
            if(skipped_a4_checks)printf("ps2-gpu2d: oracle_A4_Render_skipped=%u\n",skipped_a4_checks);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);XCloseDisplay(display);
            return errors || checks!=(getenv("PS2_GS_ORACLE")?glyph_format_count*8:PS2_GPU2D_RENDER_GLYPH_CHECKS)?1:0;
        }

        if(!strcmp(selected,"window-sampling")){
            window_sampling_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_WINDOW_SAMPLE_CHECKS?1:0;
        }
        if(!strcmp(selected,"render-geometry")){
            render_geometry_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_RENDER_GEOMETRY_CHECKS-skipped_a4_checks?1:0;
        }
        if(!strcmp(selected,"cursor-argb")){
            cursor_argb_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_CURSOR_ARGB_CHECKS?1:0;
        }
        if(!strcmp(selected,"resources")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            resource_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_RESOURCE_CHECKS?1:0;
        }

        if(!strcmp(selected,"front-formats")){
            shard_count=shard_number("PS2_GS_SHARD_COUNT",1);shard_index=shard_number("PS2_GS_SHARD_INDEX",0);
            if(!shard_count || shard_index>=shard_count)return 2;
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            front_format_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            unsigned requests=PS2_GPU2D_FRONT_FORMAT_CHECKS,expected=(requests+shard_count-1-shard_index)/shard_count;
            XCloseDisplay(display);return errors || checks!=expected || (shard_count>1 && shard_requests!=requests)?1:0;
        }
        if(!strcmp(selected,"formats-filters")){
            shard_count=shard_number("PS2_GS_SHARD_COUNT",1);shard_index=shard_number("PS2_GS_SHARD_INDEX",0);
            if(!shard_count || shard_index>=shard_count)return 2;
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            public_format_filter_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            unsigned requests=PS2_GPU2D_FORMAT_FILTER_CHECKS,expected=(requests+shard_count-1-shard_index)/shard_count;
            XCloseDisplay(display);return errors || checks!=expected || (shard_count>1 && shard_requests!=requests)?1:0;
        }

        if(!strcmp(selected,"depth30")){
            shard_count=shard_number("PS2_GS_SHARD_COUNT",1);shard_index=shard_number("PS2_GS_SHARD_INDEX",0);
            if(!shard_count || shard_index>=shard_count)return 2;
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            depth30_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            unsigned requests=PS2_GPU2D_DEPTH30_CHECKS/2,expected=2*((requests+shard_count-1-shard_index)/shard_count);
            XCloseDisplay(display);return errors || checks!=expected || (shard_count>1 && shard_requests!=requests)?1:0;
        }

        if(!strcmp(selected,"packed10-mixed")){
            shard_count=shard_number("PS2_GS_SHARD_COUNT",1);shard_index=shard_number("PS2_GS_SHARD_INDEX",0);
            if(!shard_count || shard_index>=shard_count)return 2;
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            packed10_mixed_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            unsigned requests=PS2_GPU2D_PACKED10_MIXED_CHECKS/2,expected=2*((requests+shard_count-1-shard_index)/shard_count);
            XCloseDisplay(display);return errors || checks!=expected || (shard_count>1 && shard_requests!=requests)?1:0;
        }
        if(!strcmp(selected,"packed10-alpha-write")){
            shard_count=shard_number("PS2_GS_SHARD_COUNT",1);shard_index=shard_number("PS2_GS_SHARD_INDEX",0);
            if(!shard_count || shard_index>=shard_count)return 2;
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            packed10_alpha_write_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            unsigned requests=PS2_GPU2D_PACKED10_ALPHA_WRITE_CHECKS/2,expected=2*((requests+shard_count-1-shard_index)/shard_count);
            XCloseDisplay(display);return errors || checks!=expected || (shard_count>1 && shard_requests!=requests)?1:0;
        }

        if(!strcmp(selected,"packed10-alpha-read")){
            shard_count=shard_number("PS2_GS_SHARD_COUNT",1);shard_index=shard_number("PS2_GS_SHARD_INDEX",0);
            if(!shard_count || shard_index>=shard_count)return 2;
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            packed10_alpha_read_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            unsigned expected=(PS2_GPU2D_PACKED10_ALPHA_READ_CHECKS+shard_count-1-shard_index)/shard_count;
            XCloseDisplay(display);return errors || checks!=expected || (shard_count>1 && shard_requests!=PS2_GPU2D_PACKED10_ALPHA_READ_CHECKS)?1:0;
        }
        if(!strcmp(selected,"packed10-convolution")){
            shard_count=shard_number("PS2_GS_SHARD_COUNT",1);shard_index=shard_number("PS2_GS_SHARD_INDEX",0);
            if(!shard_count || shard_index>=shard_count)return 2;
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            packed10_convolution_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            unsigned expected=(PS2_GPU2D_PACKED10_CONVOLUTION_CHECKS+shard_count-1-shard_index)/shard_count;
            XCloseDisplay(display);return errors || checks!=expected ||
                (shard_count>1 && shard_requests!=PS2_GPU2D_PACKED10_CONVOLUTION_CHECKS)?1:0;
        }
        if(!strcmp(selected,"packed10-ops")){
            shard_count=shard_number("PS2_GS_SHARD_COUNT",1);shard_index=shard_number("PS2_GS_SHARD_INDEX",0);
            if(!shard_count || shard_index>=shard_count)return 2;
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            packed10_checks(root,1);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            unsigned expected=(PS2_GPU2D_PACKED10_OPS_CHECKS+shard_count-1-shard_index)/shard_count;
            XCloseDisplay(display);return errors || checks!=expected ||
                (shard_count>1 && shard_requests!=PS2_GPU2D_PACKED10_OPS_CHECKS)?1:0;
        }
        if(!strcmp(selected,"packed10")){
            shard_count=shard_number("PS2_GS_SHARD_COUNT",1);shard_index=shard_number("PS2_GS_SHARD_INDEX",0);
            if(!shard_count || shard_index>=shard_count)return 2;
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            packed10_checks(root,0);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            unsigned expected=(PS2_GPU2D_PACKED10_CHECKS+shard_count-1-shard_index)/shard_count;
            XCloseDisplay(display);return errors || checks!=expected ||
                (shard_count>1 && shard_requests!=PS2_GPU2D_PACKED10_CHECKS)?1:0;
        }
        if(!strcmp(selected,"gradients")){
            shard_count=shard_number("PS2_GS_SHARD_COUNT",1);shard_index=shard_number("PS2_GS_SHARD_INDEX",0);
            if(!shard_count || shard_index>=shard_count)return 2;
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            gradient_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            unsigned expected=(PS2_GPU2D_GRADIENT_CHECKS+shard_count-1-shard_index)/shard_count;
            XCloseDisplay(display);return errors || checks!=expected ||
                (shard_count>1 && shard_requests!=PS2_GPU2D_GRADIENT_CHECKS)?1:0;
        }
        if(!strcmp(selected,"hsl")){
            shard_count=shard_number("PS2_GS_SHARD_COUNT",1);shard_index=shard_number("PS2_GS_SHARD_INDEX",0);
            if(!shard_count || shard_index>=shard_count)return 2;
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            hsl_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            unsigned expected=(PS2_GPU2D_HSL_CHECKS+shard_count-1-shard_index)/shard_count;
            XCloseDisplay(display);return errors || checks!=expected ||
                (shard_count>1 && shard_requests!=PS2_GPU2D_HSL_CHECKS)?1:0;
        }
        if(!strcmp(selected,"softlight")){
            shard_count=shard_number("PS2_GS_SHARD_COUNT",1);shard_index=shard_number("PS2_GS_SHARD_INDEX",0);
            if(!shard_count || shard_index>=shard_count)return 2;
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            softlight_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            unsigned expected=(PS2_GPU2D_SOFTLIGHT_CHECKS+shard_count-1-shard_index)/shard_count;
            XCloseDisplay(display);return errors || checks!=expected ||
                (shard_count>1 && shard_requests!=PS2_GPU2D_SOFTLIGHT_CHECKS)?1:0;
        }
        if(!strcmp(selected,"dodge-burn")){
            shard_count=shard_number("PS2_GS_SHARD_COUNT",1);shard_index=shard_number("PS2_GS_SHARD_INDEX",0);
            if(!shard_count || shard_index>=shard_count)return 2;
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            dodge_burn_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            unsigned expected=(PS2_GPU2D_DODGE_BURN_CHECKS+shard_count-1-shard_index)/shard_count;
            XCloseDisplay(display);return errors || checks!=expected ||
                (shard_count>1 && shard_requests!=PS2_GPU2D_DODGE_BURN_CHECKS)?1:0;
        }
        if(!strcmp(selected,"pd-ratios")){
            shard_count=shard_number("PS2_GS_SHARD_COUNT",1);shard_index=shard_number("PS2_GS_SHARD_INDEX",0);
            if(!shard_count || shard_index>=shard_count)return 2;
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            pd_ratio_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            unsigned expected=(PS2_GPU2D_PD_RATIOS_CHECKS+shard_count-1-shard_index)/shard_count;
            XCloseDisplay(display);return errors || checks!=expected ||
                (shard_count>1 && shard_requests!=PS2_GPU2D_PD_RATIOS_CHECKS)?1:0;
        }
        if(!strcmp(selected,"pd-families")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            pd_opaque_rounding_checks(root);
            blend_checks(root,PictOpDisjointClear);blend_checks(root,PictOpDisjointSrc);blend_checks(root,PictOpDisjointDst);blend_checks(root,PictOpConjointClear);blend_checks(root,PictOpConjointSrc);blend_checks(root,PictOpConjointDst);blend_checks(root,PictOpDisjointOverReverse);solid_disjoint_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_PD_FAMILIES_CHECKS?1:0;
        }
        if(!strcmp(selected,"pd-aliases")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            blend_checks(root,PictOpDisjointClear);blend_checks(root,PictOpDisjointSrc);blend_checks(root,PictOpDisjointDst);blend_checks(root,PictOpConjointClear);blend_checks(root,PictOpConjointSrc);blend_checks(root,PictOpConjointDst);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_PD_ALIASES_CHECKS?1:0;
        }
        if(!strcmp(selected,"disjoint-over")){
            shard_count=shard_number("PS2_GS_SHARD_COUNT",1);
            shard_index=shard_number("PS2_GS_SHARD_INDEX",0);
            if(!shard_count || shard_index>=shard_count){fprintf(stderr,"ps2-gpu2d: invalid shard range\n");return 2;}
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            pd_opaque_rounding_checks(root);
            blend_checks(root,PictOpDisjointOverReverse);solid_disjoint_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            unsigned expected=(PS2_GPU2D_DISJOINT_OVER_CHECKS+shard_count-1-shard_index)/shard_count;
            XCloseDisplay(display);return errors || checks!=expected ||
                (shard_count>1 && shard_requests!=PS2_GPU2D_DISJOINT_OVER_CHECKS)?1:0;
        }
        if(!strcmp(selected,"piecewise")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            blend_checks(root,PictOpOverlay);blend_checks(root,PictOpHardLight);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_PIECEWISE_CHECKS?1:0;
        }
        if(!strcmp(selected,"order-blends")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            blend_checks(root,PictOpDarken);blend_checks(root,PictOpLighten);blend_checks(root,PictOpDifference);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_ORDER_BLENDS_CHECKS?1:0;
        }
        if(!strcmp(selected,"multiply")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            blend_checks(root,PictOpMultiply);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_MULTIPLY_CHECKS?1:0;
        }
        if(!strcmp(selected,"exclusion")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            blend_checks(root,PictOpExclusion);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_EXCLUSION_CHECKS?1:0;
        }
        if(!strcmp(selected,"screen")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            blend_checks(root,PictOpScreen);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_SCREEN_CHECKS?1:0;
        }
        if(!strcmp(selected,"native15-aliases")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            native15_alias_checks(root,0);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_NATIVE15_ALIAS_CHECKS?1:0;
        }
        if(!strcmp(selected,"format-aliases")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            format_alias_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_FORMAT_ALIAS_CHECKS?1:0;
        }
        if(!strcmp(selected,"convolution-saturate")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            shifted_map_checks(root,1);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_CONVOLUTION_SATURATE_CHECKS?1:0;
        }
        if(!strcmp(selected,"shifted-maps")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            shifted_map_checks(root,0);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_SHIFTED_MAP_CHECKS?1:0;
        }
        if(!strcmp(selected,"map-saturate")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            map_saturate_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_MAP_SATURATE_CHECKS?1:0;
        }
        if(!strcmp(selected,"packed-saturate")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            byte_colour_checks(root,0,1);byte_colour_checks(root,1,1);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_PACKED_SATURATE_CHECKS-skipped_byte_checks?1:0;
        }
        if(!strcmp(selected,"saturate")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            saturate_checks(root,0);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_SATURATE_CHECKS?1:0;
        }
        if(!strcmp(selected,"solid-saturate")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            solid_saturate_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_SOLID_SATURATE_CHECKS?1:0;
        }
        if(!strcmp(selected,"coverage-saturate")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            coverage_saturate_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_COVERAGE_SATURATE_CHECKS?1:0;
        }
        if(!strcmp(selected,"bilinear-saturate")){
            XFreeGC(display,gc);XFreePixmap(display,source);XFreePixmap(display,dest);
            saturate_checks(root,1);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_BILINEAR_SATURATE_CHECKS?1:0;
        }
        if(!strcmp(selected,"alpha-aliases")){
            alpha_raw_alias_checks(root);XSync(display,False);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_ALPHA_ALIAS_CHECKS?1:0;
        }
        if(!strcmp(selected,"alpha-windows")){
            alpha_window_checks(root);XSync(display,False);
            printf("ps2-gpu2d: alpha window checks passed depth=%u\n",depth);
            if(skipped_a4_checks)printf("ps2-gpu2d: oracle_A4_Render_skipped=%u\n",skipped_a4_checks);
            printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
            XCloseDisplay(display);return errors || checks!=PS2_GPU2D_ALPHA_WINDOW_CHECKS-skipped_a4_checks?1:0;
        }
        if(strcmp(selected,"alpha-maps")){fprintf(stderr,"ps2-gpu2d: unknown test case\n");return 2;}
        alpha_map_checks(root);XSync(display,False);
        printf("ps2-gpu2d: alpha map checks passed depth=%u\n",depth);
        if(skipped_a4_checks)printf("ps2-gpu2d: oracle_A4_Render_skipped=%u\n",skipped_a4_checks);
        printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);
        XCloseDisplay(display);return errors || checks!=PS2_GPU2D_ALPHA_MAP_CHECKS-skipped_a4_checks?1:0;
    }
    unsigned long s[W*H],initial[W*H],expected[W*H];values(s,W,H,0x318181);values(initial,W,H,0x810341);
    upload(source,W,H,s);verify(source,W,H,s,bits,ZPixmap,"native readback",3);
    verify(source,W,H,s,bits&0x514985,ZPixmap,"masked native readback",3);
    verify(source,W,H,s,1,XYPixmap,"XY plane reply",3);
    printf("ps2-gpu2d: native readback and client reply checks passed depth=%u\n",depth);
    for(unsigned full=0;full<2;++full){unsigned long mask=full?bits:bits&0x51a135;
        for(unsigned alu=0;alu<16;++alu){upload(dest,W,H,initial);memcpy(expected,initial,sizeof(expected));
            XSetFunction(display,gc,alu);XSetPlaneMask(display,gc,mask);
            XCopyArea(display,source,dest,gc,2,1,13,7,3,2);
            for(unsigned y=0;y<7;++y)for(unsigned x=0;x<13;++x){unsigned i=(y+2)*W+x+3;expected[i]=(expected[i]&~mask)|(op(alu,s[(y+1)*W+x+2],expected[i])&mask);}
            verify(dest,W,H,expected,bits,ZPixmap,"Boolean copy",alu);}}
    printf("ps2-gpu2d: all 16 CopyArea ALUs and full/partial masks passed\n");
    for(unsigned direction=0;direction<4;++direction){unsigned sx=direction&1?3:0,dx=direction&1?0:3,sy=direction&2?2:0,dy=direction&2?0:2;
        upload(dest,W,H,initial);memcpy(expected,initial,sizeof(expected));XSetFunction(display,gc,GXxor);XSetPlaneMask(display,gc,bits);
        XCopyArea(display,dest,dest,gc,sx,sy,15,8,dx,dy);
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<15;++x)expected[(dy+y)*W+dx+x]=initial[(dy+y)*W+dx+x]^initial[(sy+y)*W+sx+x];
        verify(dest,W,H,expected,bits,ZPixmap,"overlapping XOR copy",GXxor);}
    printf("ps2-gpu2d: all four overlapping copy directions passed\n");
    unsigned long tile_pixels[9*5];values(tile_pixels,9,5,0x815131);
    Pixmap tile=XCreatePixmap(display,root,9,5,depth);upload(tile,9,5,tile_pixels);XSetTile(display,gc,tile);XSetTSOrigin(display,gc,-3,2);
    for(unsigned full=0;full<2;++full){unsigned long mask=full?bits:bits&0x518135;
        for(unsigned alu=0;alu<16;++alu){upload(dest,W,H,initial);memcpy(expected,initial,sizeof(expected));
            XSetFillStyle(display,gc,FillTiled);XSetFunction(display,gc,alu);XSetPlaneMask(display,gc,mask);
            XFillRectangle(display,dest,gc,1,1,17,9);
            for(unsigned y=1;y<10;++y)for(unsigned x=1;x<18;++x){unsigned i=y*W+x;unsigned long v=tile_pixels[modulo((int)y-2,5)*9+modulo((int)x+3,9)];expected[i]=(expected[i]&~mask)|(op(alu,v,expected[i])&mask);}
            verify(dest,W,H,expected,bits,ZPixmap,"tiled Boolean rectangle",alu);}}
    printf("ps2-gpu2d: all 16 tiled rectangle ALUs and full/partial masks passed\n");
    XPoint polygon[]={{1,1},{15,1},{15,4},{7,4},{7,9},{1,9}};
    for(unsigned alu=0;alu<16;++alu){unsigned long mask=bits&0x51a153;
        upload(dest,W,H,initial);memcpy(expected,initial,sizeof(expected));XSetFillStyle(display,gc,FillTiled);XSetFunction(display,gc,alu);XSetPlaneMask(display,gc,mask);
        XFillPolygon(display,dest,gc,polygon,6,Nonconvex,CoordModeOrigin);
        for(unsigned y=1;y<9;++y)for(unsigned x=1;x<(y<4?15U:7U);++x){unsigned i=y*W+x;unsigned long v=tile_pixels[modulo((int)y-2,5)*9+modulo((int)x+3,9)];expected[i]=(expected[i]&~mask)|(op(alu,v,expected[i])&mask);}
        verify(dest,W,H,expected,bits,ZPixmap,"tiled concave polygon",alu);}
    printf("ps2-gpu2d: all 16 tiled concave polygon ALUs passed\n");
    /* Reuse the GC after tiled MI geometry: changing foreground/fill style
     * must reach the native solid path, including after a filled ellipse. */
    for(unsigned full=0;full<2;++full){unsigned long mask=full?bits:bits&0x519153;
        for(unsigned alu=0;alu<16;++alu){upload(dest,W,H,initial);memcpy(expected,initial,sizeof(expected));
            XSetForeground(display,gc,bits&0x713185);XSetFillStyle(display,gc,FillSolid);
            XSetFunction(display,gc,alu);XSetPlaneMask(display,gc,mask);XFillRectangle(display,dest,gc,1,1,17,9);
            for(unsigned y=1;y<10;++y)for(unsigned x=1;x<18;++x){unsigned i=y*W+x;expected[i]=(expected[i]&~mask)|(op(alu,bits&0x713185,expected[i])&mask);}
            verify(dest,W,H,expected,bits,ZPixmap,"solid fill after tiled geometry",alu);}}
    XSetFunction(display,gc,GXcopy);XSetPlaneMask(display,gc,bits);XSetFillStyle(display,gc,FillTiled);
    XFillArc(display,dest,gc,1,1,16,8,0,360*64);XSetFillStyle(display,gc,FillSolid);
    XSetForeground(display,gc,bits&0x031845);XFillRectangle(display,dest,gc,0,0,W,H);
    for(unsigned i=0;i<W*H;++i)expected[i]=bits&0x031845;
    verify(dest,W,H,expected,bits,ZPixmap,"solid clear after tiled ellipse",GXcopy);
    printf("ps2-gpu2d: solid fill ALUs and tiled-to-solid GC transitions passed\n");
    XRenderPictFormat *format=XRenderFindVisualFormat(display,DefaultVisual(display,screen));if(!format)return 2;
    Picture ps=XRenderCreatePicture(display,source,format,0,NULL),pd=XRenderCreatePicture(display,dest,format,0,NULL);
    upload(dest,W,H,initial);memcpy(expected,initial,sizeof(expected));
    XRenderComposite(display,PictOpSrc,ps,None,pd,2,1,0,0,3,2,13,7);
    for(unsigned y=0;y<7;++y)for(unsigned x=0;x<13;++x)expected[(y+2)*W+x+3]=s[(y+1)*W+x+2];
    verify(dest,W,H,expected,bits,ZPixmap,"opaque XRender source copy",3);
    XRenderFreePicture(display,ps);XRenderFreePicture(display,pd);
    printf("ps2-gpu2d: opaque XRender source copy passed\n");
    plane_checks(root,source,dest,s,initial);
    render_checks(root,source,dest,s,initial,format);
    filter_render_checks(root,source,dest,s,initial,format);
    format_render_checks(root);
    rgba_checks(root);
    alpha_map_checks(root);
    byte_colour_checks(root,0,0);
    byte_colour_checks(root,1,0);
    coverage_core_checks(root);
    solid_render_checks(root,dest,initial,format);
    alpha_glyph_checks(dest,initial,format);
    Window window=XCreateSimpleWindow(display,root,40,80,W,H,0,0,0);XSetWindowAttributes attr={.override_redirect=True};
    XChangeWindowAttributes(display,window,CWOverrideRedirect,&attr);XMapWindow(display,window);XSync(display,False);
    XSetFillStyle(display,gc,FillSolid);XSetFunction(display,gc,GXcopy);XSetPlaneMask(display,gc,bits);XCopyArea(display,source,window,gc,0,0,W,H,0,0);
    XWarpPointer(display,None,window,0,0,0,0,7,4);XSync(display,False);
    verify(window,W,H,s,bits,ZPixmap,"front readback beneath cursor",3);
    for(unsigned alu=0;alu<16;++alu){upload(window,W,H,initial);memcpy(expected,initial,sizeof(expected));
        XSetForeground(display,gc,bits&0x713185);XSetFillStyle(display,gc,FillSolid);
        XSetFunction(display,gc,alu);XSetPlaneMask(display,gc,bits);XFillRectangle(display,window,gc,1,1,17,9);
        for(unsigned y=1;y<10;++y)for(unsigned x=1;x<18;++x){unsigned i=y*W+x;expected[i]=op(alu,bits&0x713185,expected[i])&bits;}
        verify(window,W,H,expected,bits,ZPixmap,"visible solid fill",alu);}
    printf("ps2-gpu2d: visible window solid fill ALUs passed\n");
    font_checks(root);
    XDestroyWindow(display,window);XFreePixmap(display,tile);XFreePixmap(display,dest);XFreePixmap(display,source);XFreeGC(display,gc);
    XSync(display,False);
    if(skipped_a4_checks)printf("ps2-gpu2d: oracle_A4_Render_skipped=%u\n",skipped_a4_checks);
    printf("ps2-gpu2d: completed pixel_checks=%u errors=%u PS2_GS_required=1\n",checks,errors);XCloseDisplay(display);return errors || checks!=PS2_GPU2D_PIXEL_CHECKS-skipped_a4_checks-skipped_byte_checks?1:0;
}
