#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "icon_pixels.h"

static stbtt_fontinfo face;
static uint32_t *screen;
static int pitch;
typedef struct { uint32_t paper, card, text, muted, rule, accent, button, on_button; } Palette;
static uint32_t rgb(unsigned c) {
    return 0xff000000u | (c >> 16) | (c & 0xff00) | ((c & 255) << 16);
}
static Palette colors(bool dark) {
    if (dark) return (Palette){rgb(0x15191c),rgb(0x252b2e),rgb(0xf9f6ed),rgb(0xb4bebf),
        rgb(0x41494c),rgb(0xff826d),rgb(0xff826d),rgb(0x211719)};
    return (Palette){rgb(0xf5f3ed),rgb(0xe9e6dd),rgb(0x1c2326),rgb(0x515d60),
        rgb(0xc8ccc5),rgb(0xb73627),rgb(0xb73627),rgb(0xffffff)};
}
static void rect(int x,int y,int w,int h,uint32_t c) {
    for (int j = y; j < y+h; ++j)
        for (int i=x; i<x+w; ++i) screen[j*pitch+i]=c;
}
static void rounded(int x,int y,int w,int h,int r,uint32_t c) {
    for (int j=0;j<h;j++) {
        int dy=j<r ? r-1-j : j>=h-r ? j-(h-r) : 0;
        int cut=r-(int)sqrtf((float)(r*r-dy*dy));
        rect(x+cut,y+j,w-2*cut,1,c);
    }
}
static uint32_t mix(uint32_t bg,uint32_t fg,int alpha) {
    uint32_t out=0xff000000;
    for (int shift=0;shift<24;shift+=8)
        out|=((((bg>>shift)&255)*(255-alpha)+((fg>>shift)&255)*alpha+127)/255)<<shift;
    return out;
}
bool up_font(const unsigned char *data) {
    if (!data) return false;
    int idx=stbtt_GetFontOffsetForIndex(data,0);
    return idx>=0 && stbtt_InitFont(&face,data,idx);
}
static int scalar(const unsigned char **s) {
    unsigned char c=*(*s)++;
    if (c<128) return c;
    int n=c>=0xc2 && c<0xe0 ? 1 : c>=0xe0 && c<0xf0 ? 2 : c>=0xf0 && c<=0xf4 ? 3 : 0;
    if (!n) return 0xfffd;
    int cp=c&((1<<(6-n))-1);
    for (int j=0;j<n;j++) {
        if ((**s&0xc0)!=0x80) return 0xfffd;
        cp=(cp<<6)|(*(*s)++&63);
    }
    return cp<=0x10ffff && !(cp>=0xd800 && cp<=0xdfff) ? cp : 0xfffd;
}
static void label(int x,int baseline,int size,const char *value,uint32_t c,int limit) {
    float scale=stbtt_ScaleForPixelHeight(&face,(float)size);
    float length=0;
    const unsigned char *s=(const unsigned char*)value;
    while (*s) {
        int width;
        stbtt_GetCodepointHMetrics(&face,scalar(&s),&width,NULL);
        length+=width*scale;
    }
    if (length>limit) scale*=limit/length;
    float pen=(float)x;
    s=(const unsigned char*)value;
    while (*s) {
        int w,h,dx,dy,advance,cp=scalar(&s);
        unsigned char *bits=stbtt_GetCodepointBitmap(&face,scale,scale,cp,&w,&h,&dx,&dy);
        if (bits) {
            int left=(int)roundf(pen)+dx,top=baseline+dy;
            for (int j=0;j<h;j++) for (int i=0;i<w;i++) {
                int px=left+i,py=top+j;
                if ((unsigned)px<UP_WIDTH && (unsigned)py<UP_HEIGHT && bits[j*w+i]) {
                    uint32_t *dst=&screen[py*pitch+px];
                    *dst=mix(*dst,c,bits[j*w+i]);
                }
            }
            stbtt_FreeBitmap(bits,NULL);
        }
        stbtt_GetCodepointHMetrics(&face,cp,&advance,NULL);
        pen+=scale*advance;
    }
}
static void icon(int x,int y) {
    enum { S=112, R=19 };
    for (int j=0;j<S;j++) {
        int dy=j<R ? R-1-j : j>=S-R ? j-(S-R) : 0;
        int cut=R-(int)sqrtf((float)(R*R-dy*dy));
        for (int i=cut;i<S-cut;i++) {
            unsigned k=up_icon[j*S+i];
            screen[(y+j)*pitch+x+i]=rgb((((k>>11)*255/31)<<16)|((((k>>5)&63)*255/63)<<8)|((k&31)*255/31));
        }
    }
}
static void key(int x,int y,const char *keyname,const char *act,Palette p) {
    rounded(x,y,32,31,7,p.rule);
    label(x+9,y+24,22,keyname,p.text,21);
    label(x+42,y+24,27,act,p.muted,110);
}
void up_draw(uint32_t *pixels,int stride,const UpView *v) {
    screen=pixels;
    pitch=stride;
    Palette p=colors(v->dark);
    rect(0,0,UP_WIDTH,UP_HEIGHT,p.paper);
    rect(54,42,7,24,p.accent);
    label(75,63,27,"KEFIRJS",p.text,148);
    label(1043,63,24,"Stable channel",p.muted,182);
    label(50,147,68,"Firmware updater",p.text,1190);
    rect(53,178,1171,1,p.rule);

    rounded(53,212,415,347,18,p.card);
    icon(82,240);
    label(83,411,25,"Installed firmware",p.muted,356);
    label(81,469,52,v->release.current[0]?v->release.current:"Unavailable",p.text,357);
    label(83,524,25,"KEFIRIZ  /  Horizon edition 786",p.muted,357);

    label(508,249,46,up_title(v->screen),p.text,707);
    char latest[96];
    const bool show_latest=v->release.latest[0] && (v->screen==UP_AVAILABLE ||
        v->screen==UP_CONFIRM || v->screen==UP_CURRENT || v->screen==UP_INSTALLING ||
        v->screen==UP_INSTALLED);
    if (show_latest) {
        if (v->release.bytes && (v->screen==UP_AVAILABLE || v->screen==UP_CONFIRM))
            snprintf(latest,sizeof(latest),"Release %s   /   %.1f MiB",v->release.latest,
                (double)v->release.bytes/1048576.0);
        else snprintf(latest,sizeof(latest),"Release %s",v->release.latest);
        label(510,291,28,latest,p.accent,692);
    }
    const bool notes=(v->screen==UP_AVAILABLE || v->screen==UP_CURRENT) && v->release.notes[0];
    if (notes) label(510,332,25,"What's new",p.muted,700);
    char lines[64][64];
    size_t n=up_lines(v,lines,64);
    int top=notes ? 363 : show_latest ? 349 : 302;
    unsigned pages=up_pages(v);
    for (size_t i=0;i<UP_ROWS && v->page*UP_ROWS+i<n;i++)
        label(510,top+25*(int)i,25,lines[v->page*UP_ROWS+i],p.text,692);
    if (pages>1) {
        char page[28];
        snprintf(page,sizeof(page),"%u / %u",v->page+1,pages);
        label(979,575,29,"<",v->page?p.accent:p.rule,30);
        label(1044,573,25,page,p.muted,82);
        label(1168,575,29,">",v->page+1<pages?p.accent:p.rule,30);
    }
    if (up_busy(v->screen)) {
        label(510,487,25,v->phase,p.muted,705);
        if (v->total) {
            uint64_t done=v->done<=v->total?v->done:v->total;
            double progress=(double)done/v->total;
            rect(510,513,704,8,p.rule);
            rect(510,513,(int)(progress*704),8,p.accent);
            char percent[30];
            snprintf(percent,sizeof(percent),"%.0f%%",progress*100);
            label(510,554,24,percent,p.muted,160);
        }
    }
    rect(53,590,1171,1,p.rule);
    if (!up_busy(v->screen)) {
        key(55,616,"B",v->screen==UP_CONFIRM?"Back":"Close",p);
        if (v->screen!=UP_AVAILABLE || v->compatible) {
            rounded(897,606,324,65,12,p.button);
            label(926,649,27,"A",p.on_button,26);
            label(966,650,30,up_primary(v->screen),p.on_button,236);
        }
    }
}
