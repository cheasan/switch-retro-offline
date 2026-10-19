/**
 * ra_queue.c - offline unlock queue. See ra_queue.h.
 */
#include <stdio.h>
#include <string.h>

#ifdef __SWITCH__
#include <switch.h>
static Mutex g_qlock;
static bool g_lock_ready;
#define QLOCK() do { \
        if (!g_lock_ready) { mutexInit(&g_qlock); g_lock_ready = true; } \
        mutexLock(&g_qlock); \
    } while (0)
#define QUNLOCK() mutexUnlock(&g_qlock)
#else
#include <pthread.h>
static pthread_mutex_t g_qlock = PTHREAD_MUTEX_INITIALIZER;
#define QLOCK() pthread_mutex_lock(&g_qlock)
#define QUNLOCK() pthread_mutex_unlock(&g_qlock)
#endif

#include "json_lite.h"
#include "ra_queue.h"
#include "ra_util.h"

/* RetroArch award payloads are a few hundred bytes; this is generous. */
#define ESC_Q_CAP 1024
#define ESC_B_CAP 4096

bool ra_queue_append(const char *qs, const char *body)
{
    bool has_q = qs != NULL && qs[0] != '\0';
    bool has_b = body != NULL && body[0] != '\0';
    if (!has_q && !has_b)
        return false;

    char esc_q[ESC_Q_CAP];
    char esc_b[ESC_B_CAP];
    json_escape(has_q ? qs : "", esc_q, sizeof esc_q);
    json_escape(has_b ? body : "", esc_b, sizeof esc_b);

    char path[600];
    ra_path(path, sizeof path, "queue.json");

    bool ok = false;
    QLOCK();
    FILE *f = fopen(path, "a");
    if (f != NULL) {
        if (fprintf(f, "{\"ts\":%lld,\"qs\":\"%s\",\"body\":\"%s\"}\n",
                    ra_now(), esc_q, esc_b) > 0)
            ok = true;
        fclose(f);
    }
    QUNLOCK();
    return ok;
}
