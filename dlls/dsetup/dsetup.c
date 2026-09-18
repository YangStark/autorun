/*
 * DirectX Setup stub for Wine-NX
 *
 * Halo: Combat Evolved calls DirectXSetupGetVersion() to check whether
 * DirectX 9.0b or later is installed and refuses to start if the call
 * fails or returns a version below 9.0b (4.09.00.0902).
 *
 * Wine removed dsetup.dll years ago; this minimal stub satisfies the
 * check by returning DirectX 9.0c version numbers.
 */

#include <stdarg.h>
#include "windef.h"
#include "winbase.h"

/* DirectX 9.0c version: 4.09.00.0904 */
#define DIRECTX_VERSION     MAKELONG(9, 4)    /* major=4, minor=9 */
#define DIRECTX_REVISION    MAKELONG(0, 904)  /* revision=9.00.0904 */

INT WINAPI DirectXSetupGetVersion( DWORD *version, DWORD *revision )
{
    if (version)  *version  = DIRECTX_VERSION;
    if (revision) *revision = DIRECTX_REVISION;
    return 1;  /* success */
}

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, LPVOID reserved )
{
    return TRUE;
}
