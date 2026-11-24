#include <switch.h>
#include "native_source.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BASE "sdmc:/config/kefiriz/native-source"
#define VERSION "20.5.0"
#define ROOT BASE "/" VERSION
#define CACHE ROOT "/extracted"
#define STAGE ROOT "/incoming"
#define PREVIOUS ROOT "/previous"
#define HOLD "sdmc:/config/kefiriz/native-themes/hold"
#define LIMIT (64U * 1024U * 1024U)

typedef struct {
    u64 id;
    const char *name;
    const char *files[6];
} Title;

static const Title titles[] = {
    {0x0100000000001000, "qlaunch", {"ResidentMenu.szs", "Flaunch.szs", "Set.szs", "Notification.szs", "Entrance.szs"}},
    {0x0100000000001007, "playerselect", {"Psl.szs"}},
    {0x0100000000001013, "mypage", {"MyPage.szs"}},
};

static bool exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool directory(const char *path) {
    struct stat st;
    return mkdir(path, 0777) == 0 || (errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

/* Remove only our fixed staging files. Unexpected entries keep their directory in place. */
static bool clear_tree(const char *root) {
    if (!exists(root)) return true;
    char path[192];
    for (size_t i = 0; i < sizeof(titles)/sizeof(titles[0]); i++) {
        const Title *title = &titles[i];
        for (size_t j = 0; title->files[j]; j++) {
            snprintf(path, sizeof(path), "%s/%s/lyt/%s", root, title->name, title->files[j]);
            if (unlink(path) && errno != ENOENT) return false;
        }
        snprintf(path, sizeof(path), "%s/%s/lyt", root, title->name);
        if (rmdir(path) && errno != ENOENT) return false;
        snprintf(path, sizeof(path), "%s/%s", root, title->name);
        if (rmdir(path) && errno != ENOENT) return false;
    }
    const char *markers[] = {"ver.cfg", ".kefiriz-reexport-required"};
    for (size_t i = 0; i < 2; i++) {
        snprintf(path, sizeof(path), "%s/%s", root, markers[i]);
        if (unlink(path) && errno != ENOENT) return false;
    }
    return rmdir(root) == 0;
}

static bool header_ok(const unsigned char *data) {
    u32 size = (u32)data[4] << 24 | (u32)data[5] << 16 | (u32)data[6] << 8 | data[7];
    return !memcmp(data, "Yaz0", 4) && size >= 48 && size <= LIMIT;
}

static bool cache_ready(void) {
    if (exists(CACHE "/.kefiriz-reexport-required")) return false;
    FILE *f = fopen(CACHE "/ver.cfg", "rb");
    if (!f) return false;
    char marker[sizeof(VERSION)];
    bool ok = fread(marker, 1, sizeof(marker), f) == sizeof(VERSION)-1 && !ferror(f) &&
              !memcmp(marker, VERSION, sizeof(VERSION)-1);
    fclose(f);
    if (!ok) return false;
    const char *resident = CACHE "/qlaunch/lyt/ResidentMenu.szs";
    struct stat st;
    if (stat(resident, &st) || !S_ISREG(st.st_mode) || st.st_size <= 16 || st.st_size > (off_t)LIMIT) return false;
    f = fopen(resident, "rb");
    if (!f) return false;
    unsigned char hdr[16];
    ok = fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr) && header_ok(hdr);
    fclose(f);
    return ok;
}

enum copy_result { COPIED, UNAVAILABLE, WRITE_FAILED };

static enum copy_result copy_layout(const Title *title, const char *name, const atomic_bool *cancel) {
    char path[192];
    snprintf(path, sizeof(path), "kfs:/lyt/%s", name);
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 16 || st.st_size > (off_t)LIMIT)
        return UNAVAILABLE;
    FILE *in = fopen(path, "rb");
    if (!in) return UNAVAILABLE;
    unsigned char header[16];
    if (fread(header, 1, sizeof(header), in) != sizeof(header) || !header_ok(header)) {
        fclose(in);
        return UNAVAILABLE;
    }
    snprintf(path, sizeof(path), "%s/%s/lyt/%s", STAGE, title->name, name);
    FILE *out = fopen(path, "wb");
    if (!out) {
        fclose(in);
        return WRITE_FAILED;
    }
    static unsigned char data[64 * 1024];
    bool ok = fwrite(header, 1, sizeof(header), out) == sizeof(header);
    off_t left = st.st_size - sizeof(header);
    while (left && ok && !(cancel && atomic_load(cancel))) {
        size_t n = left > (off_t)sizeof(data) ? sizeof(data) : (size_t)left;
        ok = fread(data, 1, n, in) == n && fwrite(data, 1, n, out) == n;
        left -= n;
    }
    fclose(in);
    if (fclose(out)) ok = false;
    return (ok && !left) ? COPIED : WRITE_FAILED;
}

