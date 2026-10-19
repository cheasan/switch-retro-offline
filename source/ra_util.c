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

long long ra_now(void)
{
    return (long long)(armTicksToNs(armGetSystemTick()) / 1000000000ULL);
}

#else /* host */

const char *ra_base(void)
{
    const char *e = getenv("RAOFFLINE_BASE");
    return (e != NULL && e[0] != '\0') ? e : "test/root";
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
