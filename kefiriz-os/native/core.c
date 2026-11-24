#define _POSIX_C_SOURCE 200809L
#include "draw.h"
#include "libretro.h"
#include "version.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
enum { MAX_ITEMS = 64, MAX_ROWS = 96 };
typedef enum {
    HOME,
    TORRENT,
    QUEUE,
    IMPORT,
    LIBRARY,
    ALBUM,
    PHOTO,
    PADS,
    SETTINGS,
    DOCK,
    PICK,
    PROFILES,
    POWER,
    CONFIRM,
    GUIDE,
    WIFI,
    KEYBOARD,
    DETAIL,
    QUICK
} Page;
typedef enum {
    NONE,
    OPEN,
    CAPTURE,
    SHOW_PHOTO,
    THEME,
    CLOCK,
    EDIT_SLOT,
    CHOOSE,
    PROFILE,
    POWER_ACTION,
    DO_POWER,
    GUIDE_NEXT,
    SCAN,
    JOIN,
    ENQUEUE
} Action;
typedef struct {
    char id[80], label[80];
    KzIcon icon;
    Page page;
} Item;
typedef struct {
    char label[96], value[82], data[256];
    Action action;
    int arg;
    KzIcon icon;
} Row;
typedef struct {
    Page page;
    int focus;
} Position;
static retro_environment_t environment;
static retro_video_refresh_t video;
static retro_input_poll_t poll_input;
static retro_input_state_t input;
static uint32_t frame[KZ_WIDTH * KZ_HEIGHT];
static uint64_t frame_generation;
uint64_t kz_frame_generation(void) { return frame_generation; }
static KzScene scene;
static Row rows[MAX_ROWS];
static KzRow rendered[MAX_ROWS];
static Item items[MAX_ITEMS];
/* Keep room for Quick settings and its transfer queue above any regular window. */
enum { REGULAR_DEPTH = 12 };
static Position nav[REGULAR_DEPTH + 2];
static int quick_depth;
static int depth, item_count, row_count, tile, dock, in_dock, light, clock12, profile, slot,
    edit_dock;
static int dirty, frames, tour_step, key_index, keyboard_mode, touch_down, touch_start,
    last_clock = -1;
static unsigned previous;
static char tile_ids[4][80], dock_ids[6][80], state[PATH_MAX], systemdir[PATH_MAX],
    netdir[PATH_MAX];
static char clock_text[16], detail[200], title[80], info_title[80], info_text[200], wifi_id[192],
    pass[65];
static char power_op[12], password_mask[65], selected_photo[128];
static pid_t action_pid;
static bool pixel_ready;
static const char *default_dock[] = {"torrent",     "library",  "album",
                                     "controllers", "settings", "power"};
static const char *keysets[] = {"1234567890qwertyuiopasdfghjkl-zxcvbnm._@",
                                "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM._@",
                                "!@#$%^&*()[]{}<>/\\|?~`'\":;,+-=0123456789_"};
