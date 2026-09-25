#include <switch.h>
#include <stdio.h>
#include "deploy.h"
#include "progress.h"

/* Close every display/applet resource before passing control to the NRO. */
static ViDisplay display;
static ViLayer layer;
static bool has_sm, has_applet, has_vi, has_display, has_layer, has_window, has_console;
static u64 last_tick;

static void update(RuntimeDeployPhase phase, u64 done, u64 total, const char *path, void *context)
{
    (void)context;
    if (!has_console) return;
    u64 now = armGetSystemTick();
    if (last_tick && armTicksToNs(now-last_tick) < 250000000) return;
    last_tick = now;
    appletMainLoop();
    unsigned percent = total ? (unsigned)(done * 100 / total) : 0;
    if (percent > 100) percent = 100;
    printf("\x1b[2J\x1b[H\n  Autorun\n\n  %s\n\n  %u%%    %llu / %llu MiB\n\n  %.65s\n\n  Please keep the console powered on.\n",
           phase == RuntimeDeploy_Copying ? "Installing game files" :
           phase == RuntimeDeploy_Verifying ? "Verifying installed file" : "Checking game files",
           percent, (unsigned long long)(done >> 20), (unsigned long long)(total >> 20), path ? path : "");
    consoleUpdate(NULL);
}

bool install_ui_begin(void)
{
    Result rc = smInitialize();
    if (R_FAILED(rc)) return false;
    has_sm = true;
    rc = appletInitialize();
    if (R_FAILED(rc)) return false;
    has_applet = true;
    appletSetAutoSleepDisabled(true);
    rc = viInitialize(ViServiceType_Default);
    if (R_FAILED(rc)) return false;
    has_vi = true;
    rc = viOpenDefaultDisplay(&display);
    if (R_FAILED(rc)) return false;
    has_display = true;
    rc = viCreateLayer(&display, &layer);
    if (R_FAILED(rc)) return false;
    has_layer = true;
    rc = viSetLayerScalingMode(&layer, ViScalingMode_FitToLayer);
    if (R_FAILED(rc)) return false;
    rc = nwindowCreateFromLayer(nwindowGetDefault(), &layer);
    if (R_FAILED(rc)) return false;
    has_window = true;
    PrintConsole *console = consoleInit(NULL);
    has_console = console && console->consoleInitialised;
    if (has_console) {
        runtime_deploy_set_progress(update, NULL);
        update(RuntimeDeploy_Validating, 0, 0, "Preparing...", NULL);
    }
    return has_console;
}

void install_ui_end(Result result, bool changed)
{
    runtime_deploy_set_progress(NULL, NULL);
    if (has_console) {
        printf("\x1b[2J\x1b[H\n  Autorun\n\n");
        if (R_SUCCEEDED(result)) printf("  %s\n", changed ? "Installation complete. Starting..." : "Ready. Starting...");
        else {
            printf("  %s\n\n  Error: 0x%08x\n\n  Returning to HOME.\n  Completed files are kept for the next attempt.\n",
                   result == MAKERESULT(Module_HomebrewLoader, 101) ? "Not enough free space on the SD card." : "Unable to prepare game files.", result);
        }
        consoleUpdate(NULL);
        if (R_FAILED(result)) for (int i=0;i<80;i++) { appletMainLoop(); svcSleepThread(100000000); }
        consoleExit(NULL);
    }
    if (has_window) nwindowClose(nwindowGetDefault());
    if (has_layer) viCloseLayer(&layer);
    if (has_display) viCloseDisplay(&display);
    if (has_vi) viExit();
    if (has_applet) { appletSetAutoSleepDisabled(false); appletExit(); }
    if (has_sm) smExit();
    has_console=has_window=has_layer=has_display=has_vi=has_applet=has_sm=false;
}
