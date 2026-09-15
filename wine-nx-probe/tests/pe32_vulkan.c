/* Wine-NX Vulkan checkpoint. Wine's winevulkan hands Vulkan to mesa-switch's
 * NVK on the Switch, so this says whether that chain stands up before DXVK is
 * asked to. It maps host-visible memory from this 32-bit process, then clears a
 * Win32 surface's swapchain through red, green and blue, copying one pixel of
 * every frame back through that mapping before it is presented. Exit 42 means
 * every call succeeded and every read-back matched; seeing the three colours
 * remains the hardware check. */
#include <windows.h>
#include <winternl.h>
#include "wine/vulkan.h"

#ifndef VKAPI_ATTR  /* wine/vulkan.h defines only VKAPI_CALL */
#define VKAPI_ATTR
#endif

__declspec(dllimport) NTSTATUS NTAPI NtDisplayString( const UNICODE_STRING *str );
__declspec(dllimport) NTSTATUS NTAPI NtTerminateProcess( HANDLE process, NTSTATUS status );

/* wine/vulkan.h declares no functions (VK_NO_PROTOTYPES). */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance( const VkInstanceCreateInfo *, const VkAllocationCallbacks *, VkInstance * );
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance( VkInstance, const VkAllocationCallbacks * );
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices( VkInstance, uint32_t *, VkPhysicalDevice * );
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties( VkPhysicalDevice, VkPhysicalDeviceProperties * );
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties( VkPhysicalDevice, uint32_t *, VkQueueFamilyProperties * );
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties( VkPhysicalDevice, VkPhysicalDeviceMemoryProperties * );
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties( VkPhysicalDevice, const char *, uint32_t *, VkExtensionProperties * );
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice( VkPhysicalDevice, const VkDeviceCreateInfo *, const VkAllocationCallbacks *, VkDevice * );
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice( VkDevice, const VkAllocationCallbacks * );
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue( VkDevice, uint32_t, uint32_t, VkQueue * );
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle( VkDevice );
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer( VkDevice, const VkBufferCreateInfo *, const VkAllocationCallbacks *, VkBuffer * );
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer( VkDevice, VkBuffer, const VkAllocationCallbacks * );
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements( VkDevice, VkBuffer, VkMemoryRequirements * );
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory( VkDevice, const VkMemoryAllocateInfo *, const VkAllocationCallbacks *, VkDeviceMemory * );
VKAPI_ATTR void VKAPI_CALL vkFreeMemory( VkDevice, VkDeviceMemory, const VkAllocationCallbacks * );
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory( VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize );
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory( VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void ** );
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory( VkDevice, VkDeviceMemory );
VKAPI_ATTR VkResult VKAPI_CALL vkCreateWin32SurfaceKHR( VkInstance, const VkWin32SurfaceCreateInfoKHR *, const VkAllocationCallbacks *, VkSurfaceKHR * );
VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR( VkInstance, VkSurfaceKHR, const VkAllocationCallbacks * );
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR( VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32 * );
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR( VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR * );
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR( VkPhysicalDevice, VkSurfaceKHR, uint32_t *, VkSurfaceFormatKHR * );
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR( VkDevice, const VkSwapchainCreateInfoKHR *, const VkAllocationCallbacks *, VkSwapchainKHR * );
VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR( VkDevice, VkSwapchainKHR, const VkAllocationCallbacks * );
VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR( VkDevice, VkSwapchainKHR, uint32_t *, VkImage * );
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR( VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t * );
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR( VkQueue, const VkPresentInfoKHR * );
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool( VkDevice, const VkCommandPoolCreateInfo *, const VkAllocationCallbacks *, VkCommandPool * );
VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool( VkDevice, VkCommandPool, const VkAllocationCallbacks * );
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers( VkDevice, const VkCommandBufferAllocateInfo *, VkCommandBuffer * );
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer( VkCommandBuffer, const VkCommandBufferBeginInfo * );
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer( VkCommandBuffer );
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer( VkCommandBuffer, VkCommandBufferResetFlags );
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier( VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags,
                                                 uint32_t, const VkMemoryBarrier *, uint32_t, const VkBufferMemoryBarrier *,
                                                 uint32_t, const VkImageMemoryBarrier * );
VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage( VkCommandBuffer, VkImage, VkImageLayout, const VkClearColorValue *,
                                                 uint32_t, const VkImageSubresourceRange * );
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer( VkCommandBuffer, VkImage, VkImageLayout, VkBuffer, uint32_t, const VkBufferImageCopy * );
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer( VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy * );
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit( VkQueue, uint32_t, const VkSubmitInfo *, VkFence );
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence( VkDevice, const VkFenceCreateInfo *, const VkAllocationCallbacks *, VkFence * );
VKAPI_ATTR void VKAPI_CALL vkDestroyFence( VkDevice, VkFence, const VkAllocationCallbacks * );
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences( VkDevice, uint32_t, const VkFence *, VkBool32, uint64_t );
VKAPI_ATTR VkResult VKAPI_CALL vkResetFences( VkDevice, uint32_t, const VkFence * );

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
    const char *prefix = "[VULKAN TEST] ";
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

/* Words of memory as hexadecimal, for comparing the loader's objects with the
 * [NXVK] lines Wine's unix side writes about the same address. */
static void report_words( const char *label, const DWORD *words, unsigned int count )
{
    static const char hex[] = "0123456789abcdef";
    char text[120];
    unsigned int i, j, n = 0;

    for (i = 0; i < count && n + 9 < sizeof(text); i++)
    {
        for (j = 0; j < 8; j++) text[n++] = hex[(words[i] >> (28 - j * 4)) & 15];
        text[n++] = ' ';
    }
    text[n ? n - 1 : 0] = 0;
    report_text( label, text );
}

static int same_name( const char *a, const char *b )
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

static int memory_type( const VkPhysicalDeviceMemoryProperties *props, uint32_t allowed, VkMemoryPropertyFlags flags )
{
    uint32_t i;

    for (i = 0; i < props->memoryTypeCount; i++)
        if ((allowed & (1u << i)) && (props->memoryTypes[i].propertyFlags & flags) == flags) return i;
    return -1;
}

static void transition( VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to )
{
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

    barrier.srcAccessMask = from == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ? VK_ACCESS_TRANSFER_WRITE_BIT :
                            from == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? VK_ACCESS_TRANSFER_READ_BIT : 0;
    barrier.dstAccessMask = to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ? VK_ACCESS_TRANSFER_WRITE_BIT :
                            to == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ? VK_ACCESS_TRANSFER_READ_BIT : 0;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier );
}

#define BUFFER_SIZE 0x100000

