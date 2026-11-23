#include "engine.h"
#include "engine_platform.h"
#include "publisher.h"
#include "crypto/monocypher-ed25519.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __SWITCH__
#define SD "sdmc:/"
#else
#define SD ""
#endif
#define OTA SD "config/kefirjs/ota"
#define PREP OTA "/preparing"
#define ACTIVE OTA "/active"
#define OFFLINE SD "config/kefirjs/offline/"
#define MARKER "config/kefiriz/release.txt"
#define UPDATER "switch/KEFIRJS-Updater/KEFIRJS-Updater.nro"
#define SYSAPP "switch/KEFIRIZ-System/KEFIRIZ-System.nro"
#define BASE_HASH "1fca4f0a1d2a214d7d92c0574827066e59bbfc4e230750a800654534f1820ac2"
#define MAX_BLOB (32u * 1024u * 1024u)
#define MAX_FEED 8192u
#define COUNT(x) (sizeof(x) / sizeof((x)[0]))

static const char *const allowed[] = { "hbmenu.nro", UPDATER, SYSAPP, MARKER };
static const struct { const char *path, *hashes; } retired[] = {
#include "retired.inc"
};
enum { MAX_ROWS = COUNT(allowed) + COUNT(retired) };
typedef struct { const char *path; unsigned size; char sha[65]; } Asset;
typedef struct {
    char version[24], notes[1024];
    Asset files[COUNT(allowed)];
    size_t count, bytes;
} Manifest;
typedef struct { const char *path; char old[65], next[65]; } Row;
typedef struct { Row rows[MAX_ROWS]; size_t count; } Journal;
typedef struct { char bytes[MAX_FEED + 1]; size_t used; } Buffer;
static Manifest selection;
static Buffer signed_feed;
static bool offline;

