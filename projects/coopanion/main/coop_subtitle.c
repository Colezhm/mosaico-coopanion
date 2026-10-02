// SPDX-License-Identifier: MIT
#include "coop_subtitle.h"
#include <string.h>
/* Three lines, 18 full-width glyphs each. ASCII uses a conservative 2/3 cell.
 * Explicit line breaks avoid GSP's centered-label single-line path. */
static unsigned layout(const char *text, unsigned wanted, char *out)
{
    unsigned page=0,line=0,width=0; size_t used=0;
    if(out)out[0]=0;
    for(const unsigned char *p=(const unsigned char *)text;p && *p;){
        size_t n=1;
        if(*p>=0xf0)n=4;else if(*p>=0xe0)n=3;else if(*p>=0xc0)n=2;
        for(size_t k=1;k<n;k++)if(!p[k]||(p[k]&0xc0)!=0x80){n=1;break;}
        unsigned cells=*p<128?2:3;
        if(*p=='\n'||width+cells>54){
            width=0;
            if(++line==3){page++;line=0;}
            else if(out&&page==wanted&&used+1<COOP_SUBTITLE_PAGE_BYTES)out[used++]='\n';
            if(*p=='\n'){p++;continue;}
        }
        if(out&&page==wanted&&used+n<COOP_SUBTITLE_PAGE_BYTES){memcpy(out+used,p,n);used+=n;}
        width+=cells;p+=n;
    }
    if(out)out[used]=0;
    return page+1;
}
unsigned coop_subtitle_pages(const char *text){return layout(text,0,NULL);}
void coop_subtitle_page(const char *text,unsigned page,char out[COOP_SUBTITLE_PAGE_BYTES]){layout(text,page,out);}
