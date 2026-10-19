/**
 * routes.h - request dispatch for the loopback server.
 */
#ifndef RA_OFFLINE_ROUTES_H
#define RA_OFFLINE_ROUTES_H

#include "http_server.h"

/** Fill resp for the given req. resp must be zeroed by the server. */
void route_dispatch(const http_request *req, http_response *resp);

/** Extract a urlencoded value for key from a query string or POST body.
 *  Handles %XX and '+' decoding. Returns false when absent. */
bool param_get(const char *src, const char *key, char *out, size_t outsz);

#endif /* RA_OFFLINE_ROUTES_H */
