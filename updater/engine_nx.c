#include "engine_platform.h"
#include "publisher.h"
#include <switch.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

/* This transport has a single origin. A signed feed can name only an update
   version. It does not select a hostname, a redirect or a request header. */
static const char host[] = "raw.githubusercontent.com";
static FsFile card_lock;
static bool have_file, have_lock_sockets;
static int bound[3] = {-1, -1, -1};

void kf_unlock(void) {
    if (have_lock_sockets) {
        for (size_t i = 0; i < 3; ++i) {
            if (bound[i] >= 0) close(bound[i]);
            bound[i] = -1;
        }
        socketExit(); have_lock_sockets = false;
    }
    if (have_file) { fsFileClose(&card_lock); have_file = false; }
}
bool kf_lock(void) {
    const char *path = "/config/kefirjs/ota/operation.lock";
    FsFileSystem *fs = fsdevGetDeviceFileSystem("sdmc");
    if (have_file || !fs) return false;
    Result rc = fsFsCreateFile(fs, path, 0, 0);
    if (R_FAILED(rc) && R_VALUE(rc) != 0x402) return false;
    if (R_FAILED(fsFsOpenFile(fs, path, FsOpenMode_Write, &card_lock))) return false;
    have_file = true;
    FsFile probe;
    rc = fsFsOpenFile(fs, path, FsOpenMode_Write, &probe);
    /* A filesystem-backed exclusive handle works even with airplane mode on.
       Only Horizon's documented TargetLocked result proves the second writer
       was denied for sharing. A file's mere existence never holds this lock. */
    if (R_VALUE(rc) == 0xe02) return true;
    if (R_FAILED(rc)) { kf_unlock(); return false; }
    fsFileClose(&probe);
    /* On SD providers that permit multiple writers, bind a majority of three
       loopback UDP ports. Two majorities always intersect. One unrelated port
       conflict is tolerated. There is no listener, Wi-Fi request or traffic. */
    if (R_FAILED(socketInitializeDefault())) { kf_unlock(); return false; }
    have_lock_sockets = true;
    const uint16_t ports[3] = {31247, 31249, 31253};
    unsigned owners = 0;
    for (size_t i = 0; i < 3; ++i) {
        struct sockaddr_in local = {0};
        local.sin_family = AF_INET;
        local.sin_port = htons(ports[i]);
        local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) continue;
        if (!bind(fd, (const struct sockaddr *)&local, sizeof(local))) {
            bound[i] = fd; owners++;
        } else close(fd);
    }
    if (owners >= 2) return true;
    kf_unlock(); return false;
}
typedef struct {
    SslContext context;
    SslConnection connection;
    int sock;
    bool sockets, ssl, ctx, conn;
    unsigned char buffer[16384];
    size_t pos, used;
} Stream;

