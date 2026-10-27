/**
 * ra_util.c - platform helpers (base dir, path join, clock).
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ra_util.h"

#ifdef __SWITCH__
#include <switch.h>

const char *ra_base(void)
{
    return "sdmc:/switch/raoffline";
}

const char *ra_upstream_base(void)
{
    return "retroachievements.org";
}

bool ra_net_online(void)
{
    NifmInternetConnectionType type;
    u32 strength;
    NifmInternetConnectionStatus status;
    Result rc = nifmGetInternetConnectionStatus(&type, &strength, &status);
    return R_SUCCEEDED(rc) && status == NifmInternetConnectionStatus_Connected;
}

long long ra_now(void)
{
    /* Wall-clock epoch seconds, not uptime: queue entry timestamps
     * must survive reboots so the sync worker can drop entries older
     * than RA_QUEUE_TTL_S (the RetroAchievements 7-day rule).
     * armGetSystemTick() resets at boot and would break that. */
    return (long long)time(NULL);
}

#else /* host */

const char *ra_base(void)
{
    const char *e = getenv("RAOFFLINE_BASE");
    return (e != NULL && e[0] != '\0') ? e : "test/root";
}

const char *ra_upstream_base(void)
{
    const char *e = getenv("RAOFFLINE_UPSTREAM");
    return (e != NULL && e[0] != '\0') ? e : "127.0.0.1:9090";
}

bool ra_net_online(void)
{
    return true;
}

long long ra_now(void)
{
    return (long long)time(NULL);
}

#endif

void ra_path(char *out, size_t outsz, const char *fmt, ...)
{
    char rel[512];
    va_list va;
    va_start(va, fmt);
    vsnprintf(rel, sizeof rel, fmt, va);
    va_end(va);
    snprintf(out, outsz, "%s/%s", ra_base(), rel);
}
