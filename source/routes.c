/**
 * routes.c - dorequest.php mocking (Phase 3).
 *
 * Serves the subset of the RetroAchievements API that RetroArch's cheevos
 * client (rcheevos) actually uses, from local SD cache:
 *
 *   r=login2 (and legacy r=login)  -> offline identity from account.json
 *   r=gameid&m=<cheevos md5>       -> game ID from cache/hashes.txt
 *   r=patch&g=<id>                 -> verbatim cache/<id>/patch.json
 *   r=startsession / r=ping        -> {"Success":true}
 *   r=awardachievement&a=..&h=..   -> appended to queue.json, ack'd
 *   r=submitnewticket / unknown r  -> harmless {"Success":true}
 *
 * Unlock payloads are queued as RAW urlencoded query/body (see ra_queue.h)
 * so Phase 4 can replay them byte-for-byte to the real server.
 *
 * Pure C, no switch.h: host-testable.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_server.h"
#include "json_lite.h"
#include "logger.h"
#include "ra_cache.h"
#include "ra_queue.h"
#include "ra_sync.h"
#include "ra_transport.h"
#include "routes.h"
#include "ra_util.h"

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

/** Try query first, then body. */
static bool req_param(const http_request *req, const char *key,
                      char *out, size_t outsz)
{
    return param_get(req->query, key, out, outsz) ||
           param_get(req->body, key, out, outsz);
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

/** Build a small JSON response via printf; on OOM fall back to minimal. */
static void resp_json(http_response *resp, int status, const char *fmt, ...)
{
    char buf[512];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof buf, fmt, va);
    va_end(va);

    resp->status = status;
    resp->content_type = "application/json";
    size_t len = strlen(buf);
    resp->body = malloc(len + 1);
    if (resp->body != NULL) {
        memcpy(resp->body, buf, len + 1);
        resp->body_len = len;
    }
}

/* ------------------------------------------------------------------ */
/* dorequest.php handlers                                              */
/* ------------------------------------------------------------------ */

static void doreq_login(http_response *resp)
{
    char user[64], token[128];
    long score, softcore;
    ra_cache_read_account(user, sizeof user, token, sizeof token,
                          &score, &softcore);
    logger_log("route: login as %s", user);
    resp_json(resp, 200,
              "{\"Success\":true,\"User\":\"%s\",\"Token\":\"%s\","
              "\"Score\":%ld,\"SoftcoreScore\":%ld,\"Messages\":[]}",
              user, token, score, softcore);
}

static void doreq_gameid(const http_request *req, http_response *resp)
{
    char md5[64];
    if (!req_param(req, "m", md5, sizeof md5) || strlen(md5) != 32) {
        logger_log("route: gameid MISSING/BAD hash (query='%s' body='%s')",
                   req->query ? req->query : "",
                   req->body ? req->body : "");
        resp_json(resp, 200,
                  "{\"Success\":false,\"Error\":\"missing hash\"}");
        return;
    }
    if (ra_cache_count() == 0)
        ra_cache_load();
    long gameid;
    if (ra_cache_lookup_gameid(md5, &gameid)) {
        logger_log("route: gameid %s -> %ld", md5, gameid);
        resp_json(resp, 200, "{\"Success\":true,\"GameID\":%ld}", gameid);
    } else {
        /* Makes RetroArch show "no achievements" for un-cached games. */
        logger_log("route: gameid %s -> unknown", md5);
        resp_json(resp, 200, "{\"Success\":false,\"Error\":\"Unknown Game\"}");
    }
}

static void doreq_patch(const http_request *req, http_response *resp)
{
    char g[24];
    if (!req_param(req, "g", g, sizeof g)) {
        resp_json(resp, 200, "{\"Success\":false,\"Error\":\"missing game\"}");
        return;
    }
    long gameid = strtol(g, NULL, 10);
    size_t len;
    char *patch = ra_cache_read_patch(gameid, &len);
    if (patch != NULL) {
        logger_log("route: patch %ld (%zu bytes, served verbatim)", gameid, len);
        resp->status = 200;
        resp->content_type = "application/json";
        resp->body = patch;
        resp->body_len = len;
    } else {
        logger_log("route: patch %ld not cached", gameid);
        resp_json(resp, 200,
                  "{\"Success\":false,\"Error\":\"not cached offline\"}");
    }
}