static void finish(Stream *s) {
    if (s->conn) sslConnectionClose(&s->connection);
    if (s->sock >= 0) close(s->sock);
    if (s->ctx) sslContextClose(&s->context);
    if (s->ssl) sslExit();
    if (s->sockets) socketExit();
}
static int connect_origin(void) {
    struct addrinfo *list = NULL, hints = {0};
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, "443", &hints, &list)) return -1;
    int result = -1;
    for (struct addrinfo *a = list; a; a = a->ai_next) {
        int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        int flags = fcntl(fd, F_GETFL, 0), err = 0;
        socklen_t len = sizeof(err);
        if (flags >= 0 && !fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
            int rc = connect(fd, a->ai_addr, a->ai_addrlen);
            if (rc < 0 && errno == EINPROGRESS) {
                struct pollfd wait = {.fd = fd, .events = POLLOUT};
                rc = poll(&wait, 1, 5000) > 0 ? getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) : -1;
            }
            if (!rc && !err && !fcntl(fd, F_SETFL, flags)) { result = fd; break; }
        }
        close(fd);
    }
    freeaddrinfo(list); return result;
}
static bool start(Stream *s) {
    if (!have_lock_sockets) {
        if (R_FAILED(socketInitializeDefault())) return false;
        s->sockets = true;
    }
    if (R_FAILED(sslInitialize(1))) return false;
    s->ssl = true;
    if (R_FAILED(sslCreateContext(&s->context, SslVersion_TlsV12 | SslVersion_TlsV13))) return false;
    s->ctx = true;
    s->sock = connect_origin();
    if (s->sock < 0 || R_FAILED(sslContextCreateConnection(&s->context, &s->connection))) return false;
    s->conn = true;
    if (R_FAILED(sslConnectionSetOption(&s->connection, SslOptionType_DoNotCloseSocket, true))) return false;
    errno = 0;
    int dup = socketSslConnectionSetSocketDescriptor(&s->connection, s->sock);
    if (dup >= 0) close(dup);
    else if (errno != ENOENT) return false;
    return R_SUCCEEDED(sslConnectionSetHostName(&s->connection, host, strlen(host))) &&
        R_SUCCEEDED(sslConnectionSetVerifyOption(&s->connection, SslVerifyOption_PeerCa |
                SslVerifyOption_HostName | SslVerifyOption_DateCheck)) &&
        R_SUCCEEDED(sslConnectionSetIoTimeout(&s->connection, 10000)) &&
        R_SUCCEEDED(sslConnectionDoHandshake(&s->connection, NULL, NULL, NULL, 0));
}
static bool request(Stream *s, const char *relative) {
    char req[512];
    int n = snprintf(req, sizeof(req),
        "GET /%s/main/%s HTTP/1.1\r\nHost: %s\r\nUser-Agent: KEFIRJS-Updater\r\n"
        "Accept: application/octet-stream\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
        KF_REPO, relative, host);
    if (n < 0 || (size_t)n >= sizeof(req)) return false;
    for (int offset = 0; offset < n;) {
        u32 sent = 0;
        if (R_FAILED(sslConnectionWrite(&s->connection, req + offset, n - offset, &sent)) || !sent) return false;
        offset += sent;
    }
    return true;
}
static bool refill(Stream *s) {
    if (s->pos < s->used) return true;
    u32 got = 0;
    if (R_FAILED(sslConnectionRead(&s->connection, s->buffer, sizeof(s->buffer), &got))) return false;
    s->pos = 0; s->used = got; return got != 0;
}
static bool line(Stream *s, char *dest, size_t cap) {
    size_t n = 0;
    while (n < cap && refill(s)) {
        unsigned char c = s->buffer[s->pos++];
        if (c == '\r') {
            if (!refill(s) || s->buffer[s->pos++] != '\n') return false;
            dest[n] = 0; return true;
        }
        if (c != '\t' && (c < 32 || c > 126)) return false;
        dest[n++] = (char)c;
    }
    return false;
}
static bool number(const char *s, unsigned base, size_t max, size_t *out) {
    if (!*s) return false;
    size_t v = 0;
    for (; *s; s++) {
        unsigned char c = *s;
        unsigned d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        if (d >= base || d > max || v > (max - d) / base) return false;
        v = base*v + d;
    }
    *out = v; return true;
}
static bool response(Stream *s, KfSink sink, void *user, size_t limit, KfProgress cb, void *ctx) {
    char field[2048];
    if (!line(s, field, sizeof(field)) ||
        (strncmp(field, "HTTP/1.1 200 ", 13) && strncmp(field, "HTTP/1.0 200 ", 13))) return false;
    bool chunked = false, length = false;
    size_t bytes = 0, headers = 0;
    do {
        if (!line(s, field, sizeof(field)) || (headers += strlen(field) + 2) > 32768) return false;
        if (!*field) break;
        char *sep = strchr(field, ':');
        if (!sep) return false;
        *sep++ = 0;
        while (*sep == ' ' || *sep == '\t') sep++;
        if (!strcasecmp(field, "Content-Length")) {
            if (length || chunked || !number(sep, 10, limit, &bytes)) return false;
            length = true;
        } else if (!strcasecmp(field, "Transfer-Encoding")) {
            if (length || chunked || strcasecmp(sep, "chunked")) return false;
            chunked = true;
        } else if (!strcasecmp(field, "Content-Encoding") && strcasecmp(sep, "identity")) return false;
    } while (true);
    if (!chunked && !length) return false;
    size_t received = 0;
    do {
        if (chunked) {
            if (!line(s, field, sizeof(field)) || !number(field, 16, limit - received, &bytes)) return false;
            if (!bytes) return line(s, field, sizeof(field)) && !*field;
        }
        size_t left = bytes;
        while (left) {
            if (!refill(s)) return false;
            size_t n = s->used - s->pos;
            if (n > left) n = left;
            if (!sink(s->buffer + s->pos, n, user)) return false;
            s->pos += n; left -= n; received += n;
            if (cb) cb("Downloading update", received, limit, ctx);
        }
        if (chunked && (!line(s, field, sizeof(field)) || *field)) return false;
    } while (chunked);
    return true;
}
bool kf_fetch(const char *rel, KfSink sink, void *user, size_t limit, KfProgress cb, void *ctx) {
    Stream s = {.sock = -1};
    bool ok = start(&s) && request(&s, rel) && response(&s, sink, user, limit, cb, ctx);
    finish(&s); return ok;
}
bool kf_horizon_ok(void) {
    /* libnx sets the Atmosphere flag in the high bit of hosversionGet(). */
    return (hosversionGet() & 0xffffffu) == MAKEHOSVERSION(20, 5, 0);
}
bool kf_hash(FILE *f, char hex[65]) {
    Sha256Context sha; sha256ContextCreate(&sha);
    unsigned char buf[8192], d[32]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f))) sha256ContextUpdate(&sha, buf, n);
    if (ferror(f)) return false;
    sha256ContextGetHash(&sha, d);
    for (size_t i = 0; i < sizeof(d); i++) sprintf(hex + i*2, "%02x", d[i]);
    return true;
}
bool kf_flush(FILE *f) { return !fflush(f) && !fsync(fileno(f)); }
bool kf_free(uint64_t *out) {
    FsFileSystem *sd = fsdevGetDeviceFileSystem("sdmc");
    s64 bytes;
    if (!sd || R_FAILED(fsFsGetFreeSpace(sd, "/", &bytes)) || bytes < 0) return false;
    *out = (uint64_t)bytes; return true;
}
