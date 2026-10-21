/**
 * ra_cache.c - offline RetroAchievements data cache. See ra_cache.h.
 */
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_lite.h"
#include "logger.h"
#include "ra_cache.h"
#include "ra_util.h"

#define MAX_HASHES 1024

typedef struct {
    char md5[33];
    long gameid;
} ra_hash_entry;

static ra_hash_entry g_hashes[MAX_HASHES];
static int g_count;
static bool g_account_loaded;
static char g_user[64];
static char g_token[128];
static long g_score;
static long g_softcore;

static void lower(char *s)
{
    for (; *s != '\0'; s++)
        *s = (char)tolower((unsigned char)*s);
}

/** (Re)read cache/hashes.txt into g_hashes. Keeps the old table when the
 *  file cannot be opened, so a busy SD never empties a working cache. */
static void read_hashes(void)
{
    char path[600];
    ra_path(path, sizeof path, "cache/hashes.txt");

    FILE *f = fopen(path, "r");
    if (f == NULL)
        return;

    g_count = 0;
    char line[160];
    while (g_count < MAX_HASHES && fgets(line, sizeof line, f) != NULL) {
        char md5[64];
        long id;
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;
        if (sscanf(line, "%63s %ld", md5, &id) == 2) {
            ra_hash_entry *e = &g_hashes[g_count];
            memset(e, 0, sizeof *e);
            strncpy(e->md5, md5, 32);
            e->md5[32] = '\0';
            lower(e->md5);
            e->gameid = id;
            g_count++;
        }
    }
    fclose(f);
}

bool ra_cache_load(void)
{
    static bool g_tried;
    if (!g_tried) {
        g_tried = true;
        read_hashes();
    }
    return g_count > 0;
}

static bool find_gameid(const char *norm, long *gameid)
{
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_hashes[i].md5, norm) == 0) {
            *gameid = g_hashes[i].gameid;
            return true;
        }
    }
    return false;
}

int ra_cache_count(void)
{
    return g_count;
}

bool ra_cache_lookup_gameid(const char *md5, long *gameid)
{
    if (md5 == NULL || gameid == NULL)
        return false;

    char norm[33];
    strncpy(norm, md5, sizeof norm - 1);
    norm[sizeof norm - 1] = '\0';
    lower(norm);

    if (find_gameid(norm, gameid))
        return true;

    /* Miss: games cached after boot (fetch-cache.py + copy) only show up
     * in hashes.txt. Re-read it once so they work without a reboot. */
    read_hashes();
    logger_log("cache: reloaded hashes.txt on miss (%d entries)", g_count);
    return find_gameid(norm, gameid);
}

char *ra_cache_read_patch(long gameid, size_t *len)
{
    return ra_cache_read_file("cache/%ld/patch.json", gameid, len);
}

char *ra_cache_read_sets(long gameid, size_t *len)
{
    return ra_cache_read_file("cache/%ld/sets.json", gameid, len);
}

char *ra_cache_read_file(const char *fmt, long gameid, size_t *len)
{
    char path[600];
    ra_path(path, sizeof path, fmt, gameid);

    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz <= 0 || sz > RA_PATCH_MAX) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buf = malloc((size_t)sz + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[sz] = '\0';
    fclose(f);
    if (len != NULL)
        *len = (size_t)sz;
    return buf;
}

/** mkdir that treats "already there" as success. */
static bool ensure_dir(const char *path)
{
    return mkdir(path, 0777) == 0 || errno == EEXIST;
}

bool ra_cache_store_sets(const char *md5, long gameid,
                         const char *data, size_t len)
{
    char dir[600], path[600], tmp[600];
    if (gameid <= 0 || data == NULL || len == 0 || len > RA_PATCH_MAX)
        return false;

    ra_path(dir, sizeof dir, "cache/%ld", gameid);
    ra_path(path, sizeof path, "cache");
    if (!ensure_dir(ra_base()) || !ensure_dir(path) || !ensure_dir(dir)) {
        logger_log("cache: cannot create %s (errno=%d)", dir, errno);
        return false;
    }

    /* Write a temp file, then swap it in: a crash mid-write never leaves
     * a half sets.json that RetroArch would choke on. */
    ra_path(path, sizeof path, "cache/%ld/sets.json", gameid);
    ra_path(tmp, sizeof tmp, "cache/%ld/sets.json.tmp", gameid);
    FILE *f = fopen(tmp, "wb");
    if (f == NULL) {
        logger_log("cache: cannot write %s (errno=%d)", tmp, errno);
        return false;
    }
    bool ok = fwrite(data, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    remove(path); /* FAT rename does not replace an existing file */
    if (!ok || rename(tmp, path) != 0) {
        logger_log("cache: cannot save %s (errno=%d)", path, errno);
        remove(tmp);
        return false;
    }

    /* Link the ROM hash to the game, unless hashes.txt already does. */
    long known;
    if (md5 != NULL && strlen(md5) == 32 &&
        !ra_cache_lookup_gameid(md5, &known)) {
        char norm[33];
        snprintf(norm, sizeof norm, "%s", md5);
        lower(norm);
        ra_path(path, sizeof path, "cache/hashes.txt");
        f = fopen(path, "a");
        if (f == NULL) {
            logger_log("cache: cannot append %s (errno=%d)", path, errno);
            return false;
        }
        fprintf(f, "%s %ld\n", norm, gameid);
        fclose(f);
        read_hashes();
    }
    return true;
}

bool ra_cache_read_account(char *user, size_t usersz,
                           char *token, size_t tokensz,
                           long *score, long *softcore)
{
    if (!g_account_loaded) {
        char path[600];
        char *json;

        strcpy(g_user, "raoffline");
        strcpy(g_token, "offline-token");
        g_score = 0;
        g_softcore = 0;

        ra_path(path, sizeof path, "account.json");
        FILE *f = fopen(path, "rb");
        if (f != NULL) {
            static char buf[4096];
            size_t n = fread(buf, 1, sizeof buf - 1, f);
            fclose(f);
            buf[n] = '\0';
            json = buf;
            json_get_string(json, "User", g_user, sizeof g_user);
            json_get_string(json, "Token", g_token, sizeof g_token);
            json_get_int(json, "Score", &g_score);
            json_get_int(json, "SoftcoreScore", &g_softcore);
        }
        g_account_loaded = true;
    }

    if (user != NULL)
        snprintf(user, usersz, "%s", g_user);
    if (token != NULL)
        snprintf(token, tokensz, "%s", g_token);
    if (score != NULL)
        *score = g_score;
    if (softcore != NULL)
        *softcore = g_softcore;
    return true;
}
