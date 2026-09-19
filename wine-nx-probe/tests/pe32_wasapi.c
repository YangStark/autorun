/* Wine-NX WASAPI checkpoint: the steps winmm's waveOutOpen takes, one at a
 * time, so a failing stage names itself. No CRT; kernel32, user32, ole32 and
 * ntdll imports. Exit 42 means all groups passed; 0x100 | mask reports the
 * failing groups: 1=waiting on an event beside a running thread, 2=render
 * endpoint enumeration, 4=resolving the endpoint ID from a second apartment
 * behind a message-only window, 8=IAudioClient activation and initialization,
 * 16=event-driven rendering. Detail codes are printed before exit. */
#define COBJMACROS
#include <windows.h>
#include <winternl.h>
#include <propsys.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#ifdef _WIN64
#define PE_TEST_PREFIX "[PE64 TEST] "
#else
#define PE_TEST_PREFIX "[PE32 TEST] "
#endif

#include "pe_test_io.h"

/* winmm sends WODM_OPEN to its device thread as a window message. */
#define OPEN_MESSAGE 5
#define QUIT_MESSAGE (WM_USER + 20)
#define TONE_FRAMES 48000

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

static void report( const char *label, DWORD code )
{
    static const char hex[] = "0123456789abcdef";
    WCHAR text[160];
    UNICODE_STRING str;
    unsigned int n = 0, i;
    const char *prefix = PE_TEST_PREFIX;
    while (*prefix) text[n++] = *prefix++;
    while (*label && n < 120) text[n++] = *label++;
    text[n++] = ' '; text[n++] = '0'; text[n++] = 'x';
    for (i = 0; i < 8; i++) text[n++] = hex[(code >> (28 - i * 4)) & 15];
    text[n] = 0;
    str.Buffer = text; str.Length = n * sizeof(WCHAR);
    str.MaximumLength = (n + 1) * sizeof(WCHAR);
    pe_test_display_string( &str );
}

static void report_text( const char *label, const WCHAR *text )
{
    WCHAR line[200];
    UNICODE_STRING str;
    unsigned int n = 0;
    const char *prefix = PE_TEST_PREFIX;
    while (*prefix) line[n++] = *prefix++;
    while (*label && n < 100) line[n++] = *label++;
    line[n++] = ' ';
    if (!text) text = L"(null)";
    while (*text && n < 199) line[n++] = *text++;
    line[n] = 0;
    str.Buffer = line; str.Length = n * sizeof(WCHAR);
    str.MaximumLength = (n + 1) * sizeof(WCHAR);
    pe_test_display_string( &str );
}