static const char *base(const char *key, const char *fallback) {
    const char *e = getenv(key);
    return e && *e ? e : fallback;
}
static void copy(char *dst, size_t cap, const char *src) {
    if (!cap)
        return;
    if (!src)
        src = "";
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}
static int pathjoin(char *dst, size_t cap, const char *dir, const char *name) {
    int n = snprintf(dst, cap, "%s/%s", dir, name);
    return n > 0 && (size_t)n < cap;
}
static FILE *atfile(const char *dir, const char *name, const char *mode) {
    char p[PATH_MAX];
    return pathjoin(p, sizeof p, dir, name) ? fopen(p, mode) : NULL;
}
static void strip(char *s) { s[strcspn(s, "\r\n")] = 0; }
static int firstline(const char *dir, const char *name, char *out, size_t size) {
    FILE *f = atfile(dir, name, "r");
    if (!f)
        return 0;
    int ok = fgets(out, (int)size, f) != NULL;
    fclose(f);
    if (ok)
        strip(out);
    return ok;
}
static int suffix(const char *s, const char *ending) {
    size_t n = strlen(s), m = strlen(ending);
    return n >= m && !strcmp(s + n - m, ending);
}
static int filename_ok(const char *s) {
    if (!*s || *s == '.' || strlen(s) >= 72)
        return 0;
    for (; *s; s++)
        if (!(isalnum((unsigned char)*s) || strchr("._- ", *s)))
            return 0;
    return 1;
}
static void add_item(const char *id, const char *label, KzIcon icon, Page page) {
    if (item_count == MAX_ITEMS)
        return;
    Item *i = &items[item_count++];
    copy(i->id, sizeof i->id, id);
    copy(i->label, sizeof i->label, label);
    i->icon = icon;
    i->page = page;
}
static int item_index(const char *id) {
    for (int i = 0; i < item_count; i++)
        if (!strcmp(id, items[i].id))
            return i;
    return -1;
}
static void load_items(void) {
    item_count = 0;
    add_item("torrent", "KEFIRIZ Torrent", KZ_ICON_DOWNLOAD, TORRENT);
    add_item("library", "Library", KZ_ICON_LIBRARY, LIBRARY);
    add_item("album", "Album", KZ_ICON_ALBUM, ALBUM);
    add_item("controllers", "Controllers", KZ_ICON_CONTROLLER, PADS);
    add_item("settings", "Settings", KZ_ICON_SETTINGS, SETTINGS);
    add_item("power", "Power", KZ_ICON_POWER, POWER);
    add_item("wifi", "Wi-Fi", KZ_ICON_NETWORK, WIFI);
    add_item("profiles", "Profiles", KZ_ICON_PROFILES, PROFILES);
    char dir[PATH_MAX];
    if (!pathjoin(dir, sizeof dir, state, "actions"))
        return;
    struct dirent **list;
    int n = scandir(dir, &list, NULL, alphasort);
    for (int i = 0; i < n; i++) {
        const char *name = list[i]->d_name;
        if (filename_ok(name) && suffix(name, ".sh") && strlen(name) > 3) {
            char id[80], label[80], p[PATH_MAX];
            struct stat st;
            if (pathjoin(p, sizeof p, dir, name) && lstat(p, &st) == 0 && S_ISREG(st.st_mode)) {
                snprintf(id, sizeof id, "app/%.71s", name);
                copy(label, sizeof label, name);
                label[strlen(label) - 3] = 0;
                add_item(id, label, KZ_ICON_PLAY, HOME);
            }
        }
        free(list[i]);
    }
    if (n >= 0)
        free(list);
}
static int replace_file(const char *dir, const char *name, const char *value) {
    char path[PATH_MAX], tmp[PATH_MAX];
    if (!pathjoin(path, sizeof path, dir, name) || !pathjoin(tmp, sizeof tmp, dir, ".prefs-XXXXXX"))
        return 0;
    int fd = mkstemp(tmp);
    if (fd < 0)
        return 0;
    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        unlink(tmp);
        return 0;
    }
    int ok = fputs(value, f) >= 0 && fflush(f) == 0 && fsync(fd) == 0;
    if (fclose(f) != 0)
        ok = 0;
    if (!ok || rename(tmp, path) != 0) {
        unlink(tmp);
        return 0;
    }
    return 1;
}
static void note(const char *name, const char *body);
static void prefs_name(char *name, size_t cap) { snprintf(name, cap, "profile-%d.ini", profile); }
static void save_prefs(void) {
    char data[1600], name[32];
    prefs_name(name, sizeof name);
    int n = snprintf(data, sizeof data, "light=%d\nclock12=%d\n", light, clock12);
    for (int i = 0; i < 4; i++)
        n += snprintf(data + n, sizeof data - (size_t)n, "tile%d=%s\n", i, tile_ids[i]);
    for (int i = 0; i < 6; i++)
        n += snprintf(data + n, sizeof data - (size_t)n, "dock%d=%s\n", i, dock_ids[i]);
    if (!replace_file(state, name, data))
        note("Storage", "Could not save this profile to the microSD.");
}
static void load_prefs(void) {
    light = clock12 = 0;
    memset(tile_ids, 0, sizeof tile_ids);
    copy(tile_ids[0], 80, "torrent");
    for (int i = 0; i < 6; i++)
        copy(dock_ids[i], 80, default_dock[i]);
    char name[32], line[160];
    prefs_name(name, sizeof name);
    FILE *f = atfile(state, name, "r");
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        strip(line);
        char *v = strchr(line, '=');
        if (!v)
            continue;
        *v++ = 0;
        if (!strcmp(line, "light"))
            light = !strcmp(v, "1");
        else if (!strcmp(line, "clock12"))
            clock12 = !strcmp(v, "1");
        else if (strlen(line) == 5 && !strncmp(line, "tile", 4) && line[4] >= '0' && line[4] <= '3')
            copy(tile_ids[line[4] - '0'], 80, v);
        else if (strlen(line) == 5 && !strncmp(line, "dock", 4) && line[4] >= '0' &&
                 line[4] <= '5' && item_index(v) >= 0)
            copy(dock_ids[line[4] - '0'], 80, v);
    }
    fclose(f);
}
static Page page(void) { return nav[depth].page; }
static void push(Page next) {
    int limit = next == QUICK || quick_depth ? (int)(sizeof nav / sizeof nav[0]) : REGULAR_DEPTH;
    if (depth + 1 < limit)
        nav[++depth] = (Position){next, 0};
    dirty = 1;
}
static void back(void) {
    if (page() == QUICK)
        quick_depth = 0;
    if (page() == KEYBOARD)
        memset(pass, 0, sizeof pass);
    if (depth)
        depth--;
    dirty = 1;
}
static void toggle_quick(void) {
    if (quick_depth) {
        int origin = quick_depth - 1;
        while (depth > origin)
            back();
    } else {
        push(QUICK);
        quick_depth = depth;
    }
}
static void note(const char *name, const char *body) {
    copy(info_title, sizeof info_title, name);
    copy(info_text, sizeof info_text, body);
    push(DETAIL);
}
static void row(const char *label, const char *value, KzIcon icon, Action act, int arg,
                const char *data) {
    if (row_count >= MAX_ROWS)
        return;
    Row *r = &rows[row_count++];
    copy(r->label, sizeof r->label, label);
    copy(r->value, sizeof r->value, value);
    copy(r->data, sizeof r->data, data);
    r->action = act;
    r->arg = arg;
    r->icon = icon;
}
static void window_text(const char *heading, const char *description) {
    copy(title, sizeof title, heading);
    copy(detail, sizeof detail, description);
}
static int net_request(const char *msg) {
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    if (!pathjoin(addr.sun_path, sizeof addr.sun_path, netdir, "control"))
        return 0;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return 0;
    size_t n = strlen(msg);
    int ok = connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0 &&
             send(fd, msg, n, MSG_NOSIGNAL) == (ssize_t)n;
    close(fd);
    return ok;
}
static void files_as_rows(const char *folder, const char *ext, Action action) {
    char dir[PATH_MAX];
    if (!pathjoin(dir, sizeof dir, state, folder))
        return;
    struct dirent **list;
    int n = scandir(dir, &list, NULL, alphasort);
    for (int i = 0; i < n; i++) {
        const char *name = list[i]->d_name;
        if (filename_ok(name) && suffix(name, ext))
            row(name, "", action == SHOW_PHOTO ? KZ_ICON_ALBUM : KZ_ICON_DOWNLOAD, action, 0, name);
        free(list[i]);
    }
    if (n >= 0)
        free(list);
}
static void wifi_rows(void) {
    firstline(netdir, "status", detail, sizeof detail);
    row("Scan again", "", KZ_ICON_NETWORK, SCAN, 0, NULL);
    FILE *f = atfile(netdir, "networks.tsv", "r");
    char line[640];
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        strip(line);
        char *parts[4], *p = line;
        int n = 0;
        while (n < 4) {
            parts[n++] = p;
            char *tab = strchr(p, '\t');
            if (!tab)
                break;
            *tab = 0;
            p = tab + 1;
        }
        if (n != 4)
            continue;
        int connected = !strcmp(parts[3], "online") || !strcmp(parts[3], "ready");
        int personal = !strcmp(parts[2], "password");
        row(parts[1],
            connected  ? "Connected"
            : personal ? "Password"
                       : parts[2],
            KZ_ICON_NETWORK, connected || (!personal && strcmp(parts[2], "open")) ? NONE : JOIN,
            personal, parts[0]);
    }
    fclose(f);
}
static void build(void) {
    memset(&scene, 0, sizeof scene);
    row_count = 0;
    scene.light = light;
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    strftime(clock_text, sizeof clock_text, clock12 ? "%I:%M %p" : "%H:%M", &timeinfo);
    scene.clock = clock_text;
    scene.tile_focus = in_dock ? -1 : tile;
    scene.tool_focus = in_dock ? dock : -1;
    for (int i = 0; i < 4; i++) {
        int k = item_index(tile_ids[i]);
        if (k >= 0)
            scene.tiles[i] = (KzTile){items[k].label, items[k].icon};
    }
    for (int i = 0; i < 6; i++) {
        int k = item_index(dock_ids[i]);
        scene.tools[i] = k >= 0 ? items[k].icon : KZ_ICON_NONE;
    }
    int selected = item_index(in_dock ? dock_ids[dock] : tile_ids[tile]);
    scene.selected_title = selected >= 0 ? items[selected].label : "Empty slot";
    scene.left_hint = "X   Switch user";
    scene.right_hint = selected >= 0 ? "A   Open" : "A   Add";
    if (page() == HOME)
        return;
    scene.left_hint = "B   Back";
    scene.right_hint = "A   Select";
    switch (page()) {
    case TORRENT:
        window_text("KEFIRIZ Torrent",
                    "Homebrew downloads run in the background while you use KEFIRIZ OS.");
        row("Transfer queue", "", KZ_ICON_DOWNLOAD, OPEN, QUEUE, NULL);
        row("2048", "Switch homebrew", KZ_ICON_SOFTWARE, ENQUEUE, 1, "2048.torrent");
        row("Minesweeper", "Switch homebrew", KZ_ICON_SOFTWARE, ENQUEUE, 1, "minesweeper.torrent");
        row("SudokuNX", "Switch homebrew", KZ_ICON_SOFTWARE, ENQUEUE, 1, "sudoku.torrent");
        row("Import a torrent", "", KZ_ICON_LIBRARY, OPEN, IMPORT, NULL);
        break;
    case QUEUE: {
        window_text("Transfer queue", "Files are saved in kefiriz/downloads on the Linux volume.");
        FILE *f = atfile(state, "transfers.tsv", "r");
        char line[300];
        if (f) {
            while (fgets(line, sizeof line, f)) {
                strip(line);
                char *v = strchr(line, '\t');
                if (v) {
                    *v++ = 0;
                    row(line, v, KZ_ICON_DOWNLOAD, NONE, 0, NULL);
                }
            }
            fclose(f);
        }
        if (firstline(state, "transfer-error", line, sizeof line))
            row("Transfer notice", line, KZ_ICON_DOWNLOAD, NONE, 0, NULL);
        if (!row_count)
            row("No transfers yet", "", KZ_ICON_DOWNLOAD, NONE, 0, NULL);
        break;
    }
    case IMPORT:
        window_text("Import a torrent",
                    "Choose a .torrent file from kefiriz/inbox on the Linux volume.");
        files_as_rows("inbox", ".torrent", ENQUEUE);
        if (!row_count)
            row("Inbox is empty", "", KZ_ICON_LIBRARY, NONE, 0, NULL);
        break;
    case LIBRARY:
        window_text("Library",
                    "Choose a feature to open it. Add your own actions in kefiriz/actions.");
        for (int i = 0; i < item_count; i++)
            if (items[i].page != LIBRARY)
                row(items[i].label, "", items[i].icon, OPEN, -1, items[i].id);
        break;
    case ALBUM:
        window_text("Album", "Press Y on any screen to save a picture.");
        row("Capture HOME", "", KZ_ICON_ALBUM, CAPTURE, 0, NULL);
        files_as_rows("album", ".ppm", SHOW_PHOTO);
        break;
    case PADS: {
        window_text("Controllers", "Press a button to check the current input.");
        static const char *names[] = {"B",    "Y",     "Minus",   "Plus",   "Up", "Down",
                                      "Left", "Right", "A",       "X",      "L",  "R",
                                      "ZL",   "ZR",    "L stick", "R stick"};
        char live[128] = {0};
        for (int i = 0; i < 16; i++) {
            if (previous & (1u << i)) {
                size_t used = strlen(live);
                snprintf(live + used, sizeof live - used, "%s%s", used ? " + " : "", names[i]);
            }
        }
        row("Gamepad input", *live ? live : "Ready", KZ_ICON_CONTROLLER, NONE, 0, NULL);
        row("Touch input", touch_down ? "Pressed" : "Ready", KZ_ICON_CONTROLLER, NONE, 0, NULL);
        break;
    }
    case SETTINGS:
        window_text("Settings", "KEFIRIZ OS " KF_OS_VERSION);
        row("Appearance", light ? "Porcelain" : "Graphite", KZ_ICON_SETTINGS, THEME, 0, NULL);
        row("Clock format", clock12 ? "12-hour" : "24-hour", KZ_ICON_SETTINGS, CLOCK, 0, NULL);
        row("Customize dock", "6 buttons", KZ_ICON_LIBRARY, OPEN, DOCK, NULL);
        row("Wi-Fi", "", KZ_ICON_NETWORK, OPEN, WIFI, NULL);
        row("Feature guide", KF_OS_VERSION, KZ_ICON_SOFTWARE, OPEN, GUIDE, NULL);
        break;
    case QUICK: {
        window_text("Quick settings", "Press R + ZR again to return to your window.");
        row("Return", "", KZ_ICON_BACK, DO_POWER, 0, NULL);
        row("Appearance", light ? "Porcelain" : "Graphite", KZ_ICON_SETTINGS, THEME, 0, NULL);
        row("Clock format", clock12 ? "12-hour" : "24-hour", KZ_ICON_SETTINGS, CLOCK, 0, NULL);
        row("Transfer queue", "", KZ_ICON_DOWNLOAD, OPEN, QUEUE, NULL);
        struct statvfs storage;
        if (statvfs(state, &storage) == 0) {
            char space[48];
            double gib = (double)storage.f_bavail * storage.f_frsize / (1024.0 * 1024.0 * 1024.0);
            snprintf(space, sizeof space, "%.1f GiB free", gib);
            row("Storage", space, KZ_ICON_LIBRARY, NONE, 0, NULL);
        }
        break;
    }
    case DOCK:
        window_text("Customize dock", "Choose a position and assign a feature or your own action.");
        for (int i = 0; i < 6; i++) {
            char label[40];
            snprintf(label, sizeof label, "Button %d", i + 1);
            int k = item_index(dock_ids[i]);
            row(label, k >= 0 ? items[k].label : "Empty", k >= 0 ? items[k].icon : KZ_ICON_NONE,
                EDIT_SLOT, i, NULL);
        }
        break;
    case PICK:
        window_text(edit_dock ? "Choose dock button" : "Pin to HOME",
                    "Each profile has its own HOME tiles and dock.");
        if (!edit_dock)
            row("Leave empty", "?", KZ_ICON_NONE, CHOOSE, -1, NULL);
        for (int i = 0; i < item_count; i++)
            row(items[i].label, "", items[i].icon, CHOOSE, i, NULL);
        break;
    case PROFILES:
        window_text("Switch user",
                    "Appearance, clock, tiles and dock are saved for each local profile.");
        for (int i = 1; i <= 3; i++) {
            char label[32];
            snprintf(label, sizeof label, "Player %d", i);
            row(label, i == profile ? "Active" : "", KZ_ICON_PROFILES, PROFILE, i, NULL);
        }
        break;
    case POWER:
        window_text("Power", "Restart to choose KEFIRIZ OS or Meridian in the boot menu.");
        row("Restart to boot menu", "", KZ_ICON_POWER, POWER_ACTION, 0, "reboot");
        row("Shut down", "", KZ_ICON_POWER, POWER_ACTION, 0, "poweroff");
        break;
    case CONFIRM:
        window_text(!strcmp(power_op, "reboot") ? "Restart the console?" : "Shut down the console?",
                    "Transfers resume the next time you open KEFIRIZ OS.");
        row("Cancel", "", KZ_ICON_BACK, DO_POWER, 0, NULL);
        row("Confirm", "", KZ_ICON_POWER, DO_POWER, 1, NULL);
        break;
    case GUIDE: {
        static const char *heads[] = {"Welcome to KEFIRIZ " KF_OS_VERSION, "Quick settings",
                                      "Make HOME yours"};
        static const char *texts[] = {
            "Your desktop now runs directly on Linux. Touch a tile or press A to open it. B goes back.",
            "Hold R and press ZR in any KEFIRIZ window to change appearance or check downloads.",
            "Plus assigns HOME buttons. X switches profiles. Choose Power to restart into Meridian for Switch games."};
        window_text(heads[tour_step], texts[tour_step]);
        row(tour_step == 2 ? "Open HOME" : "Continue",
            tour_step == 0   ? "1 / 3"
            : tour_step == 1 ? "2 / 3"
                             : "3 / 3",
            KZ_ICON_PLAY, GUIDE_NEXT, 0, NULL);
        if (tour_step < 2)
            row("Skip guide", "", KZ_ICON_BACK, GUIDE_NEXT, 1, NULL);
        break;
    }
    case WIFI:
    case KEYBOARD:
        window_text("Wi-Fi", "Choose Scan again to find networks.");
        wifi_rows();
        break;
    case DETAIL:
        window_text(info_title, info_text);
        row("Close", "", KZ_ICON_BACK, DO_POWER, 0, NULL);
        break;
    case PHOTO:
        window_text("Album", selected_photo);
        break;
    default:
        break;
    }
    if (nav[depth].focus >= row_count)
        nav[depth].focus = row_count ? row_count - 1 : 0;
    for (int i = 0; i < row_count; i++)
        rendered[i] = (KzRow){rows[i].label, rows[i].value, rows[i].icon, rows[i].action == NONE};
    scene.window_title = title;
    scene.window_detail = detail;
    scene.rows = rendered;
    scene.row_count = row_count;
    scene.row_focus = nav[depth].focus;
}

