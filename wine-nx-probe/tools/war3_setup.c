/* WarCraft III setup for Wine-NX: everything the game needs written once into
 * the card's registry, from one program in the launcher.
 *
 * - The game's video settings: 1280x720, 32-bit colour, 60 Hz. The Switch
 *   screen, where the pointer and the touchscreen are, is always 1280x720; at
 *   any other resolution the game's hit-testing does not line up with them.
 * - No seenintromovie: earlier setups set it, which skipped the intro movie at
 *   startup, and this one removes it so the intro plays.
 * - Gfx OpenGL, so the game draws with its own OpenGL renderer rather than
 *   Direct3D. Through Wine's Direct3D each lock of a texture or buffer waited
 *   for wined3d's command thread in a Sleep(0) loop and every frame read a
 *   texture back from the GPU: on the Switch that took two cores for the frame
 *   rate OpenGL gives on one.
 * - EmulateModelist for war3.exe, so Wine offers the game only the screen's own
 *   1280x720 mode. For each movie the game switches to 800x600, which Wine
 *   would fake by scaling it into a 960x720 box in the middle of the screen.
 *   With 800x600 refused, the game keeps 1280x720 and the movie fills the width.
 * - The MP3 decoder l3codeca.acm under Drivers32, for the movies' sound.
 * - DirectShow: quartz.dll and devenum.dll registered as regsvr32 would, since
 *   Wine's first-run setup does not run on the Switch. quartz is registered
 *   before and after devenum, because its filters need devenum's monikers.
 *   blizzard.ax, the game's video decoder, is registered by the game itself.
 *
 * Each step is reported to autorun_runtime.log as a [WAR3 SETUP] line; the
 * exit code is 0 when every step that matters worked. Running it again is
 * harmless. */
#include <windows.h>
#include <winternl.h>
#include <ole2.h>

__declspec(dllimport) NTSTATUS NTAPI NtDisplayString( const UNICODE_STRING *str );

/* Built without a C runtime; the compiler still expects these for structures. */
void *memset( void *dst, int c, size_t n )
{
    unsigned char *p = dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}

void *memcpy( void *dst, const void *src, size_t n )
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

/* One line: "[WAR3 SETUP] <label> <name>: <result>", with the value in hex. */
static void report( const char *label, const WCHAR *name, const char *result, DWORD value )
{
    static const char hex[] = "0123456789abcdef";
    const char *prefix = "[WAR3 SETUP] ";
    WCHAR buffer[400];
    UNICODE_STRING str;
    unsigned int n = 0, i;

    while (*prefix) buffer[n++] = *prefix++;
    while (*label && n < 100) buffer[n++] = *label++;
    if (name)
    {
        buffer[n++] = ' ';
        while (*name && n < 300) buffer[n++] = *name++;
    }
    buffer[n++] = ':';
    buffer[n++] = ' ';
    while (*result && n < 380) buffer[n++] = *result++;
    buffer[n++] = ' ';
    buffer[n++] = '0';
    buffer[n++] = 'x';
    for (i = 0; i < 8; i++) buffer[n++] = hex[(value >> (28 - i * 4)) & 15];
    /* No newline: the log makes a line of each call, and anything that is not
     * plain ASCII reaches it as a question mark. */
    str.Buffer = buffer;
    str.Length = n * sizeof(WCHAR);
    str.MaximumLength = str.Length;
    NtDisplayString( &str );
}

static unsigned int wide_length( const WCHAR *text )
{
    unsigned int n = 0;
    while (text[n]) n++;
    return n;
}

static LONG set_value( HKEY root, const WCHAR *path, const WCHAR *name, DWORD type, const BYTE *data, DWORD size )
{
    HKEY key;
    LONG status;

    if ((status = RegCreateKeyExW( root, path, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL ))) return status;
    status = RegSetValueExW( key, name, 0, type, data, size );
    RegCloseKey( key );
    return status;
}

static BOOL set_dword( HKEY root, const WCHAR *path, const WCHAR *name, DWORD value )
{
    LONG status = set_value( root, path, name, REG_DWORD, (const BYTE *)&value, sizeof(value) );

    report( "set", name, status ? "failed, error" : "ok, value", status ? (DWORD)status : value );
    return !status;
}

