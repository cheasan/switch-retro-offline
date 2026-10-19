/**
 * http_server.h - minimal threaded HTTP/1.1 loopback server (Phase 2).
 *
 * Pure POSIX socket code on purpose: the exact same file compiles for the
 * Switch (libnx bsd = BSD sockets) and for the host (Linux), so the whole
 * request/response path can be desktop-tested before it ever touches
 * hardware.
 *
 * Design notes:
 *  - One listener, one worker thread, strictly sequential requests.
 *    RetroArch issues a handful of small requests per game; concurrency
 *    would only burn the 4 MB budget.
 *  - Every response closes the connection (Connection: close). Legal
 *    HTTP/1.1 and keeps state handling trivial.
 *  - Request bodies capped; a 100-continue is sent if the client asks.
 */
#ifndef RA_OFFLINE_HTTP_SERVER_H
#define RA_OFFLINE_HTTP_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Parsed request. path/query never exceed their buffers (else 400). */
typedef struct {
    char   method[8];
    char   path[128];      /* without query string                */
    char   query[256];     /* raw query string, no '?'            */
    char  *body;           /* malloc'd copy, NULL when absent     */
    size_t body_len;
} http_request;

/** Response built by a handler. body must be malloc'd (or NULL); the
 *  server frees it after sending. */
typedef struct {
    int          status;
    const char  *content_type;
    char        *body;
    size_t       body_len;
} http_response;

typedef void (*http_handler_fn)(const http_request *req, http_response *resp);

/** Bind 127.0.0.1:port and listen. 0 on success. */
int http_server_start(uint16_t port);

/** Worker-thread body: accept + handle requests until *running is false
 *  or http_server_stop() unblocks the listener. */
void http_server_serve_forever(volatile bool *running, http_handler_fn handler);

/** Unblock the accept loop from another thread. */
void http_server_stop(void);

#endif /* RA_OFFLINE_HTTP_SERVER_H */
