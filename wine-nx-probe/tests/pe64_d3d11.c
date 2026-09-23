/* D3D11 FL11_0 shader, texture, depth, readback and presentation checkpoint. */
#define COBJMACROS
#include <windows.h>
#include <winternl.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include "pe_test_io.h"

#ifndef TEST_WIDTH
#define TEST_WIDTH 1280
#define TEST_HEIGHT 720
#endif
#ifndef TEST_FULLSCREEN
#define TEST_FULLSCREEN 0
#endif
#ifndef TEST_REQUIRE_DXVK
#define TEST_REQUIRE_DXVK 0
#endif

void *memset( void *dst, int value, size_t size )
{
    unsigned char *p = dst;
    while (size--) *p++ = value;
    return dst;
}

void *memcpy( void *dst, const void *src, size_t size )
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (size--) *d++ = *s++;
    return dst;
}

static void text( const char *label )
{
    const char *prefix = "[D3D11 TEST] ";
    WCHAR buffer[400];
    UNICODE_STRING string;
    unsigned int n = 0;
    while (*prefix) buffer[n++] = *prefix++;
    while (*label && n < 398) buffer[n++] = (unsigned char)*label++;
    buffer[n++] = '\n';
    string.Buffer = buffer;
    string.Length = n * sizeof(WCHAR);
    string.MaximumLength = string.Length;
    pe_test_display_string( &string );
}

static void number( const char *label, ULONG_PTR value )
{
    static const char digits[] = "0123456789abcdef";
    char buffer[160];
    unsigned int n = 0, i;
    while (*label && n < 120) buffer[n++] = *label++;
    buffer[n++] = ' ';
    buffer[n++] = '0';
    buffer[n++] = 'x';
    for (i = 0; i < sizeof(value) * 2; i++)
        buffer[n++] = digits[(value >> ((sizeof(value) * 2 - i - 1) * 4)) & 15];
    buffer[n] = 0;
    text( buffer );
}

static int module( const char *name )
{
    char path[512];
    DWORD count = GetModuleFileNameA( GetModuleHandleA( name ), path, sizeof(path) );
    if (!count || count >= sizeof(path)) return 0;
    text( path );
    if (!TEST_REQUIRE_DXVK) return 1;
    return count >= 10 && (path[0] == 'C' || path[0] == 'c') && path[1] == ':' && path[2] == '\\' &&
        (path[3] == 'd' || path[3] == 'D') && (path[4] == 'x' || path[4] == 'X') &&
        (path[5] == 'v' || path[5] == 'V') && (path[6] == 'k' || path[6] == 'K') &&
        path[7] == '6' && path[8] == '4' && path[9] == '\\';
}

static LRESULT CALLBACK window_proc( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam )
{
    return DefWindowProcW( hwnd, message, wparam, lparam );
}

#define RELEASE(p, type) do { if (p) type##_Release( p ); } while (0)
#define CHECK(label, call) do { hr = (call); number( label, (UINT)hr ); if (FAILED(hr)) goto done; } while (0)

