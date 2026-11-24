#include "ui.h"
#include <switch.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    Thread thread;
    Mutex lock;
    KfUpdate release;
    bool apply, active, done, hold_exit;
    UpScreen screen;
    char phase[112];
    uint64_t bytes, total;
    int result;
    unsigned sequence;
} UpTask;

static void progress(const char *phase,uint64_t done,uint64_t total,void *context) {
    UpTask *task=context;
    mutexLock(&task->lock);
    if (task->bytes!=done || task->total!=total || strcmp(task->phase,phase)) {
        snprintf(task->phase,sizeof(task->phase),"%s",phase);
        task->bytes=done; task->total=total; ++task->sequence;
    }
    mutexUnlock(&task->lock);
}
static void worker(void *context) {
    UpTask *task=context;
    int rc;
    if (task->apply) {
        rc=kf_apply(&task->release,progress,task);
    } else {
        rc=kf_recover(&task->release,progress,task);
        if (rc==KF_CURRENT) {
            mutexLock(&task->lock);
            task->screen=UP_CHECKING;
            task->phase[0]=0;
            task->bytes=task->total=0;
            ++task->sequence;
            mutexUnlock(&task->lock);
            rc=kf_check(&task->release,progress,task);
        }
    }
    mutexLock(&task->lock);
    task->result=rc;
    task->done=true;
    ++task->sequence;
    mutexUnlock(&task->lock);
}
static void close_worker(UpTask *t) {
    threadWaitForExit(&t->thread);
    threadClose(&t->thread);
    t->active=false;
    if (t->hold_exit) {
        appletUnlockExit();
        t->hold_exit=false;
    }
}
static void start(UpTask *t,UpView *v,bool apply,unsigned *seen) {
    if (t->active) return;
    t->apply=apply;
    t->release=v->release;
    t->done=false;
    t->phase[0]=0;
    t->bytes=t->total=0;
    t->screen=apply?UP_INSTALLING:UP_RECOVERING;
    t->sequence=1;
    *seen=0;
    t->hold_exit=R_SUCCEEDED(appletLockExit());
    Result rc=threadCreate(&t->thread,worker,t,NULL,0x40000,0x2d,-2);
    if (R_SUCCEEDED(rc)) {
        rc=threadStart(&t->thread);
        if (R_FAILED(rc)) threadClose(&t->thread);
    }
    v->page=0;
    if (R_FAILED(rc)) {
        if (t->hold_exit) appletUnlockExit();
        t->hold_exit=false;
        v->screen=UP_ERROR;
        snprintf(v->release.message,sizeof(v->release.message),
            "Could not start the update worker (0x%08x). Close the app and try again.",rc);
        return;
    }
    t->active=true;
    v->screen=apply?UP_INSTALLING:UP_RECOVERING;
    v->phase[0]=0;
    v->done=v->total=0;
}
static bool poll_worker(UpTask *t,UpView *v,unsigned *seen) {
    if (!t->active) return false;
    mutexLock(&t->lock);
    bool changed=t->sequence!=*seen, ended=t->done;
    *seen=t->sequence;
    if (changed && !ended) {
        v->screen=t->screen;
        v->done=t->bytes; v->total=t->total;
        snprintf(v->phase,sizeof(v->phase),"%s",t->phase);
    }
    mutexUnlock(&t->lock);
    if (ended) {
        close_worker(t);
        v->release=t->release;
        up_result(v,t->result);
    }
    return changed;
}
static void text_draw(const UpView *v) {
    consoleClear();
    printf("\n  KEFIRIZ Updater     %s channel\n\n  Installed  %s\n  Available  %s\n\n  %s\n\n",
        v->release.preview?"Preview":"Stable",
        v->release.current[0]?v->release.current:"Unavailable",
        v->release.latest[0]?v->release.latest:"Unknown",up_title(v->screen));
    char lines[64][64];
    size_t count=up_lines(v,lines,64);
    for (size_t i=v->page*UP_ROWS; i<count && i<(v->page+1)*UP_ROWS; ++i)
        printf("  %s\n",lines[i]);
    if (up_pages(v)>1) printf("\n  Left / Right     Page %u of %u\n",v->page+1,up_pages(v));
    if (up_busy(v->screen)) {
        printf("\n  %s",v->phase);
        if (v->total) printf("  %.0f%%",100.0*(double)v->done/v->total);
        printf("\n");
    } else {
        if (v->screen!=UP_AVAILABLE || v->compatible) printf("\n  A   %s",up_primary(v->screen));
        printf("\n  B   %s\n",v->screen==UP_CONFIRM?"Back":"Close");
        if (v->screen!=UP_CONFIRM) puts("  X   Switch channel");
        if (v->screen!=UP_CONFIRM && v->release.profiles_off && v->compatible) puts("  Y   Enable R + ZR");
    }
    consoleUpdate(NULL);
}
int main(void) {
    UpView view={0};
    UpTask task={0};
    mutexInit(&task.lock);
    u32 fw=hosversionGet();
    view.compatible=HOSVER_MAJOR(fw)==20 && HOSVER_MINOR(fw)==5 && HOSVER_MICRO(fw)==0;
    kf_local(&view.release);
    if (R_SUCCEEDED(setsysInitialize())) {
        ColorSetId set;
        view.dark=R_SUCCEEDED(setsysGetColorSetId(&set)) && set==ColorSetId_Dark;
        setsysExit();
    }
    PlFontData font;
    Framebuffer fb={0};
    bool pl=R_SUCCEEDED(plInitialize(PlServiceType_User));
    bool gfx=pl && R_SUCCEEDED(plGetSharedFontByType(&font,PlSharedFontType_Standard)) &&
        up_font(font.address) && R_SUCCEEDED(framebufferCreate(&fb,nwindowGetDefault(),
        UP_WIDTH,UP_HEIGHT,PIXEL_FORMAT_RGBA_8888,2));
    if (gfx && R_FAILED(framebufferMakeLinear(&fb))) {
        framebufferClose(&fb);
        gfx=false;
    }
    if (!gfx) consoleInit(NULL);

    PadState pad;
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    unsigned seen=0;
    bool pressed=false,dirty=true,closing=false;
    start(&task,&view,false,&seen);
    while (appletMainLoop() && !closing) {
        dirty|=poll_worker(&task,&view,&seen);
        padUpdate(&pad);
        u64 down=padGetButtonsDown(&pad);
        HidTouchScreenState touch;
        bool touching=hidGetTouchScreenStates(&touch,1)>0 && touch.count>0;
        int input=(gfx && touching && !pressed)?up_touch(touch.touches[0].x,touch.touches[0].y):-1;
        pressed=touching;
        if (down&(HidNpadButton_B|HidNpadButton_Plus)) input=UP_BACK;
        else if (down&HidNpadButton_A) input=UP_PRIMARY;
        else if (down&HidNpadButton_X) input=UP_CHANNEL;
        else if (down&HidNpadButton_Y) input=UP_PROFILES;
        else if (down&(HidNpadButton_Left|HidNpadButton_Up|HidNpadButton_StickLUp|
                      HidNpadButton_StickLLeft)) input=UP_PREVIOUS;
        else if (down&(HidNpadButton_Right|HidNpadButton_Down|HidNpadButton_StickLDown|
                      HidNpadButton_StickLRight)) input=UP_NEXT;
        if (input>=0) {
            UpCommand cmd=up_input(&view,(UpInput)input);
            if (cmd==UP_CHECK || cmd==UP_APPLY) start(&task,&view,cmd==UP_APPLY,&seen);
            else if (cmd==UP_SWITCH_CHANNEL) {
                if (kf_set_preview(!view.release.preview)) {
                    kf_local(&view.release);
                    start(&task,&view,false,&seen);
                } else {
                    view.screen=UP_ERROR;
                    view.page=0;
                    snprintf(view.release.message,sizeof(view.release.message),"Could not save the channel on this microSD.");
                }
            }
            else if (cmd==UP_ENABLE_PROFILES) {
                bool ok=kf_enable_profiles();
                kf_local(&view.release);
                view.page=0;
                view.screen=ok ? UP_PROFILES_ENABLED : UP_ERROR;
                if (!ok) snprintf(view.release.message,sizeof(view.release.message),"Could not enable Profiles. Verify the Aurora installation and service files.");
            } else if (cmd==UP_CLOSE) closing=true;
            dirty=true;
        }
        if (dirty && !closing) {
            if (gfx) {
                u32 stride;
                uint32_t *pixels=framebufferBegin(&fb,&stride);
                up_draw(pixels,(int)(stride/sizeof(*pixels)),&view);
                framebufferEnd(&fb);
            } else text_draw(&view);
            dirty=false;
        }
        svcSleepThread(task.active?65000000:16000000);
    }
    if (task.active) close_worker(&task);
    if (gfx) framebufferClose(&fb); else consoleExit(NULL);
    if (pl) plExit();
    return 0;
}
