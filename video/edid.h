/* SPDX-License-Identifier: GPL-2.0-only */
/* Conservative EDID base/CTA decoding. Unknown timings are never invented. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int edid_valid(const uint8_t *data, size_t size)
{
    static const uint8_t magic[8]={0,255,255,255,255,255,255,0};
    if(size<128 || memcmp(data,magic,8) || data[18]!=1) return 0;
    size_t expected=((size_t)data[126]+1)*128;
    if(expected>1024 || size!=expected) return 0;
    for(size_t at=0;at<size;at+=128) {
        unsigned sum=0;
        for(size_t i=0;i<128;i++) sum+=data[at+i];
        if(sum&255) return 0;
        if(at && data[at]==2 && data[at+1]>=3 && data[at+2]) {
            unsigned end=data[at+2];
            if(end<4 || end>127) return 0;
            for(unsigned i=4;i<end;) {
                unsigned length=data[at+i]&31;i++;
                if(i+length>end) return 0;
                i+=length;
            }
        }
    }
    return 1;
}

static int detailed_matches(const uint8_t *p, unsigned width, unsigned height,
                            unsigned hz, int interlaced)
{
    unsigned clock=(p[0]|(p[1]<<8))*10000;
    unsigned w=p[2]|((p[4]&240)<<4), hb=p[3]|((p[4]&15)<<8);
    unsigned h=p[5]|((p[7]&240)<<4), vb=p[6]|((p[7]&15)<<8);
    int il=(p[17]&128)!=0;
    if(!clock || !w || !h || !hb || !vb || il!=interlaced) return 0;
    unsigned refresh=(unsigned)(((uint64_t)clock+(w+hb)*(h+vb)/2)/((w+hb)*(h+vb)));
    if(il) h*=2;
    return w==width && h==height && refresh+1>=hz && refresh<=hz+1;
}

static int vic_matches(unsigned vic, unsigned w, unsigned h, unsigned hz, int il)
{
    unsigned width=0,height=0,rate=0;int scan=0;
    switch(vic) {
    case 1: width=640;height=480;rate=60;break;
    case 2: case 3: width=720;height=480;rate=60;break;
    case 4: width=1280;height=720;rate=60;break;
    case 5: width=1920;height=1080;rate=60;scan=1;break;
    case 16: width=1920;height=1080;rate=60;break;
    case 17: case 18: width=720;height=576;rate=50;break;
    case 19: width=1280;height=720;rate=50;break;
    case 20: width=1920;height=1080;rate=50;scan=1;break;
    case 31: width=1920;height=1080;rate=50;break;
    default: return 0;
    }
    return width==w && height==h && rate==hz && scan==il;
}

static int edid_supports(const uint8_t *data,size_t size,unsigned w,unsigned h,unsigned hz,int il)
{
    if(!edid_valid(data,size)) return 0;
    if(!il && hz==60 && ((w==640&&h==480&&(data[35]&32)) ||
        (w==800&&h==600&&(data[35]&1)) || (w==1024&&h==768&&(data[36]&8)))) return 1;
    for(unsigned i=38;i<54;i+=2) {
        if(data[i]==1 && data[i+1]==1) continue;
        unsigned width=((unsigned)data[i]+31)*8,height=0;
        switch(data[i+1]>>6) {
        case 0: if(data[19]>=3) height=width*10/16;break;
        case 1: height=width*3/4;break;
        case 2: height=width*4/5;break;
        case 3: height=width*9/16;break;
        }
        if(!il && width==w && height==h && (unsigned)(data[i+1]&63)+60==hz) return 1;
    }
    for(unsigned i=54;i+18<=126;i+=18)
        if(detailed_matches(data+i,w,h,hz,il)) return 1;
    for(size_t offset=128;offset<size;offset+=128) {
        const uint8_t *extension=data+offset;
        if(extension[0]!=2 || extension[1]<3) continue;
        unsigned end=extension[2];
        if(end==0) continue;
        if(end<4 || end>127) return 0;
        for(unsigned i=4;i<end;) {
            unsigned type=extension[i]>>5,len=extension[i]&31;i++;
            if(i+len>end) return 0;
            if(type==2) for(unsigned j=0;j<len;j++)
                if(vic_matches(extension[i+j]&127,w,h,hz,il)) return 1;
            i+=len;
        }
        for(unsigned i=end;i+18<=127;i+=18)
            if(detailed_matches(extension+i,w,h,hz,il)) return 1;
    }
    return 0;
}
