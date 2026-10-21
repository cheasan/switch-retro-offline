/**
 * scan.c - find, hash and cache every game. See scan.h.
 */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "json_lite.h"
#include "logger.h"
#include "ra_cache.h"
#include "ra_util.h"
#include "rc_consoles.h"
#include "rc_hash.h"
#include "scan.h"

#define MAX_DEPTH 6
#define MAX_HASHES_PER_GAME 8

/* ------------------------------------------------------------------ */
/* collect                                                             */
/* ------------------------------------------------------------------ */

/** Folder name -> console. The names cover Tico, ES-DE and RetroArch
 *  layouts; anything else falls back to rcheevos' extension guess. */
static const struct {
    const char *name;
    unsigned console;
} k_folders[] = {
    { "nes", RC_CONSOLE_NINTENDO },          { "famicom", RC_CONSOLE_NINTENDO },
    { "fds", RC_CONSOLE_FAMICOM_DISK_SYSTEM },
    { "snes", RC_CONSOLE_SUPER_NINTENDO },   { "sfc", RC_CONSOLE_SUPER_NINTENDO },
    { "gb", RC_CONSOLE_GAMEBOY },            { "gbc", RC_CONSOLE_GAMEBOY_COLOR },
    { "gba", RC_CONSOLE_GAMEBOY_ADVANCE },   { "nds", RC_CONSOLE_NINTENDO_DS },
    { "n64", RC_CONSOLE_NINTENDO_64 },       { "3ds", RC_CONSOLE_NINTENDO_3DS },
    { "gc", RC_CONSOLE_GAMECUBE },           { "gamecube", RC_CONSOLE_GAMECUBE },
    { "genesis", RC_CONSOLE_MEGA_DRIVE },    { "megadrive", RC_CONSOLE_MEGA_DRIVE },
    { "sega-cd", RC_CONSOLE_SEGA_CD },       { "segacd", RC_CONSOLE_SEGA_CD },
    { "saturn", RC_CONSOLE_SATURN },         { "dc", RC_CONSOLE_DREAMCAST },
    { "dreamcast", RC_CONSOLE_DREAMCAST },   { "psx", RC_CONSOLE_PLAYSTATION },
    { "ps1", RC_CONSOLE_PLAYSTATION },       { "playstation", RC_CONSOLE_PLAYSTATION },
    { "psp", RC_CONSOLE_PSP },               { "game-gear", RC_CONSOLE_GAME_GEAR },
    { "gamegear", RC_CONSOLE_GAME_GEAR },    { "gg", RC_CONSOLE_GAME_GEAR },
    { "master-system", RC_CONSOLE_MASTER_SYSTEM },
    { "mastersystem", RC_CONSOLE_MASTER_SYSTEM },
    { "sms", RC_CONSOLE_MASTER_SYSTEM },     { "pce", RC_CONSOLE_PC_ENGINE },
    { "pcengine", RC_CONSOLE_PC_ENGINE },    { "arcade", RC_CONSOLE_ARCADE },
    { "fbneo", RC_CONSOLE_ARCADE },          { "mame", RC_CONSOLE_ARCADE },
    { "neogeo", RC_CONSOLE_ARCADE },         { "naomi", RC_CONSOLE_ARCADE },
    { "atomiswave", RC_CONSOLE_ARCADE },
};

/** Files that can be a game. Everything else (saves, art, text) is not. */
static const char *const k_game_ext[] = {
    "nes", "fds", "unf", "unif", "sfc", "smc", "fig", "swc", "gb", "gbc",
    "gba", "nds", "dsi", "n64", "z64", "v64", "3ds", "cci", "cxi", "md",
    "gen", "smd", "32x", "sms", "gg", "pce", "sgx", "cue", "gdi", "iso",
    "cso", "pbp", "chd", "gcm", "zip", "bin",
};

/** Sub-folders of a ROM folder that never hold games. */
static const char *const k_skip_dirs[] = {
    "media", "images", "videos", "manuals", "saves", "states", "snaps",
    "boxart", "screenshots", "covers",
};

static const char *ext_of(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot ? dot + 1 : "";
}

static bool in_list(const char *s, const char *const *list, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (strcasecmp(s, list[i]) == 0)
            return true;
    return false;
}

static unsigned folder_console(const char *name)
{
    for (size_t i = 0; i < sizeof k_folders / sizeof k_folders[0]; i++)
        if (strcasecmp(name, k_folders[i].name) == 0)
            return k_folders[i].console;
    return 0;
}

static bool is_disc_console(unsigned c)
{
    return c == RC_CONSOLE_PLAYSTATION || c == RC_CONSOLE_DREAMCAST ||
           c == RC_CONSOLE_SATURN || c == RC_CONSOLE_SEGA_CD;
}

