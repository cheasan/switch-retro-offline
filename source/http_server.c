/**
 * http_server.c - minimal threaded HTTP/1.1 loopback server (Phase 2).
 *
 * Pure POSIX: compiles for both the Switch (libnx bsd) and the host.
 * See http_server.h for design notes.
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

#include "http_server.h"
#include "logger.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/** Fixed buffer holding headers (+ whatever body bytes arrive with them). */
#define REQ_BUF_CAP   (16 * 1024)
/** Hard cap for a request body; RetroArch payloads are far below this. */
#define BODY_MAX      (256 * 1024)
/** Per-connection receive timeout so a stuck client can't wedge the thread. */
#define RECV_TIMEOUT_S 10

static int g_listen_fd = -1;

int http_server_start(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        logger_log("http: socket() failed errno=%d", errno);
        return -1;
    }

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        logger_log("http: bind 127.0.0.1:%u failed errno=%d", port, errno);
        close(fd);
        return -1;
    }
    if (listen(fd, 4) < 0) {
        logger_log("http: listen failed errno=%d", errno);
        close(fd);
        return -1;
    }

    g_listen_fd = fd;
    logger_log("http: listening on 127.0.0.1:%u", port);
    return 0;
}

void http_server_stop(void)
{
    int fd = g_listen_fd;
    if (fd >= 0) {
        g_listen_fd = -1;
        shutdown(fd, SHUT_RDWR); /* unblocks accept() in the worker thread */
    }
}

/* ------------------------------------------------------------------ */
/* Request parsing                                                     */
/* ------------------------------------------------------------------ */

/** Case-insensitive search for a header value in the raw header block.
 *  Returns pointer to value start, or NULL. */
static const char *find_header(const char *headers, size_t headers_len,
                               const char *name)
{
    size_t name_len = strlen(name);
    const char *p = headers;
    const char *end = headers + headers_len;

    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        if (eol == NULL)
            eol = end;
        if ((size_t)(eol - p) > name_len &&
            strncasecmp(p, name, name_len) == 0 && p[name_len] == ':') {
            const char *v = p + name_len + 1;
            while (v < eol && (*v == ' ' || *v == '\t'))
                v++;
            return v;
        }
        p = eol + 1;
    }
    return NULL;
}

/** Parse "METHOD SP TARGET SP HTTP/x.x" into req. Returns 0 or -1. */
static int parse_request_line(char *line, http_request *req)
{
    char *sp1 = strchr(line, ' ');
    if (sp1 == NULL)
        return -1;
    *sp1 = '\0';

    char *target = sp1 + 1;
    char *sp2 = strchr(target, ' ');
    if (sp2 == NULL)
        return -1;
    *sp2 = '\0';

    if (strlen(line) >= sizeof req->method)
        return -1;
    strcpy(req->method, line);

    char *qmark = strchr(target, '?');
    if (qmark != NULL) {
        *qmark = '\0';
        qmark++;
        if (strlen(qmark) >= sizeof req->query)
            return -1;
        strcpy(req->query, qmark);
    }
    if (strlen(target) >= sizeof req->path)
        return -1;
    strcpy(req->path, target);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Response helpers                                                    */
/* ------------------------------------------------------------------ */

static const char *status_text(int status)
{
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    default:  return "OK";
    }
}

static int send_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, MSG_NOSIGNAL);
        if (n <= 0)
            return -1;
        off += (size_t)n;
    }
    return 0;
}

/** Send resp; frees resp->body. */
static void send_response(int fd, http_response *resp)
{
    char head[256];
    int hn = snprintf(head, sizeof head,
                      "HTTP/1.1 %d %s\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      resp->status, status_text(resp->status),
                      resp->content_type ? resp->content_type : "text/plain",
                      resp->body_len);
    if (hn > 0 && (size_t)hn < sizeof head) {
        if (send_all(fd, head, (size_t)hn) == 0 && resp->body_len > 0)
            send_all(fd, resp->body, resp->body_len);
    }
    logger_log("http: %u %s (%zu bytes)", resp->status,
               resp->content_type ? resp->content_type : "-", resp->body_len);
    free(resp->body);
    resp->body = NULL;
}