static BOOL set_string( HKEY root, const WCHAR *path, const WCHAR *name, const WCHAR *value )
{
    LONG status = set_value( root, path, name, REG_SZ, (const BYTE *)value,
                             (wide_length( value ) + 1) * sizeof(WCHAR) );

    report( "set", name, status ? "failed, error" : "ok", (DWORD)status );
    return !status;
}

/* A value that is not there counts as deleted. */
static BOOL delete_value( HKEY root, const WCHAR *path, const WCHAR *name )
{
    HKEY key;
    LONG status;

    if (!(status = RegOpenKeyExW( root, path, 0, KEY_SET_VALUE, &key )))
    {
        status = RegDeleteValueW( key, name );
        RegCloseKey( key );
    }
    if (status == ERROR_FILE_NOT_FOUND)
    {
        report( "delete", name, "ok, was not set, error", (DWORD)status );
        return TRUE;
    }
    report( "delete", name, status ? "failed, error" : "ok, error", (DWORD)status );
    return !status;
}

/* What regsvr32 does for a DLL: load it and call its DllRegisterServer. */
static BOOL register_dll( const WCHAR *name )
{
    HRESULT (WINAPI *register_server)(void);
    HMODULE module;
    HRESULT hr;

    if (!(module = LoadLibraryW( name )))
    {
        report( "load", name, "failed, error", GetLastError() );
        return FALSE;
    }
    if (!(register_server = (void *)GetProcAddress( module, "DllRegisterServer" )))
    {
        report( "register", name, "has no DllRegisterServer, error", GetLastError() );
        FreeLibrary( module );
        return FALSE;
    }
    hr = register_server();
    report( "register", name, SUCCEEDED(hr) ? "ok, hr" : "failed, hr", (DWORD)hr );
    FreeLibrary( module );
    return SUCCEEDED(hr);
}

void __stdcall start(void)
{
    static const WCHAR war3[] = L"Software\\Blizzard Entertainment\\Warcraft III";
    static const WCHAR video[] = L"Software\\Blizzard Entertainment\\Warcraft III\\Video";
    static const WCHAR misc[] = L"Software\\Blizzard Entertainment\\Warcraft III\\Misc";
    static const WCHAR drivers32[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Drivers32";
    static const WCHAR war3_driver[] = L"Software\\Wine\\AppDefaults\\war3.exe\\X11 Driver";
    BOOL ok = TRUE;
    HRESULT hr;

    report( "start", NULL, "build", 1 );

    ok &= set_dword( HKEY_CURRENT_USER, video, L"reswidth", 1280 );
    ok &= set_dword( HKEY_CURRENT_USER, video, L"resheight", 720 );
    ok &= set_dword( HKEY_CURRENT_USER, video, L"colordepth", 32 );
    ok &= set_dword( HKEY_CURRENT_USER, video, L"refreshrate", 60 );
    ok &= delete_value( HKEY_CURRENT_USER, misc, L"seenintromovie" );
    ok &= set_dword( HKEY_CURRENT_USER, war3, L"Gfx OpenGL", 1 );
    ok &= set_string( HKEY_CURRENT_USER, war3_driver, L"EmulateModelist", L"Y" );
    ok &= set_string( HKEY_LOCAL_MACHINE, drivers32, L"msacm.l3acm", L"l3codeca.acm" );

    hr = OleInitialize( NULL );
    report( "OleInitialize", NULL, SUCCEEDED(hr) ? "ok, hr" : "failed, hr", (DWORD)hr );
    /* The first quartz registration fails while devenum is not registered yet;
     * only the second one counts. */
    if (!register_dll( L"quartz.dll" )) report( "register", L"quartz.dll", "first try, repeated after devenum", 0 );
    ok &= register_dll( L"devenum.dll" );
    ok &= register_dll( L"quartz.dll" );
    if (SUCCEEDED(hr)) OleUninitialize();

    report( ok ? "done, all steps worked" : "done, a step FAILED (see above)", NULL, "exit code", !ok );
    ExitProcess( !ok );
}
