/**
 * host_logger.c - logger.h implementation for desktop testing.
 * Same interface as source/logger.c, but writes to stdout so the HTTP
 * path can be exercised on this laptop instead of the Switch.
 */
#include <stdio.h>
#include <stdarg.h>
#include "logger.h"

bool logger_init(void)
{
    return true;
}

void logger_log(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    putchar('\n');
    fflush(stdout);
}

void logger_exit(void)
{
}
