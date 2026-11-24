#include "engine_platform.h"
#include "publisher.h"
#include "edition.h"
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

/* Only these two HTTPS endpoints may serve the signed repository files. */
typedef struct { const char *host, *path, *suffix, *accept; } Origin;
static const Origin origins[] = {
    {"raw.githubusercontent.com", "/" KF_REPO "/main/", "", "application/octet-stream"},
    {"api.github.com", "/repos/" KF_REPO "/contents/", "?ref=main", "application/vnd.github.raw+json"}
};
static char network_error[192];
const char *kf_network_error(void) { return network_error; }

/* The reader and the optional lock use one TCP stream or three small UDP
   sockets. Reserve a bounded BSD transfer area, including in Album mode. */
static Result init_sockets(void) {
    const SocketInitConfig config = {
        .tcp_tx_buf_size = 0x4000, .tcp_rx_buf_size = 0x10000,
        .udp_tx_buf_size = 0x1000, .udp_rx_buf_size = 0x2000,
        .sb_efficiency = 2, .num_bsd_sessions = 1, .bsd_service_type = BsdServiceType_User
    };
    return socketInitialize(&config);
}
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
    const char *path = "/" KF_OTA "/operation.lock";
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
    if (R_FAILED(init_sockets())) { kf_unlock(); return false; }
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
    NifmRequest request;
    bool nifm, submitted, sockets, ssl, ctx;
} Network;

typedef struct {
    SslConnection connection;
    int sock;
    bool conn, wrote, eof;
    char error[64];
    unsigned char buffer[16384];
    size_t pos, used;
} Stream;

static bool service_error(char *error, size_t cap, const char *stage, Result rc) {
    if (R_SUCCEEDED(rc)) return false;
    snprintf(error, cap, "%s %04u-%04u", stage, 2000u + R_MODULE(rc), R_DESCRIPTION(rc));
    return true;
}

static void finish(Stream *s) {
    if (s->conn) sslConnectionClose(&s->connection);
    if (s->sock >= 0) close(s->sock);
}

static void network_close(Network *net) {
    if (net->ctx) sslContextClose(&net->context);
    if (net->ssl) sslExit();
    if (net->sockets) socketExit();
    if (net->submitted) {
        nifmRequestCancel(&net->request);
        nifmRequestClose(&net->request);
    }
    if (net->nifm) nifmExit();
}

/* Give Horizon a chance to bring Wi-Fi up after wake/title launch. Keep
   the request alive during TLS. A blocked Nintendo connectivity check
   must not prevent a direct attempt to reach the public GitHub endpoints. */
static void request_internet(Network *net) {
    if (R_FAILED(nifmInitialize(NifmServiceType_User))) return;
    net->nifm = true;
    if (R_FAILED(nifmCreateRequest(&net->request, true))) return;
    net->submitted = true;
    if (R_FAILED(nifmRequestSubmit(&net->request))) return;
    for (unsigned i = 0; i < 40; i++) {
        NifmRequestState state;
        if (R_FAILED(nifmGetRequestState(&net->request, &state)) || state != NifmRequestState_OnHold) return;
        svcSleepThread(200000000ULL);
    }
}
static bool network_open(Network *net) {
    request_internet(net);
    if (!have_lock_sockets) {
        Result rc = init_sockets();
        if (rc != MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized)) {
            if (service_error(network_error, sizeof(network_error), "Socket service", rc)) return false;
            net->sockets = true;
        }
    }
    if (service_error(network_error, sizeof(network_error), "TLS service", sslInitialize(1))) return false;
    net->ssl = true;
    /* SslVersion bits 24..31 select the NSS compatibility profile. HOS
       19+ supports profile 5 with the current cipher set; libnx passes the
       value through unchanged. The update channels target HOS 20.5.0. */
    u32 versions = SslVersion_TlsV12 | SslVersion_TlsV13;
    if (hosversionAtLeast(19, 0, 0)) versions |= 5u << 24;
    if (service_error(network_error, sizeof(network_error), "TLS context",
                      sslCreateContext(&net->context, versions))) return false;
    net->ctx = true;
    return true;
}
static int connect_origin(Stream *s, const char *host) {
    struct addrinfo *list = NULL, hints = {0};
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    int dns = getaddrinfo(host, "443", &hints, &list);
    if (dns != 0) {
        snprintf(s->error, sizeof(s->error), "DNS (%d)", dns);
        return -1;
    }
    int result = -1, last = EHOSTUNREACH;
    for (struct addrinfo *a = list; a; a = a->ai_next) {
        int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) { last = errno; continue; }
        if (fcntl(fd, F_SETFL, O_NONBLOCK) == 0) {
            int rc = connect(fd, a->ai_addr, a->ai_addrlen);
            if (rc < 0 && (errno == EINPROGRESS || errno == EWOULDBLOCK)) {
                struct pollfd wait = {.fd = fd, .events = POLLOUT};
                int ready = poll(&wait, 1, 4000);
                if (ready > 0) {
                    int err = 0; socklen_t len = sizeof(err);
                    rc = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                    if (rc == 0 && err) { errno = err; rc = -1; }
                } else if (!ready) errno = ETIMEDOUT;
            }
            if (!rc && !fcntl(fd, F_SETFL, 0)) { result = fd; break; }
        }
        last = errno;
        close(fd);
    }
    freeaddrinfo(list);
    if (result < 0) snprintf(s->error, sizeof(s->error), "TCP (%d)", last);
    return result;
}

