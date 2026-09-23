/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 * PE module identity for mmdevapi's static Horizon unixlib lookup. */
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    (void)instance; (void)reason; (void)reserved;
    return TRUE;
}
/* The export directory supplies the module identity for Unix-call lookup. */
__declspec(dllexport) void WINAPI WineNXAudioDriver(void) {}
