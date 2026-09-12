/* Wine-NX OpenGL checkpoint. A full-screen window gets a wgl context, which on
 * the Switch renders through Mesa's nouveau driver. Each frame of a second of
 * red, green and blue is read back from the GPU before it is shown. Exit 42
 * means every call succeeded and every read-back matched; seeing the three
 * colours on the screen remains the hardware check. */
#include <windows.h>
#include <winternl.h>
#include <GL/gl.h>

__declspec(dllimport) NTSTATUS NTAPI NtDisplayString( const UNICODE_STRING *str );
__declspec(dllimport) NTSTATUS NTAPI NtTerminateProcess( HANDLE process, NTSTATUS status );

static void report_text( const char *label, const char *text )
{
    const char *prefix = "[OPENGL TEST] ";
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

void __stdcall start(void)
{
    static const WCHAR class_name[] = L"pe32-opengl";
    PIXELFORMATDESCRIPTOR pfd = {
        .nSize = sizeof(pfd), .nVersion = 1,
        .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        .iPixelType = PFD_TYPE_RGBA, .cColorBits = 32, .cDepthBits = 24, .cStencilBits = 8,
        .iLayerType = PFD_MAIN_PLANE,
    };
    WNDCLASSW cls = {0};
    HINSTANCE instance = GetModuleHandleW( NULL );
    DWORD failure = 0, begin;
    HGLRC context = NULL;
    HWND window = NULL;
    HDC dc = NULL;
    MSG msg;
    int format, frame = 0;

    report( "BEGIN", 0 );
    cls.style = CS_OWNDC;
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = instance;
    cls.lpszClassName = class_name;
    if (!RegisterClassW( &cls )) { failure = 1; goto done; }
    window = CreateWindowExW( 0, class_name, class_name, WS_POPUP | WS_VISIBLE, 0, 0, 1280, 720,
                              NULL, NULL, instance, NULL );
    report( "CreateWindowExW", (ULONG_PTR)window );
    if (!window || !(dc = GetDC( window ))) { failure = 2; goto done; }
    format = ChoosePixelFormat( dc, &pfd );
    report( "ChoosePixelFormat", format );
    if (!format || !SetPixelFormat( dc, format, &pfd ))
    {
        report( "SetPixelFormat GetLastError", GetLastError() );
        failure = 3;
        goto done;
    }
    context = wglCreateContext( dc );
    report( "wglCreateContext", (ULONG_PTR)context );
    if (!context)
    {
        report( "wglCreateContext GetLastError", GetLastError() );
        failure = 4;
        goto done;
    }
    if (!wglMakeCurrent( dc, context ))
    {
        report( "wglMakeCurrent GetLastError", GetLastError() );
        failure = 8;
        goto done;
    }
    report_text( "GL_VENDOR", (const char *)glGetString( GL_VENDOR ) );
    report_text( "GL_RENDERER", (const char *)glGetString( GL_RENDERER ) );
    report_text( "GL_VERSION", (const char *)glGetString( GL_VERSION ) );

    begin = GetTickCount();
    for (frame = 0; frame < 180 && !failure; frame++)
    {
        unsigned char pixel[4] = {0};
        int phase = frame / 60;

        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
        if (!(frame % 60)) report( "phase", phase );
        glClearColor( phase == 0, phase == 1, phase == 2, 1.0f );
        glClear( GL_COLOR_BUFFER_BIT );
        glReadPixels( 640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel );
        if (pixel[0] != (phase == 0 ? 255 : 0) || pixel[1] != (phase == 1 ? 255 : 0) ||
            pixel[2] != (phase == 2 ? 255 : 0))
        {
            report( "read-back mismatch at frame", frame );
            report( "pixel RGBA", (DWORD)pixel[0] << 24 | pixel[1] << 16 | pixel[2] << 8 | pixel[3] );
            failure = 5;
        }
        if (glGetError() != GL_NO_ERROR) failure = 6;
        if (!SwapBuffers( dc )) failure = 7;
    }
    report( "frames", frame );
    report( "milliseconds", GetTickCount() - begin );

done:
    if (context)
    {
        wglMakeCurrent( NULL, NULL );
        wglDeleteContext( context );
    }
    if (dc) ReleaseDC( window, dc );
    if (window) DestroyWindow( window );
    report( failure ? "FAIL" : "PASS API; verify red, green then blue", failure );
    NtTerminateProcess( (HANDLE)-1, failure ? 0x300 | failure : 42 );
    for (;;) {}
}
