#include "draw.h"
#include <string.h>

typedef struct { uint32_t offset; int16_t x, y, w, h, advance; } KzGlyph;
#include "draw_art.inc"

typedef struct { uint32_t *p; size_t pitch; } Canvas;
typedef struct { int x, y, w, h; } Box;
typedef struct {
    uint32_t text, secondary, empty, line, tile, well, surface, raised, focus, accent, ink;
} Palette;

static const Palette dark = {0xf4f2e9,0xb8bbb5,0x858b82,0x444846,0x292d2e,0x25292a,
    0x282d2e,0x353a3a,0xfff9e9,0xeb745e,0x251c19};
static const Palette light = {0x252d2d,0x505d58,0x64706a,0xbfc6be,0xebeee7,0xe1e5de,
    0xf4f4eb,0xe4e8e0,0x984432,0xe56e57,0x2a1713};
static uint32_t background[KZ_WIDTH*KZ_HEIGHT], app_art[242*242];
static unsigned char glyph_pixels[KZ_FONT_BYTES];
static int assets_ready, background_light = -1;
enum { TILE_X = 76, TILE_Y = 202, TILE_STEP = 276, TILE_SIZE = 242,
       TOOL_X = 376, TOOL_Y = 554, TOOL_STEP = 94, TOOL_R = 32, VISIBLE_ROWS = 5 };

