/**
 * main.c - ra-offline cacher: one button caches every game on the memory card (SD).
 *
 * A homebrew app (launch from hbmenu). It finds each game in the ROM
 * folders, hashes it the way RetroArch does, and saves its achievement
 * data to sdmc:/switch/raoffline/cache. The sysmodule then serves those
 * games offline. Needs Wi-Fi and the account in account.json.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "logger.h"
#include "ra_cache.h"
#include "ra_transport.h"
#include "ra_util.h"
#include "rc_hash.h"
#include "scan.h"

#define VERSION "0.4.5"
#define MAX_ROOTS 16

static const char *const k_default_roots[] = {
    "sdmc:/tico/roms", "sdmc:/roms", "sdmc:/retroarch/roms",
};

static char g_root_buf[MAX_ROOTS][512];
static const char *g_roots[MAX_ROOTS];
static int g_nroots;
static PadState g_pad;

static const char *const k_result_text[] = {
    "cached ", "already", "no set ", "no hash", "FAILED ",
};

/** Default folders, plus any extra lines in cacher-roots.txt. */
static void load_roots(void)
{
    g_nroots = 0;
    for (size_t i = 0; i < sizeof k_default_roots / sizeof k_default_roots[0]; i++)
        g_roots[g_nroots++] = k_default_roots[i];

    char path[600];
    ra_path(path, sizeof path, "cacher-roots.txt");
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return;
    while (g_nroots < MAX_ROOTS &&
           fgets(g_root_buf[g_nroots], sizeof g_root_buf[0], f) != NULL) {
        char *line = g_root_buf[g_nroots];
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0' && line[0] != '#')
            g_roots[g_nroots] = line, g_nroots++;
    }
    fclose(f);
}

static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static void on_progress(void *ud, int idx, int total, const char *path,
                        scan_result r, const char *title)
{
    (void)ud;
    char name[48];
    snprintf(name, sizeof name, "%s", base_name(path));
    printf("%4d/%-4d %s  %-47s", idx, total, k_result_text[r], name);
    if (r == SCAN_CACHED && title[0] != '\0')
        printf("\n                   -> %.58s", title);
    printf("\n");
    consoleUpdate(NULL);
}

static bool on_cancel(void *ud)
{
    (void)ud;
    padUpdate(&g_pad);
    return (padGetButtonsDown(&g_pad) & HidNpadButton_B) != 0;
}

static void print_menu(const char *user)
{
    printf("\x1b[2J\x1b[1;1H");
    printf("ra-offline cacher v" VERSION "\n\n");
    printf("Saves the achievement data of every game on your memory card,\n");
    printf("so each game also has achievements when you play offline.\n\n");
    printf("Account: %s\n", user);
    printf("Folders:");
    for (int i = 0; i < g_nroots; i++)
        printf(" %s", g_roots[i] + 5); /* skip "sdmc:" */
    printf("\n\n");
    printf("  A  Cache all games\n");
    printf("  Y  Cache all games, and ask again about games with no set\n");
    printf("  +  Exit\n\n");
    consoleUpdate(NULL);
}

static void run(const char *user, const char *token, bool recheck)
{
    if (!ra_net_online()) {
        printf("No internet. Connect to Wi-Fi, then press A again.\n\n");
        consoleUpdate(NULL);
        return;
    }

    printf("Looking for games...\n");
    consoleUpdate(NULL);
    scan_list list;
    scan_collect(g_roots, g_nroots, &list);
    printf("Found %d games. Press B to stop.\n\n", list.count);
    consoleUpdate(NULL);
    logger_log("cacher v" VERSION ": %d games, recheck=%d", list.count, recheck);

    /* Keep the console awake: auto-sleep would cut the network. */
    appletSetMediaPlaybackState(true);
    scan_stats st;
    scan_run(&list, ra_transport_default(), user, token, recheck,
             on_progress, on_cancel, NULL, &st);
    appletSetMediaPlaybackState(false);
    scan_free(&list);

    printf("\n%s. %d cached now, %d already cached, %d with no achievement "
           "set,\n%d could not be read, %d failed.\n",
           st.cancelled ? "Stopped" : "Done", st.cached, st.already,
           st.no_set, st.no_hash, st.failed);
    if (st.auth_error)
        printf("The server refused your account. Run tools/fetch-cache.py "
               "once on a computer\nto refresh account.json, then try again.\n");
    else if (st.failed > 0)
        printf("Failed games stay uncached. Press A to try them again.\n");
    printf("\nPress A to run again, + to exit.\n");
    consoleUpdate(NULL);
    logger_log("cacher: cached=%d already=%d no_set=%d no_hash=%d failed=%d "
               "auth=%d cancelled=%d", st.cached, st.already, st.no_set,
               st.no_hash, st.failed, st.auth_error, st.cancelled);
}

int main(void)
{
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);

    socketInitializeDefault();
    nifmInitialize(NifmServiceType_User);
    sslInitialize(1);
    rc_hash_init_default_cdreader();

    load_roots();
    char user[64], token[128];
    ra_cache_read_account(user, sizeof user, token, sizeof token, NULL, NULL);
    /* ra_cache falls back to "raoffline" when account.json is missing. */
    bool have_account = strcmp(user, "raoffline") != 0;

    print_menu(user);
    if (!have_account) {
        printf("No account found in /switch/raoffline/account.json.\n");
        printf("Run tools/fetch-cache.py once on a computer, copy its\n");
        printf("raoffline folder to the memory card, then start this app again.\n");
        consoleUpdate(NULL);
    }

    while (appletMainLoop()) {
        padUpdate(&g_pad);
        u64 down = padGetButtonsDown(&g_pad);
        if (down & HidNpadButton_Plus)
            break;
        if (have_account && (down & (HidNpadButton_A | HidNpadButton_Y))) {
            print_menu(user);
            run(user, token, (down & HidNpadButton_Y) != 0);
        }
        consoleUpdate(NULL);
    }

    sslExit();
    nifmExit();
    socketExit();
    consoleExit(NULL);
    return 0;
}
