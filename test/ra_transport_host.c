/**
 * ra_transport_host.c - plain-TCP transport for desktop testing.
 * Upstream is "host:port" from $RAOFFLINE_UPSTREAM (default 127.0.0.1:9090),
 * speaking plain HTTP so tests run against test/fake_ra_server.py.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "ra_transport.h"
#include "ra_util.h"

#define RA_UPSTREAM_DEFAULT "127.0.0.1:9090"
#define IO_TIMEOUT_S 10

static int parse_upstream(char *host, size_t hostsz, int *port)
{
    const char *up = ra_upstream_base();
    char buf[256];
    snprintf(buf, sizeof buf, "%s", up);
    char *colon = strchr(buf, ':');
    if (colon == NULL)
        return -1;
    *colon = '\0';
    snprintf(host, hostsz, "%s", buf);
    *port = atoi(colon + 1);
    return (*port > 0 && host[0] != '\0') ? 0 : -1;
}

static int connect_upstream(void)
{
    char host[256];
    int port;
    if (parse_upstream(host, sizeof host, &port) != 0)
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
    addr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int send_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, 0);
        if (n <= 0)
            return -1;
        off += (size_t)n;
    }
    return 0;
}

static int post(void *ctx, const char *query_extra, const char *body,
                char *resp, size_t respsz)
{
    (void)ctx;
    int fd = connect_upstream();
    if (fd < 0)
        return -1;

    char path[512];
    snprintf(path, sizeof path, "/dorequest.php%s%s",
             query_extra && query_extra[0] ? "?" : "",
             query_extra ? query_extra : "");

    char head[512];
    int hn = snprintf(head, sizeof head,
                      "POST %s HTTP/1.1\r\n"
                      "Host: %s\r\n"
                      "Content-Type: application/x-www-form-urlencoded\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      path, ra_upstream_base(), body ? strlen(body) : 0);

    if (send_all(fd, head, (size_t)hn) != 0 ||
        (body && *body && send_all(fd, body, strlen(body)) != 0)) {
        close(fd);
        return -1;
    }

    /* Read the whole response. */
    char raw[8192];
    size_t got = 0;
    for (;;) {
        if (got + 1 >= sizeof raw)
            break;
        ssize_t n = recv(fd, raw + got, sizeof raw - 1 - got, 0);
        if (n <= 0)
            break;
        got += (size_t)n;
        raw[got] = '\0';
    }
    close(fd);
    if (got == 0)
        return -1;

    /* Status line: "HTTP/1.1 200 OK" */
    int status = -1;
    if (strncmp(raw, "HTTP/", 5) == 0) {
        const char *sp = strchr(raw, ' ');
        if (sp != NULL)
            status = atoi(sp + 1);
    }
    const char *body_start = strstr(raw, "\r\n\r\n");
    if (body_start != NULL) {
        body_start += 4;
        snprintf(resp, respsz, "%s", body_start);
    } else if (respsz > 0) {
        resp[0] = '\0';
    }
    return status;
}

static ra_transport g_transport = { .post = post, .ctx = NULL };

const ra_transport *ra_transport_default(void)
{
    return &g_transport;
}
