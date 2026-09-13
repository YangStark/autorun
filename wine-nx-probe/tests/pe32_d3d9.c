/* Wine-NX Direct3D 9 checkpoint. Wine's d3d9 and wined3d translate this to
 * OpenGL, which on the Switch is Mesa's nouveau driver, so this says whether
 * that whole chain stands up before a game is asked to. A full-screen device
 * clears through red, green and blue, draws a triangle, and reads the result
 * back from the GPU. Exit 42 means every call succeeded and every read-back
 * matched; seeing the three colours remains the hardware check. */
#include <windows.h>
#include <winternl.h>
#define COBJMACROS
#include <d3d9.h>

__declspec(dllimport) NTSTATUS NTAPI NtDisplayString( const UNICODE_STRING *str );
__declspec(dllimport) NTSTATUS NTAPI NtTerminateProcess( HANDLE process, NTSTATUS status );

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
    NtDisplayString( &str );
}

static void report( const char *label, DWORD value )
{
    static const char hex[] = "0123456789abcdef";
    char text[11];
    unsigned int i;

    text[0] = '0';
    text[1] = 'x';
    for (i = 0; i < 8; i++) text[2 + i] = hex[(value >> (28 - i * 4)) & 15];
    text[10] = 0;
    report_text( label, text );
}

struct vertex { float x, y, z, rhw; DWORD colour; };

void __stdcall start(void)
{
    static const WCHAR class_name[] = L"pe32-d3d9";
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
    DWORD failure = 0, begin;
    WNDCLASSW cls = {0};
    D3DCAPS9 caps = {0};
    HWND window = NULL;
    int frame = 0;
    HRESULT hr;
    MSG msg;

    report( "BEGIN", 0 );
    cls.style = CS_OWNDC;
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = instance;
    cls.lpszClassName = class_name;
    if (!RegisterClassW( &cls )) { failure = 1; goto done; }
    window = CreateWindowExW( 0, class_name, class_name, WS_POPUP | WS_VISIBLE, 0, 0, 1280, 720,
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

    present.Windowed = TRUE;
    present.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present.BackBufferWidth = 1280;
    present.BackBufferHeight = 720;
    present.BackBufferFormat = D3DFMT_X8R8G8B8;
    present.BackBufferCount = 1;
    present.hDeviceWindow = window;
    present.EnableAutoDepthStencil = TRUE;
    present.AutoDepthStencilFormat = D3DFMT_D24S8;
    hr = IDirect3D9_CreateDevice( d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                  D3DCREATE_SOFTWARE_VERTEXPROCESSING, &present, &device );
    report( "CreateDevice", hr );
    if (FAILED(hr)) { failure = 4; goto done; }

    hr = IDirect3DDevice9_CreateOffscreenPlainSurface( device, 1280, 720, D3DFMT_X8R8G8B8,
                                                       D3DPOOL_SYSTEMMEM, &readback, NULL );
    report( "CreateOffscreenPlainSurface", hr );
    if (FAILED(hr)) { failure = 5; goto done; }

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
        IDirect3DDevice9_DrawPrimitiveUP( device, D3DPT_TRIANGLELIST, 1, triangle, sizeof(*triangle) );
        IDirect3DDevice9_EndScene( device );

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
        pixel = ((DWORD *)((char *)locked.pBits + 360 * locked.Pitch))[900] & 0x00ffffff;
        IDirect3DSurface9_UnlockRect( readback );
        if (pixel != (colour & 0x00ffffff))
        {
            report( "read-back mismatch at frame", frame );
            report( "pixel", pixel );
            failure = 11;
            break;
        }
        if (FAILED(hr = IDirect3DDevice9_Present( device, NULL, NULL, NULL, NULL )))
        {
            report( "Present", hr );
            failure = 12;
            break;
        }
    }
    report( "frames", frame );
    report( "milliseconds", GetTickCount() - begin );

done:
    if (readback) IDirect3DSurface9_Release( readback );
    if (device) IDirect3DDevice9_Release( device );
    if (d3d) IDirect3D9_Release( d3d );
    if (window) DestroyWindow( window );
    if (failure) report( "FAIL step", failure );
    else report_text( "PASS", NULL );
    NtTerminateProcess( GetCurrentProcess(), failure ? failure : 42 );
}
