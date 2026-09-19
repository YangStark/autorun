/* The Sims 2 Ultimate Collection setup for Wine-NX: what the release's
 * "Instalar Registros" batch file writes, written from inside the card.
 *
 * The game is shipped installed -- Base, the expansions and the stuff packs are
 * whole folders of data -- and the only thing left to do is tell it where each
 * one is. The batch file is nothing but reg add lines, and every path in it
 * comes from %CD%, so it cannot be shipped as a registry file either: the card
 * puts the game somewhere else than the computer it was unpacked on. This does
 * the same from wherever it is run: the packs live beside this program's own
 * folder, and their paths are written as the game will read them.
 *
 * - Software\Electronic Arts\The Sims 2 Ultimate Collection 25, with the list
 *   of expansions the game looks through. The list keeps the order and the
 *   empty place the release's own has: the game reads it by position.
 * - A key for each pack that is really there, with Installed and its Path;
 *   one for a pack that is not would send the game to a folder with nothing in
 *   it. The base game and the last expansion also carry Game Registry, which
 *   is where the game looks for the rest.
 * - 1.0\language, the number the batch file asks for, and the locale the same
 *   number stands for under Software\Maxis\The Sims 2 Legacy. The release's
 *   own anadius.cfg sets its language to "invalid" so that the game reads that
 *   second key instead, and asks for it by name: without it the game says
 *   "open: Invalid handle" and stops before it starts. language.txt beside this
 *   program holds the number, and without one it is 1, English (United States);
 *   the numbers are in the readme.
 * - vidc.VP60 and vidc.VP61 under Drivers32, which is what the release's
 *   vp6.reg holds. Not for the game's own movies, which are Maxis' own format
 *   and want no codec, but for the video it writes: it records gameplay
 *   through Video for Windows, which finds a codec by those two names, and
 *   reads a custom video made as a VP6 AVI the same way. The DLL is the
 *   release's to copy, nobody else having it to give away, and registering a
 *   codec that is not there costs nothing.
 *
 * Each step is reported to wine-nx-runtime.log as a [SIMS2 SETUP] line; the
 * exit code is 0 when every step worked. Running it again is harmless. */
#include <windows.h>
#include <winternl.h>

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

static unsigned int wide_length( const WCHAR *text )
{
    unsigned int n = 0;
    while (text[n]) n++;
    return n;
}

static void wide_append( WCHAR *out, unsigned int *at, unsigned int max, const WCHAR *text )
{
    while (*text && *at + 1 < max) out[(*at)++] = *text++;
    out[*at] = 0;
}

