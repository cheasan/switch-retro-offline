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

/** Largest reply fetch() accepts (raw bytes, headers included). */
#define RA_FETCH_MAX (2 * 1024 * 1024)

/** User-Agent for every request ra-offline itself sends upstream.
 *  The first token stays a known RetroAchievements client: the real
 *  server rejects award/leaderboard submissions from unknown clients
 *  with 403 "unsupported_client" (found in hardware testing). The
 *  "ra-offline/<version>" suffix is the unique tag RetroAchievements
 *  asked for (RAdmin, 2026-10): it lets them tag cached unlocks for
 *  analytics. Same shape as the approved RAOfflineProxy uses:
 *  "RetroArch/1.21.0 (Linux) RAOfflineProxy/<version>". */
#define RAOFFLINE_UA "RetroArch/1.22.2 ra-offline/0.4.6"

typedef struct {
    /** POST to upstream /dorequest.php (query_extra may be NULL).
     *  On HTTP reply: returns status code and fills resp (NUL-terminated).
     *  On connection failure returns negative. */
    int  (*post)(void *ctx, const char *query_extra, const char *body,
                 char *resp, size_t respsz);
    /** POST body to upstream /dorequest.php and return the whole reply
     *  body (malloc'd, NUL-terminated, at most RA_FETCH_MAX bytes; caller
     *  frees), or NULL on connection failure. *status = HTTP status. */
    char *(*fetch)(void *ctx, const char *body, int *status, size_t *len);
    void *ctx;
} ra_transport;

/** Platform default: host -> $RAOFFLINE_UPSTREAM, Switch -> real RA. */
const ra_transport *ra_transport_default(void);

#endif /* RA_OFFLINE_TRANSPORT_H */