static const CLSID clsid_enumerator = {0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
static const IID iid_enumerator = {0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};
static const IID iid_client = {0x1cb9ad4c, 0xdbfa, 0x4c32, {0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2}};
static const IID iid_render = {0xf294acfc, 0x3146, 0x4483, {0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2}};
static const IID iid_clock = {0xcd63314f, 0x3fba, 0x4a1b, {0x81, 0x2c, 0xef, 0x96, 0x35, 0x87, 0x28, 0xe7}};
static const IID iid_volume = {0x93014887, 0x242d, 0x4068, {0x8a, 0x15, 0xcf, 0x5e, 0x93, 0xb9, 0x0f, 0xe3}};
static const PROPERTYKEY key_friendly_name = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

static WAVEFORMATEX format = { WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0 };
static WCHAR *endpoint_id;
static PROPVARIANT name_value;

static HANDLE park_event;

static DWORD WINAPI signal_worker( void *event )
{
    SetEvent( event );
    WaitForSingleObject( park_event, INFINITE );
    return 3;
}

/* winmm waits for its device thread's readiness event next to the thread
 * handle itself, so the wait must report the event while the thread lives. */
static DWORD test_mixed_wait(void)
{
    HANDLE objects[2];
    DWORD wait;

    park_event = CreateEventW( NULL, TRUE, FALSE, NULL );
    objects[0] = CreateEventW( NULL, FALSE, FALSE, NULL );
    if (!park_event || !objects[0]) return 1;
    objects[1] = CreateThread( NULL, 0, signal_worker, objects[0], 0, NULL );
    if (!objects[1]) return 2;
    wait = WaitForMultipleObjects( 2, objects, FALSE, 10000 );
    report( "wait for the event beside the running thread (expected 0)", wait );
    if (wait != WAIT_OBJECT_0) return 3;
    if (WaitForSingleObject( objects[1], 0 ) != WAIT_TIMEOUT) return 4;
    SetEvent( park_event );
    wait = WaitForMultipleObjects( 2, objects, FALSE, 10000 );
    report( "wait for the thread beside the reset event (expected 1)", wait );
    if (wait != WAIT_OBJECT_0 + 1) return 5;
    CloseHandle( objects[1] );
    CloseHandle( objects[0] );
    CloseHandle( park_event );
    return 0;
}

/* The enumeration winmm performs from waveOutGetNumDevs on the caller's thread. */
static DWORD test_enumerate(void)
{
    IMMDeviceEnumerator *enumerator;
    IMMDeviceCollection *collection;
    IMMDevice *device, *default_device, *found;
    IPropertyStore *store;
    WCHAR *id;
    UINT count = 0;
    HRESULT hr;
    int cmp;

    hr = CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );
    report( "CoInitializeEx apartment", hr );
    if (FAILED( hr )) return 1;
    hr = CoCreateInstance( &clsid_enumerator, NULL, CLSCTX_INPROC_SERVER, &iid_enumerator, (void **)&enumerator );
    report( "CoCreateInstance MMDeviceEnumerator", hr );
    if (FAILED( hr )) return 2;
    hr = IMMDeviceEnumerator_EnumAudioEndpoints( enumerator, eRender, DEVICE_STATE_ACTIVE, &collection );
    report( "EnumAudioEndpoints render", hr );
    if (FAILED( hr )) return 3;
    hr = IMMDeviceCollection_GetCount( collection, &count );
    report( "active render endpoints", count );
    if (FAILED( hr ) || !count) return 4;
    hr = IMMDeviceCollection_Item( collection, 0, &device );
    report( "Item 0", hr );
    if (FAILED( hr )) return 5;
    hr = IMMDevice_GetId( device, &endpoint_id );
    report( "GetId", hr );
    if (FAILED( hr )) return 6;
    report_text( "endpoint", endpoint_id );

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint( enumerator, eRender, eConsole, &default_device );
    report( "GetDefaultAudioEndpoint console", hr );
    if (FAILED( hr )) return 7;
    report( "default is endpoint 0 (expected 1)", default_device == device );
    hr = IMMDevice_GetId( default_device, &id );
    report( "default GetId", hr );
    if (FAILED( hr )) return 8;
    cmp = lstrcmpW( endpoint_id, id );
    report( "lstrcmpW of both IDs (expected 0)", cmp );
    CoTaskMemFree( id );
    IMMDevice_Release( default_device );
    if (cmp || default_device != device) return 9;

    hr = IMMDevice_OpenPropertyStore( device, STGM_READ, &store );
    report( "OpenPropertyStore", hr );
    if (FAILED( hr )) return 10;
    hr = IPropertyStore_GetValue( store, &key_friendly_name, &name_value );
    report( "GetValue FriendlyName", hr );
    report( "FriendlyName type (expected 1f)", name_value.vt );
    if (SUCCEEDED( hr ) && name_value.vt == VT_LPWSTR) report_text( "name", name_value.pwszVal );
    IPropertyStore_Release( store );
    if (FAILED( hr ) || name_value.vt != VT_LPWSTR) return 11;
    PropVariantClear( &name_value );

    report( "thread locale", GetThreadLocale() );
    SetLastError( 0 );
    cmp = CompareStringW( GetThreadLocale(), 0, endpoint_id, -1, endpoint_id, -1 );
    report( "CompareStringW of the ID with itself (expected 2)", cmp );
    if (cmp != CSTR_EQUAL)
    {
        report( "CompareStringW GetLastError", GetLastError() );
        return 12;
    }
    hr = IMMDeviceEnumerator_GetDevice( enumerator, endpoint_id, &found );
    report( "GetDevice on the enumerating thread", hr );
    if (FAILED( hr )) return 13;
    report( "GetDevice returned endpoint 0 (expected 1)", found == device );
    IMMDevice_Release( found );
    if (found != device) return 14;

    IMMDevice_Release( device );
    IMMDeviceCollection_Release( collection );
    IMMDeviceEnumerator_Release( enumerator );
    CoUninitialize();
    return 0;
}