/* One line: "[SIMS2 SETUP] <label> <name>: <result>", with the value in hex. */
static void report( const char *label, const WCHAR *name, const char *result, DWORD value )
{
    static const char hex[] = "0123456789abcdef";
    const char *prefix = "[SIMS2 SETUP] ";
    WCHAR buffer[400];
    UNICODE_STRING str;
    unsigned int n = 0, i;

    while (*prefix) buffer[n++] = *prefix++;
    while (*label && n < 100) buffer[n++] = *label++;
    if (name)
    {
        buffer[n++] = ' ';
        while (*name && n < 320) buffer[n++] = *name++;
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
    str.Length = (USHORT)(n * sizeof(WCHAR));
    str.MaximumLength = str.Length;
    NtDisplayString( &str );
}

static LONG set_value_in( HKEY root, const WCHAR *path, const WCHAR *name, DWORD type,
                          const BYTE *data, DWORD size )
{
    HKEY key;
    LONG status;

    if ((status = RegCreateKeyExW( root, path, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL ))) return status;
    status = RegSetValueExW( key, name, 0, type, data, size );
    RegCloseKey( key );
    return status;
}

static LONG set_value( const WCHAR *path, const WCHAR *name, DWORD type, const BYTE *data, DWORD size )
{
    return set_value_in( HKEY_CURRENT_USER, path, name, type, data, size );
}

/* A codec is the machine's, not one person's. */
static BOOL set_machine_string( const WCHAR *path, const WCHAR *name, const WCHAR *value )
{
    LONG status = set_value_in( HKEY_LOCAL_MACHINE, path, name, REG_SZ, (const BYTE *)value,
                                (wide_length( value ) + 1) * sizeof(WCHAR) );

    report( "set", name, status ? "failed, error" : "ok", (DWORD)status );
    return !status;
}

static BOOL set_string( const WCHAR *path, const WCHAR *name, const WCHAR *value )
{
    LONG status = set_value( path, name, REG_SZ, (const BYTE *)value,
                             (wide_length( value ) + 1) * sizeof(WCHAR) );

    report( "set", name, status ? "failed, error" : "ok", (DWORD)status );
    return !status;
}

static BOOL set_dword( const WCHAR *path, const WCHAR *name, DWORD value )
{
    LONG status = set_value( path, name, REG_DWORD, (const BYTE *)&value, sizeof(value) );

    report( "set", name, status ? "failed, error" : "ok, value", status ? (DWORD)status : value );
    return !status;
}

/* The folder this program is in, without its name or the trailing slash --
 * except at the root of a drive, where the slash is part of the name. */
static BOOL own_folder( WCHAR *out, unsigned int max )
{
    unsigned int n = GetModuleFileNameW( NULL, out, max );

    if (!n || n >= max) return FALSE;
    while (n && out[n - 1] != '\\') n--;
    if (!n) return FALSE;
    out[n > 3 ? n - 1 : n] = 0;
    return TRUE;
}

/* The folder above that one, where the game sits beside this program rather
 * than inside it: C:\The Sims 2 next to C:\The Sims 2 Setup. */
static BOOL parent_folder( const WCHAR *folder, WCHAR *out, unsigned int max )
{
    unsigned int n = 0;

    while (folder[n] && n + 1 < max) { out[n] = folder[n]; n++; }
    out[n] = 0;
    while (n && out[n - 1] != '\\') n--;
    if (!n) return FALSE;
    out[n > 3 ? n - 1 : n] = 0;
    return TRUE;
}

/* folder\name, without doubling the slash at the root of a drive. */
static void join( WCHAR *out, unsigned int max, const WCHAR *folder, const WCHAR *name )
{
    unsigned int at = 0;

    wide_append( out, &at, max, folder );
    if (at && out[at - 1] != '\\') wide_append( out, &at, max, L"\\" );
    wide_append( out, &at, max, name );
}

static BOOL folder_exists( const WCHAR *path )
{
    DWORD attributes = GetFileAttributesW( path );

    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

/* Whether the packs are here: the base game, or the newest expansion for
 * someone who copied only part of the collection. */
static BOOL holds_the_game( const WCHAR *root )
{
    WCHAR path[MAX_PATH];

    join( path, MAX_PATH, root, L"Base" );
    if (folder_exists( path )) return TRUE;
    join( path, MAX_PATH, root, L"EP9" );
    return folder_exists( path );
}

/* The number in language.txt beside this program, or 1 for English. */
static DWORD chosen_language( const WCHAR *setup_folder )
{
    WCHAR path[MAX_PATH];
    char text[16];
    DWORD read = 0, value = 0, i;
    HANDLE file;

    join( path, MAX_PATH, setup_folder, L"language.txt" );
    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    if (file == INVALID_HANDLE_VALUE) return 1;
    if (ReadFile( file, text, sizeof(text) - 1, &read, NULL ))
        for (i = 0; i < read && text[i] >= '0' && text[i] <= '9'; i++) value = value * 10 + (DWORD)(text[i] - '0');
    CloseHandle( file );
    return value ? value : 1;
}

void __stdcall start(void)
{
    /* Every pack the collection has, in the order the game's own list gives
     * them: the executable it is known by and the folder it lives in. */
    static const struct { const WCHAR *exe, *folder; } packs[] =
    {
        { L"Sims2.exe",    L"Base" },
        { L"Sims2EP1.exe", L"EP1" }, { L"Sims2EP2.exe", L"EP2" }, { L"Sims2EP3.exe", L"EP3" },
        { L"Sims2SP1.exe", L"SP1" }, { L"Sims2SP2.exe", L"SP2" },
        { L"Sims2EP4.exe", L"EP4" }, { L"Sims2EP5.exe", L"EP5" },
        { L"Sims2SP4.exe", L"SP4" }, { L"Sims2SP5.exe", L"SP5" },
        { L"Sims2EP6.exe", L"EP6" }, { L"Sims2SP6.exe", L"SP6" },
        { L"Sims2EP7.exe", L"EP7" }, { L"Sims2SP7.exe", L"SP7" },
        { L"Sims2SP8.exe", L"SP8" }, { L"Sims2EP8.exe", L"EP8" },
        { L"Sims2EP9.exe", L"EP9" },
    };
    /* The list the game reads by position, empty place and all. */
    static const WCHAR eps_installed[] =
        L"Sims2EP1.exe,Sims2EP2.exe,Sims2EP3.exe,Sims2SP1.exe,Sims2SP2.exe,Sims2EP4.exe,"
        L"Sims2EP5.exe,Sims2SP4.exe,Sims2SP5.exe,Sims2EP6.exe,Sims2SP6.exe,,Sims2EP7.exe,"
        L"Sims2SP7.exe,Sims2SP8.exe,Sims2EP8.exe,Sims2EP9.exe";
    static const WCHAR collection[] = L"Software\\Electronic Arts\\The Sims 2 Ultimate Collection 25";
    static const WCHAR drivers32[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Drivers32";
    WCHAR setup_folder[MAX_PATH], game[MAX_PATH], path[MAX_PATH * 2], folder[MAX_PATH];
    unsigned int i, at, found = 0;
    BOOL ok = TRUE;

    report( "start", NULL, "build", 1 );
    if (!own_folder( setup_folder, MAX_PATH ))
    {
        report( "find the game", NULL, "failed: cannot read this program's own path, error", GetLastError() );
        ExitProcess( 1 );
    }
    /* Where the packs are: in here, if they were copied in beside this
     * program; in the folder the readme names, beside this one; or in the
     * folder this one is in, for a setup dropped into the game itself. */
    {
        WCHAR above[MAX_PATH];
        BOOL have_above = parent_folder( setup_folder, above, MAX_PATH );
        unsigned int n = 0;

        wide_append( game, &n, MAX_PATH, setup_folder );
        if (!holds_the_game( game ) && have_above)
        {
            join( game, MAX_PATH, above, L"The Sims 2" );
            if (!holds_the_game( game ))
            {
                n = 0;
                wide_append( game, &n, MAX_PATH, above );
            }
        }
    }
    if (!holds_the_game( game ))
    {
        report( "find the game", game, "FAILED: no Base or EP9 folder there, error", 0 );
        ExitProcess( 1 );
    }
    report( "the game is in", game, "found", 0 );

    ok &= set_string( collection, L"DisplayName", L"The Sims 2 Legacy" );
    ok &= set_string( collection, L"EPsInstalled", eps_installed );

    {
        /* The number the batch file asks for, and the locale it stands for:
         * the game takes one and the release's launcher emulation the other,
         * and they have to agree. */
        static const struct { DWORD number; const WCHAR *locale; } locales[] =
        {
            {  1, L"en_US" }, {  2, L"fr_FR" }, {  3, L"de_DE" }, {  4, L"it_IT" },
            {  5, L"es_ES" }, {  6, L"sv_SE" }, {  7, L"fi_FI" }, {  8, L"nl_NL" },
            {  9, L"da_DK" }, { 10, L"pt_BR" }, { 11, L"cs_CZ" }, { 13, L"en_GB" },
            { 14, L"ja_JP" }, { 15, L"ko_KR" }, { 16, L"ru_RU" }, { 17, L"zh_CN" },
            { 18, L"zh_TW" }, { 20, L"pl_PL" }, { 21, L"th_TH" }, { 22, L"no_NO" },
            { 23, L"pt_PT" }, { 24, L"hu_HU" },
        };
        DWORD language = chosen_language( setup_folder );
        const WCHAR *locale = L"en_US";
        unsigned int n;

        for (n = 0; n < sizeof(locales) / sizeof(locales[0]); n++)
            if (locales[n].number == language) locale = locales[n].locale;

        at = 0;
        wide_append( path, &at, MAX_PATH * 2, collection );
        wide_append( path, &at, MAX_PATH * 2, L"\\1.0" );
        ok &= set_dword( path, L"language", language );
        ok &= set_string( L"Software\\Maxis\\The Sims 2 Legacy", L"Locale", locale );
    }

    for (i = 0; i < sizeof(packs) / sizeof(packs[0]); i++)
    {
        join( folder, MAX_PATH, game, packs[i].folder );
        if (!folder_exists( folder ))
        {
            /* A key for a pack that is not there would send the game to an
             * empty folder, which is worse than not knowing about it. */
            report( "skip", packs[i].exe, "not installed, error", 0 );
            continue;
        }
        found++;
        at = 0;
        wide_append( path, &at, MAX_PATH * 2, collection );
        wide_append( path, &at, MAX_PATH * 2, L"\\" );
        wide_append( path, &at, MAX_PATH * 2, packs[i].exe );
        ok &= set_dword( path, L"Installed", 1 );
        ok &= set_string( path, L"Path", folder );
        /* Where the base game and the newest expansion look for the rest. */
        if (!i || i + 1 == sizeof(packs) / sizeof(packs[0]))
            ok &= set_string( path, L"Game Registry", collection );
    }

    /* The movies' codec, by the two names Video for Windows opens it under. */
    ok &= set_machine_string( drivers32, L"vidc.VP60", L"vp6vfw.dll" );
    ok &= set_machine_string( drivers32, L"vidc.VP61", L"vp6vfw.dll" );

    if (!found)
    {
        report( "done", NULL, "FAILED: no pack found beside this program, error", 0 );
        ExitProcess( 1 );
    }
    report( ok ? "done, all steps worked, packs" : "done, a step FAILED (see above), packs", NULL,
            "count", found );
    ExitProcess( !ok );
}