/* ------------------------------------------------------------------ */
/* Connection handling                                                 */
/* ------------------------------------------------------------------ */

static void set_recv_timeout(int fd)
{
    struct timeval tv = { .tv_sec = RECV_TIMEOUT_S, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}

static void handle_connection(int fd)
{
    char *buf = malloc(REQ_BUF_CAP + 1);
    if (buf == NULL)
        return;
    set_recv_timeout(fd);

    size_t got = 0;
    char *header_end = NULL;
    while (header_end == NULL && got < REQ_BUF_CAP) {
        ssize_t n = recv(fd, buf + got, REQ_BUF_CAP - got, 0);
        if (n <= 0)
            goto out; /* closed or timed out before a full header */
        got += (size_t)n;
        buf[got] = '\0';
        header_end = strstr(buf, "\r\n\r\n");
    }
    if (header_end == NULL)
        goto out;

    http_request req;
    memset(&req, 0, sizeof req);

    /* Request line = buf .. first \r\n */
    char *line_end = strstr(buf, "\r\n");
    *line_end = '\0';
    if (parse_request_line(buf, &req) != 0) {
        http_response resp = { 400, "text/plain", NULL, 0 };
        send_response(fd, &resp);
        goto out_free;
    }

    /* Header block length: from after the request line to the blank line. */
    size_t headers_len = (size_t)(header_end - (line_end + 2));

    const char *expect = find_header(buf, headers_len, "Expect");
    if (expect != NULL && strncasecmp(expect, "100-continue", 12) == 0)
        send_all(fd, "HTTP/1.1 100 Continue\r\n\r\n", 25);

    size_t body_start = (size_t)(header_end + 4 - buf);
    size_t have_body = got - body_start;
    size_t content_len = 0;
    const char *cl = find_header(buf, headers_len, "Content-Length");
    if (cl != NULL)
        content_len = (size_t)strtoul(cl, NULL, 10);

    /* RetroArch always sends Content-Length (payloads live in memory);
     * chunked bodies are never expected on this loopback. */
    if (find_header(buf, headers_len, "Transfer-Encoding") != NULL ||
        (cl == NULL && req.method[0] == 'P')) {
        http_response resp = { 411, "text/plain", NULL, 0 };
        send_response(fd, &resp);
        goto out_free;
    }

    if (content_len > BODY_MAX) {
        http_response resp = { 413, "text/plain", NULL, 0 };
        send_response(fd, &resp);
        goto out_free;
    }
    if (content_len > 0) {
        req.body = malloc(content_len + 1);
        if (req.body == NULL) {
            http_response resp = { 500, "text/plain", NULL, 0 };
            send_response(fd, &resp);
            goto out_free;
        }
        size_t copy = have_body < content_len ? have_body : content_len;
        memcpy(req.body, buf + body_start, copy);
        while (copy < content_len) {
            ssize_t n = recv(fd, req.body + copy, content_len - copy, 0);
            if (n <= 0) {
                free(req.body);
                req.body = NULL;
                goto out_free;
            }
            copy += (size_t)n;
        }
        req.body[content_len] = '\0';
        req.body_len = content_len;
    }

    logger_log("http: %s %s%s%s", req.method, req.path,
               req.query[0] ? "?" : "", req.query);

    http_response resp;
    memset(&resp, 0, sizeof resp);
    extern http_handler_fn g_route_handler;
    g_route_handler(&req, &resp);
    send_response(fd, &resp);

    free(req.body);

out_free:
    free(buf);
    return;
out:
    free(buf);
}

/* Handler set by serve_forever so handle_connection can reach it. */
static http_handler_fn s_handler;
http_handler_fn g_route_handler = NULL;

void http_server_serve_forever(volatile bool *running, http_handler_fn handler)
{
    s_handler = handler;
    g_route_handler = handler;

    while (*running) {
        int fd = accept(g_listen_fd, NULL, NULL);
        if (fd < 0) {
            if (!*running)
                break; /* unblocked by stop() */
            if (errno == EINTR)
                continue;
            logger_log("http: accept failed errno=%d", errno);
            break;
        }
        handle_connection(fd);
        close(fd);
    }
    logger_log("http: worker exiting");
}