static int fail(KfUpdate *u, const char *text) {
    snprintf(u->message, sizeof(u->message), "%s", text);
    return KF_ERROR;
}
static bool digits(const char *s, unsigned max, unsigned *value) {
    if (!*s || (*s == '0' && s[1])) return false;
    unsigned n = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return false;
        unsigned d = (unsigned)(*s - '0');
        if (d > max || n > (max - d) / 10) return false;
        n = 10 * n + d;
    }
    *value = n;
    return true;
}
static bool version(const char *s, unsigned *v) {
    size_t len = strlen(s);
    if (!len || len >= 24) return false;
    char copy[24];
    memcpy(copy, s, len + 1);
    char *p = copy;
    for (size_t i = 0; i < 3; i++) {
        char *dot = strchr(p, '.');
        if ((dot != NULL) != (i < 2)) return false;
        if (dot) *dot = 0;
        if (!digits(p, 9999, &v[i])) return false;
        if (dot) p = dot + 1;
    }
    return v[0] != 0;
}
static int compare(const char *left, const char *right) {
    unsigned a[3], b[3];
    if (!version(left, a) || !version(right, b)) return -2;
    for (size_t i = 0; i < 3; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
static bool unhex(const char *s, size_t size, uint8_t *out) {
    if (strlen(s) != size * 2) return false;
    for (size_t i = 0; i < 2 * size; i++) {
        unsigned c = (unsigned char)s[i];
        unsigned v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else return false;
        if (out) {
            if (i & 1) out[i / 2] |= (uint8_t)v;
            else out[i / 2] = (uint8_t)(v << 4);
        }
    }
    return true;
}
static size_t columns(char *p, char **items, size_t n) {
    for (size_t i = 0; i < n; i++) {
        items[i] = p;
        char *tab = strchr(p, '\t');
        if (!tab) return i + 1;
        *tab = 0;
        p = tab + 1;
    }
    return n + 1;
}
static char *next_line(char **cursor) {
    char *line = *cursor, *end = strchr(line, '\n');
    if (!end) return NULL;
    *end = 0;
    *cursor = end + 1;
    return line;
}
static const char *allowed_path(const char *p) {
    for (size_t i = 0; i < COUNT(allowed); i++) if (!strcmp(p, allowed[i])) return allowed[i];
    return NULL;
}
static const char *obsolete(const char *path, const char *hex) {
    if (!unhex(hex, 32, NULL)) return NULL;
    for (size_t i = 0; i < COUNT(retired); i++) {
        if (strcmp(path, retired[i].path)) continue;
        for (const char *p = retired[i].hashes; *p; ) {
            if (!strncmp(p, hex, 64)) return retired[i].path;
            p += 64;
            if (*p == ' ') p++;
        }
    }
    return NULL;
}
static bool parse(const Buffer *input, Manifest *m) {
    const char magic[] = "KEFIRJS-SIGNED-1\n";
    size_t prefix = sizeof(magic) - 1, start = prefix + 129;
    if (input->used <= start || memcmp(input->bytes, magic, prefix) ||
        input->bytes[start-1] != '\n' || input->bytes[input->used-1] != '\n') return false;
    const char *body = input->bytes + start;
    size_t size = input->used - start;
    char hsig[129]; uint8_t sig[64], key[32];
    memcpy(hsig, input->bytes + prefix, 128); hsig[128] = 0;
    if (!unhex(hsig, 64, sig) || !unhex(KF_PUBKEY_HEX, 32, key) ||
        crypto_ed25519_check(sig, key, (const uint8_t *)body, size)) return false;
    for (size_t i = 0; i < size; i++)
        if ((body[i] < 32 || body[i] > 126) && body[i] != '\t' && body[i] != '\n') return false;
    char temp[MAX_FEED + 1];
    memcpy(temp, body, size); temp[size] = 0;
    char *cursor = temp, *line = next_line(&cursor);
    if (!line || strcmp(line, "KEFIRJS-UPDATE-1")) return false;
    line = next_line(&cursor);
    unsigned v[3];
    if (!line || strncmp(line, "version\t", 8) || !version(line + 8, v)) return false;
    memset(m, 0, sizeof(*m)); strcpy(m->version, line + 8);
    line = next_line(&cursor);
    if (!line || strcmp(line, "base\t20.5.0-786")) return false;
    unsigned notes = 0;
    while ((line = next_line(&cursor))) {
        char *f[4];
        size_t n = columns(line, f, COUNT(f));
        if (n == 2 && !strcmp(f[0], "note") && !m->count && ++notes <= 8) {
            size_t len = strlen(f[1]);
            if (!len || len > 112) return false;
            if (m->notes[0]) strcat(m->notes, "\n");
            strcat(m->notes, f[1]);
            continue;
        }
        if (n != 4 || !notes || strcmp(f[0], "file") || m->count == COUNT(m->files)) return false;
        Asset *a = &m->files[m->count];
        a->path = allowed_path(f[1]);
        if (!a->path || !digits(f[2], MAX_BLOB, &a->size) || !a->size ||
            !unhex(f[3], 32, NULL) || m->bytes > MAX_BLOB - a->size) return false;
        for (size_t i = 0; i < m->count; i++) if (a->path == m->files[i].path) return false;
        strcpy(a->sha, f[3]); m->bytes += a->size; m->count++;
    }
    return !*cursor && m->count && !strcmp(m->files[m->count - 1].path, MARKER);
}
static bool mem_sink(const void *p, size_t n, void *user) {
    Buffer *b = user;
    if (n > MAX_FEED - b->used) return false;
    memcpy(b->bytes + b->used, p, n); b->used += n; b->bytes[b->used] = 0;
    return true;
}
static bool file_sink(const void *p, size_t n, void *user) { return fwrite(p, 1, n, user) == n; }
static bool pump(FILE *f, KfSink sink, void *ctx, size_t limit) {
    uint8_t block[8192]; size_t n;
    while ((n = fread(block, 1, sizeof(block), f))) {
        if (n > limit || !sink(block, n, ctx)) return false;
        limit -= n;
    }
    return !ferror(f);
}
static bool from_file(const char *path, KfSink sink, void *ctx, size_t limit) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = pump(f, sink, ctx, limit);
    fclose(f); return ok;
}
static bool read_marker(const char *path, char out[24]) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char line[96];
    size_t n = fread(line, 1, sizeof(line), f);
    bool ok = !ferror(f) && n < sizeof(line) && !memchr(line, 0, n);
    fclose(f);
    if (!ok) return false;
    while (n && (line[n-1] == '\r' || line[n-1] == '\n')) n--;
    line[n] = 0;
    if (strncmp(line, "KEFIRIZ ", 8)) return false;
    char *end = strchr(line + 8, ' '); unsigned v[3];
    if (!end || strcmp(end, " / Horizon edition 786")) return false;
    *end = 0;
    if (!version(line + 8, v)) return false;
    strcpy(out, line + 8); return true;
}
void kf_local(KfUpdate *u) {
    memset(u, 0, sizeof(*u));
    if (!read_marker(SD MARKER, u->current)) strcpy(u->current, "Unknown");
}
int kf_check(KfUpdate *u, KfProgress cb, void *ctx) {
    kf_local(u);
    selection.count = 0; signed_feed.used = 0; offline = false;
    if (cb) cb("Checking GitHub", 0, 0, ctx);
    if (!kf_fetch("updates/stable.kfu", mem_sink, &signed_feed, MAX_FEED, NULL, NULL)) {
        signed_feed.used = 0;
        if (!from_file(OFFLINE "updates/stable.kfu", mem_sink, &signed_feed, MAX_FEED))
            return fail(u, "Could not reach GitHub or find an offline update. Check the connection and system date.");
        offline = true;
    }
    Manifest verified;
    if (!parse(&signed_feed, &verified)) return fail(u, "The update signature or release format is invalid.");
    strcpy(u->latest, verified.version); strcpy(u->notes, verified.notes); u->bytes = verified.bytes;
    int cmp = compare(u->current, u->latest);
    if (cmp == -2 || compare(u->current, "1.1.0") < 0)
        return fail(u, "Install KEFIRIZ Horizon edition 786 with the Windows installer first.");
    if (cmp >= 0) {
        strcpy(u->message, cmp ? "The installed release is newer than this channel." : "You are up to date.");
        return KF_CURRENT;
    }
    selection = verified;
    strcpy(u->message, offline ? "Signed offline update available." : "Signed update available.");
    return KF_AVAILABLE;
}

