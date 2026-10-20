/**
 * main.c - ra-offline: offline RetroAchievements loopback proxy.
 *
 * Phase 1: Atmosphere sysmodule boilerplate.
 *  - Fixed 4 MiB newlib heap (sysmodule memory is strict).
 *  - SD logging to /switch/raoffline/sysmodule.log.
 *  - Paced main loop (svcSleepThread) that proves liveness via heartbeats.
 *
 * Phase 2 will add: bsd socket init + HTTP loopback server thread.
 * Phase 4 will add: nifm network detection + background sync.
 */

#include <stdio.h>
#include <malloc.h>
#include <switch.h>
#include "logger.h"
#include "http_server.h"
#include "routes.h"
#include "ra_cache.h"
#include "ra_sync.h"
#include "ra_transport.h"
#include "ra_util.h"

/** Size of the newlib heap backing all malloc()/stdio use. The plan
 *  allows ~4-8 MB for a sysmodule; start at 4 MB and only grow if a
 *  later phase measurably needs it. */
#define HEAP_SIZE              0x400000

/** Main loop pacing: sleep between ticks so the module never spins a core. */
#define LOOP_SLEEP_NS          100000000ULL   /* 100 ms */
#define HEARTBEAT_EVERY_TICKS  300            /* ~30 s  */

/** Loopback server (Phase 2). */
#define SERVER_PORT            8080
#define SERVER_THREAD_STACK    0x10000        /* 64 KB */
#define SERVER_THREAD_PRIO     0x2d           /* just below main (42)  */

/* We are a background system module, not an applet. Not for the NRO
 * build: homebrew gets its applet type from hbloader, and forcing None
 * would make appletMainLoop() exit instantly. */
#ifndef RA_NRO
u32 __nx_applet_type = AppletType_None;
#endif

static Thread g_server_thread;
static volatile bool g_running = true;
static Result g_nifm_rc = 1; /* non-zero = "not initialised" */
static Result g_ssl_rc = 1;  /* non-zero = "not initialised" */
static bool g_was_online = false;
static u32 g_last_sync_tick = 0;

/** Try a sync pass if we just came online, or retry every 10 min while
 *  online (catches previously-failed lines). Runs inline: the loop stalls
 *  for the round-trips, which is fine - only heartbeats pause. */
static void maybe_sync(bool online, u32 ticks)
{
    if (!online) {
        g_was_online = false;
        return;
    }
    bool rising = !g_was_online;
    g_was_online = true;

    bool due = rising || (ticks - g_last_sync_tick) > 6000; /* 10 min */
    if (!due || ra_sync_pending() == 0)
        return;

    g_last_sync_tick = ticks;
    logger_log("net: online (rising=%s), syncing %ld queued",
               rising ? "yes" : "no", ra_sync_pending());
    ra_sync_result res;
    ra_sync_run(ra_transport_default(), &res);
}

/**
 * libnx hook: hand newlib a fixed static heap.
 * Avoids svcSetHeapSize games entirely; the buffer lives in the module's
 * BSS so its cost is known at build time.
 */
void __libnx_initheap(void)
{
    static u8 inner_heap[HEAP_SIZE];
    extern void *fake_heap_start;
    extern void *fake_heap_end;

    fake_heap_start = inner_heap;
    fake_heap_end = inner_heap + sizeof(inner_heap);
}

/** libnx hook: acquire the services the module needs.
 *
 * BOOT DIAGNOSTICS (v0.4.1-diag): the hardware test showed the module
 * dying before main()'s logger existed, so nothing was ever written to
 * the log. Now SD logging comes up FIRST and every init stage writes a
 * marker. Whatever call hangs or crashes, the last marker present in
 * sysmodule.log names the culprit.
 */
