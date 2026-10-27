/**
 * ra_sync.c - replay queued unlock payloads to the real RetroAchievements.
 *
 * Concurrency safety (server thread may append while we run):
 *   1. Under the queue lock: read queue.json -> snapshot of N lines.
 *   2. Unlock; run the network round-trips (slow, no lock held).
 *   3. Under the lock: re-read the file, rewrite it keeping
 *      - lines 0..N-1 that FAILED, and
 *      - every line at index >= N (appended during step 2).
 *      Appends only ever add lines at the end, so indexes 0..N-1 are
 *      still the snapshot lines.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_lite.h"
#include "logger.h"
#include "ra_queue.h"
#include "ra_sync.h"
#include "ra_util.h"

/** Refuse to load more than 2 MB of queue (heap budget); the rest syncs
 *  on the next pass. */
#define QUEUE_SNAPSHOT_MAX (2 * 1024 * 1024)

/* Shared with ra_queue.c's lock (defined there). */
void ra_queue_lock(void);
void ra_queue_unlock(void);

static char *read_file_locked(size_t *len_out)
{
    char path[600];
    ra_path(path, sizeof path, "queue.json");
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) {
        fclose(f);
        return NULL;
    }
    if (sz > QUEUE_SNAPSHOT_MAX)
        sz = QUEUE_SNAPSHOT_MAX;

    char *buf = malloc((size_t)sz + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    if (len_out != NULL)
        *len_out = n;
    return buf;
}

long ra_sync_pending(void)
{
    ra_queue_lock();
    size_t len;
    char *buf = read_file_locked(&len);
    ra_queue_unlock();
    if (buf == NULL)
        return 0;

    long count = 0;
    for (char *p = buf; *p != '\0'; p++)
        if (*p == '\n')
            count++;
    /* trailing line without newline still counts */
    if (len > 0 && buf[len - 1] != '\n')
        count++;
    free(buf);
    return count;
}

bool ra_sync_run(const ra_transport *t, ra_sync_result *out)
{
    ra_sync_result res = { 0, 0, 0, 0 };
    if (t == NULL || t->post == NULL)
        return false;

    /* 1. Snapshot under lock. */
    ra_queue_lock();
    size_t snap_len;
    char *snapshot = read_file_locked(&snap_len);
    ra_queue_unlock();
    if (snapshot == NULL) {
        if (out != NULL) {
            out->total = out->synced = out->failed = out->expired = 0;
        }
        return true; /* nothing queued */
    }

    /* Count lines in the snapshot. */
    int nlines = 0;
    for (size_t i = 0; i < snap_len; i++)
        if (snapshot[i] == '\n')
            nlines++;
    if (snap_len > 0 && snapshot[snap_len - 1] != '\n')
        nlines++;

    bool *synced = calloc((size_t)nlines, 1);
    if (synced == NULL) {
        free(snapshot);
        return false;
    }

    /* 2. Replay each line (no lock held during network I/O). */
    char *saveptr = NULL;
    int idx = 0;
    long long now = ra_now();
    for (char *line = strtok_r(snapshot, "\n", &saveptr);
         line != NULL;
         line = strtok_r(NULL, "\n", &saveptr), idx++) {
        res.total++;

        if (line[0] == '\0') {
            synced[idx] = true; /* blank: drop */
            continue;
        }

        /* RetroAchievements rule (RAdmin, 2026-10): never replay an
         * entry older than 7 days - achievement logic changes
         * frequently, so a stale unlock may no longer be valid.
         * Dropped here regardless of send success; ts defaults to 0
         * (malformed line or pre-0.4.6 uptime stamp), which reads as
         * ancient and drops the line too. A clock behind ts (console
         * clock reset) keeps the line: the negative age does not pass
         * the time-to-live (TTL) check. */
        long ts = 0;
        json_get_int(line, "ts", &ts);
        if (now - (long long)ts > RA_QUEUE_TTL_S) {
            synced[idx] = true; /* expired: drop without sending */
            res.expired++;
            logger_log("sync: DROP expired (%lld days old), not sent",
                       (now - (long long)ts) / (24 * 60 * 60));
            continue;
        }

        char qs[1024];
        char body[4096];
        json_get_string(line, "qs", qs, sizeof qs);
        json_get_string(line, "body", body, sizeof body);

        const char *send_body = body[0] != '\0' ? body : qs;
        char respbuf[512];
        int status = t->post(t->ctx, NULL, send_body,
                             respbuf, sizeof respbuf);

        bool ok = false;
        char err[128] = "";
        if (status == 200) {
            json_get_bool(respbuf, "Success", &ok);
            json_get_string(respbuf, "Error", err, sizeof err);
        }

        /* "User already has ..." = the unlock is on the server already.
         * rcheevos counts it as success too; retrying would loop forever. */
        if (!ok && strncmp(err, "User already has", 16) == 0) {
            synced[idx] = true;
            res.synced++;
            logger_log("sync: ok, already on server (%s)", send_body);
        } else if (ok) {
            synced[idx] = true;
            res.synced++;
            logger_log("sync: ok (%s)", send_body);
        } else {
            res.failed++;
            logger_log("sync: FAILED status=%d error='%s', keeping (%s)",
                       status, err, send_body);
        }
    }
    free(snapshot);

    /* 3. Rewrite under lock, preserving appends made during step 2. */
    ra_queue_lock();
    size_t cur_len;
    char *current = read_file_locked(&cur_len);
    bool write_ok = true;

    char *kept = malloc(cur_len + snap_len + 16);
    if (current == NULL || kept == NULL) {
        write_ok = false; /* keep the file as-is; retry next pass */
        free(current);
    } else {
        size_t o = 0;
        int j = 0;
        char *saveptr2 = NULL;
        for (char *line = strtok_r(current, "\n", &saveptr2);
             line != NULL;
             line = strtok_r(NULL, "\n", &saveptr2), j++) {
            bool keep;
            if (j < nlines)
                keep = !synced[j];
            else
                keep = true; /* appended during sync */
            if (keep) {
                size_t ll = strlen(line);
                memcpy(kept + o, line, ll);
                o += ll;
                kept[o++] = '\n';
            }
        }

        char path[600];
        char tmp[640];
        ra_path(path, sizeof path, "queue.json");
        ra_path(tmp, sizeof tmp, "queue.json.tmp");
        FILE *f = fopen(tmp, "w");
        if (f != NULL) {
            if (o > 0 && fwrite(kept, 1, o, f) != o)
                write_ok = false;
            if (fclose(f) != 0)
                write_ok = false;
            if (write_ok) {
                if (remove(path) != 0) { /* rename over existing needs this on some devoptab */
                    /* ignore; rename may still succeed */
                }
                if (rename(tmp, path) != 0)
                    write_ok = false;
            }
            if (!write_ok)
                remove(tmp);
        } else {
            write_ok = false;
        }
    }
    ra_queue_unlock();

    free(kept);
    free(current);
    free(synced);

    if (out != NULL)
        *out = res;
    logger_log("sync: total=%d synced=%d failed=%d expired=%d "
               "(file kept: %s)",
               res.total, res.synced, res.failed, res.expired,
               write_ok ? "yes" : "NO (write error)");
    return write_ok;
}