/* Every directory and file name below is fixed, derived from an integer, or
   validated against the destination table. No path in a feed is used directly. */
static bool folder(const char *path) {
    struct stat st;
#ifdef _WIN32
    if (!mkdir(path)) return true;
#else
    if (!mkdir(path, 0777)) return true;
#endif
    return errno == EEXIST && !stat(path, &st) && S_ISDIR(st.st_mode);
}
static bool parents(const char *path) {
    char name[256];
    if (strlen(path) >= sizeof(name)) return false;
    strcpy(name, path);
    for (char *p = name + strlen(SD); *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        if (!folder(name)) return false;
        *p = '/';
    }
    return true;
}
static bool gone(const char *path) { return remove(path) == 0 || errno == ENOENT; }
static bool regular_hash(const char *path, char out[65]) {
    FILE *in = fopen(path, "rb");
    if (!in) { if (errno == ENOENT) { strcpy(out, "-"); return true; } return false; }
    bool ok = kf_hash(in, out);
    fclose(in); return ok;
}
static bool is_base(void) {
    char hex[65];
    return kf_horizon_ok() && regular_hash(SD "atmosphere/package3", hex) && !strcmp(hex, BASE_HASH);
}
static void member(char out[256], const char *dir, const char *kind, size_t index) {
    snprintf(out, 256, "%s/%s-%02u.bin", dir, kind, (unsigned)index);
}
static bool close_out(FILE *f, bool success) {
    if (success) success = kf_flush(f);
    if (fclose(f)) success = false;
    return success;
}
static bool store_bytes(const char *path, const void *data, size_t count) {
    FILE *f = fopen(path, "wb");
    return f && close_out(f, fwrite(data, 1, count, f) == count);
}
static bool saved(const char *from, const char *to, const char *expected) {
    FILE *out = fopen(to, "wb");
    if (!out) return false;
    bool ok = from_file(from, file_sink, out, MAX_BLOB);
    char actual[65];
    return close_out(out, ok) && regular_hash(to, actual) && !strcmp(actual, expected);
}
static bool clear_work(void) {
    struct stat st;
    if (stat(PREP, &st)) return errno == ENOENT;
    for (size_t i = 0; i < MAX_ROWS; i++) {
        char f[256]; member(f, PREP, "old", i); if (!gone(f)) return false;
        member(f, PREP, "new", i); if (!gone(f)) return false;
    }
    return gone(PREP "/stable.kfu") && gone(PREP "/originals.tsv") &&
           gone(PREP "/payload.bin") && !rmdir(PREP);
}
static bool row_start(Journal *j, const char *path, const char *old, const char *next) {
    if (j->count >= MAX_ROWS) return false;
    Row *r = &j->rows[j->count++];
    r->path = path; strcpy(r->old, old); strcpy(r->next, next);
    return true;
}
static bool record(FILE *f, const Journal *j) {
    if (fputs("KEFIRJS-JOURNAL-1\n", f) == EOF) return false;
    for (size_t i = 0; i < j->count; i++) {
        const Row *r = &j->rows[i];
        if (fprintf(f, "%u\t%s\t%s\t%s\n", (unsigned)i, r->path, r->old, r->next) < 0) return false;
    }
    return true;
}
static bool read_journal(Journal *j, const Manifest *m) {
    Buffer b = {0};
    if (!from_file(ACTIVE "/originals.tsv", mem_sink, &b, MAX_FEED) ||
        memchr(b.bytes, 0, b.used)) return false;
    char *cursor = b.bytes, *line = next_line(&cursor);
    if (!line || strcmp(line, "KEFIRJS-JOURNAL-1")) return false;
    j->count = 0; size_t assets = 0;
    while ((line = next_line(&cursor))) {
        unsigned index; char *f[4];
        if (columns(line, f, 4) != 4 || !digits(f[0], MAX_ROWS, &index) || index != j->count ||
            (strcmp(f[2], "-") && !unhex(f[2], 32, NULL))) return false;
        const char *path = allowed_path(f[1]);
        if (path) {
            if (assets >= m->count || m->files[assets].path != path || strcmp(f[3], m->files[assets].sha)) return false;
            assets++;
        } else if (strcmp(f[3], "-") || !(path = obsolete(f[1], f[2]))) return false;
        for (size_t i = 0; i < j->count; i++) if (!strcmp(j->rows[i].path, path)) return false;
        if (!row_start(j, path, f[2], f[3])) return false;
    }
    return !*cursor && assets == m->count && !strcmp(j->rows[j->count-1].path, MARKER);
}
static bool version_dir(char path[256], const Manifest *m) {
    struct stat st;
    snprintf(path, 256, OTA "/%s", m->version);
    return stat(path, &st) && errno == ENOENT;
}
static bool install_asset(const char *stage, const char *target, size_t index, const Row *r) {
    if (!rename(stage, target)) return true;
    /* libnx fsdev translates Horizon's PathAlreadyExists (0x402) to EEXIST.
       On a different error, including a missing stage, leave the target alone. */
    if (errno != EEXIST || !strcmp(r->old, "-")) return false;
    char swap[256], old[65], next[65];
    member(swap, ACTIVE, "swap", index);
    if (!regular_hash(target, old) || strcmp(old, r->old) ||
        !regular_hash(stage, next) || strcmp(next, r->next) || rename(target, swap)) return false;
    if (rename(stage, target)) {
        /* The previous file can be restored by a same-volume rename even if
           the volume has filled up. The verified backup is also still present. */
        (void)rename(swap, target);
        return false;
    }
    return gone(swap);
}
static bool commit(Journal *j, const Manifest *m, KfProgress cb, void *ctx) {
    char final[256], target[256], stage[256], backup[256], hex[65];
    if (!version_dir(final, m)) return false;
    /* Validate all usable originals and pending stages before resuming writes. */
    for (size_t i = 0; i < j->count; i++) {
        Row *r = &j->rows[i];
        snprintf(target, sizeof(target), SD "%s", r->path);
        if (!regular_hash(target, hex)) return false;
        if (strcmp(r->old, r->next) && strcmp(r->old, "-")) {
            char old[65];
            member(backup, ACTIVE, "old", i);
            if (!regular_hash(backup, old) || strcmp(old, r->old)) return false;
        }
        if (!strcmp(hex, r->next)) continue;
        if (strcmp(hex, "-") && strcmp(hex, r->old)) return false;
        if (strcmp(r->next, "-")) {
            member(stage, ACTIVE, "new", i);
            if (!regular_hash(stage, hex) || strcmp(hex, r->next)) return false;
        }
    }
    for (size_t i = 0; i < j->count; i++) {
        Row *r = &j->rows[i];
        if (cb) cb("Installing update", i, j->count, ctx);
        snprintf(target, sizeof(target), SD "%s", r->path);
        if (!regular_hash(target, hex)) return false;
        if (!strcmp(hex, r->next)) continue;
        if (strcmp(hex, "-") && strcmp(hex, r->old)) return false;
        if (!strcmp(r->next, "-")) { if (!gone(target)) return false; continue; }
        member(stage, ACTIVE, "new", i);
        if (!parents(target)) return false;
        if (!install_asset(stage, target, i, r)) return false;
        if (!regular_hash(target, hex) || strcmp(hex, r->next)) return false;
    }
    if (cb) cb("Finishing update", j->count, j->count, ctx);
    return rename(ACTIVE, final) == 0;
}
static int recover_locked(KfUpdate *u, KfProgress cb, void *ctx) {
    kf_local(u);
    struct stat st;
    if (stat(ACTIVE, &st)) return errno == ENOENT ? KF_CURRENT : fail(u, "Could not read the update journal.");
    Buffer feed = {0}; Manifest m; Journal j;
    if (!from_file(ACTIVE "/stable.kfu", mem_sink, &feed, MAX_FEED) || !parse(&feed, &m) || !read_journal(&j, &m) ||
        !commit(&j, &m, cb, ctx)) return fail(u, "Recovery needs the card files. Keep the card and use the matching Windows installer.");
    kf_local(u); strcpy(u->message, "Recovered the interrupted update."); return KF_RECOVERED;
}
static bool count_space(const Manifest *m, uint64_t *needed) {
    *needed = 2 * (uint64_t)m->bytes + MAX_FEED + 65536;
    for (size_t i = 0; i < m->count + COUNT(retired); i++) {
        const char *p = i < m->count ? m->files[i].path : retired[i-m->count].path;
        char name[256]; struct stat st;
        snprintf(name, sizeof(name), SD "%s", p);
        if (stat(name, &st)) { if (errno != ENOENT) return false; continue; }
        if (!S_ISREG(st.st_mode)) return false;
        if (st.st_size >= 0 && st.st_size <= (long)MAX_BLOB) *needed += (uint64_t)st.st_size;
    }
    return true;
}
static bool extract(FILE *blob, const Asset *a, const char *dest) {
    FILE *f = fopen(dest, "wb"); if (!f) return false;
    uint8_t buf[8192]; unsigned left = a->size;
    while (left) {
        size_t n = left < sizeof(buf) ? left : sizeof(buf);
        if (fread(buf, 1, n, blob) != n || fwrite(buf, 1, n, f) != n) break;
        left -= (unsigned)n;
    }
    char hex[65];
    return close_out(f, left == 0) && regular_hash(dest, hex) && !strcmp(hex, a->sha);
}
static bool prepare(FILE *blob, const Manifest *m, Journal *j, const char *current, KfProgress cb, void *ctx) {
    j->count = 0;
    char target[256], dest[256], old[65];
    for (size_t i = 0; i < m->count; i++) {
        const Asset *a = &m->files[i];
        if (i == m->count - 1) {
            for (size_t k = 0; k < COUNT(retired); k++) {
                snprintf(target, sizeof(target), SD "%s", retired[k].path);
                if (!regular_hash(target, old)) return false;
                if (!obsolete(retired[k].path, old)) continue;
                member(dest, PREP, "old", j->count);
                if (!saved(target, dest, old) || !row_start(j, retired[k].path, old, "-")) return false;
            }
        }
        if (cb) cb("Verifying files", i, m->count, ctx);
        snprintf(target, sizeof(target), SD "%s", a->path);
        if (!regular_hash(target, old)) return false;
        if (!strcmp(current, "1.1.0") && !strcmp(a->path, "hbmenu.nro") && strcmp(old, a->sha) &&
            strcmp(old, "d89d0a70d4844ac43b1d57e65b3787acc9255658f9ec4bf875bec0bee6a28c1d")) return false;
        member(dest, PREP, "new", j->count);
        if (!extract(blob, a, dest)) return false;
        if (!strcmp(a->path, MARKER)) {
            char v[24];
            if (!read_marker(dest, v) || strcmp(v, m->version)) return false;
        }
        if (!strcmp(old, a->sha)) {
            if (!gone(dest)) return false;
        } else if (strcmp(old, "-")) {
            member(dest, PREP, "old", j->count);
            if (!saved(target, dest, old)) return false;
        }
        if (!row_start(j, a->path, old, a->sha)) return false;
    }
    return fgetc(blob) == EOF && !ferror(blob);
}
static int apply_locked(KfUpdate *u, KfProgress cb, void *ctx) {
    if (!selection.count || strcmp(u->latest, selection.version)) return fail(u, "Check for an update before installing.");
    struct stat st;
    if (!stat(ACTIVE, &st) || errno != ENOENT) return fail(u, "Reopen the updater to recover the previous transaction.");
    char current[24], final[256];
    if (!read_marker(SD MARKER, current) || strcmp(current, u->current) ||
        compare(current, "1.1.0") < 0 || compare(current, selection.version) != -1)
        return fail(u, "This update requires the matching installed 20.5.0 / 786 release.");
    if (!version_dir(final, &selection)) return fail(u, "A saved transaction for this version already exists.");
    if (!parents(PREP "/placeholder") || !clear_work() || !folder(PREP)) return fail(u, "Could not prepare the update directory.");
    uint64_t needed, available;
    if (!count_space(&selection, &needed) || !kf_free(&available) || available < needed)
        return fail(u, "Insufficient free microSD space for the update and its backup.");
    char relative[128]; snprintf(relative, sizeof(relative), "updates/%s/update.bin", selection.version);
    FILE *f = fopen(PREP "/payload.bin", "wb");
    if (!f) return fail(u, "Could not open a staging file on the card.");
    if (cb) cb("Downloading update", 0, selection.bytes, ctx);
    bool got;
    if (offline) {
        char path[256]; snprintf(path, sizeof(path), OFFLINE "%s", relative);
        got = from_file(path, file_sink, f, selection.bytes);
    } else got = kf_fetch(relative, file_sink, f, selection.bytes, cb, ctx);
    if (!close_out(f, got)) return fail(u, "Download interrupted. Your installed release is still available.");
    Journal j; f = fopen(PREP "/payload.bin", "rb");
    if (!f) return fail(u, "Could not read the download.");
    bool verified = prepare(f, &selection, &j, current, cb, ctx);
    fclose(f);
    if (!verified) return fail(u, "Could not verify the update or back up the installed files.");
    if (!gone(PREP "/payload.bin") || !store_bytes(PREP "/stable.kfu", signed_feed.bytes, signed_feed.used))
        return fail(u, "Could not save the verified manifest.");
    f = fopen(PREP "/originals.tsv", "wb");
    if (!f || !close_out(f, record(f, &j)) || rename(PREP, ACTIVE))
        return fail(u, "The card could not save the update journal.");
    if (!commit(&j, &selection, cb, ctx)) return fail(u, "Update interrupted. Reopen KEFIRJS Updater to resume or use the Windows installer.");
    selection.count = 0; kf_local(u); strcpy(u->message, "Update installed. Return to HOME to continue.");
    return KF_APPLIED;
}
static int exclusive(KfUpdate *u, KfProgress cb, void *ctx, bool recovery) {
    if (!is_base())
        return fail(u, "This update requires the matching installed 20.5.0 / 786 release.");
    if (!parents(OTA "/operation.lock") || !kf_lock())
        return fail(u, "Could not lock the card. Close any other updater and try again.");
    int rc = recovery ? recover_locked(u, cb, ctx) : apply_locked(u, cb, ctx);
    kf_unlock();
    return rc;
}
int kf_apply(KfUpdate *u, KfProgress cb, void *ctx) {
    return exclusive(u, cb, ctx, false);
}
int kf_recover(KfUpdate *u, KfProgress cb, void *ctx) {
    kf_local(u);
    struct stat st;
    if (stat(ACTIVE, &st)) return errno == ENOENT ? KF_CURRENT : fail(u, "Could not read the update journal.");
    return exclusive(u, cb, ctx, true);
}
