/* Wine-NX Direct3D 9 checkpoint. Wine's d3d9 and wined3d translate this to
 * OpenGL, which on the Switch is Mesa's nouveau driver, and DXVK's d3d9
 * translates it to Vulkan, so this says whether either chain stands up before
 * a game is asked to. A full-screen device clears through red, green and blue,
 * draws a triangle, and reads every frame back from the GPU. Exit 42 means
 * every call succeeded and every read-back matched; seeing the three colours
 * remains the hardware check. After the first mismatch the test reads that
 * frame again a second later and looks at more of it, to tell a read-back that
 * ran before the GPU finished from a frame that was never drawn, and it keeps
 * presenting so the screen shows what the GPU drew. */
#include <windows.h>
#include <winternl.h>
#define COBJMACROS
#include <d3d9.h>

#include "pe_test_io.h"

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

static void report_text( const char *label, const char *text )
{
    const char *prefix = "[D3D9 TEST] ";
    WCHAR buffer[320];
    UNICODE_STRING str;
    unsigned int n = 0;

    while (*prefix) buffer[n++] = *prefix++;
    while (*label && n < 200) buffer[n++] = *label++;
    if (text)
    {
        buffer[n++] = ' ';
        while (*text && n < 316) buffer[n++] = (unsigned char)*text++;
    }
    buffer[n++] = '\n';
    str.Buffer = buffer;
    str.Length = n * sizeof(WCHAR);
    str.MaximumLength = str.Length;
    pe_test_display_string( &str );
}

static void report( const char *label, ULONG_PTR value )
{
    static const char hex[] = "0123456789abcdef";
    char text[2 + sizeof(value) * 2 + 1];
    unsigned int i;

    text[0] = '0';
    text[1] = 'x';
    for (i = 0; i < sizeof(value) * 2; i++)
        text[2 + i] = hex[(value >> ((sizeof(value) * 2 - 1 - i) * 4)) & 15];
    text[2 + sizeof(value) * 2] = 0;
    report_text( label, text );
}

static DWORD pixel_at( const D3DLOCKED_RECT *locked, unsigned int x, unsigned int y )
{
    return ((DWORD *)((char *)locked->pBits + y * locked->Pitch))[x] & 0x00ffffff;
}

/* The packager also builds an 800x600 full-screen copy, the mode NFSU2 asks for. */
#ifndef TEST_WIDTH
#define TEST_WIDTH 1280
#define TEST_HEIGHT 720
#define TEST_WINDOWED TRUE
#endif

/* Where the clear colour is checked, and a point inside the white triangle. */
#define CLEAR_X (TEST_WIDTH * 7 / 10)
#define CLEAR_Y (TEST_HEIGHT / 2)
#define TRIANGLE_X 320
#define TRIANGLE_Y 400

static void describe( const char *what, const D3DLOCKED_RECT *locked )
{
    char label[96];
    unsigned int x, y, n, nonzero = 0;

    for (y = 0; y < TEST_HEIGHT; y += 16)
        for (x = 0; x < TEST_WIDTH; x += 16)
            if (pixel_at( locked, x, y )) nonzero++;

    for (n = 0; what[n] && n < 60; n++) label[n] = what[n];
    memcpy( label + n, ": clear pixel", 14 );
    report( label, pixel_at( locked, CLEAR_X, CLEAR_Y ) );
    memcpy( label + n, ": triangle pixel", 17 );
    report( label, pixel_at( locked, TRIANGLE_X, TRIANGLE_Y ) );
    memcpy( label + n, ": nonzero samples (16px grid)", 30 );
    report( label, nonzero );
}

/* The first mismatching frame, looked at again: the same copy a second later
 * (did the GPU finish after the lock returned?), then a fresh copy of the
 * render target (was anything drawn at all?). */
