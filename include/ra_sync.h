/**
 * ra_sync.h - offline unlock queue -> real server sync (Phase 4).
 */
#ifndef RA_OFFLINE_SYNC_H
#define RA_OFFLINE_SYNC_H

#include <stdbool.h>
#include <stddef.h>

#include "ra_transport.h"

typedef struct {
    int total;   /**< lines found in the snapshot       */
    int synced;  /**< acknowledged by the real server   */
    int failed;  /**< kept in the queue for next time   */
    int expired; /**< dropped: older than RA_QUEUE_TTL_S */
} ra_sync_result;

/** Lines currently waiting in queue.json. */
long ra_sync_pending(void);

/** One sync pass. Only the lines in the snapshot at start are processed;
 *  awards appended while the network round-trips run are preserved
 *  untouched. Failed lines stay queued. */
bool ra_sync_run(const ra_transport *t, ra_sync_result *out);

#endif /* RA_OFFLINE_SYNC_H */