void __stdcall start(void)
{
    static const WCHAR class_name[] = L"pe32-vulkan";
    static const char *instance_extensions[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };
    static const char *device_extensions[] = { "VK_KHR_swapchain" };
    static const float priority = 1.0f;
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    VkInstanceCreateInfo instance_info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    VkDeviceQueueCreateInfo queue_info = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    VkDeviceCreateInfo device_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkWin32SurfaceCreateInfoKHR surface_info = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    VkSwapchainCreateInfoKHR swapchain_info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkCommandBufferAllocateInfo cmd_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkQueueFamilyProperties families[8];
    VkPhysicalDeviceMemoryProperties memory_props;
    VkPhysicalDeviceProperties props;
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR formats[16];
    VkMemoryRequirements requirements;
    VkImage images[8];
    VkInstance instance = NULL;
    VkPhysicalDevice gpu = NULL;
    VkDevice device = NULL;
    VkQueue queue = NULL;
    VkCommandBuffer cmd = NULL;
    VkBuffer buffer = 0;
    VkDeviceMemory memory = 0;
    VkSurfaceKHR surface = 0;
    VkSwapchainKHR swapchain = 0;
    VkCommandPool pool = 0;
    VkFence fence = 0;
    VkFormat format;
    VkExtent2D extent;
    VkBool32 supported = VK_FALSE;
    HINSTANCE module = GetModuleHandleW( NULL );
    uint32_t count, family, image_count, i;
    unsigned char *mapped = NULL;
    int type, readback = 0, rgb_order = 0, frame = 0;
    DWORD failure = 0, begin = 0;
    WNDCLASSW cls = {0};
    HWND window = NULL;
    VkResult res;
    MSG msg;

#define CHECK( label, step ) do { report( label, (DWORD)res ); if (res != VK_SUCCESS) { failure = step; goto done; } } while (0)

    report( "BEGIN", 0 );
    cls.style = CS_OWNDC;
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = module;
    cls.lpszClassName = class_name;
    if (!RegisterClassW( &cls )) { failure = 1; goto done; }
    window = CreateWindowExW( 0, class_name, class_name, WS_POPUP | WS_VISIBLE, 0, 0, 1280, 720,
                              NULL, NULL, module, NULL );
    report( "CreateWindowExW", (ULONG_PTR)window );
    if (!window) { failure = 2; goto done; }

    app.pApplicationName = "pe32-vulkan";
    app.apiVersion = VK_API_VERSION_1_1;
    instance_info.pApplicationInfo = &app;
    instance_info.enabledExtensionCount = 2;
    instance_info.ppEnabledExtensionNames = instance_extensions;
    res = vkCreateInstance( &instance_info, NULL, &instance );
    CHECK( "vkCreateInstance", 3 );

    count = 1;
    res = vkEnumeratePhysicalDevices( instance, &count, &gpu );
    if (res == VK_INCOMPLETE) res = VK_SUCCESS;
    CHECK( "vkEnumeratePhysicalDevices", 4 );
    if (!count || !gpu) { failure = 4; goto done; }
    vkGetPhysicalDeviceProperties( gpu, &props );
    report_text( "GPU", props.deviceName );
    report( "apiVersion", props.apiVersion );

    /* What the loader holds for this device, and the extensions it lists. */
    {
        static VkExtensionProperties extensions[512];
        uint32_t found = 0;

        report( "physical device at", (ULONG_PTR)gpu );
        report_words( "physical device words", (const DWORD *)gpu, 12 );
        count = 512;
        res = vkEnumerateDeviceExtensionProperties( gpu, NULL, &count, extensions );
        report( "vkEnumerateDeviceExtensionProperties", res );
        report( "device extensions listed", count );
        for (i = 0; i < count && i < 512; i++)
            if (same_name( extensions[i].extensionName, "VK_KHR_swapchain" )) found = 1;
        report( "VK_KHR_swapchain listed", found );
    }

    count = 8;
    vkGetPhysicalDeviceQueueFamilyProperties( gpu, &count, families );
    for (family = 0; family < count; family++)
        if (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) break;
    if (family == count) { report( "no graphics queue among families", count ); failure = 5; goto done; }

    queue_info.queueFamilyIndex = family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = device_extensions;
    res = vkCreateDevice( gpu, &device_info, NULL, &device );
    CHECK( "vkCreateDevice", 6 );
    vkGetDeviceQueue( device, family, 0, &queue );
    vkGetPhysicalDeviceMemoryProperties( gpu, &memory_props );

    /* Host-cached memory: Wine imports its own
     * 32-bit pages into it (VK_EXT_external_memory_host), and vkMapMemory must
     * hand those back in any address space. */
    {
        VkBuffer cached_buffer = 0;
        VkDeviceMemory cached_memory = 0;
        unsigned char *cached = NULL;

        buffer_info.size = 0x10000;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        res = vkCreateBuffer( device, &buffer_info, NULL, &cached_buffer );
        CHECK( "vkCreateBuffer host-cached", 30 );
        vkGetBufferMemoryRequirements( device, cached_buffer, &requirements );
        type = memory_type( &memory_props, requirements.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT );
        report( "host-cached memory type", type );
        if (type >= 0)
        {
            alloc_info.allocationSize = requirements.size;
            alloc_info.memoryTypeIndex = type;
            res = vkAllocateMemory( device, &alloc_info, NULL, &cached_memory );
            CHECK( "vkAllocateMemory host-cached", 31 );
            res = vkBindBufferMemory( device, cached_buffer, cached_memory, 0 );
            CHECK( "vkBindBufferMemory host-cached", 32 );
            res = vkMapMemory( device, cached_memory, 0, 0x10000, 0, (void **)&cached );
            CHECK( "vkMapMemory host-cached", 33 );
            report( "host-cached mapped at", (ULONG_PTR)cached );
            for (i = 0; i < 0x10000; i++) cached[i] = (unsigned char)(i * 5 + 1);
            vkUnmapMemory( device, cached_memory );
            cached = NULL;
            res = vkMapMemory( device, cached_memory, 0, 0x10000, 0, (void **)&cached );
            CHECK( "vkMapMemory host-cached again", 34 );
            for (i = 0; i < 0x10000; i++)
            {
                if (cached[i] == (unsigned char)(i * 5 + 1)) continue;
                report( "host-cached memory mismatch at offset", i );
                failure = 35;
                goto done;
            }
            report( "host-cached memory kept its contents at", (ULONG_PTR)cached );
            vkUnmapMemory( device, cached_memory );
        }
        vkDestroyBuffer( device, cached_buffer, NULL );
        if (cached_memory) vkFreeMemory( device, cached_memory, NULL );
    }

    /* Host-visible memory mapped into this 32-bit process: Wine imports its own
     * low pages into Vulkan for that (VK_EXT_external_memory_host). */
    buffer_info.size = BUFFER_SIZE;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    res = vkCreateBuffer( device, &buffer_info, NULL, &buffer );
    CHECK( "vkCreateBuffer", 7 );
    vkGetBufferMemoryRequirements( device, buffer, &requirements );
    vkGetPhysicalDeviceMemoryProperties( gpu, &memory_props );
    if ((type = memory_type( &memory_props, requirements.memoryTypeBits,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT )) < 0)
    {
        report( "no host-visible memory type among", requirements.memoryTypeBits );
        failure = 8;
        goto done;
    }
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = type;
    res = vkAllocateMemory( device, &alloc_info, NULL, &memory );
    CHECK( "vkAllocateMemory", 9 );
    res = vkBindBufferMemory( device, buffer, memory, 0 );
    CHECK( "vkBindBufferMemory", 10 );
    res = vkMapMemory( device, memory, 0, BUFFER_SIZE, 0, (void **)&mapped );
    CHECK( "vkMapMemory", 11 );
    report( "mapped at", (ULONG_PTR)mapped );
    for (i = 0; i < BUFFER_SIZE; i++) mapped[i] = (unsigned char)(i * 7 + 3);
    vkUnmapMemory( device, memory );
    mapped = NULL;
    res = vkMapMemory( device, memory, 0, BUFFER_SIZE, 0, (void **)&mapped );
    CHECK( "vkMapMemory again", 12 );
    for (i = 0; i < BUFFER_SIZE; i++)
    {
        if (mapped[i] == (unsigned char)(i * 7 + 3)) continue;
        report( "mapped memory mismatch at offset", i );
        failure = 13;
        goto done;
    }
    report( "mapped memory kept its contents, bytes", BUFFER_SIZE );

    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = family;
    res = vkCreateCommandPool( device, &pool_info, NULL, &pool );
    CHECK( "vkCreateCommandPool", 20 );
    cmd_info.commandPool = pool;
    cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_info.commandBufferCount = 1;
    res = vkAllocateCommandBuffers( device, &cmd_info, &cmd );
    CHECK( "vkAllocateCommandBuffers", 21 );
    res = vkCreateFence( device, &fence_info, NULL, &fence );
    CHECK( "vkCreateFence", 22 );

    /* Exercise both directions while the coherent allocation stays mapped.
     * CPU-only map/remap cannot detect a falsely advertised coherent type.
     * Copy between disjoint ranges, changing the data on each submission;
     * deliberately use no vkFlush/InvalidateMappedMemoryRanges calls.
     */
    for (frame = 0; frame < 2; frame++)
    {
        VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        VkBufferCopy copy = { 0, BUFFER_SIZE / 2, 4096 };

        for (i = 0; i < 4096; i++)
        {
            mapped[i] = (unsigned char)(i * 7 + 3 + frame * 31);
            mapped[BUFFER_SIZE / 2 + i] = (unsigned char)~mapped[i];
        }
        res = vkResetCommandBuffer( cmd, 0 );
        CHECK( "reset coherent copy command", 36 );
        res = vkResetFences( device, 1, &fence );
        CHECK( "reset coherent copy fence", 37 );
        res = vkBeginCommandBuffer( cmd, &begin_info );
        CHECK( "begin coherent copy", 38 );
        barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0, 1, &barrier, 0, NULL, 0, NULL );
        vkCmdCopyBuffer( cmd, buffer, buffer, 1, &copy );
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                              0, 1, &barrier, 0, NULL, 0, NULL );
        res = vkEndCommandBuffer( cmd );
        CHECK( "end coherent copy", 39 );
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        res = vkQueueSubmit( queue, 1, &submit, fence );
        CHECK( "submit coherent copy", 40 );
        res = vkWaitForFences( device, 1, &fence, VK_TRUE, 10000000000ull );
        CHECK( "wait coherent copy", 41 );
        for (i = 0; i < 4096; i++)
        {
            if (mapped[BUFFER_SIZE / 2 + i] == (unsigned char)(i * 7 + 3 + frame * 31)) continue;
            report( "coherent GPU copy mismatch at", i );
            failure = 43;
            goto done;
        }
        report( "coherent CPU-GPU-CPU copy passed", frame + 1 );
    }

    surface_info.hinstance = module;
    surface_info.hwnd = window;
    res = vkCreateWin32SurfaceKHR( instance, &surface_info, NULL, &surface );
    CHECK( "vkCreateWin32SurfaceKHR", 14 );
    res = vkGetPhysicalDeviceSurfaceSupportKHR( gpu, family, surface, &supported );
    CHECK( "vkGetPhysicalDeviceSurfaceSupportKHR", 15 );
    report( "surface supported", supported );
    res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR( gpu, surface, &caps );
    CHECK( "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", 16 );
    count = 16;
    res = vkGetPhysicalDeviceSurfaceFormatsKHR( gpu, surface, &count, formats );
    if (res == VK_INCOMPLETE) res = VK_SUCCESS;
    CHECK( "vkGetPhysicalDeviceSurfaceFormatsKHR", 17 );
    if (!count) { failure = 17; goto done; }
    format = formats[0].format;
    for (i = 0; i < count; i++)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_R8G8B8A8_UNORM)
            format = formats[i].format;
    report( "format", format );

    extent = caps.currentExtent;
    if (extent.width == 0xffffffff) { extent.width = 1280; extent.height = 720; }
    report( "width", extent.width );
    report( "height", extent.height );
    image_count = caps.minImageCount + 1;
    if (caps.maxImageCount && image_count > caps.maxImageCount) image_count = caps.maxImageCount;

    swapchain_info.surface = surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = format;
    swapchain_info.imageColorSpace = formats[0].colorSpace;
    swapchain_info.imageExtent = extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = caps.currentTransform;
    swapchain_info.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                                    ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                    : (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_info.clipped = VK_TRUE;
    res = vkCreateSwapchainKHR( device, &swapchain_info, NULL, &swapchain );
    CHECK( "vkCreateSwapchainKHR", 18 );
    count = 8;
    res = vkGetSwapchainImagesKHR( device, swapchain, &count, images );
    if (res == VK_INCOMPLETE) res = VK_SUCCESS;
    CHECK( "vkGetSwapchainImagesKHR", 19 );
    report( "swapchain images", count );

    /* One pixel of every frame goes back through the mapping above, where the
     * format's byte order is known. */
    if (format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB) readback = 1;
    else if (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB) readback = rgb_order = 1;
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) readback = 0;
    report( "read-back", readback );

    begin = GetTickCount();
    for (frame = 0; frame < 180 && !failure; frame++)
    {
        static const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkPresentInfoKHR present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        VkBufferImageCopy copy = {0};
        VkClearColorValue colour = {{ 0.0f, 0.0f, 0.0f, 1.0f }};
        int phase = frame / 60;
        uint32_t index;

        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
        if (!(frame % 60)) report( "phase", phase );
        colour.float32[phase] = 1.0f;

        res = vkResetFences( device, 1, &fence );
        if (res) { report( "reset acquire fence", res ); failure = 44; break; }
        res = vkAcquireNextImageKHR( device, swapchain, 10000000000ull, 0, fence, &index );
        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) { report( "vkAcquireNextImageKHR", res ); failure = 23; break; }
        res = vkWaitForFences( device, 1, &fence, VK_TRUE, 10000000000ull );
        if (res) { report( "wait acquire fence", res ); failure = 45; break; }

        vkResetCommandBuffer( cmd, 0 );
        if ((res = vkBeginCommandBuffer( cmd, &begin_info ))) { report( "vkBeginCommandBuffer", res ); failure = 24; break; }
        transition( cmd, images[index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
        vkCmdClearColorImage( cmd, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &colour, 1, &range );
        if (readback)
        {
            VkMemoryBarrier host_barrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
            transition( cmd, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageOffset.x = extent.width * 900 / 1280;
            copy.imageOffset.y = extent.height / 2;
            copy.imageExtent.width = 1;
            copy.imageExtent.height = 1;
            copy.imageExtent.depth = 1;
            vkCmdCopyImageToBuffer( cmd, images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy );
            vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                                  0, 1, &host_barrier, 0, NULL, 0, NULL );
            transition( cmd, images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
        }
        else transition( cmd, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
        if ((res = vkEndCommandBuffer( cmd ))) { report( "vkEndCommandBuffer", res ); failure = 25; break; }

        vkResetFences( device, 1, &fence );
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        if ((res = vkQueueSubmit( queue, 1, &submit, fence ))) { report( "vkQueueSubmit", res ); failure = 26; break; }
        if ((res = vkWaitForFences( device, 1, &fence, VK_TRUE, UINT64_MAX ))) { report( "vkWaitForFences", res ); failure = 27; break; }

        if (readback)
        {
            unsigned char red = mapped[rgb_order ? 0 : 2], green = mapped[1], blue = mapped[rgb_order ? 2 : 0];
            unsigned char want[3] = { phase == 0 ? 255 : 0, phase == 1 ? 255 : 0, phase == 2 ? 255 : 0 };

            if (red != want[0] || green != want[1] || blue != want[2])
            {
                report( "read-back mismatch at frame", frame );
                report( "pixel", (red << 16) | (green << 8) | blue );
                failure = 28;
                break;
            }
        }

        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &index;
        res = vkQueuePresentKHR( queue, &present );
        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) { report( "vkQueuePresentKHR", res ); failure = 29; break; }
    }
    report( "frames", frame );
    report( "milliseconds", GetTickCount() - begin );

done:
    if (device) vkDeviceWaitIdle( device );
    if (fence) vkDestroyFence( device, fence, NULL );
    if (pool) vkDestroyCommandPool( device, pool, NULL );
    if (swapchain) vkDestroySwapchainKHR( device, swapchain, NULL );
    if (surface) vkDestroySurfaceKHR( instance, surface, NULL );
    if (mapped) vkUnmapMemory( device, memory );
    if (buffer) vkDestroyBuffer( device, buffer, NULL );
    if (memory) vkFreeMemory( device, memory, NULL );
    if (device) vkDestroyDevice( device, NULL );
    if (instance) vkDestroyInstance( instance, NULL );
    if (window) DestroyWindow( window );
    if (failure) report( "FAIL step", failure );
    else report_text( "PASS", NULL );
    NtTerminateProcess( GetCurrentProcess(), failure ? failure : 42 );
}