static const char *ready_message(void) {
    if (exists(HOLD)) return "HOME resources ready. On Windows run INSTALL.bat artwork.";
    return "HOME resources ready on microSD for INSTALL.bat.";
}

static const char *capture(bool refresh, const atomic_bool *cancel) {
    /* Recovery may have run while a swap left only the previous directory on SD. */
    if (!exists(CACHE) && exists(PREVIOUS) && !exists(HOLD) && rename(PREVIOUS, CACHE))
        return "Could not restore the previous HOME resource cache.";
    if (!refresh && cache_ready()) {
        clear_tree(PREVIOUS);
        return cancel ? NULL : ready_message();
    }
    const char *sd_error = "Could not save HOME resources. Check the microSD and retry in KEFIRIZ System.";
    Result rc = romfsMountDataStorageFromProgram(titles[0].id, "kfs");
    if (R_FAILED(rc)) {
        static char detail[120];
        snprintf(detail, sizeof(detail), "Horizon could not open HOME resources (FS 0x%08lX).", (unsigned long)rc);
        return detail;
    }
    bool ok = directory(BASE) && directory(ROOT) && clear_tree(STAGE) && directory(STAGE);
    bool home = false;
    for (size_t i = 0; i < sizeof(titles)/sizeof(titles[0]); i++) {
        const Title *title = &titles[i];
        if (i && (!ok || !home || (cancel && atomic_load(cancel)))) break;
        if (i && R_FAILED(romfsMountDataStorageFromProgram(title->id, "kfs"))) continue;
        char path[192];
        snprintf(path, sizeof(path), "%s/%s", STAGE, title->name);
        if (ok) ok = directory(path);
        snprintf(path, sizeof(path), "%s/%s/lyt", STAGE, title->name);
        if (ok) ok = directory(path);
        for (size_t j = 0; ok && title->files[j] && !(cancel && atomic_load(cancel)); j++) {
            enum copy_result result = copy_layout(title, title->files[j], cancel);
            if (!i && !j) home = result == COPIED;
            if (result == WRITE_FAILED) ok = false;
        }
        romfsUnmount("kfs");
    }
    if (cancel && atomic_load(cancel)) return NULL;
    if (!ok) return sd_error;
    if (!home) return "Horizon did not provide a supported HOME menu layout.";
    /* A fresh read may have less access when a system applet is suspended.
       Preserve a complete, current export until its replacement covers it. */
    if (cache_ready()) {
        for (size_t i = 0; i < sizeof(titles)/sizeof(titles[0]); i++) {
            const Title *title = &titles[i];
            for (size_t j = 0; title->files[j]; j++) {
                char old[192], next[192];
                snprintf(old, sizeof(old), "%s/%s/lyt/%s", CACHE, title->name, title->files[j]);
                snprintf(next, sizeof(next), "%s/%s/lyt/%s", STAGE, title->name, title->files[j]);
                if (exists(old) && !exists(next))
                    return "Previous HOME resources kept. Some layouts were unavailable.";
            }
        }
    }
    FILE *marker = fopen(STAGE "/ver.cfg", "wb");
    if (!marker) return sd_error;
    ok = fwrite(VERSION, 1, sizeof(VERSION)-1, marker) == sizeof(VERSION)-1;
    if (fclose(marker) || !ok) return sd_error;
    if (!clear_tree(PREVIOUS)) return sd_error;
    bool saved = exists(CACHE);
    if (saved && rename(CACHE, PREVIOUS)) return sd_error;
    if (rename(STAGE, CACHE)) {
        if (saved) rename(PREVIOUS, CACHE);
        return sd_error;
    }
    if (saved) clear_tree(PREVIOUS);
    return ready_message();
}

const char *k_native_source(bool refresh, const atomic_bool *cancel) {
    if (hosversionGet() != MAKEHOSVERSION(20,5,0) || !hosversionIsAtmosphere())
        return cancel ? NULL : "HOME artwork supports Horizon 20.5.0 with Atmosphere.";
    if (!refresh && cache_ready() && !exists(PREVIOUS)) return cancel ? NULL : ready_message();
    /* An album applet and a suspended full application can coexist. SM releases
       this process-owned name on exit, including after a crash or forced close. */
    SmServiceName name = smEncodeName("kiz:art");
    Handle lock;
    if (R_FAILED(smRegisterService(&lock, name, false, 1)))
        return cancel ? NULL : "HOME resource capture is busy or unavailable.";
    const char *message = capture(refresh, cancel);
    smUnregisterService(name);
    svcCloseHandle(lock);
    return message;
}
