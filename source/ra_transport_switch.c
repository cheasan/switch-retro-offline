/**
 * ra_transport_switch.c - Switch transport: bsd TCP + Horizon ssl service.
 *
 * Uses the console's OWN TLS stack (ssl:) instead of pulling libcurl or a
 * software TLS library into a 4 MB sysmodule. DNS resolves through the
 * sfdnsres service via newlib's gethostbyname.
 *
 * NOTE: certificate verification uses Horizon's default (PeerCa|HostName),
 * which includes the Let's Encrypt roots on current firmware. If a
 * handshake fails on hardware, the queue is NOT touched and sync simply
 * retries later - nothing is ever lost on transport failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <switch.h>

#include "logger.h"
#include "ra_http_resp.h"
#include "ra_transport.h"

#define RA_HOST "retroachievements.org"
#define RA_PORT 443
#define IO_TIMEOUT_S 15

/* Every failure path returns -1; log which step it was. */
static void step_failed(const char *step, Result rc)
{
    logger_log("transport: %s failed (rc=0x%x)", step, (unsigned)rc);
}

static int tcp_connect(void)
{
    struct hostent *he = gethostbyname(RA_HOST);
    if (he == NULL || he->h_addr_list[0] == NULL) {
        logger_log("transport: dns lookup failed for %s", RA_HOST);
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        logger_log("transport: socket failed errno=%d", errno);
        return -1;
    }

    struct timeval tv = { .tv_sec = IO_TIMEOUT_S, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(RA_PORT);
    memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        logger_log("transport: connect failed errno=%d", errno);
        close(fd);
        return -1;
    }
    return fd;
}

/* The loop thread (sync) and the HTTP worker (fetch on a cache miss)
 * share the single ssl session; one request at a time. */
static Mutex g_tlock;
static bool g_tlock_ready;

/** One TLS round trip. On success returns 0 and sets *raw_out to the
 *  whole reply (headers + body, malloc'd, NUL-terminated); else -1. */
