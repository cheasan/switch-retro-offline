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

/* We are a background system module, not an applet. */
u32 __nx_applet_type = AppletType_None;

static Thread g_server_thread;
static volatile bool g_running = true;

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

/** libnx hook: acquire the services the module needs. */
void __appInit(void)
{
    Result rc = smInitialize();
    if (R_FAILED(rc))
        fatalThrow(rc);

    /* Pin the HOS version so libnx version guards behave correctly. */
    if (hosversionGet() == 0) {
        rc = setsysInitialize();
        if (R_SUCCEEDED(rc)) {
            SetSysFirmwareVersion fw;
            if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
                hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
            setsysExit();
        }
    }

    rc = socketInitializeDefault(); /* bsd:u - loopback HTTP server */
    if (R_FAILED(rc))
        fatalThrow(rc);

    /* Phase 4 adds nifmInitialize() here. */
}

/** libnx hook: release services in reverse order of acquisition. */
void __appExit(void)
{
    socketExit();
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
    logger_log("ra-offline v0.2 (phase 2) starting");
    if (!have_log)
        logger_log("WARNING: SD logging unavailable; continuing headless");

    logger_log("firmware: %d.%d.%d, heap budget: %u KB",
               hosversionGet() >> 16 & 0xff,
               hosversionGet() >> 8 & 0xff,
               hosversionGet() & 0xff,
               HEAP_SIZE / 1024);
    log_heap_state();

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

        if (++ticks % HEARTBEAT_EVERY_TICKS == 0) {
            struct mallinfo mi = mallinfo();
            logger_log("heartbeat: uptime=%us heap_used=%lu",
                       ticks / 10, (unsigned long)mi.uordblks);
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
