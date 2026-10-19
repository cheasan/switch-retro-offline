/**
 * ra_transport.h - pluggable HTTP POST client for the sync worker.
 *
 * Two backends from one interface:
 *   - ra_transport_host.c   plain TCP, for desktop tests (fake RA server)
 *   - ra_transport_switch.c bsd TCP + Horizon ssl service (system TLS)
 *                           talking to https://retroachievements.org
 */
#ifndef RA_OFFLINE_TRANSPORT_H
#define RA_OFFLINE_TRANSPORT_H

#include <stddef.h>

typedef struct {
    /** POST to upstream /dorequest.php (query_extra may be NULL).
     *  On HTTP reply: returns status code and fills resp (NUL-terminated).
     *  On connection failure returns negative. */
    int  (*post)(void *ctx, const char *query_extra, const char *body,
                 char *resp, size_t respsz);
    void *ctx;
} ra_transport;

/** Platform default: host -> $RAOFFLINE_UPSTREAM, Switch -> real RA. */
const ra_transport *ra_transport_default(void);

#endif /* RA_OFFLINE_TRANSPORT_H */