static UINT32 tone_position;

/* Half a second left, then half a second right; a quiet triangle wave.
 * Integer synthesis keeps this probe independent of CRT and libm. */
static void fill_tone( BYTE *data, UINT32 frames )
{
    short *samples = (short *)data;
    UINT32 i;

    for (i = 0; i < frames; i++, tone_position++)
    {
        unsigned int period = tone_position < TONE_FRAMES / 2 ? 120 : 80;
        int phase = tone_position % period;
        int value = phase < (int)period / 2 ? phase : (int)period - phase;
        value = (value * 8192 / (int)period) - 2048;
        samples[i * 2] = tone_position < TONE_FRAMES / 2 ? value : 0;
        samples[i * 2 + 1] = tone_position >= TONE_FRAMES / 2 ? value : 0;
    }
}

static DWORD render_tone( IAudioClient *client, IAudioRenderClient *render, HANDLE event )
{
    UINT32 frames = 0, padding = 0, chunk, signals = 0;
    DWORD start, wait;
    BYTE *data;
    HRESULT hr;

    hr = IAudioClient_GetBufferSize( client, &frames );
    report( "GetBufferSize", hr );
    report( "buffer frames", frames );
    if (FAILED( hr ) || !frames) return 1;
    hr = IAudioClient_GetCurrentPadding( client, &padding );
    if (FAILED( hr ) || padding > frames)
    {
        report( "GetCurrentPadding before Start", hr );
        return 2;
    }
    chunk = frames - padding;
    if (chunk > TONE_FRAMES) chunk = TONE_FRAMES;
    hr = IAudioRenderClient_GetBuffer( render, chunk, &data );
    report( "GetBuffer", hr );
    if (FAILED( hr )) return 3;
    fill_tone( data, chunk );
    hr = IAudioRenderClient_ReleaseBuffer( render, chunk, 0 );
    report( "ReleaseBuffer", hr );
    if (FAILED( hr )) return 4;
    hr = IAudioClient_Start( client );
    report( "Start", hr );
    if (FAILED( hr )) return 5;

    start = GetTickCount();
    while (tone_position < TONE_FRAMES)
    {
        wait = WaitForSingleObject( event, 1000 );
        if (wait != WAIT_OBJECT_0)
        {
            report( "render event wait", wait );
            return 6;
        }
        signals++;
        hr = IAudioClient_GetCurrentPadding( client, &padding );
        if (FAILED( hr ) || padding > frames)
        {
            report( "GetCurrentPadding while playing", hr );
            return 7;
        }
        chunk = frames - padding;
        if (chunk > TONE_FRAMES - tone_position) chunk = TONE_FRAMES - tone_position;
        if (!chunk) continue;
        hr = IAudioRenderClient_GetBuffer( render, chunk, &data );
        if (FAILED( hr ))
        {
            report( "GetBuffer while playing", hr );
            return 8;
        }
        fill_tone( data, chunk );
        hr = IAudioRenderClient_ReleaseBuffer( render, chunk, 0 );
        if (FAILED( hr ))
        {
            report( "ReleaseBuffer while playing", hr );
            return 9;
        }
    }
    report( "render event signals", signals );
    report( "milliseconds to queue one second", GetTickCount() - start );
    while (GetTickCount() - start < 3000)
    {
        hr = IAudioClient_GetCurrentPadding( client, &padding );
        if (FAILED( hr ) || !padding) break;
        Sleep( 20 );
    }
    report( "milliseconds to play one second", GetTickCount() - start );
    hr = IAudioClient_Stop( client );
    report( "Stop", hr );
    if (FAILED( hr )) return 10;
    return GetTickCount() - start < 750 ? 11 : 0;
}

