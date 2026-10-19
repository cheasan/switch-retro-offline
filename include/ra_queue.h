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

#endif /* RA_OFFLINE_QUEUE_H */