void __appInit(void)
{
    Result rc = smInitialize();
    if (R_FAILED(rc))
        fatalThrow(rc);   /* sm is needed for fs itself; can't log yet */

    /* fs must be initialised before fsdevMountSdmc: we override the
     * libnx default __appInit, which normally does this for us. Its
     * absence is why the hardware run never produced a log. */
    rc = fsInitialize();
    if (R_FAILED(rc))
        fatalThrow(rc);

    bool sd = logger_init();
    if (!sd) {
        /* BOOT FORENSICS: never fail silently again. Surface exactly
         * which layer refused, via the fatal screen / crash report. */
        FsFileSystem pfs;
        Result mrc = fsOpenSdCardFileSystem(&pfs);
        if (R_FAILED(mrc))
            fatalThrow(mrc);      /* fsp-srv refused SD: code says why */
        Result crc = fsFsCreateDirectory(&pfs, "/switch/raoffline");
        fsFsClose(&pfs);
        if (R_FAILED(crc))
            fatalThrow(crc);      /* raw fs works, dir create failed */
        fatalThrow(MAKERESULT(164, 2)); /* raw fs fine; fopen() failed */
    }
    logger_log("init: stage 1 enter (sm+fs ok, sd log %s)", sd ? "up" : "DOWN");

    /* Pin the HOS version so libnx version guards behave correctly. */
    if (hosversionGet() == 0) {
        logger_log("init: stage 2 -> setsysInitialize");
        rc = setsysInitialize();
        logger_log("init: setsys rc=0x%x", rc);
        if (R_SUCCEEDED(rc)) {
            SetSysFirmwareVersion fw;
            if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) {
                hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
                logger_log("init: hosversion pinned %d.%d.%d",
                           fw.major, fw.minor, fw.micro);
            }
            setsysExit();
        }
    }

    logger_log("init: stage 3 -> socketInitializeDefault");
    rc = socketInitializeDefault(); /* bsd:u - loopback HTTP server */
    if (R_FAILED(rc)) {
        logger_log("init: FATAL socket rc=0x%x", rc);
        fatalThrow(rc);
    }
    logger_log("init: stage 4 socket ok");

    logger_log("init: stage 5 -> nifmInitialize(User)");
    /* Best effort: if nifm fails we simply never sync (log says so). */
    g_nifm_rc = nifmInitialize(NifmServiceType_User);
    logger_log("init: stage 6 nifm rc=0x%x%s",
               g_nifm_rc, R_SUCCEEDED(g_nifm_rc) ? " (ok)" : " (sync off)");

    /* Sync talks TLS through the console's ssl service. Without this,
     * sslCreateContext fails with 0x1159 (libnx NotInitialized) - found
     * on hardware. One session: sync sends one request at a time. */
    logger_log("init: stage 7 -> sslInitialize(1)");
    g_ssl_rc = sslInitialize(1);
    logger_log("init: stage 8 ssl rc=0x%x%s",
               g_ssl_rc, R_SUCCEEDED(g_ssl_rc) ? " (ok)" : " (sync off)");
}

/** libnx hook: release services in reverse order of acquisition. */
void __appExit(void)
{
    if (R_SUCCEEDED(g_ssl_rc))
        sslExit();
    if (R_SUCCEEDED(g_nifm_rc))
        nifmExit();
    socketExit();
    fsExit();
    smExit();
}

static void server_thread_main(void *arg)
{
    (void)arg;
    logger_log("http: worker starting");
    http_server_serve_forever(&g_running, route_dispatch);
}

static void log_heap_state(void)
{
    struct mallinfo mi = mallinfo();
    logger_log("heap: arena=%lu used=%lu free=%lu",
               (unsigned long)mi.arena, (unsigned long)mi.uordblks,
               (unsigned long)mi.fordblks);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    bool have_log = logger_init();
#ifdef RA_NRO
    logger_log("ra-offline v0.4.4 (NRO build) starting");
#else
    logger_log("ra-offline v0.4.4 (sysmodule) starting");
#endif
    if (!have_log)
        logger_log("WARNING: SD logging unavailable; continuing headless");
    if (R_FAILED(g_nifm_rc))
        logger_log("WARNING: nifm init failed (0x%x) - sync disabled",
                   g_nifm_rc);

    logger_log("firmware: %d.%d.%d, heap budget: %u KB",
               hosversionGet() >> 16 & 0xff,
               hosversionGet() >> 8 & 0xff,
               hosversionGet() & 0xff,
               HEAP_SIZE / 1024);
    log_heap_state();

    ra_cache_load();
    logger_log("cache: %d ROM hash entries loaded", ra_cache_count());

    if (http_server_start(SERVER_PORT) == 0) {
        Result rc = threadCreate(&g_server_thread, server_thread_main, NULL,
                                 NULL, SERVER_THREAD_STACK, SERVER_THREAD_PRIO,
                                 -2);
        if (R_SUCCEEDED(rc)) {
            rc = threadStart(&g_server_thread);
            if (R_FAILED(rc))
                logger_log("FATAL: threadStart failed 0x%x", rc);
        } else {
            logger_log("FATAL: threadCreate failed 0x%x", rc);
        }
    } else {
        logger_log("FATAL: could not start loopback server on port %u",
                   SERVER_PORT);
    }

    u32 ticks = 0;
    while (g_running && appletMainLoop()) {
        svcSleepThread(LOOP_SLEEP_NS);

        if (R_SUCCEEDED(g_nifm_rc) && ticks % 10 == 0) /* every second */
            maybe_sync(ra_net_online(), ticks);

        if (++ticks % HEARTBEAT_EVERY_TICKS == 0) {
            struct mallinfo mi = mallinfo();
            logger_log("heartbeat: uptime=%us heap_used=%lu queue=%ld",
                       ticks / 10, (unsigned long)mi.uordblks,
                       ra_sync_pending());
        }

        /* Phase 3: queue maintenance hooks.
         * Phase 4: poll nifm and trigger the sync worker on Wi-Fi. */
    }

    logger_log("termination requested, shutting down");
    g_running = false;
    http_server_stop();
    threadWaitForExit(&g_server_thread);
    threadClose(&g_server_thread);
    log_heap_state();
    logger_exit();
    return 0;
}
