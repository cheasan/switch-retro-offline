/**
 * json_lite.c - deliberately tiny JSON readers. See json_lite.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_lite.h"

/** Find the value position (just past ':') for the first occurrence of
 *  "key": in json, or NULL. */
static const char *find_key(const char *json, const char *key)
{
    size_t klen = strlen(key);
    const char *p = json;

    while ((p = strchr(p, '"')) != NULL) {
        if (strncmp(p + 1, key, klen) == 0 && p[1 + klen] == '"') {
            const char *v = p + klen + 2;
            while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n')
                v++;
            if (*v == ':')
                return v + 1;
        }
        p++;
    }
    return NULL;
}

bool json_get_string(const char *json, const char *key, char *out, size_t outsz)
{
    const char *v = find_key(json, key);
    if (v == NULL || out == NULL || outsz == 0)
        return false;

    v = strchr(v, '"');
    if (v == NULL)
        return false;
    v++;

    size_t o = 0;
    while (*v != '\0' && *v != '"' && o + 1 < outsz) {
        if (*v == '\\' && v[1] != '\0')
            v++; /* skip the escape char, keep the literal */
        out[o++] = *v++;
    }
    out[o] = '\0';
    return true;
}

bool json_get_int(const char *json, const char *key, long *out)
{
    const char *v = find_key(json, key);
    if (v == NULL || out == NULL)
        return false;

    char *end;
    long val = strtol(v, &end, 10);
    if (end == v)
        return false;
    *out = val;
    return true;
}

void json_escape(const char *in, char *out, size_t outsz)
{
    size_t o = 0;
    if (out == NULL || outsz == 0)
        return;

    if (in == NULL)
        in = "";

    while (*in != '\0' && o + 7 < outsz) {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
            in++;
        } else if (c == '\n') {
            out[o++] = '\\'; out[o++] = 'n'; in++;
        } else if (c == '\r') {
            out[o++] = '\\'; out[o++] = 'r'; in++;
        } else if (c == '\t') {
            out[o++] = '\\'; out[o++] = 't'; in++;
        } else if (c < 0x20) {
            o += (size_t)snprintf(out + o, outsz - o, "\\u%04x", c);
            in++;
        } else {
            out[o++] = (char)c;
            in++;
        }
    }
    out[o] = '\0';
}
