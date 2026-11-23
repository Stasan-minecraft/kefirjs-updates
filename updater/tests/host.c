/* Desktop adapter for the real updater engine. Run inside a disposable SD fixture. */
#include "../engine_platform.h"
#include "../publisher.h"
#include <openssl/evp.h>
#include <curl/curl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#include <sys/file.h>
#include <fcntl.h>
#endif

static void wait_for_kill(void) {
    FILE *signal = fopen("host-lock-ready", "wb");
    if (signal) fclose(signal);
#ifdef _WIN32
    Sleep(INFINITE);
#else
    for (;;) pause();
#endif
}
#ifdef _WIN32
static HANDLE lock_handle;
bool kf_lock(void) {
    lock_handle = CreateFileA("config/kefirjs/ota/operation.lock", GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (lock_handle == INVALID_HANDLE_VALUE) return false;
    if (getenv("KF_TEST_HOLD_PREPARE")) wait_for_kill();
    return true;
}
void kf_unlock(void) { CloseHandle(lock_handle); }
#else
static int lock_file;
bool kf_lock(void) {
    lock_file = open("config/kefirjs/ota/operation.lock", O_RDWR | O_CREAT, 0600);
    if (lock_file < 0) return false;
    if (flock(lock_file, LOCK_EX | LOCK_NB)) { close(lock_file); return false; }
    if (getenv("KF_TEST_HOLD_PREPARE")) wait_for_kill();
    return true;
}
void kf_unlock(void) { close(lock_file); }
#endif

bool kf_flush(FILE *f) {
    if (fflush(f)) return false;
#ifdef _WIN32
    return _commit(_fileno(f)) == 0;
#else
    return fsync(fileno(f)) == 0;
#endif
}
bool kf_free(uint64_t *bytes) { *bytes = getenv("KF_TEST_FULL") ? 0 : UINT64_C(1)<<36; return true; }
bool kf_horizon_ok(void) { return !getenv("KF_TEST_WRONG_HOS"); }
bool kf_hash(FILE *f, char out[65]) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return false;
    unsigned char buf[8192], digest[32]; unsigned len = 0; size_t n;
    bool ok = !!EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    while (ok && (n = fread(buf, 1, sizeof(buf), f))) ok = !!EVP_DigestUpdate(ctx, buf, n);
    ok = ok && !ferror(f) && EVP_DigestFinal_ex(ctx, digest, &len) && len == sizeof(digest);
    EVP_MD_CTX_free(ctx);
    if (ok) for (size_t i = 0; i < len; i++) sprintf(out + i*2, "%02x", digest[i]);
    return ok;
}
typedef struct { KfSink sink; void *dst; size_t limit; } Sink;
static size_t write_cb(char *p, size_t size, size_t count, void *user) {
    Sink *s = user;
    size_t n = size * count;
    if (n > s->limit || !s->sink(p, n, s->dst)) return 0;
    s->limit -= n; return n;
}
bool kf_fetch(const char *rel, KfSink sink, void *dst, size_t limit, KfProgress cb, void *ctx) {
    (void)cb; (void)ctx;
    if (!getenv("KF_TEST_ONLINE")) return false;
    CURL *c = curl_easy_init();
    if (!c) return false;
    char url[512];
    snprintf(url, sizeof(url), "https://raw.githubusercontent.com/%s/main/%s", KF_REPO, rel);
    Sink target = {sink, dst, limit};
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &target);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    const char *ca = getenv("KF_TEST_CA");
    if (ca) curl_easy_setopt(c, CURLOPT_CAINFO, ca);
    bool ok = curl_easy_perform(c) == CURLE_OK;
    curl_easy_cleanup(c); return ok;
}
static void tick(const char *phase, uint64_t done, uint64_t total, void *user) {
    (void)total; (void)user;
    const char *interrupt = getenv("KF_TEST_INTERRUPT_BEFORE");
    if (getenv("KF_TEST_HOLD_COMMIT") && !strcmp(phase, "Installing update") && !done) wait_for_kill();
    if (interrupt && !strcmp(phase, "Installing update") && done == strtoul(interrupt, NULL, 10)) _Exit(77);
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    KfUpdate u;
    int rc = kf_recover(&u, tick, NULL);
    if (!strcmp(argv[1], "recover")) goto print;
    if (rc == KF_ERROR) goto print;
    rc = kf_check(&u, tick, NULL);
    if (!strcmp(argv[1], "apply") && rc == KF_AVAILABLE) rc = kf_apply(&u, tick, NULL);
print:
    printf("%d | %s | %s | %llu | %s\n", rc, u.current, u.latest, (unsigned long long)u.bytes, u.message);
    return rc == KF_ERROR ? 1 : 0;
}
