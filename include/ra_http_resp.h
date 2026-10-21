/**
 * ra_http_resp.h - split a raw HTTP/1.1 reply into status + body.
 *
 * Shared by both transports. Handles "Transfer-Encoding: chunked", which
 * the real server uses for large replies (achievement sets).
 *
 * Pure C, no switch.h: host-testable.
 */
#ifndef RA_OFFLINE_HTTP_RESP_H
#define RA_OFFLINE_HTTP_RESP_H

#include <stddef.h>

/** Parse raw[0..got). Sets *status (-1 if no status line) and returns the
 *  decoded body as a malloc'd, NUL-terminated buffer (*len = its length),
 *  or NULL when the reply has no header end or a chunk is cut short. */
char *ra_http_body(const char *raw, size_t got, int *status, size_t *len);

#endif /* RA_OFFLINE_HTTP_RESP_H */
