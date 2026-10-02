// SPDX-License-Identifier: MIT
#include "coop_animation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static unsigned u16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t u32(const uint8_t *p) { return u16(p) | (uint32_t)u16(p + 2) << 16; }
static const uint8_t *entry(const uint8_t *p, unsigned i) { return p + 24 + u16(p + 10)*12 + i*12; }
const uint8_t *coop_animation_palette(const uint8_t *p) { return entry(p,u16(p+8)); }
static bool length(const uint8_t *s, size_t n, size_t *at, size_t *v)
{
    if (*v != 15) return true;
    unsigned b;
    do { if (*at >= n) return false; b=s[(*at)++]; *v+=b;
         if (*v > COOP_ANIMATION_PIXELS) return false; } while (b==255);
    return true;
}
static bool unpack(const uint8_t *s, size_t n, uint8_t *out)
{
    size_t i=0,j=0;
    while(i<n) {
        unsigned token=s[i++]; size_t lit=token>>4;
        if(!length(s,n,&i,&lit)||lit>n-i||lit>COOP_ANIMATION_PIXELS-j) return false;
        memcpy(out+j,s+i,lit);i+=lit;j+=lit;
        if(i==n) return !(token&15)&&j==COOP_ANIMATION_PIXELS;
        if(n-i<2) return false;
        unsigned off=u16(s+i);i+=2;
        if(!off||off>j) return false;
        size_t match=token&15;
        if(!length(s,n,&i,&match))return false;
        match+=4;if(match>COOP_ANIMATION_PIXELS-j)return false;
        for(size_t k=0;k<match;k++,j++)out[j]=out[j-off];
    }
    return false;
}
bool coop_animation_decode(const uint8_t *p, int frame, int previous, uint8_t *indices, uint8_t *scratch)
{
    int first=frame;
    while(u16(entry(p,first)+8)!=65535) {
        if(first-1==previous)break;
        if(first<=0)return false;
        first--;
    }
    for(int i=first;i<=frame;i++) {
        const uint8_t *e=entry(p,i);bool delta=u16(e+8)!=65535;
        if(!unpack(p+u32(e),u32(e+4),scratch))return false;
        if(delta)for(unsigned k=0;k<COOP_ANIMATION_PIXELS;k++)indices[k]^=scratch[k];
        else memcpy(indices,scratch,COOP_ANIMATION_PIXELS);
    }
    return true;
}
bool coop_animation_validate(const uint8_t *p, size_t n)
{
    if(!p||n<24||n>COOP_ASSET_MAX||memcmp(p,"COO2",4)||u16(p+4)!=192||u16(p+6)!=216||u16(p+14)!=1||u32(p+16)!=n||u32(p+20))return false;
    unsigned frames=u16(p+8),clips=u16(p+10),colors=u16(p+12);
    if(!frames||frames>512||!clips||clips>64||colors<2||colors>256)return false;
    size_t end=24+clips*12+frames*12+colors*3;
    if(end>n)return false;
    uint8_t *indices=malloc(COOP_ANIMATION_PIXELS),*scratch=malloc(COOP_ANIMATION_PIXELS);
    bool ok=false;unsigned frame=0;uint64_t ids=0;
    if(!indices||!scratch)goto done;
    for(unsigned c=0;c<clips;c++){
        const uint8_t *clip=p+24+c*12;unsigned id=u16(clip),start=u16(clip+2),count=u16(clip+4);
        if(id>=64||(ids&(UINT64_C(1)<<id))||start!=frame||!count||count>32||frame+count>frames||!u16(clip+6)||u16(clip+8)>1||u16(clip+10))goto done;
        ids|=UINT64_C(1)<<id;
        for(unsigned j=0;j<count;j++,frame++){
            const uint8_t *e=entry(p,frame);size_t off=u32(e),len=u32(e+4);unsigned base=u16(e+8);
            if(off!=end||off>n||!len||len>n-off||u16(e+10)||(base!=65535&&(!j||base!=frame-1)))goto done;
            end=off+len;
            if(!unpack(p+off,len,scratch))goto done;
            for(unsigned k=0;k<COOP_ANIMATION_PIXELS;k++){
                indices[k]=base==65535?scratch[k]:indices[k]^scratch[k];
                if(indices[k]>=colors)goto done;
            }
        }
    }
    ok=frame==frames&&end==n&&(ids&1);
done:
    free(indices);free(scratch);return ok;
}
int coop_animation_frame(const uint8_t *p, unsigned id, float seconds)
{
    const uint8_t *clip=p+24;
    for(unsigned i=0;i<u16(p+10);i++)if(u16(p+24+i*12)==0){clip=p+24+i*12;break;}
    for(unsigned i=0;i<u16(p+10);i++)if(u16(p+24+i*12)==id){clip=p+24+i*12;break;}
    unsigned count=u16(clip+4),ms=u16(clip+6);float phase=fmaxf(0,seconds)*1000/ms;
    if(u16(clip+8))phase=fmodf(phase,1);
    unsigned frame=(unsigned)fminf(count-1,floorf(phase*count));
    /* The last neutral sample is a blink, not an 800 ms closed-eye hold. */
    if(id==0&&count==4)frame=phase<.4f?0:phase<.75f?1:phase<.96f?2:3;
    return u16(clip+2)+frame;
}
