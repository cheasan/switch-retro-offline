/**
 * ra_http_resp.c - split a raw HTTP/1.1 reply. See ra_http_resp.h.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "ra_http_resp.h"

/** Does the header block (NUL-terminated copy) say chunked? */
static int is_chunked(const char *headers)
{
    const char *p = headers;
    while ((p = strchr(p, '\n')) != NULL) {
        p++;
        if (strncasecmp(p, "Transfer-Encoding:", 18) == 0) {
            const char *v = p + 18;
            const char *eol = strchr(v, '\n');
            size_t n = eol ? (size_t)(eol - v) : strlen(v);
            for (size_t i = 0; i + 7 <= n; i++)
                if (strncasecmp(v + i, "chunked", 7) == 0)
                    return 1;
        }
    }
    return 0;
}

/** Decode chunked framing from b[0..n) into out (at least n+1 bytes). */
static long dechunk(const char *b, size_t n, char *out)
{
    size_t i = 0, o = 0;
    for (;;) {
        char *end;
        if (i >= n || !isxdigit((unsigned char)b[i]))
            return -1;
        unsigned long size = strtoul(b + i, &end, 16);
        const char *crlf = strstr(end, "\r\n");
        if (crlf == NULL || (size_t)(crlf - b) >= n)
            return -1;
        i = (size_t)(crlf - b) + 2;
        if (size == 0)
            return (long)o;
        if (size > n - i)
            return -1;
        memcpy(out + o, b + i, size);
        o += size;
        i += size + 2; /* chunk data + CRLF */
    }
}

char *ra_http_body(const char *raw, size_t got, int *status, size_t *len)
{
    *status = -1;
    *len = 0;
    if (got >= 5 && strncmp(raw, "HTTP/", 5) == 0) {
        const char *sp = memchr(raw, ' ', got);
        if (sp != NULL)
            *status = atoi(sp + 1);
    }

    const char *hend = strstr(raw, "\r\n\r\n");
    if (hend == NULL)
        return NULL;
    size_t hlen = (size_t)(hend - raw);
    const char *b = hend + 4;
    size_t n = got - (size_t)(b - raw);

    char *headers = malloc(hlen + 1);
    char *out = malloc(n + 1);
    if (headers == NULL || out == NULL) {
        free(headers);
        free(out);
        return NULL;
    }
    memcpy(headers, raw, hlen);
    headers[hlen] = '\0';

    long olen;
    if (is_chunked(headers)) {
        olen = dechunk(b, n, out);
    } else {
        memcpy(out, b, n);
        olen = (long)n;
    }
    free(headers);
    if (olen < 0) {
        free(out);
        return NULL;
    }
    out[olen] = '\0';
    *len = (size_t)olen;
    return out;
}