void start( void )
{
    static const char vs_source[] =
        "float4 main(uint id : SV_VertexID) : SV_Position {"
        "float2 p[3] = { float2(-0.75,-0.75), float2(0,0.75), float2(0.75,-0.75) };"
        "return float4(p[id],0.5,1); }";
    static const char ps_source[] =
        "Texture2D image : register(t0); SamplerState samp : register(s0);"
        "float4 main() : SV_Target { return image.SampleLevel(samp,float2(0.5,0.5),0); }";
    static const float colors[3][4] = {{1,0,0,1}, {0,1,0,1}, {0,0,1,1}};
    static const DWORD pixels[] = {0x0000ff, 0x00ff00, 0xff0000};
    const D3D_FEATURE_LEVEL required = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL actual = 0;
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    IDXGIOutput *output = NULL;
    IDXGISwapChain *swapchain = NULL;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11Texture2D *back = NULL, *staging = NULL, *texture = NULL, *depth = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11DepthStencilView *dsv = NULL;
    ID3D11ShaderResourceView *srv = NULL;
    ID3D11SamplerState *sampler = NULL;
    ID3D11RasterizerState *raster = NULL;
    ID3D11VertexShader *vs = NULL;
    ID3D11PixelShader *ps = NULL;
    ID3DBlob *vs_blob = NULL, *ps_blob = NULL, *errors = NULL;
    DXGI_SWAP_CHAIN_DESC swap_desc = {0};
    DXGI_ADAPTER_DESC1 adapter_desc;
    DXGI_OUTPUT_DESC output_desc;
    D3D11_TEXTURE2D_DESC texture_desc;
    D3D11_SUBRESOURCE_DATA initial = {0};
    D3D11_SAMPLER_DESC sampler_desc = {0};
    D3D11_RASTERIZER_DESC raster_desc = {0};
    D3D11_VIEWPORT viewport = {0};
    D3D11_MAPPED_SUBRESOURCE mapped;
    WNDCLASSW wc = {0};
    HWND hwnd = NULL;
    UINT mode_count = 0, color, frame;
    DWORD white = 0xffffffff, exit_code = 1;
    HRESULT hr = E_FAIL;
    MSG msg;

    text( TEST_FULLSCREEN ? "BEGIN fullscreen" : "BEGIN windowed" );
    if (!module( "dxgi.dll" ) || !module( "d3d11.dll" ))
    {
        text( "FAIL: expected matching DLLs in C:\\dxvk64" );
        goto done;
    }
    CHECK( "CreateDXGIFactory1", CreateDXGIFactory1( &IID_IDXGIFactory1, (void **)&factory ) );
    CHECK( "EnumAdapters1", IDXGIFactory1_EnumAdapters1( factory, 0, &adapter ) );
    CHECK( "GetDesc1", IDXGIAdapter1_GetDesc1( adapter, &adapter_desc ) );
    number( "VendorId", adapter_desc.VendorId );
    number( "DeviceId", adapter_desc.DeviceId );
    CHECK( "EnumOutputs", IDXGIAdapter1_EnumOutputs( adapter, 0, &output ) );
    CHECK( "GetOutputDesc", IDXGIOutput_GetDesc( output, &output_desc ) );
    CHECK( "GetDisplayModeList", IDXGIOutput_GetDisplayModeList( output, DXGI_FORMAT_R8G8B8A8_UNORM,
                                                               0, &mode_count, NULL ) );
    number( "display modes", mode_count );
    if (!mode_count) goto done;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW( NULL );
    wc.lpszClassName = L"WineNXD3D11";
    if (!RegisterClassW( &wc )) goto done;
    hwnd = CreateWindowExW( 0, wc.lpszClassName, L"Wine-NX D3D11", WS_POPUP,
                           0, 0, TEST_WIDTH, TEST_HEIGHT, NULL, NULL, wc.hInstance, NULL );
    if (!hwnd) goto done;
    ShowWindow( hwnd, SW_SHOW );
    swap_desc.BufferDesc.Width = TEST_WIDTH;
    swap_desc.BufferDesc.Height = TEST_HEIGHT;
    swap_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_desc.BufferDesc.RefreshRate.Numerator = 60;
    swap_desc.BufferDesc.RefreshRate.Denominator = 1;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.BufferCount = 2;
    swap_desc.OutputWindow = hwnd;
    swap_desc.Windowed = !TEST_FULLSCREEN;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    CHECK( "D3D11CreateDeviceAndSwapChain", D3D11CreateDeviceAndSwapChain( (IDXGIAdapter *)adapter,
           D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, &required, 1, D3D11_SDK_VERSION,
           &swap_desc, &swapchain, &device, &actual, &context ) );
    number( "feature level", actual );
    if (actual != required) goto done;
    CHECK( "GetBuffer", IDXGISwapChain_GetBuffer( swapchain, 0, &IID_ID3D11Texture2D, (void **)&back ) );
    CHECK( "CreateRenderTargetView", ID3D11Device_CreateRenderTargetView( device, (ID3D11Resource *)back,
                                                                       NULL, &rtv ) );
    ID3D11Texture2D_GetDesc( back, &texture_desc );
    texture_desc.Usage = D3D11_USAGE_STAGING;
    texture_desc.BindFlags = 0;
    texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    texture_desc.MiscFlags = 0;
    CHECK( "CreateReadback", ID3D11Device_CreateTexture2D( device, &texture_desc, NULL, &staging ) );
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    CHECK( "CreateDepthTexture", ID3D11Device_CreateTexture2D( device, &texture_desc, NULL, &depth ) );
    CHECK( "CreateDepthStencilView", ID3D11Device_CreateDepthStencilView( device, (ID3D11Resource *)depth,
                                                                       NULL, &dsv ) );
    texture_desc.Width = texture_desc.Height = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.Usage = D3D11_USAGE_IMMUTABLE;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    initial.pSysMem = &white;
    initial.SysMemPitch = sizeof(white);
    CHECK( "CreateSampledTexture", ID3D11Device_CreateTexture2D( device, &texture_desc, &initial, &texture ) );
    CHECK( "CreateShaderResourceView", ID3D11Device_CreateShaderResourceView( device, (ID3D11Resource *)texture,
                                                                           NULL, &srv ) );
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = sampler_desc.AddressV = sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    CHECK( "CreateSamplerState", ID3D11Device_CreateSamplerState( device, &sampler_desc, &sampler ) );
    raster_desc.FillMode = D3D11_FILL_SOLID;
    raster_desc.CullMode = D3D11_CULL_NONE;
    raster_desc.DepthClipEnable = TRUE;
    CHECK( "CreateRasterizerState", ID3D11Device_CreateRasterizerState( device, &raster_desc, &raster ) );
    CHECK( "CompileVS", D3DCompile( vs_source, sizeof(vs_source) - 1, "test-vs", NULL, NULL, "main", "vs_5_0",
                                   0, 0, &vs_blob, &errors ) );
    RELEASE( errors, ID3D10Blob );
    errors = NULL;
    CHECK( "CompilePS", D3DCompile( ps_source, sizeof(ps_source) - 1, "test-ps", NULL, NULL, "main", "ps_5_0",
                                   0, 0, &ps_blob, &errors ) );
    CHECK( "CreateVertexShader", ID3D11Device_CreateVertexShader( device, ID3D10Blob_GetBufferPointer(vs_blob),
           ID3D10Blob_GetBufferSize(vs_blob), NULL, &vs ) );
    CHECK( "CreatePixelShader", ID3D11Device_CreatePixelShader( device, ID3D10Blob_GetBufferPointer(ps_blob),
           ID3D10Blob_GetBufferSize(ps_blob), NULL, &ps ) );
    viewport.Width = TEST_WIDTH;
    viewport.Height = TEST_HEIGHT;
    viewport.MaxDepth = 1;
    ID3D11DeviceContext_RSSetViewports( context, 1, &viewport );
    ID3D11DeviceContext_RSSetState( context, raster );
    ID3D11DeviceContext_IASetPrimitiveTopology( context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    ID3D11DeviceContext_VSSetShader( context, vs, NULL, 0 );
    ID3D11DeviceContext_PSSetShader( context, ps, NULL, 0 );
    ID3D11DeviceContext_PSSetShaderResources( context, 0, 1, &srv );
    ID3D11DeviceContext_PSSetSamplers( context, 0, 1, &sampler );
    for (color = 0; color < 3; color++)
    {
        number( "color", color );
        for (frame = 0; frame < 45; frame++)
        {
            DWORD clear, triangle;
            while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
            {
                TranslateMessage( &msg );
                DispatchMessageW( &msg );
            }
            ID3D11DeviceContext_OMSetRenderTargets( context, 1, &rtv, dsv );
            ID3D11DeviceContext_ClearRenderTargetView( context, rtv, colors[color] );
            ID3D11DeviceContext_ClearDepthStencilView( context, dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0 );
            ID3D11DeviceContext_Draw( context, 3, 0 );
            ID3D11DeviceContext_CopyResource( context, (ID3D11Resource *)staging, (ID3D11Resource *)back );
            hr = ID3D11DeviceContext_Map( context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped );
            if (FAILED(hr)) { number( "Map FAIL", (UINT)hr ); goto done; }
            clear = ((DWORD *)((char *)mapped.pData + TEST_HEIGHT / 2 * mapped.RowPitch))[TEST_WIDTH * 9 / 10] & 0xffffff;
            triangle = ((DWORD *)((char *)mapped.pData + TEST_HEIGHT / 2 * mapped.RowPitch))[TEST_WIDTH / 2] & 0xffffff;
            ID3D11DeviceContext_Unmap( context, (ID3D11Resource *)staging, 0 );
            if (clear != pixels[color] || triangle != 0xffffff)
            {
                number( "clear actual", clear );
                number( "clear expected", pixels[color] );
                number( "triangle actual", triangle );
                goto done;
            }
            hr = IDXGISwapChain_Present( swapchain, 1, 0 );
            if (FAILED(hr)) { number( "Present FAIL", (UINT)hr ); goto done; }
            Sleep( 16 );
        }
    }
    text( "readback and presentation PASS" );
    exit_code = 42;
done:
    if (errors) text( ID3D10Blob_GetBufferPointer( errors ) );
    if (swapchain && TEST_FULLSCREEN) IDXGISwapChain_SetFullscreenState( swapchain, FALSE, NULL );
    if (context) { ID3D11DeviceContext_ClearState( context ); ID3D11DeviceContext_Flush( context ); }
    RELEASE( errors, ID3D10Blob );
    RELEASE( vs_blob, ID3D10Blob );
    RELEASE( ps_blob, ID3D10Blob );
    RELEASE( vs, ID3D11VertexShader );
    RELEASE( ps, ID3D11PixelShader );
    RELEASE( sampler, ID3D11SamplerState );
    RELEASE( raster, ID3D11RasterizerState );
    RELEASE( srv, ID3D11ShaderResourceView );
    RELEASE( rtv, ID3D11RenderTargetView );
    RELEASE( dsv, ID3D11DepthStencilView );
    RELEASE( staging, ID3D11Texture2D );
    RELEASE( depth, ID3D11Texture2D );
    RELEASE( texture, ID3D11Texture2D );
    RELEASE( back, ID3D11Texture2D );
    RELEASE( swapchain, IDXGISwapChain );
    RELEASE( context, ID3D11DeviceContext );
    RELEASE( device, ID3D11Device );
    RELEASE( output, IDXGIOutput );
    RELEASE( adapter, IDXGIAdapter1 );
    RELEASE( factory, IDXGIFactory1 );
    if (hwnd) DestroyWindow( hwnd );
    text( exit_code == 42 ? "PASS" : "FAIL" );
    pe_test_terminate( exit_code );
}