static int exchange(const char *query_extra, const char *body,
                    char **raw_out, size_t *got_out)
{
    *raw_out = NULL;
    *got_out = 0;

    int fd = tcp_connect();
    if (fd < 0)
        return -1;

    int result = -1;
    int sfd = -1; /* ssl's own copy of the socket, closed before the conn */
    SslContext ctxs;
    SslConnection conn;
    bool conn_ok = false;

    Result rc = sslCreateContext(&ctxs, SslVersion_Auto);
    if (R_FAILED(rc)) {
        step_failed("sslCreateContext", rc);
        goto out_close;
    }

    rc = sslContextCreateConnection(&ctxs, &conn);
    if (R_FAILED(rc)) {
        step_failed("sslContextCreateConnection", rc);
        goto out_ctx;
    }

    rc = sslConnectionSetHostName(&conn, RA_HOST,
                                  (u32)strlen(RA_HOST) + 1);
    if (R_FAILED(rc)) {
        step_failed("sslConnectionSetHostName", rc);
        goto out_conn;
    }

    /* The raw ssl call wants the bsd socket id, not newlib's fd; passing
     * fd straight failed with 0xe87b on hardware. The wrapper converts it.
     * ENOENT only means ssl returned no copy of the socket - not an error. */
    sfd = socketSslConnectionSetSocketDescriptor(&conn, fd);
    if (sfd < 0 && errno != ENOENT) {
        step_failed("socketSslConnectionSetSocketDescriptor",
                    socketGetLastResult());
        goto out_conn;
    }

    rc = sslConnectionDoHandshake(&conn, NULL, NULL, NULL, 0);
    if (R_FAILED(rc)) {
        step_failed("sslConnectionDoHandshake", rc);
        goto out_conn;
    }
    conn_ok = true;

    /* Build the request while TLS is up. */
    char path[128];
    snprintf(path, sizeof path, "/dorequest.php%s%s",
             query_extra && query_extra[0] ? "?" : "",
             query_extra ? query_extra : "");

    char head[512];
    size_t body_len = body ? strlen(body) : 0;
    /* Known client first (the server 403s unknown clients on award
     * submissions), plus our unique tag so RetroAchievements can tag
     * cached unlocks - see RAOFFLINE_UA in ra_transport.h. */
    int hn = snprintf(head, sizeof head,
                      "POST %s HTTP/1.1\r\n"
                      "Host: %s\r\n"
                      "User-Agent: %s\r\n"
                      "Content-Type: application/x-www-form-urlencoded\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      path, RA_HOST, RAOFFLINE_UA, body_len);

    {
        char req[1024];
        size_t req_len = 0;
        if (hn > 0 && (size_t)hn < sizeof head &&
            body_len + (size_t)hn < sizeof req) {
            memcpy(req, head, (size_t)hn);
            req_len = (size_t)hn;
            if (body_len > 0) {
                memcpy(req + req_len, body, body_len);
                req_len += body_len;
            }
            u32 written;
            rc = sslConnectionWrite(&conn, req, (u32)req_len, &written);
            if (R_FAILED(rc)) {
                step_failed("sslConnectionWrite", rc);
                goto out_conn;
            }
        } else {
            goto out_conn; /* request too large; shouldn't happen */
        }
    }

    /* Read response headers + body; grow the buffer as data arrives. */
    size_t cap = 8192, got = 0;
    char *raw = malloc(cap);
    if (raw == NULL)
        goto out_conn;
    for (;;) {
        if (got + 1 >= cap) {
            if (cap >= RA_FETCH_MAX)
                break;
            char *bigger = realloc(raw, cap * 2);
            if (bigger == NULL)
                break;
            raw = bigger;
            cap *= 2;
        }
        u32 n;
        rc = sslConnectionRead(&conn, raw + got, (u32)(cap - 1 - got), &n);
        if (R_FAILED(rc) || n == 0)
            break;
        got += n;
    }
    raw[got] = '\0';
    if (got == 0) {
        step_failed("sslConnectionRead (no data)", rc);
        free(raw);
        goto out_conn;
    }
    *raw_out = raw;
    *got_out = got;
    result = 0;

out_conn:
    if (sfd >= 0)
        close(sfd);
    sslConnectionClose(&conn);
out_ctx:
    sslContextClose(&ctxs);
out_close:
    close(fd);
    (void)conn_ok;
    return result;
}

static int locked_exchange(const char *query_extra, const char *body,
                           char **raw_out, size_t *got_out)
{
    if (!g_tlock_ready) {
        mutexInit(&g_tlock);
        g_tlock_ready = true;
    }
    mutexLock(&g_tlock);
    int rc = exchange(query_extra, body, raw_out, got_out);
    mutexUnlock(&g_tlock);
    return rc;
}

static int post(void *ctx, const char *query_extra, const char *body,
                char *resp, size_t respsz)
{
    (void)ctx;
    if (resp != NULL && respsz > 0)
        resp[0] = '\0';

    char *raw;
    size_t got;
    if (locked_exchange(query_extra, body, &raw, &got) != 0)
        return -1;

    int status;
    size_t len;
    char *b = ra_http_body(raw, got, &status, &len);
    if (resp != NULL) {
        /* Cut-short chunk framing: fall back to the raw body text, which
         * still holds the few flat keys sync reads. */
        const char *hend = strstr(raw, "\r\n\r\n");
        if (b != NULL)
            snprintf(resp, respsz, "%s", b);
        else if (hend != NULL)
            snprintf(resp, respsz, "%s", hend + 4);
    }
    free(b);
    free(raw);
    return status;
}

static char *fetch(void *ctx, const char *body, int *status, size_t *len)
{
    (void)ctx;
    *status = -1;
    *len = 0;

    char *raw;
    size_t got;
    if (locked_exchange(NULL, body, &raw, &got) != 0)
        return NULL;

    char *b = ra_http_body(raw, got, status, len);
    free(raw);
    return b;
}

static ra_transport g_transport = { .post = post, .fetch = fetch, .ctx = NULL };

const ra_transport *ra_transport_default(void)
{
    return &g_transport;
}
