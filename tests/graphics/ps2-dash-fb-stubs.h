/* SPDX-License-Identifier: MIT */
#ifndef PS2_DASH_FB_STUBS_H
#define PS2_DASH_FB_STUBS_H
static uint32_t boolean(unsigned,uint32_t,uint32_t);
static DevPrivateKeyRec fb_screen_key={.offset=14*sizeof(void*),.initialized=TRUE,.type=PRIVATE_SCREEN};
DevPrivateKey fbGetScreenPrivateKey(void){return &fb_screen_key;}
#define FB_STUB(name) void name(DrawablePtr d,GCPtr g,int off,int sx,int sy,int axis,int x,int y,int e,int e1,int e3,int len) \
{(void)d;(void)g;(void)off;(void)sx;(void)sy;(void)axis;(void)x;(void)y;(void)e;(void)e1;(void)e3;(void)len;assert(0);}
FB_STUB(fbBresSolid8) FB_STUB(fbBresSolid16) FB_STUB(fbBresSolid32)
FB_STUB(fbBresDash8) FB_STUB(fbBresDash16) FB_STUB(fbBresDash32)
void fbFill(DrawablePtr d,GCPtr g,int x,int y,int w,int h)
{(void)d;(void)g;(void)x;(void)y;(void)w;(void)h;assert(0);}
int ChangeGC(ClientPtr c,GCPtr g,BITS32 mask,ChangeGCValPtr v)
{(void)c;(void)g;(void)mask;(void)v;assert(0);return 0;}
void ValidateGC(DrawablePtr d,GCPtr g){(void)d;(void)g;assert(0);}
static FbBits expanded(uint32_t pixel,unsigned bpp)
{ return bpp==16 ? (pixel&0xffff)|((pixel&0xffff)<<16) : pixel; }
static void reduced(unsigned alu,uint32_t colour,uint32_t mask,unsigned bpp,FbBits *a,FbBits *x)
{
    uint32_t f0=boolean(alu,colour,0),f1=boolean(alu,colour,~0U);
    *a=expanded((f0^f1)|~mask,bpp);*x=expanded(f0&mask,bpp);
}
#endif