static void doreq_sets(const http_request *req, http_response *resp)
{
    /* RetroArch 1.22 (rcheevos v11) calls this with the ROM hash
     * (m=<md5>) BEFORE any gameid lookup; older tools may pass g=.
     * Accept both. */
    char g[24], md5[64];
    long gameid = -1;
    if (req_param(req, "g", g, sizeof g))
        gameid = strtol(g, NULL, 10);
    else if (req_param(req, "m", md5, sizeof md5) && strlen(md5) == 32) {
        if (ra_cache_count() == 0)
            ra_cache_load();
        ra_cache_lookup_gameid(md5, &gameid);
    }

    if (gameid <= 0) {
        logger_log("route: achievementsets unmatched (query='%s' body='%s')",
                   req->query ? req->query : "",
                   req->body ? req->body : "");
        resp_json(resp, 200,
                  "{\"Success\":false,\"Error\":\"not cached offline\"}");
        return;
    }
    size_t len;
    char *sets = ra_cache_read_sets(gameid, &len);
    if (sets != NULL) {
        logger_log("route: achievementsets %ld (%zu bytes, served verbatim)",
                   gameid, len);
        resp->status = 200;
        resp->content_type = "application/json";
        resp->body = sets;
        resp->body_len = len;
    } else {
        logger_log("route: achievementsets %ld not cached", gameid);
        resp_json(resp, 200,
                  "{\"Success\":false,\"Error\":\"not cached offline\"}");
    }
}

static void doreq_award(const http_request *req, http_response *resp)
{
    char a[24], h[8];
    if (!req_param(req, "a", a, sizeof a)) {
        resp_json(resp, 200,
                  "{\"Success\":false,\"Error\":\"missing id\"}");
        return;
    }
    if (!req_param(req, "h", h, sizeof h))
        strcpy(h, "0");

    if (ra_queue_append(req->query, req->body)) {
        logger_log("route: award achievement %s (hardcore=%s) queued", a, h);
        char user[64], token[128];
        long score, softcore;
        ra_cache_read_account(user, sizeof user, token, sizeof token,
                              &score, &softcore);
        resp_json(resp, 200,
                  "{\"Success\":true,\"AwardedAchievementID\":%s,"
                  "\"AchievementsRemaining\":0,\"Score\":%ld}",
                  a, score);
    } else {
        logger_log("route: award %s QUEUE WRITE FAILED", a);
        resp_json(resp, 200, "{\"Success\":false,\"Error\":\"queue\"}");
    }
}

/** Leaderboard entries queue for replay like unlocks; the ack needs a
 *  Response object or RetroArch shows a submit error (found on host). */
static void doreq_lboard(const http_request *req, http_response *resp)
{
    if (!ra_queue_append(req->query, req->body)) {
        logger_log("route: submitlbentry QUEUE WRITE FAILED");
        resp_json(resp, 200, "{\"Success\":false,\"Error\":\"queue\"}");
        return;
    }
    char lb[24];
    if (!req_param(req, "i", lb, sizeof lb))
        strcpy(lb, "0");
    logger_log("route: submitlbentry %s queued", lb);
    resp_json(resp, 200,
              "{\"Success\":true,\"Response\":{\"Score\":0,"
              "\"BestScore\":0,\"NewRank\":1,\"BestRank\":1}}");
}

static void handle_dorequest(const http_request *req, http_response *resp)
{
    char r[32];
    if (!req_param(req, "r", r, sizeof r)) {
        resp_json(resp, 200, "{\"Success\":false,\"Error\":\"missing r\"}");
        return;
    }

    if (strcmp(r, "login") == 0 || strcmp(r, "login2") == 0)
        doreq_login(resp);
    else if (strcmp(r, "gameid") == 0)
        doreq_gameid(req, resp);
    else if (strcmp(r, "patch") == 0)
        doreq_patch(req, resp);
    else if (strcmp(r, "achievementsets") == 0)
        doreq_sets(req, resp);
    else if (strcmp(r, "awardachievement") == 0)
        doreq_award(req, resp);
    else if (strcmp(r, "submitlbentry") == 0)
        doreq_lboard(req, resp);
    else {
        /* startsession, ping, submitnewticket, anything unexpected:
         * harmless success keeps RetroArch happy offline. */
        logger_log("route: dorequest r=%s (generic ok)", r);
        resp_json(resp, 200, "{\"Success\":true}");
    }
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
        resp_json(resp, 200,
                  "{\"status\":\"ok\",\"phase\":4,\"cache_hashes\":%d,"
                  "\"queue_pending\":%ld,\"online\":%s}",
                  ra_cache_count(), ra_sync_pending(),
                  ra_net_online() ? "true" : "false");
        return;
    }
    if (strcmp(req->path, "/sync-now") == 0) {
        ra_sync_result res;
        bool ok = ra_sync_run(ra_transport_default(), &res);
        resp_json(resp, ok ? 200 : 500,
                  "{\"Success\":%s,\"synced\":%d,\"failed\":%d,"
                  "\"left\":%ld}",
                  ok ? "true" : "false", res.synced, res.failed,
                  ra_sync_pending());
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
