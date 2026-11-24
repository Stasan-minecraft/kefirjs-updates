/* Minimal libretro frontend for the POSIX host integration test. */
#include "../native/libretro.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 1280, HEIGHT = 720 };
static const uint32_t *image;
static size_t stride;
static unsigned buttons;
static int px, py, touching;

static bool env(unsigned kind,void *data) {
    if(kind==RETRO_ENVIRONMENT_SET_PIXEL_FORMAT)
        return *(const enum retro_pixel_format *)data==RETRO_PIXEL_FORMAT_XRGB8888;
    return kind==RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME;
}
static void video(const void *data,unsigned w,unsigned h,size_t pitch) {
    assert(data && w==WIDTH && h==HEIGHT && pitch>=WIDTH*4);
    image=data;stride=pitch/4;
}
static void poll_input(void) {}
static int16_t input(unsigned port,unsigned device,unsigned index,unsigned id) {
    if(port||index)return 0;
    if(device==RETRO_DEVICE_JOYPAD)return id < 16 && (buttons & (1u << id)) != 0;
    if(device!=RETRO_DEVICE_POINTER)return 0;
    if(id==RETRO_DEVICE_ID_POINTER_X)return (int16_t)(((px*2+1)*32768/WIDTH)-32768);
    if(id==RETRO_DEVICE_ID_POINTER_Y)return (int16_t)(((py*2+1)*32768/HEIGHT)-32768);
    return id==RETRO_DEVICE_ID_POINTER_PRESSED && touching;
}
static void start(void) {
    retro_init();assert(retro_load_game(NULL));retro_run();
}
static void save_image(const char *name) {
    assert(name[0] && strspn(name,"abcdefghijklmnopqrstuvwxyz-")==strlen(name));
    char path[64];snprintf(path,sizeof(path),"%s.ppm",name);
    FILE *f=fopen(path,"wb");assert(f && image);
    fprintf(f,"P6\n%d %d\n255\n",WIDTH,HEIGHT);
    uint8_t row[WIDTH*3];
    for(int y=0;y<HEIGHT;y++){
        for(int x=0;x<WIDTH;x++){
            uint32_t c=image[(size_t)y*stride+x];
            row[x*3]=(uint8_t)(c>>16);row[x*3+1]=(uint8_t)(c>>8);row[x*3+2]=(uint8_t)c;
        }
        assert(fwrite(row,1,sizeof row,f)==sizeof row);
    }
    assert(fclose(f)==0);
}
int main(void) {
    assert(getenv("KEFIRIZ_STATE_DIR") && getenv("KEFIRIZ_POWER_DIR") && getenv("KEFIRIZ_NETWORK_DIR"));
    retro_set_environment(env);retro_set_video_refresh(video);
    retro_set_input_poll(poll_input);retro_set_input_state(input);
    struct retro_system_info info;retro_get_system_info(&info);
    assert(!strcmp(info.library_name,"KEFIRIZ OS") && retro_api_version()==RETRO_API_VERSION);
    start();puts("ready");fflush(stdout);
    char line[160],name[32];
    while(fgets(line,sizeof line,stdin)) {
        int n,x,y;
        if(sscanf(line,"press %d",&n)==1) {
            assert(n>=0 && n<16);buttons=1u<<n;retro_run();buttons=0;retro_run();
        } else if(sscanf(line,"buttons %d",&n)==1) {
            assert(n>=0 && n<=65535);buttons=(unsigned)n;retro_run();
        } else if(sscanf(line,"touch %d %d",&x,&y)==2) {
            assert(x>=0 && x<WIDTH && y>=0 && y<HEIGHT);
            px=x;py=y;touching=1;retro_run();touching=0;retro_run();
        } else if(sscanf(line,"frames %d",&n)==1) {
            assert(n>0 && n<=500);while(n--)retro_run();
        } else if(sscanf(line,"save %31s",name)==1) save_image(name);
        else if(!strcmp(line,"reinit\n")) {retro_unload_game();retro_deinit();start();}
        else if(!strcmp(line,"quit\n")) break;
        else abort();
        puts("ok");fflush(stdout);
    }
    retro_unload_game();retro_deinit();return 0;
}