static int minimum(int a,int b) { return a < b ? a : b; }
static int maximum(int a,int b) { return a > b ? a : b; }
static int inside(Box b, int x, int y) {
    return x >= b.x && y >= b.y && x < b.x+b.w && y < b.y+b.h;
}
static uint32_t blend(uint32_t base, uint32_t fg, unsigned a) {
    if (a==255) return fg;
    if (!a) return base;
    unsigned inv=255-a;
    unsigned rb=(((base>>16)&255)*inv+((fg>>16)&255)*a+127)/255;
    unsigned gb=(((base>>8)&255)*inv+((fg>>8)&255)*a+127)/255;
    unsigned bb=((base&255)*inv+(fg&255)*a+127)/255;
    return (rb<<16)|(gb<<8)|bb;
}
static void pixel(Canvas c,int x,int y,uint32_t color,unsigned alpha) {
    if ((unsigned)x<KZ_WIDTH && (unsigned)y<KZ_HEIGHT) {
        uint32_t *p=c.p+(size_t)y*c.pitch+x; *p=blend(*p,color,alpha);
    }
}
static void rect(Canvas c, Box b, uint32_t color) {
    int right=minimum(KZ_WIDTH,b.x+b.w), bottom=minimum(KZ_HEIGHT,b.y+b.h);
    for(int y=maximum(0,b.y);y<bottom;y++) {
        uint32_t *row=c.p+(size_t)y*c.pitch;
        for(int x=maximum(0,b.x);x<right;x++) row[x]=color;
    }
}
static void rounded(Canvas c,Box b,int r,uint32_t color) {
    r=minimum(r,minimum(b.w,b.h)/2);
    if (r<=0) {rect(c,b,color); return;}
    rect(c,(Box){b.x+r,b.y,b.w-r*2,b.h},color);
    rect(c,(Box){b.x,b.y+r,r,b.h-r*2},color);
    rect(c,(Box){b.x+b.w-r,b.y+r,r,b.h-r*2},color);
    for(int i=0;i<r;i++) for(int j=0;j<r;j++) {
        int dx=2*(r-i)-1,dy=2*(r-j)-1;
        int a=128+(4*r*r-dx*dx-dy*dy)*64/r;
        if(a<=0)continue;
        unsigned opacity=(unsigned)minimum(255,a);
        pixel(c,b.x+i,b.y+j,color,opacity);
        pixel(c,b.x+b.w-1-i,b.y+j,color,opacity);
        pixel(c,b.x+i,b.y+b.h-1-j,color,opacity);
        pixel(c,b.x+b.w-1-i,b.y+b.h-1-j,color,opacity);
    }
}
static Box inset(Box b,int amount) {
    return (Box){b.x+amount,b.y+amount,b.w-2*amount,b.h-2*amount};
}
static void panel(Canvas c,Box b,int radius,uint32_t border,uint32_t fill) {
    rounded(c,b,radius,border); rounded(c,inset(b,2),maximum(0,radius-2),fill);
}
static void unpack_color(uint32_t *target,const unsigned char *rle,size_t n) {
    for(size_t i=0;i<n;i+=4) {
        uint32_t color=((uint32_t)rle[i+1]<<16)|((uint32_t)rle[i+2]<<8)|rle[i+3];
        for(unsigned j=0;j<rle[i];j++) *target++=color;
    }
}
static void assets(int light_mode) {
    if(!assets_ready) {
        size_t k=0;
        for(size_t i=0;i<sizeof(font_rle);i+=2) {
            memset(glyph_pixels+k,font_rle[i+1],font_rle[i]); k+=font_rle[i];
        }
        unpack_color(app_art,torrent_rle,sizeof(torrent_rle));
        assets_ready=1;
    }
    if(background_light!=light_mode) {
        if(light_mode) unpack_color(background,bg_light_rle,sizeof(bg_light_rle));
        else unpack_color(background,bg_dark_rle,sizeof(bg_dark_rle));
        background_light=light_mode;
    }
}
static unsigned utf8(const char **text) {
    const unsigned char *p=(const unsigned char*)*text;
    unsigned c=*p++;
    if(c>=0xc2 && c<=0xf4) {
        unsigned count=c<0xe0?1:c<0xf0?2:3;
        unsigned value=c&(0x7f>>(count+1));
        while(count--) {
            if((*p&0xc0)!=0x80){ *text=(const char*)p; return '?'; }
            value=(value<<6)|(*p++&63);
        }
        c=value;
    } else if(c>=128) c='?';
    *text=(const char*)p; return c;
}
static const KzGlyph *glyph(unsigned code,int bold) {
    int lo=0,hi=KZ_GLYPH_COUNT;
    while(lo<hi){int mid=(lo+hi)/2;if(font_codes[mid]<code)lo=mid+1;else hi=mid;}
    if(lo>=KZ_GLYPH_COUNT || font_codes[lo]!=code) lo='?'-32;
    return &font_glyphs[lo+(bold?KZ_GLYPH_COUNT:0)];
}
static int advance(const KzGlyph *g,int size) {return (g->advance*size+320)/640;}
static int measure(const char *s,int size,int bold) {
    int width=0;
    if(s)while(*s && width<32768) width+=advance(glyph(utf8(&s),bold),size);
    return width;
}
static void mask(Canvas c,const unsigned char *data,int sw,int sh,Box b,uint32_t color) {
    if(!b.w||!b.h||!sw||!sh)return;
    for(int j=0;j<b.h;j++) {
        int sy=minimum(sh*256-256,maximum(0,(2*j+1)*sh*128/b.h-128));
        int y=sy>>8,y2=minimum(sh-1,y+1), fy=sy&255;
        for(int i=0;i<b.w;i++) {
            int sx=minimum(sw*256-256,maximum(0,(2*i+1)*sw*128/b.w-128));
            int x=sx>>8,x2=minimum(sw-1,x+1), fx=sx&255;
            int a=data[y*sw+x]*(256-fx)+data[y*sw+x2]*fx;
            int z=data[y2*sw+x]*(256-fx)+data[y2*sw+x2]*fx;
            unsigned alpha=(unsigned)((a*(256-fy)+z*fy+32768)>>16);
            if(alpha)pixel(c,b.x+i,b.y+j,color,alpha);
        }
    }
}
static void text(Canvas c,int x,int baseline,const char *s,int size,int bold,uint32_t color,int width) {
    if(!s||width<=0)return;
    int left=x; int dots=advance(glyph(0x2026,bold),size);
    int clipped=measure(s,size,bold)>width;
    while(*s) {
        const KzGlyph *g=glyph(utf8(&s),bold);
        int step=advance(g,size);
        if(clipped && x-left+step+dots>width) {
            if(x-left+dots>width)break;
            g=glyph(0x2026,bold); s=""; step=dots;
        }
        Box b={x+g->x*size/40,baseline+g->y*size/40,(g->w*size+20)/40,(g->h*size+20)/40};
        mask(c,glyph_pixels+g->offset,g->w,g->h,b,color);
        x+=step;
    }
}
static void right_text(Canvas c,int right,int baseline,const char *s,int size,int bold,uint32_t color,int limit) {
    int width=minimum(limit,measure(s,size,bold)); text(c,right-width,baseline,s,size,bold,color,width);
}
static void center_text(Canvas c,Box b,const char *s,int size,int bold,uint32_t color) {
    int width=minimum(b.w-8,measure(s,size,bold));
    text(c,b.x+(b.w-width)/2,b.y+b.h/2+size*34/100,s,size,bold,color,width);
}
static void icon(Canvas c,int x,int y,int size,KzIcon type,uint32_t color) {
    if(type<=KZ_ICON_NONE || type>KZ_ICON_PLAY)return;
    mask(c,icon_masks+((int)type-1)*1600,40,40,(Box){x,y,size,size},color);
}
static void tile(Canvas c,const Palette *p,const KzTile *t,int i,int selected) {
    Box b={TILE_X+i*TILE_STEP,TILE_Y,TILE_SIZE,TILE_SIZE};
    if(selected) {
        rounded(c,inset(b,-6),12,p->focus);
        rounded(c,inset(b,-3),10,blend(p->well,p->text,100));
    }
    int empty=(!t->title||!*t->title) && t->icon==KZ_ICON_NONE;
    if(t->icon==KZ_ICON_DOWNLOAD) {
        rounded(c,b,8,0xf9f6ee);
        for(int j=0;j<TILE_SIZE;j++) {
            int pad=j==0||j==TILE_SIZE-1?6:j<3||j>TILE_SIZE-4?3:1;
            memcpy(c.p+(size_t)(b.y+j)*c.pitch+b.x+pad,app_art+j*242+pad,(242-2*pad)*sizeof(uint32_t));
        }
    } else {
        panel(c,b,9,p->line,p->tile);
        if(empty) mask(c,empty_mask,68,96,(Box){b.x+(b.w-68)/2,b.y+(b.h-96)/2,68,96},p->empty);
        else {
            icon(c,b.x+89,b.y+65,64,t->icon?t->icon:KZ_ICON_SOFTWARE,p->text);
            center_text(c,(Box){b.x+13,b.y+150,b.w-26,51},t->title,23,1,p->text);
        }
    }
}
static Box tool_box(int i) { return (Box){TOOL_X+i*TOOL_STEP-TOOL_R,TOOL_Y-TOOL_R,TOOL_R*2,TOOL_R*2}; }
static void dock(Canvas c,const Palette *p,const KzScene *s) {
    for(int i=0;i<KZ_TOOL_COUNT;i++) {
        Box b=tool_box(i); KzIcon id=s->tools[i];
        if(id==KZ_ICON_NONE)continue;
        if(s->tool_focus==i) rounded(c,inset(b,-5),TOOL_R+5,p->focus);
        int coral=(id==KZ_ICON_DOWNLOAD);
        panel(c,b,TOOL_R,coral?blend(p->accent,0xffffff,42):p->line,coral?p->accent:p->raised);
        icon(c,b.x+13,b.y+13,38,id,coral?0xfffbef:p->text);
    }
}
static const Box sheet={174,84,932,552}, close_box={1023,107,56,52};
static Box row_box(int i){return (Box){207,223+63*i,866,58};}
static int first_row(const KzScene *s) {
    return maximum(0,minimum(maximum(0,s->row_count-VISIBLE_ROWS),s->row_focus-VISIBLE_ROWS+1));
}
static void description(Canvas c,const char *s,uint32_t color) {
    const int width=845,size=21;
    if(!s || measure(s,size,0)<=width) {text(c,213,199,s,size,0,color,width);return;}
    const char *pos=s,*last_space=NULL,*end=s;int used=0;
    while(*pos) {
        const char *start=pos;
        unsigned code=utf8(&pos);
        if(code==' ')last_space=start;
        used+=advance(glyph(code,0),size);
        if(used>width || pos-s>=255)break;
        end=pos;
    }
    if(last_space && last_space>s)end=last_space;
    char line[256];
    size_t n=(size_t)(end-s);
    memcpy(line,s,n);line[n]=0;
    text(c,213,184,line,size,0,color,width);
    while(*end==' ')end++;
    text(c,213,208,end,size,0,color,width);
}
static void scrim(Canvas c) {
    for(int y=0;y<KZ_HEIGHT;y++) {
        uint32_t *row=c.p+(size_t)y*c.pitch;
        for(int x=0;x<KZ_WIDTH;x++)row[x]=((row[x]>>1)&0x7f7f7f)+((row[x]>>3)&0x1f1f1f);
    }
}
static void window(Canvas c,const KzScene *s,const Palette *p) {
    scrim(c);panel(c,sheet,16,p->line,p->surface);
    rounded(c,(Box){211,111,46,4},2,p->accent);
    text(c,212,161,s->window_title,40,1,p->text,775);
    description(c,s->window_detail,p->secondary);
    panel(c,close_box,9,p->line,p->raised);
    icon(c,close_box.x+14,close_box.y+12,28,KZ_ICON_BACK,p->text);
    int first=first_row(s), count=s->rows?maximum(0,s->row_count):0;
    for(int i=0;i<VISIBLE_ROWS && first+i<count;i++) {
        int pos=first+i; const KzRow *r=&s->rows[pos]; Box b=row_box(i);
        int selected=pos==s->row_focus; uint32_t fg=r->disabled?p->secondary:p->text;
        panel(c,b,8,selected?p->focus:p->line,selected?blend(p->raised,p->accent,23):p->well);
        if(selected)rounded(c,(Box){b.x+3,b.y+11,3,b.h-22},1,p->accent);
        int start=b.x+22;
        if(r->icon!=KZ_ICON_NONE) {icon(c,start,b.y+13,32,r->icon,selected?p->accent:fg);start+=48;}
        int value_width=minimum(304,measure(r->value,20,0));
        text(c,start,b.y+37,r->label,24,selected,fg,b.x+b.w-24-start-(value_width?value_width+30:0));
        right_text(c,b.x+b.w-20,b.y+37,r->value,20,0,p->secondary,304);
    }
    if(count>VISIBLE_ROWS) {
        int h=308*VISIBLE_ROWS/count; h=maximum(12,h);
        int y=223+(308-h)*first/(count-VISIBLE_ROWS);
        rounded(c,(Box){1084,y,3,h},1,p->accent);
    }
    rect(c,(Box){211,559,858,1},p->line);
    text(c,213,601,s->left_hint,19,0,p->secondary,415);
    right_text(c,1067,601,s->right_hint,19,0,p->secondary,415);
}
void kz_draw(uint32_t *pixels,size_t pitch_pixels,const KzScene *scene) {
    if(!pixels || pitch_pixels<KZ_WIDTH || !scene)return;
    Canvas c={pixels,pitch_pixels}; const Palette *p=scene->light?&light:&dark;
    assets(!!scene->light);
    for(int y=0;y<KZ_HEIGHT;y++)memcpy(pixels+(size_t)y*pitch_pixels,background+y*KZ_WIDTH,KZ_WIDTH*sizeof(uint32_t));
    right_text(c,1138,79,scene->clock,21,0,p->text,160);
    text(c,74,177,scene->selected_title,31,0,p->text,1054);
    for(int i=0;i<KZ_TILE_COUNT;i++)tile(c,p,&scene->tiles[i],i,scene->tile_focus==i);
    dock(c,p,scene);
    if(scene->window_title && *scene->window_title)window(c,scene,p);
    else {
        text(c,74,681,scene->left_hint,18,0,p->secondary,480);
        right_text(c,1140,681,scene->right_hint,18,0,p->secondary,480);
    }
}
int kz_hit(const KzScene *scene,int x,int y) {
    if(!scene || (unsigned)x>=KZ_WIDTH || (unsigned)y>=KZ_HEIGHT)return -1;
    if(scene->window_title && *scene->window_title) {
        if(inside(close_box,x,y))return -2;
        int first=first_row(scene);
        if(scene->rows)for(int i=0;i<VISIBLE_ROWS && first+i<scene->row_count;i++)
            if(inside(row_box(i),x,y))return 20+first+i;
        return -1;
    }
    for(int i=0;i<KZ_TILE_COUNT;i++)
        if(inside((Box){TILE_X+i*TILE_STEP,TILE_Y,TILE_SIZE,TILE_SIZE},x,y))return i;
    for(int i=0;i<KZ_TOOL_COUNT;i++) {
        int dx=x-TOOL_X-i*TOOL_STEP,dy=y-TOOL_Y;
        if(scene->tools[i]!=KZ_ICON_NONE && dx*dx+dy*dy<=TOOL_R*TOOL_R)return 10+i;
    }
    return -1;
}
static Box key_box(int i) {
    if(i<40)return (Box){183+(i%10)*92,253+(i/10)*60,84,52};
    return (Box){183+(i-40)*184,493,176,52};
}
void kz_keyboard(uint32_t *pixels,size_t pitch_pixels,int light_mode,const char *masked,const char *keys,int focused) {
    if(!pixels||pitch_pixels<KZ_WIDTH)return;
    assets(!!light_mode);
    Canvas c={pixels,pitch_pixels};const Palette *p=light_mode?&light:&dark;
    scrim(c);panel(c,(Box){147,78,986,566},16,p->line,p->surface);
    rounded(c,(Box){185,104,46,4},2,p->accent);
    text(c,184,155,"Wi-Fi password",39,1,p->text,900);
    panel(c,(Box){183,175,912,60},8,p->line,p->well);
    text(c,203,215,masked,24,0,p->text,873);
    static const char *actions[]={"Shift","Symbols","Space","Delete","Connect"};
    size_t provided=keys?strlen(keys):0;
    for(int i=0;i<45;i++) {
        Box b=key_box(i);char key[2]={0,0};int selected=i==focused;
        if(i<40 && (size_t)i<provided)key[0]=keys[i];
        uint32_t fill=i==44?p->accent:p->raised;
        if(selected)rounded(c,inset(b,-3),10,p->focus);
        panel(c,b,7,selected?p->surface:p->line,fill);
        center_text(c,b,i<40?key:actions[i-40],i<40?27:22,i>=40,i==44?p->ink:p->text);
    }
    rect(c,(Box){185,569,908,1},p->line);
    text(c,186,610,"B  Cancel",19,0,p->secondary,300);
    right_text(c,1094,610,"A  Select",19,0,p->secondary,300);
}
int kz_key_hit(int x,int y) {
    for(int i=0;i<45;i++)if(inside(key_box(i),x,y))return i;
    return -1;
}
