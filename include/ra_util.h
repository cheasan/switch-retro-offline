/**
 * ra_util.h - small platform helpers shared by sysmodule and host build.
 */
#ifndef RA_OFFLINE_UTIL_H
#define RA_OFFLINE_UTIL_H

#include <stdbool.h>
#include <stddef.h>

/** Runtime base dir for all ra-offline data.
 *  Switch: sdmc:/switch/raoffline. Host: $RAOFFLINE_BASE or "test/root". */
const char *ra_base(void);

/** Join base dir + printf-style relative path into out. */
void ra_path(char *out, size_t outsz, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/** Seconds since some fixed point (epoch on host, boot uptime on Switch).
 *  Only used for ordering/timestamps in the queue log. */
long long ra_now(void);

#endif /* RA_OFFLINE_UTIL_H */