static void show_photo(void) {
    char name[180];
    snprintf(name, sizeof name, "album/%s", selected_photo);
    FILE *f = atfile(state, name, "rb");
    if (!f) {
        back();
        note("Album", "This picture could not be opened.");
        return;
    }
    char header[32];
    const char expected[] = "P6\n640 360\n255\n";
    if (fread(header, 1, sizeof expected - 1, f) != sizeof expected - 1 ||
        memcmp(header, expected, sizeof expected - 1)) {
        fclose(f);
        back();
        note("Album", "This image is not a KEFIRIZ capture.");
        return;
    }
    unsigned char scan[640 * 3];
    for (int y = 0; y < 360; y++) {
        if (fread(scan, 1, sizeof scan, f) != sizeof scan) {
            fclose(f);
            back();
            note("Album", "This picture is incomplete.");
            return;
        }
        uint32_t *line = frame + 2 * y * KZ_WIDTH;
        for (int x = 0; x < 640; x++) {
            uint32_t c =
                (uint32_t)scan[x * 3] << 16 | (uint32_t)scan[x * 3 + 1] << 8 | scan[x * 3 + 2];
            line[x * 2] = line[x * 2 + 1] = c;
        }
        memcpy(line + KZ_WIDTH, line, KZ_WIDTH * sizeof *line);
    }
    fclose(f);
}
static void paint(void) {
    ++frame_generation;
    dirty = 0;
    build();
    if (page() == PHOTO) {
        show_photo();
        return;
    }
    kz_draw(frame, KZ_WIDTH, &scene);
    if (page() == KEYBOARD) {
        size_t n = strlen(pass);
        memset(password_mask, '*', n);
        password_mask[n] = 0;
        kz_keyboard(frame, KZ_WIDTH, light, password_mask, keysets[keyboard_mode], key_index);
    }
}
static int capture(void) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    struct tm t;
    localtime_r(&now.tv_sec, &t);
    char date[32], name[96], path[PATH_MAX];
    strftime(date, sizeof date, "%Y-%m-%d_%H-%M-%S", &t);
    snprintf(name, sizeof name, "album/%s-%03ld.ppm", date, now.tv_nsec / 1000000);
    if (!pathjoin(path, sizeof path, state, name))
        return 0;
    FILE *f = fopen(path, "wbx");
    if (!f)
        return 0;
    int ok = fputs("P6\n640 360\n255\n", f) >= 0;
    unsigned char line[640 * 3];
    for (int y = 0; y < 360 && ok; y++) {
        for (int x = 0; x < 640; x++) {
            uint32_t c = frame[2 * y * KZ_WIDTH + 2 * x];
            line[3 * x] = c >> 16;
            line[3 * x + 1] = c >> 8;
            line[3 * x + 2] = c;
        }
        ok = fwrite(line, 1, sizeof line, f) == sizeof line;
    }
    if (fclose(f) != 0)
        ok = 0;
    if (!ok)
        unlink(path);
    return ok;
}
static int queue_file(const Row *r) {
    char name[340], dst[PATH_MAX], dir[PATH_MAX], tmp[PATH_MAX];
    if (!filename_ok(r->data) || !suffix(r->data, ".torrent"))
        return 0;
    snprintf(name, sizeof name, "%s/%s", r->arg ? "catalog" : "inbox", r->data);
    FILE *in = atfile(r->arg ? systemdir : state, name, "rb");
    if (!in)
        return 0;
    if (!pathjoin(dir, sizeof dir, state, "queue") || !pathjoin(dst, sizeof dst, dir, r->data) ||
        !pathjoin(tmp, sizeof tmp, dir, ".request-XXXXXX")) {
        fclose(in);
        return 0;
    }
    int fd = mkstemp(tmp);
    if (fd < 0) {
        fclose(in);
        return 0;
    }
    FILE *out = fdopen(fd, "wb");
    if (!out) {
        close(fd);
        unlink(tmp);
        fclose(in);
        return 0;
    }
    char buf[8192];
    size_t n, total = 0;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof buf, in))) {
        total += n;
        if (total > 4 * 1024 * 1024 || fwrite(buf, 1, n, out) != n) {
            ok = 0;
            break;
        }
    }
    if (ferror(in) || !total || fflush(out) != 0 || fsync(fd) != 0)
        ok = 0;
    fclose(in);
    if (fclose(out) != 0)
        ok = 0;
    if (!ok || rename(tmp, dst) != 0) {
        unlink(tmp);
        return 0;
    }
    return 1;
}
static void launch_action(const Item *item) {
    if (action_pid) {
        note("Custom action", "Another action is still running.");
        return;
    }
    char dir[PATH_MAX], script[PATH_MAX], output[PATH_MAX];
    struct stat st;
    if (!pathjoin(dir, sizeof dir, state, "actions") ||
        !pathjoin(script, sizeof script, dir, item->id + 4) ||
        !pathjoin(output, sizeof output, state, "action-output.txt") || lstat(script, &st) != 0 ||
        !S_ISREG(st.st_mode)) {
        note("Custom action", "This action could not be found.");
        return;
    }
    posix_spawn_file_actions_t files;
    int err = posix_spawn_file_actions_init(&files);
    if (!err) {
        err = posix_spawn_file_actions_addopen(&files, STDOUT_FILENO, output,
                                               O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (!err)
            err = posix_spawn_file_actions_adddup2(&files, STDOUT_FILENO, STDERR_FILENO);
        char *args[] = {"sh", script, NULL};
        pid_t pid = 0;
        if (!err)
            err = posix_spawn(&pid, "/bin/sh", &files, NULL, args, environ);
        if (!err)
            action_pid = pid;
        posix_spawn_file_actions_destroy(&files);
    }
    note(item->label, err ? "Could not start the action." : "Running...");
}
static void open_page(Page p) {
    if (p == GUIDE)
        tour_step = 0;
    if (p == WIFI)
        net_request("scan\n");
    if (p == LIBRARY)
        load_items();
    push(p);
}
static void open_item(const char *id) {
    int k = item_index(id);
    if (k < 0) {
        edit_dock = 0;
        slot = tile;
        push(PICK);
        return;
    }
    if (!strncmp(items[k].id, "app/", 4))
        launch_action(&items[k]);
    else
        open_page(items[k].page);
}
static void finish_guide(void) {
    back();
    if (!replace_file(state, "guide-" KF_OS_VERSION, "seen\n"))
        note("Storage", "The guide completion could not be saved.");
}
static void submit_network(void) {
    char msg[320];
    snprintf(msg, sizeof msg, "connect\t%s\t%s\n", wifi_id, pass);
    int ok = net_request(msg);
    memset(msg, 0, sizeof msg);
    memset(pass, 0, sizeof pass);
    if (page() == KEYBOARD)
        back();
    if (!ok)
        note("Wi-Fi", "The wireless service is unavailable. Try again shortly.");
}
static void key_press(void) {
    size_t n = strlen(pass);
    if (key_index < 40 && (size_t)key_index < strlen(keysets[keyboard_mode]) && n < 64) {
        pass[n] = keysets[keyboard_mode][key_index];
        pass[n + 1] = 0;
    }
    if (key_index == 40)
        keyboard_mode = keyboard_mode == 1 ? 0 : 1;
    if (key_index == 41)
        keyboard_mode = keyboard_mode == 2 ? 0 : 2;
    if (key_index == 42 && n < 64) {
        pass[n] = ' ';
        pass[n + 1] = 0;
    }
    if (key_index == 43 && n)
        pass[n - 1] = 0;
    if (key_index == 44)
        submit_network();
    dirty = 1;
}
static void activate(void) {
    if (page() == HOME) {
        open_item(in_dock ? dock_ids[dock] : tile_ids[tile]);
        return;
    }
    if (page() == KEYBOARD) {
        key_press();
        return;
    }
    if (page() == PHOTO) {
        back();
        return;
    }
    int focus = nav[depth].focus;
    if (focus < 0 || focus >= row_count)
        return;
    Row r = rows[focus];
    switch (r.action) {
    case OPEN:
        if (r.arg < 0)
            open_item(r.data);
        else
            open_page((Page)r.arg);
        break;
    case THEME:
        light = !light;
        save_prefs();
        break;
    case CLOCK:
        clock12 = !clock12;
        save_prefs();
        break;
    case EDIT_SLOT:
        slot = r.arg;
        edit_dock = 1;
        push(PICK);
        break;
    case CHOOSE:
        copy(edit_dock ? dock_ids[slot] : tile_ids[slot], 80, r.arg >= 0 ? items[r.arg].id : "");
        back();
        save_prefs();
        break;
    case PROFILE: {
        char n[8];
        snprintf(n, sizeof n, "%d\n", r.arg);
        if (!replace_file(state, "active-profile", n)) {
            note("Profiles", "Could not save the active profile.");
            break;
        }
        profile = r.arg;
        load_prefs();
        depth = 0;
        tile = dock = in_dock = 0;
        break;
    }
    case POWER_ACTION:
        copy(power_op, sizeof power_op, r.data);
        push(CONFIRM);
        break;
    case DO_POWER:
        if (r.arg) {
            FILE *f = atfile(base("KEFIRIZ_POWER_DIR", "/run/kefiriz-power"), power_op, "w");
            if (f) {
                fclose(f);
                back();
                note("Power", "Power request sent.");
            } else
                note("Power", "The power service is unavailable.");
        } else
            back();
        break;
    case GUIDE_NEXT:
        if (r.arg || tour_step == 2)
            finish_guide();
        else
            tour_step++;
        break;
    case SCAN:
        if (!net_request("scan\n"))
            note("Wi-Fi", "The wireless service is unavailable. Try again shortly.");
        break;
    case JOIN:
        copy(wifi_id, sizeof wifi_id, r.data);
        memset(pass, 0, sizeof pass);
        if (r.arg) {
            keyboard_mode = key_index = 0;
            push(KEYBOARD);
        } else
            submit_network();
        break;
    case ENQUEUE:
        if (queue_file(&r))
            note("Added to queue", "Open Transfer queue to follow this download.");
        else
            note("Transfer queue", "Could not queue this torrent. Check the file and free space.");
        break;
    case SHOW_PHOTO:
        copy(selected_photo, sizeof selected_photo, r.data);
        push(PHOTO);
        break;
    case CAPTURE: {
        int save_depth = depth;
        depth = 0;
        build();
        kz_draw(frame, KZ_WIDTH, &scene);
        depth = save_depth;
        if (!capture())
            note("Album", "Could not save this capture.");
        break;
    }
    default:
        break;
    }
    dirty = 1;
}
static void move_focus(int dx, int dy) {
    if (page() == HOME) {
        int *i = in_dock ? &dock : &tile, count = in_dock ? 6 : 4;
        if (dx)
            *i = (*i + dx + count) % count;
        if (dy > 0 && !in_dock) {
            dock = (tile * 5 + 1) / 3;
            in_dock = 1;
        }
        if (dy < 0 && in_dock) {
            tile = (dock * 3 + 2) / 5;
            in_dock = 0;
        }
    } else if (page() == KEYBOARD) {
        int col = key_index < 40 ? key_index % 10 : (key_index - 40) * 2;
        int y = key_index < 40 ? key_index / 10 : 4;
        if (dx) {
            if (y == 4)
                col = (col + dx * 2 + 10) % 10;
            else
                col = (col + dx + 10) % 10;
        }
        y = (y + dy + 5) % 5;
        key_index = y == 4 ? 40 + col / 2 : y * 10 + col;
    } else if (row_count) {
        int *i = &nav[depth].focus;
        if (dy)
            *i = (*i + dy + row_count) % row_count;
        if (dx) {
            *i += dx * 5;
            if (*i < 0)
                *i = 0;
            if (*i >= row_count)
                *i = row_count - 1;
        }
    }
    dirty = 1;
}
static void go_back(void) {
    if (page() == GUIDE)
        finish_guide();
    else
        back();
}
static void press(unsigned edges) {
#define BUTTON(id) (edges & (1u << RETRO_DEVICE_ID_JOYPAD_##id))
    if (BUTTON(B)) {
        go_back();
        return;
    }
    if (BUTTON(Y) && page() != KEYBOARD) {
        if (!capture())
            note("Album", "Could not save this capture.");
        else
            note("Album", "Screenshot saved.");
        return;
    }
    if (BUTTON(X) && page() == HOME) {
        push(PROFILES);
        return;
    }
    if (BUTTON(START) && page() == HOME) {
        slot = in_dock ? dock : tile;
        edit_dock = in_dock;
        load_items();
        push(PICK);
        return;
    }
    if (BUTTON(UP))
        move_focus(0, -1);
    else if (BUTTON(DOWN))
        move_focus(0, 1);
    else if (BUTTON(LEFT))
        move_focus(-1, 0);
    else if (BUTTON(RIGHT))
        move_focus(1, 0);
    else if (BUTTON(A))
        activate();
#undef BUTTON
}
static int hit(int x, int y) {
    if (page() == PHOTO)
        return -2;
    if (page() == KEYBOARD) {
        int k = kz_key_hit(x, y);
        return k >= 0 ? 1000 + k : -1;
    }
    return kz_hit(&scene, x, y);
}
static void touch(int target) {
    if (target == -2) {
        go_back();
        return;
    }
    if (target < 0)
        return;
    if (target >= 1000) {
        key_index = target - 1000;
        key_press();
        return;
    }
    if (page() == HOME) {
        in_dock = target >= 10;
        if (in_dock)
            dock = target - 10;
        else
            tile = target;
    } else if (target >= 20)
        nav[depth].focus = target - 20;
    else
        return;
    activate();
}
void retro_set_environment(retro_environment_t cb) {
    environment = cb;
    bool ok = true;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &ok);
}
void retro_set_video_refresh(retro_video_refresh_t cb) { video = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { poll_input = cb; }
void retro_set_input_state(retro_input_state_t cb) { input = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { (void)cb; }
unsigned retro_api_version(void) { return RETRO_API_VERSION; }
void retro_get_system_info(struct retro_system_info *out) {
    *out = (struct retro_system_info){.library_name = "KEFIRIZ OS",
                                      .library_version = KF_OS_VERSION,
                                      .valid_extensions = "kfstart",
                                      .need_fullpath = false};
}
void retro_get_system_av_info(struct retro_system_av_info *out) {
    *out = (struct retro_system_av_info){
        .geometry = {KZ_WIDTH, KZ_HEIGHT, KZ_WIDTH, KZ_HEIGHT, 16.0f / 9.0f},
        .timing = {30.0, 48000.0}};
}
void retro_set_controller_port_device(unsigned port, unsigned device) {
    (void)port;
    (void)device;
}
void retro_init(void) {
    copy(state, sizeof state, base("KEFIRIZ_STATE_DIR", "/storage/kefiriz"));
    copy(systemdir, sizeof systemdir, base("KEFIRIZ_SYSTEM_DIR", "/usr/lib/kefiriz"));
    copy(netdir, sizeof netdir, base("KEFIRIZ_NETWORK_DIR", "/run/kefiriz-network"));
    mkdir(state, 0700);
    const char *dirs[] = {"album", "actions", "inbox", "queue", "downloads"};
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        char p[PATH_MAX];
        if (pathjoin(p, sizeof p, state, dirs[i]))
            mkdir(p, 0700);
    }
    profile = 1;
    char active[8];
    if (firstline(state, "active-profile", active, sizeof active) && active[0] >= '1' &&
        active[0] <= '3')
        profile = active[0] - '0';
    load_items();
    load_prefs();
    depth = tile = dock = in_dock = frames = tour_step = touch_down = 0;
    nav[0] = (Position){HOME, 0};
    quick_depth = 0;
    previous = 0;
    dirty = 1;
    if (!firstline(state, "guide-" KF_OS_VERSION, active, sizeof active) || strcmp(active, "seen"))
        open_page(GUIDE);
    enum retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
    pixel_ready = environment && environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);
}
void retro_deinit(void) { memset(pass, 0, sizeof pass); }
bool retro_load_game(const struct retro_game_info *game) {
    (void)game;
    return pixel_ready;
}
void retro_reset(void) {
    depth = 0;
    quick_depth = 0;
    memset(pass, 0, sizeof pass);
    dirty = 1;
}
void retro_run(void) {
    if (!video || !poll_input || !input)
        return;
    if (dirty)
        paint();
    poll_input();
    unsigned current = 0;
    for (unsigned i = 0; i < 16; i++)
        if (input(0, RETRO_DEVICE_JOYPAD, 0, i))
            current |= 1u << i;
    unsigned edges = current & ~previous;
    const unsigned shoulders = (1u << RETRO_DEVICE_ID_JOYPAD_R) | (1u << RETRO_DEVICE_ID_JOYPAD_R2);
    bool shortcut = (current & shoulders) == shoulders && (edges & shoulders);
    if (current != previous && page() == PADS)
        dirty = 1;
    previous = current;
    if (shortcut)
        toggle_quick();
    else
        press(edges);
    int down = input(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED);
    if (shortcut)
        touch_start = -1;
    else if (down) {
        int px = input(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X),
            py = input(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
        int target = hit((px + 32768) * KZ_WIDTH / 65536, (py + 32768) * KZ_HEIGHT / 65536);
        if (!touch_down)
            touch_start = target;
        else if (target != touch_start)
            touch_start = -1;
    } else if (touch_down && !edges)
        touch(touch_start);
    if (!!down != !!touch_down && page() == PADS)
        dirty = 1;
    touch_down = !!down;
    if (++frames % 30 == 0) {
        int minute = (int)(time(NULL) / 60);
        if (minute != last_clock) {
            dirty = 1;
            last_clock = minute;
        }
        if (page() == WIFI || page() == QUEUE)
            dirty = 1;
        if (action_pid) {
            int status;
            pid_t done = waitpid(action_pid, &status, WNOHANG);
            if (done != 0) {
                action_pid = 0;
                if (page() == DETAIL) {
                    if (!firstline(state, "action-output.txt", info_text, sizeof info_text))
                        copy(info_text, sizeof info_text,
                             done > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0
                                 ? "Action completed."
                                 : "Action failed.");
                    dirty = 1;
                }
            }
        }
    }
    if (dirty)
        paint();
    video(frame, KZ_WIDTH, KZ_HEIGHT, KZ_WIDTH * 4);
}
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
void retro_unload_game(void) {}
bool retro_load_game_special(unsigned t, const struct retro_game_info *i, size_t n) {
    (void)t;
    (void)i;
    (void)n;
    return false;
}
size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *p, size_t n) {
    (void)p;
    (void)n;
    return false;
}
bool retro_unserialize(const void *p, size_t n) {
    (void)p;
    (void)n;
    return false;
}
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned i, bool enabled, const char *code) {
    (void)i;
    (void)enabled;
    (void)code;
}
void *retro_get_memory_data(unsigned id) {
    (void)id;
    return NULL;
}
size_t retro_get_memory_size(unsigned id) {
    (void)id;
    return 0;
}
