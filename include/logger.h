/**
 * logger.h - tiny SD-card logger for ra-offline (Phase 1).
 *
 * Writes to sdmc:/switch/raoffline/sysmodule.log using stdio on top of
 * fsdevMountSdmc(). Every line is flushed immediately so the log survives
 * a crash or force-termination of the sysmodule.
 */
#ifndef RA_OFFLINE_LOGGER_H
#define RA_OFFLINE_LOGGER_H

#include <stdbool.h>
#include <switch.h>

#define RA_LOG_PATH "sdmc:/switch/raoffline/sysmodule.log"
#define RA_DATA_DIR "sdmc:/switch/raoffline"

/** Mount SD and open the log file for appending. Returns false if SD
 *  logging is unavailable (the module should still keep running). */
bool logger_init(void);

/** Append one timestamped line. No-op if the logger failed to open. */
void logger_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/** Close the log file. Safe to call twice. */
void logger_exit(void);

#endif /* RA_OFFLINE_LOGGER_H */