static void diagnose( IDirect3DDevice9 *device, IDirect3DSurface9 *readback )
{
    IDirect3DSurface9 *target = NULL;
    D3DLOCKED_RECT locked;
    HRESULT hr;

    Sleep( 1000 );
    if (SUCCEEDED(hr = IDirect3DSurface9_LockRect( readback, &locked, NULL, D3DLOCK_READONLY )))
    {
        describe( "same copy a second later", &locked );
        IDirect3DSurface9_UnlockRect( readback );
    }
    else report( "LockRect a second later", hr );

    if (FAILED(hr = IDirect3DDevice9_GetRenderTarget( device, 0, &target )))
    {
        report( "GetRenderTarget again", hr );
        return;
    }
    hr = IDirect3DDevice9_GetRenderTargetData( device, target, readback );
    IDirect3DSurface9_Release( target );
    if (FAILED(hr))
    {
        report( "GetRenderTargetData again", hr );
        return;
    }
    Sleep( 1000 );
    if (SUCCEEDED(hr = IDirect3DSurface9_LockRect( readback, &locked, NULL, D3DLOCK_READONLY )))
    {
        describe( "fresh copy", &locked );
        IDirect3DSurface9_UnlockRect( readback );
    }
    else report( "LockRect of a fresh copy", hr );
}

struct vertex { float x, y, z, rhw; DWORD colour; };

/* Materialize DXVK's Vulkan mapping buffer, then seed it before each copy.
 * Unchanged 0xa5 bytes mean no copy reached this CPU mapping; zero bytes mean
 * something wrote zeros. No draw or shader is involved in these checks. */
