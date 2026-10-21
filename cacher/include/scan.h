/**
 * scan.h - find every game on the memory card (SD), hash it like RetroArch does
 * (rcheevos), and cache its achievement data for offline play.
 *
 * Pure C, no switch.h: host-testable.
 */
#ifndef RA_CACHER_SCAN_H
#define RA_CACHER_SCAN_H

#include <stdbool.h>

#include "ra_transport.h"

typedef struct {
    char *path;        /**< full path, malloc'd                      */
    unsigned console;  /**< rcheevos console id, 0 = guess by ext     */
} scan_game;

typedef struct {
    scan_game *items;
    int count;
    int cap;
} scan_list;

/** What happened to one game. */
typedef enum {
    SCAN_CACHED,      /**< fetched and saved now                     */
    SCAN_ALREADY,     /**< already in the cache                      */
    SCAN_NO_SET,      /**< the server has no achievements for it     */
    SCAN_NO_HASH,     /**< rcheevos could not hash the file          */
    SCAN_FAILED       /**< network or server error; try again later  */
} scan_result;

typedef struct {
    int cached, already, no_set, no_hash, failed;
    bool auth_error;  /**< the server refused the account token       */
    bool cancelled;
} scan_stats;

/** Called after each game. title is the game's name when known. */
typedef void (*scan_progress)(void *ud, int idx, int total,
                              const char *path, scan_result r,
                              const char *title);
/** Polled between games; return true to stop early. */
typedef bool (*scan_cancel)(void *ud);

/** Walk each root folder and collect game files. It skips missing
 *  roots, track files of disc games (.bin next to a .cue/.gdi), and
 *  non-game files (saves, images, text). */
void scan_collect(const char *const *roots, int nroots, scan_list *out);

void scan_free(scan_list *list);

/** Hash and cache every game in list. It skips games that the server
 *  already said have no achievements, unless recheck is true. */
void scan_run(const scan_list *list, const ra_transport *t,
              const char *user, const char *token, bool recheck,
              scan_progress progress, scan_cancel cancel, void *ud,
              scan_stats *stats);

#endif /* RA_CACHER_SCAN_H */
