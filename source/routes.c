/**
 * routes.c - request dispatch (Phase 2: plumbing + stubs).
 *
 * Phase 3 replaces the dorequest.php stubs with cached RetroAchievements
 * data and adds the unlock queue. Response shapes here are intentionally
 * tiny; correctness against real RetroArch expectations is a Phase 3 job.
 *
 * Pure C, no switch.h: host-testable.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_server.h"
#include "logger.h"
#include "routes.h"

/* ------------------------------------------------------------------ */
/* urlencoded helpers                                                  */
/* ------------------------------------------------------------------ */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool param_get(const char *src, const char *key, char *out, size_t outsz)
{
    if (src == NULL || out == NULL || outsz == 0)
        return false;

    size_t key_len = strlen(key);
    const char *p = src;

    while (*p != '\0') {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            size_t o = 0;
            p += key_len + 1;
            while (*p != '\0' && *p != '&' && o + 1 < outsz) {
                if (*p == '%' && hexval(p[1]) >= 0 && hexval(p[2]) >= 0) {
                    out[o++] = (char)(hexval(p[1]) * 16 + hexval(p[2]));
                    p += 3;
                } else if (*p == '+') {
                    out[o++] = ' ';
                    p++;
                } else {
                    out[o++] = *p++;
                }
            }
            out[o] = '\0';
            return true;
        }
        p = strchr(p, '&');
        if (p == NULL)
            break;
        p++;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* response helpers                                                    */
/* ------------------------------------------------------------------ */

static void resp_text(http_response *resp, int status, const char *text)
{
    resp->status = status;
    resp->content_type = "text/plain";
    size_t len = strlen(text);
    resp->body = malloc(len + 1);
    if (resp->body != NULL) {
        memcpy(resp->body, text, len + 1);
        resp->body_len = len;
    }
}

static void resp_json(http_response *resp, int status, const char *json)
{
    resp->status = status;
    resp->content_type = "application/json";
    size_t len = strlen(json);
    resp->body = malloc(len + 1);
    if (resp->body != NULL) {
        memcpy(resp->body, json, len + 1);
        resp->body_len = len;
    }
}

/* ------------------------------------------------------------------ */
/* dorequest.php stubs (Phase 3 makes these real)                      */
/* ------------------------------------------------------------------ */

/** Minimal login shape RetroArch accepts; offline identity for now. */
static const char STUB_LOGIN[] =
    "{\"Success\":true,\"User\":\"raoffline\",\"Token\":\"offline-token\","
    "\"Score\":0,\"SoftcoreScore\":0,\"Messages\":[]}";

static void handle_dorequest(const http_request *req, http_response *resp)
{
    char r[32];
    if (!param_get(req->query, "r", r, sizeof r))
        param_get(req->body, "r", r, sizeof r);

    logger_log("route: dorequest r=%s", r[0] ? r : "<none>");

    if (strcmp(r, "login") == 0 || strcmp(r, "login2") == 0) {
        resp_json(resp, 200, STUB_LOGIN);
        return;
    }
    /* Everything else gets a harmless success until Phase 3 wires the
     * per-r cached responses (gameid, patch, unlock/awardachievement,
     * startsession, ping, ...). */
    resp_json(resp, 200, "{\"Success\":true}");
}

/* ------------------------------------------------------------------ */
/* dispatch                                                            */
/* ------------------------------------------------------------------ */

void route_dispatch(const http_request *req, http_response *resp)
{
    memset(resp, 0, sizeof *resp);

    if (strcmp(req->path, "/ping") == 0) {
        resp_text(resp, 200, "pong");
        return;
    }
    if (strcmp(req->path, "/health") == 0) {
        resp_json(resp, 200, "{\"status\":\"ok\",\"phase\":2}");
        return;
    }
    if (strcmp(req->path, "/dorequest.php") == 0) {
        if (strcmp(req->method, "POST") != 0 && strcmp(req->method, "GET") != 0) {
            resp_json(resp, 405, "{\"Success\":false,\"Error\":\"method\"}");
            return;
        }
        handle_dorequest(req, resp);
        return;
    }
    logger_log("route: 404 %s", req->path);
    resp_text(resp, 404, "not found\n");
}
