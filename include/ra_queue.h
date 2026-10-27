/**
 * ra_queue.h - offline unlock queue (Phase 3).
 *
 * Appends are JSON Lines (one JSON object per line) to <base>/queue.json.
 * JSONL keeps every append a single atomic-ish line write: no read-modify-
 * write of a big array, and a crash can only lose the line being written,
 * never corrupt earlier ones. Phase 4 reads lines, forwards them, then
 * rewrites the file without the synced lines.
 */
#ifndef RA_OFFLINE_QUEUE_H
#define RA_OFFLINE_QUEUE_H

#include <stdbool.h>

/** Append one entry. qs/body are the RAW urlencoded request pieces that
 *  produced the unlock (either may be NULL/empty); Phase 4 replays them
 *  verbatim against the real server. */
bool ra_queue_append(const char *qs, const char *body);

/** RetroAchievements requirement (RAdmin, 2026-10): the sync worker
 *  drops each queued entry 7 days after the player earns it, whether
 *  or not the module ever sent it, because achievement logic changes
 *  frequently. The sync worker never uploads entries older than
 *  this. */
#define RA_QUEUE_TTL_S (7LL * 24 * 60 * 60)

/** Lock/unlock the queue + file (used by the sync worker). */
void ra_queue_lock(void);
void ra_queue_unlock(void);

#endif /* RA_OFFLINE_QUEUE_H */
