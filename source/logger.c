/**
 * logger.c - tiny SD-card logger for ra-offline (Phase 1).
 *
 * Design notes:
 *  - fopen/fclose per call would be safest but slower; we keep one FILE*
 *    open for the module lifetime and fflush() after every line, which is
 *    a good middle ground for a sysmodule that logs a few lines per hour.
 *  - Timestamps are uptime seconds (svcGetSystemTick based); wall-clock
 *    time is deliberately not used in Phase 1 (no extra service deps).
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <switch.h>
#include "logger.h"

static FILE *g_log = NULL;
static bool g_sd_mounted = false;

bool logger_init(void)
{
    Result rc = fsdevMountSdmc();
    if (R_FAILED(rc))
        return false;
    g_sd_mounted = true;

    /* No fsdevEnsureDirectory in current libnx: create both levels via the
     * raw service API and ignore AlreadyExists; fopen() below is the real
     * success test. */
    FsFileSystem *sd = fsdevGetDeviceFileSystem("sdmc");
    if (sd != NULL) {
        fsFsCreateDirectory(sd, "/switch");
        fsFsCreateDirectory(sd, "/switch/raoffline");
    }

    g_log = fopen(RA_LOG_PATH, "a");
    return g_log != NULL;
}

void logger_log(const char *fmt, ...)
{
    if (g_log == NULL)
        return;

    /* Uptime in seconds with millisecond precision. */
    u64 ns = armTicksToNs(armGetSystemTick());
    unsigned long secs = (unsigned long)(ns / 1000000000ULL);
    unsigned long msec = (unsigned long)((ns / 1000000ULL) % 1000ULL);

    fprintf(g_log, "[%7lu.%03lu] ", secs, msec);

    va_list va;
    va_start(va, fmt);
    vfprintf(g_log, fmt, va);
    va_end(va);

    fputc('\n', g_log);
    fflush(g_log);
}

void logger_exit(void)
{
    if (g_log != NULL) {
        fclose(g_log);
        g_log = NULL;
    }
    if (g_sd_mounted) {
        fsdevUnmountAll();
        g_sd_mounted = false;
    }
}