static void list_add(scan_list *l, const char *path, unsigned console)
{
    if (l->count == l->cap) {
        int cap = l->cap ? l->cap * 2 : 64;
        scan_game *items = realloc(l->items, (size_t)cap * sizeof *items);
        if (items == NULL)
            return;
        l->items = items;
        l->cap = cap;
    }
    char *copy = strdup(path);
    if (copy == NULL)
        return;
    l->items[l->count].path = copy;
    l->items[l->count].console = console;
    l->count++;
}

static bool is_dir(const char *dir, const struct dirent *e)
{
#ifdef DT_DIR
    if (e->d_type == DT_DIR)
        return true;
    if (e->d_type != DT_UNKNOWN)
        return false;
#endif
    char path[1024];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void walk(const char *dir, unsigned console, int depth, scan_list *out)
{
    if (depth > MAX_DEPTH)
        return;
    DIR *d = opendir(dir);
    if (d == NULL)
        return;

    /* Read the folder once: the .bin rule needs to know about .cue/.gdi
     * before it looks at any file. */
    char **names = NULL;
    bool *dirs = NULL;
    int n = 0, cap = 0;
    bool has_index = false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            char **nn = realloc(names, (size_t)cap * sizeof *nn);
            bool *nd = realloc(dirs, (size_t)cap * sizeof *nd);
            if (nn != NULL)
                names = nn;
            if (nd != NULL)
                dirs = nd;
            if (nn == NULL || nd == NULL)
                break;
        }
        names[n] = strdup(e->d_name);
        if (names[n] == NULL)
            break;
        dirs[n] = is_dir(dir, e);
        const char *x = ext_of(e->d_name);
        if (!dirs[n] && (strcasecmp(x, "cue") == 0 || strcasecmp(x, "gdi") == 0))
            has_index = true;
        n++;
    }
    closedir(d);

    for (int i = 0; i < n; i++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        if (dirs[i]) {
            if (!in_list(names[i], k_skip_dirs,
                         sizeof k_skip_dirs / sizeof k_skip_dirs[0])) {
                unsigned sub = folder_console(names[i]);
                walk(path, sub ? sub : console, depth + 1, out);
            }
            continue;
        }
        const char *x = ext_of(names[i]);
        if (!in_list(x, k_game_ext, sizeof k_game_ext / sizeof k_game_ext[0]))
            continue;
        /* Disc track files: the .cue/.gdi stands for the whole disc. */
        if (strcasecmp(x, "bin") == 0 && (has_index || is_disc_console(console)))
            continue;
        /* Zip files are games only in arcade folders. */
        if (strcasecmp(x, "zip") == 0 && console != RC_CONSOLE_ARCADE)
            continue;
        list_add(out, path, console);
    }

    for (int i = 0; i < n; i++)
        free(names[i]);
    free(names);
    free(dirs);
}

void scan_collect(const char *const *roots, int nroots, scan_list *out)
{
    memset(out, 0, sizeof *out);
    for (int i = 0; i < nroots; i++)
        walk(roots[i], 0, 0, out);
}

void scan_free(scan_list *list)
{
    for (int i = 0; i < list->count; i++)
        free(list->items[i].path);
    free(list->items);
    memset(list, 0, sizeof *list);
}

/* ------------------------------------------------------------------ */
/* hash + cache                                                        */
/* ------------------------------------------------------------------ */

/** Hashes in a growable list (no-set list and this run's seen list). */
typedef struct {
    char (*h)[33];
    int count;
    int cap;
} hash_set;

static bool set_has(const hash_set *s, const char *hash)
{
    for (int i = 0; i < s->count; i++)
        if (strcmp(s->h[i], hash) == 0)
            return true;
    return false;
}

static void set_add(hash_set *s, const char *hash)
{
    if (s->count == s->cap) {
        int cap = s->cap ? s->cap * 2 : 64;
        char (*h)[33] = realloc(s->h, (size_t)cap * sizeof *h);
        if (h == NULL)
            return;
        s->h = h;
        s->cap = cap;
    }
    snprintf(s->h[s->count++], 33, "%s", hash);
}

/** cache/nogame.txt: hashes the server knows no achievements for. */
static void load_no_set(hash_set *s)
{
    char path[600];
    ra_path(path, sizeof path, "cache/nogame.txt");
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return;
    char line[64];
    while (fgets(line, sizeof line, f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 32)
            set_add(s, line);
    }
    fclose(f);
}

static void remember_no_set(const char *hash)
{
    char path[600];
    ra_path(path, sizeof path, "cache/nogame.txt");
    FILE *f = fopen(path, "a");
    if (f == NULL)
        return;
    fprintf(f, "%s\n", hash);
    fclose(f);
}

