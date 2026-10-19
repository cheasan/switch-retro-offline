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
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <switch.h>

#include "ra_transport.h"

#define RA_HOST "retroachievements.org"
#define RA_PORT 443
#define IO_TIMEOUT_S 15

static int tcp_connect(void)
{
    struct hostent *he = gethostbyname(RA_HOST);
    if (he == NULL || he->h_addr_list[0] == NULL)
        return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct timeval tv = { .tv_sec = IO_TIMEOUT_S, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(RA_PORT);
    memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int post(void *ctx, const char *query_extra, const char *body,
                char *resp, size_t respsz)
{
    (void)ctx;
    if (resp != NULL && respsz > 0)
        resp[0] = '\0';

    int fd = tcp_connect();
    if (fd < 0)
        return -1;

    int status = -1;
    SslContext ctxs;
    SslConnection conn;
    bool conn_ok = false;

    Result rc = sslCreateContext(&ctxs, SslVersion_Auto);
    if (R_FAILED(rc))
        goto out_close;

    rc = sslContextCreateConnection(&ctxs, &conn);
    if (R_FAILED(rc))
        goto out_ctx;

    rc = sslConnectionSetHostName(&conn, RA_HOST,
                                  (u32)strlen(RA_HOST) + 1);
    if (R_FAILED(rc))
        goto out_conn;

    int sfd;
    rc = sslConnectionSetSocketDescriptor(&conn, fd, &sfd);
    if (R_FAILED(rc))
        goto out_conn;

    rc = sslConnectionDoHandshake(&conn, NULL, NULL, NULL, 0);
    if (R_FAILED(rc))
        goto out_conn;
    conn_ok = true;

    /* Build the request while TLS is up. */
    char path[128];
    snprintf(path, sizeof path, "/dorequest.php%s%s",
             query_extra && query_extra[0] ? "?" : "",
             query_extra ? query_extra : "");

    char head[512];
    size_t body_len = body ? strlen(body) : 0;
    int hn = snprintf(head, sizeof head,
                      "POST %s HTTP/1.1\r\n"
                      "Host: %s\r\n"
                      "Content-Type: application/x-www-form-urlencoded\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      path, RA_HOST, body_len);

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
            if (R_FAILED(rc))
                goto out_conn;
        } else {
            goto out_conn; /* request too large; shouldn't happen */
        }
    }

    /* Read response headers + body. */
    static char raw[8192];
    size_t got = 0;
    for (;;) {
        if (got + 1 >= sizeof raw)
            break;
        u32 n;
        rc = sslConnectionRead(&conn, raw + got, (u32)(sizeof raw - 1 - got), &n);
        if (R_FAILED(rc) || n == 0)
            break;
        got += n;
        raw[got] = '\0';
    }
    if (got == 0)
        goto out_conn;

    if (strncmp(raw, "HTTP/", 5) == 0) {
        const char *sp = strchr(raw, ' ');
        if (sp != NULL)
            status = atoi(sp + 1);
    }
    const char *b = strstr(raw, "\r\n\r\n");
    if (b != NULL && resp != NULL)
        snprintf(resp, respsz, "%s", b + 4);

out_conn:
    sslConnectionClose(&conn);
out_ctx:
    sslContextClose(&ctxs);
out_close:
    close(fd);
    (void)conn_ok;
    return status;
}

static ra_transport g_transport = { .post = post, .ctx = NULL };

const ra_transport *ra_transport_default(void)
{
    return &g_transport;
}