static HWND device_window;
static IMMDeviceEnumerator *device_enumerator;
static volatile LONG device_quit;
static DWORD open_mask;

/* What winmm's WOD_Open does inside its device thread's window procedure. */
static DWORD open_device(void)
{
    IMMDevice *device;
    IAudioClient *client = NULL;
    IAudioRenderClient *render = NULL;
    IAudioClock *clock = NULL;
    IAudioStreamVolume *volume = NULL;
    WAVEFORMATEX *closest = NULL;
    HANDLE event = NULL;
    GUID session;
    HRESULT hr;
    DWORD detail = 0;
    int cmp;

    report( "device thread locale", GetThreadLocale() );
    cmp = CompareStringW( GetThreadLocale(), 0, endpoint_id, -1, endpoint_id, -1 );
    report( "device thread CompareStringW of the ID with itself (expected 2)", cmp );
    hr = IMMDeviceEnumerator_GetDevice( device_enumerator, endpoint_id, &device );
    report( "GetDevice on the device thread", hr );
    if (FAILED( hr ))
    {
        open_mask |= 4;
        return 1;
    }

    hr = IMMDevice_Activate( device, &iid_client, CLSCTX_INPROC_SERVER, NULL, (void **)&client );
    report( "Activate IAudioClient", hr );
    if (FAILED( hr )) { open_mask |= 8; detail = 2; goto done; }
    hr = IAudioClient_IsFormatSupported( client, AUDCLNT_SHAREMODE_SHARED, &format, &closest );
    report( "IsFormatSupported PCM stereo 48000 Hz 16-bit", hr );
    CoTaskMemFree( closest );
    CoCreateGuid( &session );
    hr = IAudioClient_Initialize( client, AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
                                  10 * 100000, 0, &format, &session );
    report( "Initialize shared event-driven 100 ms", hr );
    if (FAILED( hr )) { open_mask |= 8; detail = 3; goto done; }
    hr = IAudioClient_GetService( client, &iid_clock, (void **)&clock );
    report( "GetService IAudioClock", hr );
    if (FAILED( hr )) { open_mask |= 8; detail = 4; goto done; }
    hr = IAudioClient_GetService( client, &iid_render, (void **)&render );
    report( "GetService IAudioRenderClient", hr );
    if (FAILED( hr )) { open_mask |= 8; detail = 5; goto done; }
    hr = IAudioClient_GetService( client, &iid_volume, (void **)&volume );
    report( "GetService IAudioStreamVolume", hr );
    if (FAILED( hr )) { open_mask |= 8; detail = 6; goto done; }
    event = CreateEventW( NULL, FALSE, FALSE, NULL );
    hr = IAudioClient_SetEventHandle( client, event );
    report( "SetEventHandle", hr );
    if (FAILED( hr )) { open_mask |= 8; detail = 7; goto done; }

    detail = render_tone( client, render, event );
    if (detail)
    {
        open_mask |= 16;
        detail += 0x10;
    }

done:
    if (volume) IAudioStreamVolume_Release( volume );
    if (render) IAudioRenderClient_Release( render );
    if (clock) IAudioClock_Release( clock );
    if (client) IAudioClient_Release( client );
    if (event) CloseHandle( event );
    IMMDevice_Release( device );
    return detail;
}

static LRESULT WINAPI device_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case OPEN_MESSAGE: return open_device();
    case QUIT_MESSAGE: device_quit = 1; return 0x51;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