static bool start(Stream *s, Network *net, const Origin *origin) {
    s->sock = connect_origin(s, origin->host);
    if (s->sock < 0 || service_error(s->error, sizeof(s->error), "TLS connection",
            sslContextCreateConnection(&net->context, &s->connection))) return false;
    s->conn = true;
    if (service_error(s->error, sizeof(s->error), "TLS socket setup",
            sslConnectionSetOption(&s->connection, SslOptionType_DoNotCloseSocket, true))) return false;
    errno = 0;
    int dup = socketSslConnectionSetSocketDescriptor(&s->connection, s->sock);
    if (dup >= 0) close(dup);
    else if (errno != ENOENT) {
        snprintf(s->error, sizeof(s->error), "TLS socket (%d)", errno);
        return false;
    }
    if (service_error(s->error, sizeof(s->error), "TLS hostname",
            sslConnectionSetHostName(&s->connection, origin->host, strlen(origin->host) + 1)) ||
        service_error(s->error, sizeof(s->error), "TLS verification",
            sslConnectionSetVerifyOption(&s->connection, SslVerifyOption_PeerCa |
                SslVerifyOption_HostName | SslVerifyOption_DateCheck)) ||
        service_error(s->error, sizeof(s->error), "TLS timeout",
            sslConnectionSetIoTimeout(&s->connection, 10000))) return false;
    Result rc = sslConnectionDoHandshake(&s->connection, NULL, NULL, NULL, 0);
    if (R_FAILED(rc)) {
        Result verify = sslConnectionGetVerifyCertError(&s->connection);
        if (service_error(s->error, sizeof(s->error), "Certificate", verify)) return false;
        service_error(s->error, sizeof(s->error), "TLS handshake", rc);
        return false;
    }
    return true;
}
static bool request(Stream *s, const Origin *origin, const char *relative) {
    char req[512];
    int n = snprintf(req, sizeof(req),
        "GET %s%s%s HTTP/1.1\r\nHost: %s\r\nUser-Agent: KEFIRJS-Updater\r\n"
        "Accept: %s\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
        origin->path, relative, origin->suffix, origin->host, origin->accept);
    if (n < 0 || (size_t)n >= sizeof(req)) return false;
    for (int offset = 0; offset < n;) {
        u32 sent = 0;
        if (service_error(s->error, sizeof(s->error), "HTTPS write",
                sslConnectionWrite(&s->connection, req + offset, n - offset, &sent)) || !sent) return false;
        offset += sent;
    }
    return true;
}
static bool refill(Stream *s) {
    if (s->pos < s->used) return true;
    u32 got = 0;
    if (service_error(s->error, sizeof(s->error), "HTTPS read",
            sslConnectionRead(&s->connection, s->buffer, sizeof(s->buffer), &got))) return false;
    s->pos = 0; s->used = got; s->eof = !got;
    return got != 0;
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
    if (!line(s, field, sizeof(field)) || strlen(field) < 12 ||
        (strncmp(field, "HTTP/1.1 ", 9) && strncmp(field, "HTTP/1.0 ", 9))) return false;
    char code[4] = {field[9], field[10], field[11], 0};
    size_t status;
    if ((field[12] && field[12] != ' ') || !number(code, 10, 599, &status) || status < 100) return false;
    if (status != 200) {
        snprintf(s->error, sizeof(s->error), "HTTP %u", (unsigned)status);
        return false;
    }
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
    /* GitHub may frame an HTTP/1.x body by closing the connection. Distinguish
       authenticated EOF from a TLS read failure and enforce the same byte cap. */
    size_t received = 0;
    do {
        if (chunked) {
            if (!line(s, field, sizeof(field))) return false;
            char *extension = strchr(field, ';');
            if (extension) *extension = 0;
            if (!number(field, 16, limit - received, &bytes)) return false;
            if (!bytes) {
                do {
                    if (!line(s, field, sizeof(field)) || (headers += strlen(field) + 2) > 32768) return false;
                } while (*field && strchr(field, ':'));
                return !*field;
            }
        } else if (!length) {
            if (!refill(s)) return s->eof;
            bytes = s->used - s->pos;
            if (bytes > limit - received) return false;
        }
        size_t left = bytes;
        while (left) {
            if (!refill(s)) return false;
            size_t n = s->used - s->pos;
            if (n > left) n = left;
            s->wrote = true;
            if (!sink(s->buffer + s->pos, n, user)) {
                snprintf(s->error, sizeof(s->error), "Could not store download");
                return false;
            }
            s->pos += n; left -= n; received += n;
            if (cb) cb("Downloading update", received, limit, ctx);
        }
        if (chunked && (!line(s, field, sizeof(field)) || *field)) return false;
    } while (chunked || !length);
    return true;
}
bool kf_fetch(const char *rel, KfSink sink, void *user, size_t limit, KfProgress cb, void *ctx) {
    Network net = {0};
    network_error[0] = 0;
    bool ok = network_open(&net);
    if (ok) {
        char raw_error[64] = "";
        for (size_t i = 0; i < sizeof(origins)/sizeof(origins[0]); ++i) {
            Stream s = {.sock = -1};
            ok = start(&s, &net, &origins[i]) && request(&s, &origins[i], rel) &&
                 response(&s, sink, user, limit, cb, ctx);
            finish(&s);
            if (ok) break;
            if (!*s.error) snprintf(s.error, sizeof(s.error), "Invalid or incomplete HTTP response");
            if (!i) {
                snprintf(raw_error, sizeof(raw_error), "%s", s.error);
                snprintf(network_error, sizeof(network_error), "GitHub raw: %s.", s.error);
            } else snprintf(network_error, sizeof(network_error), "GitHub raw: %s. GitHub API: %s.", raw_error, s.error);
            /* A sink can be a partially written file. Never concatenate responses. */
            if (s.wrote) break;
        }
    }
    network_close(&net);
    if (ok) network_error[0] = 0;
    return ok;
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