static DWORD check_clear_only( IDirect3DDevice9 *device, IDirect3DSurface9 *readback )
{
    IDirect3DSurface9 *target = NULL;
    D3DLOCKED_RECT locked;
    DWORD mode, x, y, wrong, unchanged, failures = 0;
    HRESULT hr;

#define CLEAR_CHECK(call) do { if (FAILED(hr = (call))) { \
    report( #call, hr ); failures++; goto done; } } while (0)
    CLEAR_CHECK( IDirect3DDevice9_GetRenderTarget( device, 0, &target ) );
    CLEAR_CHECK( IDirect3DDevice9_Clear( device, 0, NULL, D3DCLEAR_TARGET, 0xff0000ff, 1.0f, 0 ) );
    CLEAR_CHECK( IDirect3DDevice9_GetRenderTargetData( device, target, readback ) );
    for (mode = 0; mode < 2; mode++)
    {
        DWORD colour = mode ? 0xff00ff00 : 0xffff0000;
        DWORD flags = D3DCLEAR_TARGET | (mode ? D3DCLEAR_ZBUFFER : 0);

        CLEAR_CHECK( IDirect3DSurface9_LockRect( readback, &locked, NULL, 0 ) );
        for (y = 0; y < TEST_HEIGHT; y++) memset( (char *)locked.pBits + y * locked.Pitch, 0xa5, TEST_WIDTH * 4 );
        CLEAR_CHECK( IDirect3DSurface9_UnlockRect( readback ) );
        CLEAR_CHECK( IDirect3DDevice9_Clear( device, 0, NULL, flags, colour, 1.0f, 0 ) );
        CLEAR_CHECK( IDirect3DDevice9_GetRenderTargetData( device, target, readback ) );
        CLEAR_CHECK( IDirect3DSurface9_LockRect( readback, &locked, NULL, D3DLOCK_READONLY ) );
        wrong = unchanged = 0;
        for (y = 0; y < TEST_HEIGHT; y++)
            for (x = 0; x < TEST_WIDTH; x++)
            {
                DWORD pixel = pixel_at( &locked, x, y );
                if (pixel != (colour & 0xffffff)) wrong++;
                if (pixel == 0xa5a5a5) unchanged++;
            }
        report_text( mode ? "colour+depth clear without a draw" : "colour clear without a draw",
                     wrong ? "FAIL" : "PASS" );
        if (wrong)
        {
            report( "wrong pixels", wrong );
            report( "pixels retaining CPU sentinel", unchanged );
            describe( "clear-only copy", &locked );
            failures++;
        }
        CLEAR_CHECK( IDirect3DSurface9_UnlockRect( readback ) );
    }
done:
    if (target) IDirect3DSurface9_Release( target );
    return failures;
#undef CLEAR_CHECK
}

void __stdcall start(void)
{
#ifdef _WIN64
    static const WCHAR class_name[] = L"pe64-d3d9";
#else
    static const WCHAR class_name[] = L"pe32-d3d9";
#endif
    /* Away from the centre, so the read-back below samples the clear colour. */
    struct vertex triangle[3] =
    {
        { 100.0f, 500.0f, 0.5f, 1.0f, 0xffffffff },
        { 320.0f, 100.0f, 0.5f, 1.0f, 0xffffffff },
        { 540.0f, 500.0f, 0.5f, 1.0f, 0xffffffff },
    };
    D3DPRESENT_PARAMETERS present = {0};
    D3DADAPTER_IDENTIFIER9 adapter = {0};
    IDirect3DSurface9 *target = NULL, *readback = NULL;
    IDirect3DDevice9 *device = NULL;
    IDirect3D9 *d3d = NULL;
    HINSTANCE instance = GetModuleHandleW( NULL );
    DWORD failure = 0, begin, mismatches = 0, clear_failures = 0, triangle_misses = 0;
    WNDCLASSW cls = {0};
    D3DCAPS9 caps = {0};
    HWND window = NULL;
    int frame = 0;
    HRESULT hr;
    MSG msg;

    report( "BEGIN", 0 );
    report_text( "checkpoint", "clear-only readback v2, triangle readback" );
    cls.style = CS_OWNDC;
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = instance;
    cls.lpszClassName = class_name;
    if (!RegisterClassW( &cls )) { failure = 1; goto done; }
    window = CreateWindowExW( 0, class_name, class_name, WS_POPUP | WS_VISIBLE, 0, 0, TEST_WIDTH, TEST_HEIGHT,
                              NULL, NULL, instance, NULL );
    report( "CreateWindowExW", (ULONG_PTR)window );
    if (!window) { failure = 2; goto done; }

    if (!(d3d = Direct3DCreate9( D3D_SDK_VERSION )))
    {
        report( "Direct3DCreate9 returned NULL, GetLastError", GetLastError() );
        failure = 3;
        goto done;
    }
    if (SUCCEEDED(IDirect3D9_GetAdapterIdentifier( d3d, D3DADAPTER_DEFAULT, 0, &adapter )))
    {
        report_text( "adapter", adapter.Description );
        report_text( "driver", adapter.Driver );
    }
    if (SUCCEEDED(IDirect3D9_GetDeviceCaps( d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps )))
    {
        report( "VertexShaderVersion", caps.VertexShaderVersion );
        report( "PixelShaderVersion", caps.PixelShaderVersion );
        report( "MaxTextureWidth", caps.MaxTextureWidth );
    }

    report( "back buffer width", TEST_WIDTH );
    report( "back buffer height", TEST_HEIGHT );
    report( "windowed", TEST_WINDOWED );
    present.Windowed = TEST_WINDOWED;
    present.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present.BackBufferWidth = TEST_WIDTH;
    present.BackBufferHeight = TEST_HEIGHT;
    present.BackBufferFormat = D3DFMT_X8R8G8B8;
    present.BackBufferCount = 1;
    present.hDeviceWindow = window;
    present.EnableAutoDepthStencil = TRUE;
    present.AutoDepthStencilFormat = D3DFMT_D24S8;
    hr = IDirect3D9_CreateDevice( d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                  D3DCREATE_SOFTWARE_VERTEXPROCESSING, &present, &device );
    report( "CreateDevice", hr );
    if (FAILED(hr)) { failure = 4; goto done; }

    hr = IDirect3DDevice9_CreateOffscreenPlainSurface( device, TEST_WIDTH, TEST_HEIGHT, D3DFMT_X8R8G8B8,
                                                       D3DPOOL_SYSTEMMEM, &readback, NULL );
    report( "CreateOffscreenPlainSurface", hr );
    if (FAILED(hr)) { failure = 5; goto done; }

    clear_failures = check_clear_only( device, readback );

    IDirect3DDevice9_SetRenderState( device, D3DRS_LIGHTING, FALSE );
    IDirect3DDevice9_SetRenderState( device, D3DRS_CULLMODE, D3DCULL_NONE );
    IDirect3DDevice9_SetFVF( device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE );

    begin = GetTickCount();
    for (frame = 0; frame < 180 && !failure; frame++)
    {
        int phase = frame / 60;
        D3DCOLOR colour = phase == 0 ? D3DCOLOR_XRGB(255, 0, 0) :
                          phase == 1 ? D3DCOLOR_XRGB(0, 255, 0) : D3DCOLOR_XRGB(0, 0, 255);
        D3DLOCKED_RECT locked;
        DWORD pixel;

        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
        if (!(frame % 60)) report( "phase", phase );

        hr = IDirect3DDevice9_Clear( device, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, colour, 1.0f, 0 );
        if (FAILED(hr)) { report( "Clear", hr ); failure = 6; break; }
        if (FAILED(hr = IDirect3DDevice9_BeginScene( device ))) { report( "BeginScene", hr ); failure = 7; break; }
        hr = IDirect3DDevice9_DrawPrimitiveUP( device, D3DPT_TRIANGLELIST, 1, triangle, sizeof(*triangle) );
        if (FAILED(hr)) { report( "DrawPrimitiveUP", hr ); failure = 14; break; }
        if (FAILED(hr = IDirect3DDevice9_EndScene( device )))
        { report( "EndScene", hr ); failure = 15; break; }

        /* Read the frame back from the GPU before it is shown, as the OpenGL
         * checkpoint does, so a blank screen cannot pass as success. */
        if (FAILED(hr = IDirect3DDevice9_GetRenderTarget( device, 0, &target )))
        {
            report( "GetRenderTarget", hr );
            failure = 8;
            break;
        }
        hr = IDirect3DDevice9_GetRenderTargetData( device, target, readback );
        IDirect3DSurface9_Release( target );
        target = NULL;
        if (FAILED(hr)) { report( "GetRenderTargetData", hr ); failure = 9; break; }
        if (FAILED(hr = IDirect3DSurface9_LockRect( readback, &locked, NULL, D3DLOCK_READONLY )))
        {
            report( "LockRect", hr );
            failure = 10;
            break;
        }
        pixel = pixel_at( &locked, CLEAR_X, CLEAR_Y );
        if (pixel != (colour & 0x00ffffff) && !mismatches)
        {
            report( "read-back mismatch at frame", frame );
            describe( "first look", &locked );
            report( "expected clear pixel", colour & 0x00ffffff );
        }
        /* The draw needs vertex data and shader constants written by the CPU,
         * which a clear does not. */
        if ((pixel = pixel_at( &locked, TRIANGLE_X, TRIANGLE_Y )) != 0xffffff && !triangle_misses++)
        {
            report( "triangle missing from the read-back at frame", frame );
            report( "triangle pixel", pixel );
        }
        pixel = pixel_at( &locked, CLEAR_X, CLEAR_Y );
        IDirect3DSurface9_UnlockRect( readback );
        if (pixel != (colour & 0x00ffffff) && !mismatches++) diagnose( device, readback );

        if (FAILED(hr = IDirect3DDevice9_Present( device, NULL, NULL, NULL, NULL )))
        {
            report( "Present", hr );
            failure = 12;
            break;
        }
    }
    report( "frames", frame );
    report( "mismatched frames", mismatches );
    report( "frames without the triangle", triangle_misses );
    report( "milliseconds", GetTickCount() - begin );
    if (!failure && mismatches) failure = 11;
    if (!failure && clear_failures) failure = 13;
    if (!failure && triangle_misses) failure = 16;

done:
    if (readback) IDirect3DSurface9_Release( readback );
    if (device) IDirect3DDevice9_Release( device );
    if (d3d) IDirect3D9_Release( d3d );
    if (window) DestroyWindow( window );
    if (failure) report( "FAIL step", failure );
    else report_text( "PASS", NULL );
    pe_test_terminate( failure ? failure : 42 );
}
