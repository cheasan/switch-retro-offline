/**
 * ra_cache.h - offline RetroAchievements data cache (Phase 3).
 *
 * SD layout (created by tools/fetch-cache.py on the laptop, then copied):
 *
 *   <base>/account.json          {"User":..,"Token":..,"Score":..}
 *   <base>/cache/hashes.txt      lines of: <md5hex> <gameid>   (# = comment)
 *   <base>/cache/<gameid>/patch.json   verbatim dorequest patch response
 */
#ifndef RA_OFFLINE_CACHE_H
#define RA_OFFLINE_CACHE_H

#include <stdbool.h>
#include <stddef.h>

/** Refuse absurdly large patch files (heap is 4 MB total). */
#define RA_PATCH_MAX (2 * 1024 * 1024)

/** Load cache/hashes.txt into RAM. Call once at boot; safe to re-call. */
bool ra_cache_load(void);

/** Number of loaded ROM-hash entries. */
int ra_cache_count(void);

/** Look up the game ID for a 32-hex-char RetroArch cheevos hash. */
bool ra_cache_lookup_gameid(const char *md5, long *gameid);

/** Read cache/<gameid>/patch.json. Returns malloc'd buffer (NUL-
 *  terminated) and sets *len, or NULL when absent/oversized. */
char *ra_cache_read_patch(long gameid, size_t *len);

/** Read cache/<gameid>/sets.json verbatim (r=achievementsets payload
 *  for RetroArch 1.22+ / rcheevos v11). malloc'd, NUL-terminated;
 *  caller frees. NULL if not cached. */
char *ra_cache_read_sets(long gameid, size_t *len);

/** Shared reader used by the two above. */
char *ra_cache_read_file(const char *path_fmt, long gameid, size_t *len);

/** Offline identity from account.json (defaults when missing). */
bool ra_cache_read_account(char *user, size_t usersz,
                           char *token, size_t tokensz,
                           long *score, long *softcore);

#endif /* RA_OFFLINE_CACHE_H */
