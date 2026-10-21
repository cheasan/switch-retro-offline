/**
 * cacher_log.c - logger.h for the cacher app.
 *
 * The app writes its own file, so it never mixes lines into the
 * sysmodule's log. libnx mounts the SD card for homebrew apps, so no
 * mount happens here. Open, append and close per line, like logger.c.
 */
#include <stdarg.h>
#include <stdio.h>

#include "logger.h"

#define CACHER_LOG_PATH "sdmc:/switch/raoffline/cacher.log"

bool logger_init(void)
{
    return true;
}

void logger_log(const char *fmt, ...)
{
    FILE *f = fopen(CACHER_LOG_PATH, "a");
    if (f == NULL)
        return;
    va_list va;
    va_start(va, fmt);
    vfprintf(f, fmt, va);
    va_end(va);
    fputc('\n', f);
    fclose(f);
}

void logger_exit(void)
{
}
