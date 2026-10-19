/**
 * test_main.c - desktop harness for the loopback server.
 *
 * Runs the exact same http_server.c + routes.c that ship inside the
 * sysmodule, on plain Linux. Point curl at 127.0.0.1:8080 to test:
 *
 *   curl http://127.0.0.1:8080/ping
 *   curl -X POST 'http://127.0.0.1:8080/dorequest.php?r=login'
 *   curl -X POST 'http://127.0.0.1:8080/dorequest.php' -d 'r=login&u=sam'
 */
#include <signal.h>
#include <stdio.h>
#include "http_server.h"
#include "ra_cache.h"
#include "routes.h"

static volatile bool g_run = true;

static void on_sigint(int sig)
{
    (void)sig;
    g_run = false;
    http_server_stop();
}

int main(void)
{
    signal(SIGINT, on_sigint);
    signal(SIGPIPE, SIG_IGN);

    ra_cache_load();
    printf("cache: %d hash entries loaded\n", ra_cache_count());

    if (http_server_start(8080) != 0) {
        fprintf(stderr, "failed to start server on 8080\n");
        return 1;
    }
    printf("host test server ready: curl http://127.0.0.1:8080/ping\n");
    http_server_serve_forever(&g_run, route_dispatch);
    printf("server stopped\n");
    return 0;
}
