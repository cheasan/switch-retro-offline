/**
 * logger.c - tiny SD-card logger for ra-offline.
 *
 * Design notes:
 *  - We deliberately do NOT keep the log file open. A permanently held
 *    write handle on FAT32 blocks every other process (sys-ftpd, probes)
 *    from even reading the file, which made the log invisible over FTP
 *    for the entire hardware bring-up. Instead: open/append/close per
 *    line. A sysmodule logs a few lines per hour; the cost is nil.
 *  - Every line is flushed at write time by construction, so the log
 *    always survives a crash or force-termination.
 *  - Timestamps are uptime seconds (svcGetSystemTick based); wall-clock
 *    time is deliberately not used (no extra service deps).
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <switch.h>
#include "logger.h"

static bool g_sd_mounted = false;

bool logger_init(void)
{
    if (g_sd_mounted)
        return true;

    Result rc = fsdevMountSdmc();
    if (R_FAILED(rc))
        return false;
    g_sd_mounted = true;

    /* No fsdevEnsureDirectory in current libnx: create both levels via
     * the raw service API and ignore AlreadyExists; fopen() in
     * logger_log() is the real success test. */
    FsFileSystem *sd = fsdevGetDeviceFileSystem("sdmc");
    if (sd != NULL) {
        fsFsCreateDirectory(sd, "/switch");
        fsFsCreateDirectory(sd, "/switch/raoffline");
    }
    return true;
}

void logger_log(const char *fmt, ...)
{
    if (!g_sd_mounted)
        return;

    FILE *f = fopen(RA_LOG_PATH, "a");
    if (f == NULL)
        return;

    /* Uptime in seconds with millisecond precision. */
    u64 ns = armTicksToNs(armGetSystemTick());
    unsigned long secs = (unsigned long)(ns / 1000000000ULL);
    unsigned long msec = (unsigned long)((ns / 1000000ULL) % 1000ULL);

    fprintf(f, "[%7lu.%03lu] ", secs, msec);

    va_list va;
    va_start(va, fmt);
    vfprintf(f, fmt, va);
    va_end(va);

    fputc('\n', f);
    fclose(f);
}

void logger_exit(void)
{
    if (g_sd_mounted) {
        fsdevUnmountAll();
        g_sd_mounted = false;
    }
}
