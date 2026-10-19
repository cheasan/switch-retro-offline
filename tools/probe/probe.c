/**
 * probe.c - ra-offline diagnostic: list running processes.
 *
 * Answers one question: is the ra-offline sysmodule (TID
 * 4300000000000042) actually running, or did boot2/ldr silently
 * refuse to launch it?
 *
 * Writes an evidence file to sdmc:/switch/raoffline/probe.log and
 * prints the same on screen. Exit with + or B.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#define RA_TID 0x4300000000000042ULL

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    consoleInit(NULL);
    printf("\nra-offline process probe\n========================\n\n");

    FILE *f = NULL;
    if (R_SUCCEEDED(fsdevMountSdmc())) {
        f = fopen("sdmc:/switch/raoffline/probe.log", "w");
        if (f == NULL)
            printf("note: could not open probe.log\n");
    } else {
        printf("note: SD mount failed\n");
    }

    Result rc = pmdmntInitialize();
    printf("pmdmntInitialize: 0x%x\n", rc);
    Result rc2 = ldrDmntInitialize();
    printf("ldrDmntInitialize: 0x%x\n\n", rc2);

    u64 pids[300];
    s32 num = 0;
    rc = svcGetProcessList(&num, pids, 300);
    printf("svcGetProcessList: rc=0x%x num=%d\n", rc, num);
    if (f) fprintf(f, "svcGetProcessList rc=0x%x num=%d\n", rc, num);

    for (s32 i = 0; i < num && i < 300; i++) {
        u64 prog = 0;
        Result rcp = pmdmntGetProgramId(&prog, pids[i]);

        const char *tag = "";
        if (prog == RA_TID)                    tag = "  <== ra-offline";
        else if (prog == 0x420000000000000BULL) tag = "  (sys-patch)";
        else if (prog == 0x420000000007E51AULL) tag = "  (sys-ftpd-light)";
        else if (prog == 0x00FF0000636C6BFFULL) tag = "  (sys-clk)";

        printf("[%2d] pid=%-4lu prog=%016lx rc=0x%-4x%s\n",
               i, pids[i], prog, rcp, tag);
        if (f) fprintf(f, "pid=%lu prog=0x%016lx rc=0x%x%s\n",
                       pids[i], prog, rcp, tag);

        /* Build-id of the ra-offline process: tells us WHICH binary
         * is actually loaded (diag build vs forensics build). */
        if (prog == RA_TID) {
            LoaderModuleInfo mods[2];
            s32 nmod = 0;
            if (R_SUCCEEDED(ldrDmntGetProcessModuleInfo(pids[i], mods, 2, &nmod))
                && nmod > 0) {
                printf("     build-id: ");
                for (int k = 0; k < 8; k++) printf("%02x", mods[0].build_id[k]);
                printf("...\n");
                if (f) {
                    fprintf(f, "ra-offline build-id: ");
                    for (int k = 0; k < 32; k++) fprintf(f, "%02x", mods[0].build_id[k]);
                    fprintf(f, "\n");
                }
            } else {
                printf("     module info rc=0x%x nmod=%d\n", rcp, nmod);
                if (f) fprintf(f, "module-info rc=0x%x nmod=%d\n", rcp, nmod);
            }
        }
    }

    /* The console's own view of the data dir + the log file. */
    {
        FsFileSystem sd;
        Result rc3 = fsOpenSdCardFileSystem(&sd);
        printf("\nSD open (raw): 0x%x\n", rc3);
        if (f) fprintf(f, "raw SD open rc=0x%x\n", rc3);
        if (R_SUCCEEDED(rc3)) {
            FsDir dir;
            Result rc4 = fsFsOpenDirectory(&sd, "/switch/raoffline",
                                           FsDirOpenMode_ReadFiles, &dir);
            printf("open dir: 0x%x\n", rc4);
            if (R_SUCCEEDED(rc4)) {
                FsDirectoryEntry ent; s64 n = 0;
                while (R_SUCCEEDED(fsDirRead(&dir, &n, 1, &ent)) && n == 1) {
                    printf("  file: %s (%lu bytes)\n", ent.name,
                           (unsigned long)ent.file_size);
                    if (f) fprintf(f, "dir: %s %lu\n", ent.name,
                                   (unsigned long)ent.file_size);
                }
                fsDirClose(&dir);
            }
            FsFile logf;
            Result rc5 = fsFsOpenFile(&sd, "/switch/raoffline/sysmodule.log",
                                      FsOpenMode_Read, &logf);
            printf("open log: 0x%x\n", rc5);
            if (f) fprintf(f, "open log rc=0x%x\n", rc5);
            if (R_SUCCEEDED(rc5)) {
                char buf[512] = {0};
                u64 rd = 0;
                fsFileRead(&logf, 0, buf, sizeof(buf) - 1, 0, &rd);
                printf("log first bytes (%lu):\n%.400s\n",
                       (unsigned long)rd, buf);
                if (f) fprintf(f, "log bytes=%lu first=%.400s\n",
                               (unsigned long)rd, buf);
                fsFileClose(&logf);
            }
            fsFsClose(&sd);
        }
    }

    u64 pid = 0;
    rc = pmdmntGetProcessId(&pid, RA_TID);
    printf("\n==> ra-offline (4300000000000042): %s%s%lu\n",
           R_SUCCEEDED(rc) ? "RUNNING" : "NOT RUNNING",
           R_SUCCEEDED(rc) ? " pid=" : " rc=0x", R_SUCCEEDED(rc) ? pid : rc);
    if (f) fprintf(f, "ra-offline %s rc=0x%x pid=%lu\n",
                   R_SUCCEEDED(rc) ? "RUNNING" : "NOT RUNNING", rc, pid);

    if (f) fclose(f);
    printf("\nPress + or B to exit.\n");
    consoleUpdate(NULL);

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);
        if (k & (HidNpadButton_Plus | HidNpadButton_B))
            break;
        consoleUpdate(NULL);
    }

    pmdmntExit();
    ldrDmntExit();
    consoleExit(NULL);
    return 0;
}