static bool sets_file_exists(long gameid)
{
    char path[600];
    ra_path(path, sizeof path, "cache/%ld/sets.json", gameid);
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    fclose(f);
    return true;
}

/** Hash one file the way RetroArch would. Returns the number of
 *  candidate hashes (a file with no known console can have several). */
static int hash_game(const scan_game *g, char hashes[][33])
{
    if (g->console != 0)
        return rc_hash_generate_from_file(hashes[0], g->console, g->path) ? 1 : 0;

    int n = 0;
    rc_hash_iterator_t it;
    rc_hash_initialize_iterator(&it, g->path, NULL, 0);
    while (n < MAX_HASHES_PER_GAME && rc_hash_iterate(hashes[n], &it))
        n++;
    rc_hash_destroy_iterator(&it);
    return n;
}

/** Ask the server about one hash. Sets *title on success. */
static scan_result fetch_one(const ra_transport *t, const char *user,
                             const char *token, const char *hash,
                             char *title, size_t titlesz, bool *auth_error)
{
    char body[512];
    snprintf(body, sizeof body, "r=achievementsets&u=%s&t=%s&m=%s",
             user, token, hash);

    int status;
    size_t len;
    char *reply = t->fetch(t->ctx, body, &status, &len);
    if (reply == NULL) {
        logger_log("cacher: %s fetch failed (no reply)", hash);
        return SCAN_FAILED;
    }

    bool ok = false;
    long gameid = 0;
    char err[160] = "";
    json_get_bool(reply, "Success", &ok);
    json_get_int(reply, "GameId", &gameid);
    json_get_string(reply, "Title", title, titlesz);
    json_get_string(reply, "Error", err, sizeof err);

    scan_result r;
    if (status == 401 || status == 403 || strstr(err, "token") != NULL ||
        strstr(err, "credentials") != NULL) {
        logger_log("cacher: server refused the account (status=%d '%s')",
                   status, err);
        *auth_error = true;
        r = SCAN_FAILED;
    } else if (status != 200 || !ok) {
        logger_log("cacher: %s failed (status=%d '%s')", hash, status, err);
        r = SCAN_FAILED;
    } else if (gameid <= 0) {
        remember_no_set(hash);
        r = SCAN_NO_SET;
    } else if (ra_cache_store_sets(hash, gameid, reply, len)) {
        logger_log("cacher: %s -> %ld '%s' cached (%zu bytes)",
                   hash, gameid, title, len);
        r = SCAN_CACHED;
    } else {
        r = SCAN_FAILED;
    }
    free(reply);
    return r;
}

void scan_run(const scan_list *list, const ra_transport *t,
              const char *user, const char *token, bool recheck,
              scan_progress progress, scan_cancel cancel, void *ud,
              scan_stats *stats)
{
    memset(stats, 0, sizeof *stats);
    hash_set no_set = { 0 }, seen = { 0 };
    load_no_set(&no_set);
    ra_cache_load();

    for (int i = 0; i < list->count; i++) {
        if (cancel != NULL && cancel(ud)) {
            stats->cancelled = true;
            break;
        }
        const scan_game *g = &list->items[i];
        char hashes[MAX_HASHES_PER_GAME][33];
        char title[128] = "";
        int nh = hash_game(g, hashes);

        scan_result r = nh == 0 ? SCAN_NO_HASH : SCAN_NO_SET;
        for (int h = 0; h < nh && r != SCAN_CACHED && r != SCAN_ALREADY &&
                        !stats->auth_error; h++) {
            long gameid;
            if (set_has(&seen, hashes[h]) ||
                (ra_cache_lookup_gameid(hashes[h], &gameid) &&
                 sets_file_exists(gameid))) {
                r = SCAN_ALREADY;
            } else if (!recheck && set_has(&no_set, hashes[h])) {
                r = SCAN_NO_SET;
            } else {
                scan_result one = fetch_one(t, user, token, hashes[h], title,
                                            sizeof title, &stats->auth_error);
                if (one != SCAN_NO_SET || r != SCAN_FAILED)
                    r = one;
                if (one == SCAN_FAILED)
                    continue; /* a copy later in the list tries again */
            }
            set_add(&seen, hashes[h]);
        }

        switch (r) {
        case SCAN_CACHED:  stats->cached++;  break;
        case SCAN_ALREADY: stats->already++; break;
        case SCAN_NO_SET:  stats->no_set++;  break;
        case SCAN_NO_HASH: stats->no_hash++; break;
        case SCAN_FAILED:  stats->failed++;  break;
        }
        if (progress != NULL)
            progress(ud, i + 1, list->count, g->path, r, title);
        if (stats->auth_error)
            break;
    }
    free(no_set.h);
    free(seen.h);
}
