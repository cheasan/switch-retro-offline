/**
 * cacher_host.c - desktop harness for the cacher's scan.c.
 *
 * Runs the same scan + hash + cache code as the Switch app, against
 * folders on this laptop and the fake server in test/fake_ra_server.py:
 *
 *   RAOFFLINE_BASE=<data dir> ./build-host/cacher-test [--recheck] <root>...
 *
 * Prints one line per game, then a summary line for the test script.
 */
#include <stdio.h>
#include <string.h>

#include "ra_cache.h"
#include "scan.h"

static const char *const k_names[] = {
    "cached", "already", "no-set", "no-hash", "failed",
};

static void on_progress(void *ud, int idx, int total, const char *path,
                        scan_result r, const char *title)
{
    (void)ud;
    printf("[%d/%d] %s %s %s\n", idx, total, k_names[r], path, title);
}

int main(int argc, char **argv)
{
    bool recheck = false;
    const char *roots[16];
    int nroots = 0;
    for (int i = 1; i < argc && nroots < 16; i++) {
        if (strcmp(argv[i], "--recheck") == 0)
            recheck = true;
        else
            roots[nroots++] = argv[i];
    }

    char user[64], token[128];
    ra_cache_read_account(user, sizeof user, token, sizeof token, NULL, NULL);

    scan_list list;
    scan_collect(roots, nroots, &list);
    printf("found %d games\n", list.count);

    scan_stats st;
    scan_run(&list, ra_transport_default(), user, token, recheck,
             on_progress, NULL, NULL, &st);
    scan_free(&list);
    printf("summary: cached=%d already=%d no-set=%d no-hash=%d failed=%d "
           "auth=%d\n", st.cached, st.already, st.no_set, st.no_hash,
           st.failed, st.auth_error ? 1 : 0);
    return 0;
}
