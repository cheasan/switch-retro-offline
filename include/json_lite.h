/**
 * json_lite.h - deliberately tiny JSON readers.
 *
 * ra-offline avoids a real JSON library: the two big JSON payloads in this
 * project (patch data, unlock queue entries) are stored and forwarded
 * verbatim. The only parsing needed is a handful of flat key/value reads
 * (account.json, server sync replies), which these helpers handle.
 *
 * Flat objects with unique keys only; no nesting, no arrays. Escaped
 * string values are supported.
 */
#ifndef RA_OFFLINE_JSON_LITE_H
#define RA_OFFLINE_JSON_LITE_H

#include <stdbool.h>
#include <stddef.h>

/** Read a string value for key into out (unescaped). */
bool json_get_string(const char *json, const char *key, char *out, size_t outsz);

/** Read an integer value for key. */
bool json_get_int(const char *json, const char *key, long *out);

/** Read a boolean literal (true/false) for key. */
bool json_get_bool(const char *json, const char *key, bool *out);

/** Escape a raw string for embedding in JSON (quotes, backslash, C0).
 *  Always NUL-terminates; truncates if needed. */
void json_escape(const char *in, char *out, size_t outsz);

#endif /* RA_OFFLINE_JSON_LITE_H */