/* winmm's device thread: a multithreaded apartment, its own enumerator and a
 * message-only window that serves the caller's sent messages. */
static DWORD WINAPI device_main( void *ready )
{
    HRESULT hr;
    MSG msg;

    hr = CoInitializeEx( NULL, COINIT_MULTITHREADED );
    report( "device thread CoInitializeEx multithreaded", hr );
    if (FAILED( hr )) return 1;
    hr = CoCreateInstance( &clsid_enumerator, NULL, CLSCTX_INPROC_SERVER, &iid_enumerator,
                           (void **)&device_enumerator );
    report( "device thread CoCreateInstance MMDeviceEnumerator", hr );
    if (FAILED( hr ))
    {
        CoUninitialize();
        return 2;
    }
    device_window = CreateWindowW( L"Message", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL );
    if (!device_window)
    {
        report( "CreateWindow Message GetLastError", GetLastError() );
        IMMDeviceEnumerator_Release( device_enumerator );
        CoUninitialize();
        return 3;
    }
    SetWindowLongPtrW( device_window, GWLP_WNDPROC, (LONG_PTR)device_proc );
    SetEvent( ready );
    while (!device_quit)
    {
        DWORD wait = MsgWaitForMultipleObjects( 0, NULL, FALSE, INFINITE, QS_ALLINPUT );
        if (wait != WAIT_OBJECT_0)
        {
            report( "device thread MsgWaitForMultipleObjects", wait );
            break;
        }
        while (PeekMessageW( &msg, device_window, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
    }
    DestroyWindow( device_window );
    IMMDeviceEnumerator_Release( device_enumerator );
    CoUninitialize();
    return 0;
}

static DWORD test_device_thread(void)
{
    HANDLE objects[2];
    DWORD wait, code = 0, mask = 0;
    LRESULT result;

    objects[0] = CreateEventW( NULL, FALSE, FALSE, NULL );
    objects[1] = objects[0] ? CreateThread( NULL, 0, device_main, objects[0], 0, NULL ) : NULL;
    if (!objects[1])
    {
        report( "device thread creation GetLastError", GetLastError() );
        return 4;
    }
    wait = WaitForMultipleObjects( 2, objects, FALSE, 20000 );
    report( "wait for the device thread window (expected 0)", wait );
    CloseHandle( objects[0] );
    if (wait != WAIT_OBJECT_0)
    {
        if (wait == WAIT_OBJECT_0 + 1 && GetExitCodeThread( objects[1], &code ))
            report( "device thread exit code", code );
        CloseHandle( objects[1] );
        return 4;
    }
    result = SendMessageW( device_window, OPEN_MESSAGE, 0, 0 );
    report( "open through the device thread window", (DWORD)result );
    mask |= open_mask;
    result = SendMessageW( device_window, QUIT_MESSAGE, 0, 0 );
    report( "quit reply (expected 51)", (DWORD)result );
    wait = WaitForSingleObject( objects[1], 10000 );
    report( "device thread exit (expected 0)", wait );
    CloseHandle( objects[1] );
    if (result != 0x51 || wait != WAIT_OBJECT_0) mask |= 4;
    return mask;
}

void __stdcall start(void)
{
    DWORD result, mask = 0;

    report( "BEGIN wait on an event beside a running thread", 0 );
    result = test_mixed_wait();
    report( result ? "FAIL mixed wait" : "PASS mixed wait", result );
    if (result) mask |= 1;

    report( "BEGIN render endpoint enumeration", 0 );
    result = test_enumerate();
    report( result ? "FAIL enumeration" : "PASS enumeration", result );
    if (result) mask |= 2;
    else
    {
        report( "BEGIN open through a device thread", 0 );
        result = test_device_thread();
        report( result ? "FAIL device thread open" : "PASS device thread open", result );
        mask |= result;
    }
    report( mask ? "FAIL combined mask" : "PASS ALL; verify left then right tone", mask );
    pe_test_terminate( mask ? 0x100 | mask : 42 );
    for (;;) {}
}
