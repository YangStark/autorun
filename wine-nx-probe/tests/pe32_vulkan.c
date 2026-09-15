/* Wine-NX Vulkan checkpoint. Wine's winevulkan hands Vulkan to mesa-switch's
 * NVK on the Switch, so this says whether that chain stands up before DXVK is
 * asked to. It maps host-visible memory from this 32-bit process, repeats what
 * DXVK does with memory and completion (buffers and images bound at offsets
 * inside larger allocations, timeline semaphore waits, render pass clears read
 * back), then clears a Win32 surface's swapchain through red, green and blue,
 * copying one pixel of every frame back through that mapping before it is
 * presented. Exit 42 means every call succeeded and every read-back matched;
 * seeing the three colours remains the hardware check. */
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
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer2( VkCommandBuffer, const VkCopyImageToBufferInfo2 * );
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2( VkCommandBuffer, const VkDependencyInfo * );
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers( VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer * );
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer( VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy * );
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit( VkQueue, uint32_t, const VkSubmitInfo *, VkFence );
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence( VkDevice, const VkFenceCreateInfo *, const VkAllocationCallbacks *, VkFence * );
VKAPI_ATTR void VKAPI_CALL vkDestroyFence( VkDevice, VkFence, const VkAllocationCallbacks * );
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences( VkDevice, uint32_t, const VkFence *, VkBool32, uint64_t );
VKAPI_ATTR VkResult VKAPI_CALL vkResetFences( VkDevice, uint32_t, const VkFence * );
VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle( VkQueue );
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2( VkQueue, uint32_t, const VkSubmitInfo2 *, VkFence );
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore( VkDevice, const VkSemaphoreCreateInfo *, const VkAllocationCallbacks *, VkSemaphore * );
VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore( VkDevice, VkSemaphore, const VkAllocationCallbacks * );
VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphores( VkDevice, const VkSemaphoreWaitInfo *, uint64_t );
VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValue( VkDevice, VkSemaphore, uint64_t * );
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage( VkDevice, const VkImageCreateInfo *, const VkAllocationCallbacks *, VkImage * );
VKAPI_ATTR void VKAPI_CALL vkDestroyImage( VkDevice, VkImage, const VkAllocationCallbacks * );
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements( VkDevice, VkImage, VkMemoryRequirements * );
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2( VkDevice, const VkImageMemoryRequirementsInfo2 *, VkMemoryRequirements2 * );
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory( VkDevice, VkImage, VkDeviceMemory, VkDeviceSize );
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView( VkDevice, const VkImageViewCreateInfo *, const VkAllocationCallbacks *, VkImageView * );
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView( VkDevice, VkImageView, const VkAllocationCallbacks * );
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRendering( VkCommandBuffer, const VkRenderingInfo * );
VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering( VkCommandBuffer );

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

/* DXVK sub-allocates: buffers at offsets inside one larger allocation. */
#define CHUNK_SIZE 0x800000
#define SRC_OFFSET 0x300000
#define DST_OFFSET 0x500000
#define PIECE_SIZE 0x10000
#define IMAGE_CHUNK_SIZE 0x400000
#define IMAGE_OFFSET 0x100000

static unsigned char piece_byte( DWORD i, unsigned char seed )
{
    return (unsigned char)(i * 13 + seed * 29 + 1);
}

static void fill_piece( unsigned char *ptr, unsigned char seed )
{
    DWORD i;

    for (i = 0; i < PIECE_SIZE; i++) ptr[i] = piece_byte( i, seed );
}

static BOOL piece_holds( const unsigned char *ptr, unsigned char seed )
{
    DWORD i;

    for (i = 0; i < PIECE_SIZE; i++) if (ptr[i] != piece_byte( i, seed )) return FALSE;
    return TRUE;
}

/* Where else in the allocation a filled piece's first bytes are, or ~0. */
static DWORD find_piece( const unsigned char *chunk, unsigned char seed )
{
    DWORD offset, i;

    for (offset = 0; offset + PIECE_SIZE <= CHUNK_SIZE; offset += 0x1000)
    {
        if (offset == SRC_OFFSET) continue;
        for (i = 0; i < 16 && chunk[offset + i] == piece_byte( i, seed ); i++) ;
        if (i == 16) return offset;
    }
    return ~0u;
}

static VkResult record_copy( VkCommandBuffer cmd, VkBuffer src, VkBuffer dst )
{
    VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    VkBufferCopy copy = { 0, 0, PIECE_SIZE };
    VkResult res;

    if ((res = vkResetCommandBuffer( cmd, 0 )) || (res = vkBeginCommandBuffer( cmd, &begin_info ))) return res;
    barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL );
    vkCmdCopyBuffer( cmd, src, dst, 1, &copy );
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL );
    return vkEndCommandBuffer( cmd );
}

/* Clears a 64x64 image, by a render pass or by vkCmdClearColorImage, and
 * copies its middle pixel to the start of dst. */
static VkResult record_clear_readback( VkCommandBuffer cmd, VkImage image, VkImageView view, VkBuffer dst,
                                       const VkClearColorValue *colour, BOOL render_pass )
{
    static const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VkMemoryBarrier host_barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    VkBufferImageCopy copy = {0};
    VkResult res;

    if ((res = vkResetCommandBuffer( cmd, 0 )) || (res = vkBeginCommandBuffer( cmd, &begin_info ))) return res;
    if (render_pass)
    {
        VkRenderingAttachmentInfo attachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        VkRenderingInfo rendering = { VK_STRUCTURE_TYPE_RENDERING_INFO };

        transition( cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
        attachment.imageView = view;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.clearValue.color = *colour;
        rendering.renderArea.extent.width = 64;
        rendering.renderArea.extent.height = 64;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &attachment;
        vkCmdBeginRendering( cmd, &rendering );
        vkCmdEndRendering( cmd );
        transition( cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
    }
    else
    {
        transition( cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
        vkCmdClearColorImage( cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, colour, 1, &range );
        transition( cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
    }
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset.x = 32;
    copy.imageOffset.y = 32;
    copy.imageExtent.width = 1;
    copy.imageExtent.height = 1;
    copy.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer( cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, 1, &copy );
    host_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host_barrier, 0, NULL, 0, NULL );
    return vkEndCommandBuffer( cmd );
}

static VkResult submit_fence( VkDevice device, VkQueue queue, VkCommandBuffer cmd, VkFence fence )
{
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkResult res;

    if ((res = vkResetFences( device, 1, &fence ))) return res;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    if ((res = vkQueueSubmit( queue, 1, &submit, fence ))) return res;
    return vkWaitForFences( device, 1, &fence, VK_TRUE, UINT64_MAX );
}

/* As DXVK submits and waits: vkQueueSubmit2 signalling a timeline semaphore
 * value, then vkWaitSemaphores for it. The counter read right after submitting
 * says whether the GPU could still have been busy. */
static VkResult submit_timeline_commands( VkDevice device, VkQueue queue, uint32_t count,
                                          const VkCommandBuffer *commands, VkSemaphore timeline,
                                          uint64_t value, uint64_t *counter_after_submit )
{
    VkCommandBufferSubmitInfo cmd_info[2] = {{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO },
                                          { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO }};
    VkSemaphoreSubmitInfo signal = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
    VkSubmitInfo2 submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    VkSemaphoreWaitInfo wait = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
    VkResult res;

    uint32_t i;

    if (count > 2) return VK_ERROR_INITIALIZATION_FAILED;
    for (i = 0; i < count; i++) cmd_info[i].commandBuffer = commands[i];
    signal.semaphore = timeline;
    signal.value = value;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    submit.commandBufferInfoCount = count;
    submit.pCommandBufferInfos = cmd_info;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    if ((res = vkQueueSubmit2( queue, 1, &submit, 0 ))) return res;
    if ((res = vkGetSemaphoreCounterValue( device, timeline, counter_after_submit ))) return res;
    wait.semaphoreCount = 1;
    wait.pSemaphores = &timeline;
    wait.pValues = &value;
    return vkWaitSemaphores( device, &wait, UINT64_MAX );
}

static VkResult submit_timeline( VkDevice device, VkQueue queue, VkCommandBuffer cmd, VkSemaphore timeline,
                                 uint64_t value, uint64_t *counter_after_submit )
{
    return submit_timeline_commands( device, queue, 1, &cmd, timeline, value, counter_after_submit );
}

/* Exercise the entire D3D9-sized surface, a nonzero buffer copy offset, and
 * dependencies between command buffers and between submissions. The previous
 * dedicated-image check deliberately tests just a small rendering area. */
static DWORD full_frame_readback( VkDevice device, VkQueue queue, VkCommandPool pool,
                                  const VkPhysicalDeviceMemoryProperties *props, VkImage image,
                                  VkImageView view, VkSemaphore timeline, uint64_t *value )
{
    enum { width = 1280, height = 720, prefix = 0x10000, bytes = width * height * 4 };
    VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkCommandBufferAllocateInfo cmd_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VkRenderingAttachmentInfo attachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    VkRenderingInfo rendering = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    VkBufferImageCopy2 region = { VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2 };
    VkCopyImageToBufferInfo2 copy = { VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2 };
    VkImageMemoryBarrier2 image_barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    VkMemoryBarrier2 host_barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    VkDependencyInfo dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    VkMemoryRequirements requirements;
    VkCommandBuffer commands[2] = {0};
    VkBuffer buffer = 0;
    VkDeviceMemory memory = 0;
    unsigned char *mapped = NULL;
    uint64_t counter;
    uint32_t mode, i, wrong, guards, first;
    DWORD failure = 0;
    VkResult res;
    int type;

#define FULL_CHECK(call) do { if ((res = (call)) != VK_SUCCESS) { \
    report( #call, res ); failure = 66; goto done; } } while (0)
    buffer_info.size = prefix + bytes + prefix;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    FULL_CHECK( vkCreateBuffer( device, &buffer_info, NULL, &buffer ) );
    vkGetBufferMemoryRequirements( device, buffer, &requirements );
    type = memory_type( props, requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
    if (type < 0) { failure = 66; goto done; }
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = type;
    FULL_CHECK( vkAllocateMemory( device, &alloc_info, NULL, &memory ) );
    FULL_CHECK( vkBindBufferMemory( device, buffer, memory, 0 ) );
    FULL_CHECK( vkMapMemory( device, memory, 0, VK_WHOLE_SIZE, 0, (void **)&mapped ) );
    cmd_info.commandPool = pool;
    cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_info.commandBufferCount = 2;
    FULL_CHECK( vkAllocateCommandBuffers( device, &cmd_info, commands ) );

    for (mode = 0; mode < 2; mode++)
    {
        DWORD expected = mode ? 0xff00ff00 : 0xffff0000;
        memset( mapped, 0xa5, buffer_info.size );
        FULL_CHECK( vkResetCommandBuffer( commands[0], 0 ) );
        FULL_CHECK( vkBeginCommandBuffer( commands[0], &begin_info ) );
        transition( commands[0], image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
        attachment.imageView = view;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.clearValue.color.float32[0] = !mode;
        attachment.clearValue.color.float32[1] = mode;
        attachment.clearValue.color.float32[3] = 1.0f;
        rendering.renderArea.extent.width = width;
        rendering.renderArea.extent.height = height;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &attachment;
        vkCmdBeginRendering( commands[0], &rendering );
        vkCmdEndRendering( commands[0] );
        FULL_CHECK( vkEndCommandBuffer( commands[0] ) );

        FULL_CHECK( vkResetCommandBuffer( commands[1], 0 ) );
        FULL_CHECK( vkBeginCommandBuffer( commands[1], &begin_info ) );
        image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        image_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        image_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.image = image;
        image_barrier.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        host_barrier.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        host_barrier.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
        host_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        host_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &host_barrier;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &image_barrier;
        vkCmdPipelineBarrier2( commands[1], &dependency );
        region.bufferOffset = prefix;
        region.bufferRowLength = width;
        region.bufferImageHeight = height;
        region.imageSubresource = (VkImageSubresourceLayers){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = (VkExtent3D){ width, height, 1 };
        copy.srcImage = image;
        copy.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copy.dstBuffer = buffer;
        copy.regionCount = 1;
        copy.pRegions = &region;
        vkCmdCopyImageToBuffer2( commands[1], &copy );
        host_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        host_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        host_barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        host_barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        dependency.imageMemoryBarrierCount = 0;
        vkCmdPipelineBarrier2( commands[1], &dependency );
        FULL_CHECK( vkEndCommandBuffer( commands[1] ) );
        if (mode)
        {
            FULL_CHECK( submit_timeline( device, queue, commands[0], timeline, ++*value, &counter ) );
            FULL_CHECK( submit_timeline( device, queue, commands[1], timeline, ++*value, &counter ) );
        }
        else FULL_CHECK( submit_timeline_commands( device, queue, 2, commands, timeline, ++*value, &counter ) );
        wrong = guards = 0;
        first = ~0u;
        for (i = 0; i < width * height; i++)
            if (((DWORD *)(mapped + prefix))[i] != expected)
            {
                if (!wrong) first = i;
                wrong++;
            }
        for (i = 0; i < prefix; i++)
            if (mapped[i] != 0xa5 || mapped[prefix + bytes + i] != 0xa5) guards++;
        report_text( mode ? "full frame, separate submissions" : "full frame, two command buffers",
                     wrong || guards ? "FAIL" : "PASS" );
        if (wrong || guards)
        {
            report( "wrong pixels", wrong );
            report( "changed guard bytes", guards );
            if (wrong)
            {
                report( "first wrong pixel index", first );
                report( "first wrong pixel value", ((DWORD *)(mapped + prefix))[first] );
            }
            failure = 67;
        }
    }
done:
    vkDeviceWaitIdle( device );
    if (commands[0]) vkFreeCommandBuffers( device, pool, 2, commands );
    if (mapped) vkUnmapMemory( device, memory );
    if (buffer) vkDestroyBuffer( device, buffer, NULL );
    if (memory) vkFreeMemory( device, memory, NULL );
    return failure;
#undef FULL_CHECK
}

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
    VkPhysicalDeviceVulkan12Features features12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan13Features features13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    DWORD later_failure = 0;  /* the first failed DXVK-like check; the test goes on past those */
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
    report_text( "checkpoint", "full-frame readback v2" );
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
    app.apiVersion = VK_API_VERSION_1_3;
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
    /* What DXVK enables for the checks below. */
    features12.timelineSemaphore = VK_TRUE;
    features12.pNext = &features13;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    device_info.pNext = &features12;
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

    /* What DXVK does that the checks above do not: buffers bound at offsets
     * inside one larger allocation of imported coherent memory, copies waited
     * for with a timeline semaphore, and an image bound inside host-cached
     * memory cleared and read back into such a buffer. Each check reports and
     * the test goes on, so one run shows them all. */
    {
        static const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        static const unsigned char want[3][4] = { { 0, 0, 255, 255 }, { 0, 255, 0, 255 }, { 255, 0, 0, 255 } };
        static const char *const clear_names[3] =
        {
            "render pass clear, timeline wait",
            "vkCmdClearColorImage, timeline wait",
            "render pass clear, fence wait",
        };
        VkSemaphoreTypeCreateInfo timeline_type = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
        VkSemaphoreCreateInfo semaphore_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkImageCreateInfo image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        VkMemoryRequirements src_req, dst_req, image_req;
        VkBuffer src = 0, dst = 0;
        VkDeviceMemory chunk = 0, image_chunk = 0;
        VkDeviceSize image_offset;
        VkImage image = 0;
        VkImageView view = 0;
        VkSemaphore timeline = 0;
        unsigned char *chunk_ptr = NULL, *pixel;
        uint64_t value = 0, counter = 0;
        DWORD early = 0, reached = 0, seen, j;
        int chunk_type, image_type, step;

        buffer_info.size = PIECE_SIZE;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        res = vkCreateBuffer( device, &buffer_info, NULL, &src );
        CHECK( "vkCreateBuffer offset source", 50 );
        res = vkCreateBuffer( device, &buffer_info, NULL, &dst );
        CHECK( "vkCreateBuffer offset destination", 50 );
        vkGetBufferMemoryRequirements( device, src, &src_req );
        vkGetBufferMemoryRequirements( device, dst, &dst_req );
        chunk_type = memory_type( &memory_props, src_req.memoryTypeBits & dst_req.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
        if (chunk_type < 0)
        {
            report( "no coherent type for offset buffers among", src_req.memoryTypeBits );
            failure = 51;
            goto done;
        }
        alloc_info.allocationSize = CHUNK_SIZE;
        alloc_info.memoryTypeIndex = chunk_type;
        res = vkAllocateMemory( device, &alloc_info, NULL, &chunk );
        CHECK( "vkAllocateMemory 8 MiB chunk", 51 );
        res = vkBindBufferMemory( device, src, chunk, SRC_OFFSET );
        CHECK( "vkBindBufferMemory source at 3 MiB", 52 );
        res = vkBindBufferMemory( device, dst, chunk, DST_OFFSET );
        CHECK( "vkBindBufferMemory destination at 5 MiB", 52 );
        res = vkMapMemory( device, chunk, 0, CHUNK_SIZE, 0, (void **)&chunk_ptr );
        CHECK( "vkMapMemory chunk", 53 );

        /* A copy between offset-bound buffers, waited for with a fence. */
        fill_piece( chunk_ptr + SRC_OFFSET, 9 );
        memset( chunk_ptr + DST_OFFSET, 0, PIECE_SIZE );
        res = record_copy( cmd, src, dst );
        CHECK( "record offset copy", 54 );
        res = submit_fence( device, queue, cmd, fence );
        CHECK( "offset copy with a fence", 54 );
        if (piece_holds( chunk_ptr + DST_OFFSET, 9 )) report_text( "offset-bound copy passed", NULL );
        else
        {
            report( "offset-bound copy missed 5 MiB; its data is at chunk offset", find_piece( chunk_ptr, 9 ) );
            if (!later_failure) later_failure = 54;
        }

        /* The same copy, waited for as DXVK waits. */
        timeline_type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        semaphore_info.pNext = &timeline_type;
        res = vkCreateSemaphore( device, &semaphore_info, NULL, &timeline );
        CHECK( "vkCreateSemaphore timeline", 55 );
        for (j = 0; j < 8; j++)
        {
            fill_piece( chunk_ptr + SRC_OFFSET, (unsigned char)(10 + j) );
            memset( chunk_ptr + DST_OFFSET, 0, PIECE_SIZE );
            res = record_copy( cmd, src, dst );
            CHECK( "record timeline copy", 56 );
            res = submit_timeline( device, queue, cmd, timeline, ++value, &counter );
            CHECK( "timeline copy", 56 );
            if (counter >= value) reached++;
            if (!piece_holds( chunk_ptr + DST_OFFSET, (unsigned char)(10 + j) ))
            {
                early++;
                vkQueueWaitIdle( queue );
                report( "a timeline copy missed its wait; landed after vkQueueWaitIdle",
                        piece_holds( chunk_ptr + DST_OFFSET, (unsigned char)(10 + j) ) );
            }
        }
        report( "timeline copies already done when submitted", reached );
        report( "timeline waits that returned before the copy landed", early );
        if (early && !later_failure) later_failure = 57;

        /* An image bound at an offset in host-cached memory, as DXVK binds
         * its images, cleared and read back into the offset-bound buffer. */
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
        image_info.extent.width = 64;
        image_info.extent.height = 64;
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        res = vkCreateImage( device, &image_info, NULL, &image );
        CHECK( "vkCreateImage", 58 );
        vkGetImageMemoryRequirements( device, image, &image_req );
        image_type = memory_type( &memory_props, image_req.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT );
        if (image_type < 0) image_type = memory_type( &memory_props, image_req.memoryTypeBits, 0 );
        report( "image memory type", image_type );
        alloc_info.allocationSize = IMAGE_CHUNK_SIZE;
        alloc_info.memoryTypeIndex = image_type;
        res = vkAllocateMemory( device, &alloc_info, NULL, &image_chunk );
        CHECK( "vkAllocateMemory 4 MiB image chunk", 58 );
        image_offset = (IMAGE_OFFSET + image_req.alignment - 1) & ~(image_req.alignment - 1);
        res = vkBindImageMemory( device, image, image_chunk, image_offset );
        CHECK( "vkBindImageMemory at 1 MiB", 58 );
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_B8G8R8A8_UNORM;
        view_info.subresourceRange = range;
        res = vkCreateImageView( device, &view_info, NULL, &view );
        CHECK( "vkCreateImageView", 59 );

        for (step = 0; step < 3; step++)
        {
            VkClearColorValue colour = {{ step == 0, step == 1, step == 2, 1.0f }};

            pixel = chunk_ptr + DST_OFFSET;
            memset( pixel, 0, 4 );
            res = record_clear_readback( cmd, image, view, dst, &colour, step != 1 );
            CHECK( "record clear and read-back", 60 );
            if (step == 2) res = submit_fence( device, queue, cmd, fence );
            else res = submit_timeline( device, queue, cmd, timeline, ++value, &counter );
            CHECK( "submit clear and read-back", 60 );
            seen = pixel[0] | (pixel[1] << 8) | (pixel[2] << 16) | ((DWORD)pixel[3] << 24);
            if (pixel[0] == want[step][0] && pixel[1] == want[step][1] &&
                pixel[2] == want[step][2] && pixel[3] == want[step][3])
                report_text( clear_names[step], "read back" );
            else
            {
                report_text( clear_names[step], "read back wrong" );
                report( "  pixel bytes B G R A (little-endian)", seen );
                vkQueueWaitIdle( queue );
                seen = pixel[0] | (pixel[1] << 8) | (pixel[2] << 16) | ((DWORD)pixel[3] << 24);
                report( "  after vkQueueWaitIdle", seen );
                if (!later_failure) later_failure = 61 + step;
            }
        }

        /* A 1280x720 image, which NVK wants in a dedicated allocation, as
         * DXVK's back buffer is: in host-cached memory that Wine imports, NVK
         * must still address it through its own tiled mapping. */
        {
            VkMemoryDedicatedRequirements dedicated_req = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
            VkMemoryRequirements2 req2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
            VkImageMemoryRequirementsInfo2 req_info = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
            VkMemoryDedicatedAllocateInfo dedicated_info = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
            VkClearColorValue red = {{ 1.0f, 0.0f, 0.0f, 1.0f }};
            VkDeviceMemory big_memory = 0;
            VkImageView big_view = 0;
            VkImage big = 0;

            image_info.extent.width = 1280;
            image_info.extent.height = 720;
            res = vkCreateImage( device, &image_info, NULL, &big );
            CHECK( "vkCreateImage 1280x720", 64 );
            req2.pNext = &dedicated_req;
            req_info.image = big;
            vkGetImageMemoryRequirements2( device, &req_info, &req2 );
            report( "1280x720 image prefers a dedicated allocation", dedicated_req.prefersDedicatedAllocation );
            image_type = memory_type( &memory_props, req2.memoryRequirements.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT );
            if (image_type < 0) image_type = memory_type( &memory_props, req2.memoryRequirements.memoryTypeBits, 0 );
            dedicated_info.image = big;
            alloc_info.pNext = &dedicated_info;
            alloc_info.allocationSize = req2.memoryRequirements.size;
            alloc_info.memoryTypeIndex = image_type;
            res = vkAllocateMemory( device, &alloc_info, NULL, &big_memory );
            alloc_info.pNext = NULL;
            CHECK( "vkAllocateMemory dedicated", 64 );
            res = vkBindImageMemory( device, big, big_memory, 0 );
            CHECK( "vkBindImageMemory dedicated", 64 );
            view_info.image = big;
            res = vkCreateImageView( device, &view_info, NULL, &big_view );
            CHECK( "vkCreateImageView dedicated", 64 );

            pixel = chunk_ptr + DST_OFFSET;
            memset( pixel, 0, 4 );
            res = record_clear_readback( cmd, big, big_view, dst, &red, TRUE );
            CHECK( "record dedicated clear and read-back", 65 );
            res = submit_fence( device, queue, cmd, fence );
            CHECK( "submit dedicated clear and read-back", 65 );
            seen = pixel[0] | (pixel[1] << 8) | (pixel[2] << 16) | ((DWORD)pixel[3] << 24);
            if (pixel[0] == want[0][0] && pixel[1] == want[0][1] && pixel[2] == want[0][2] && pixel[3] == want[0][3])
                report_text( "dedicated 1280x720 image, render pass clear", "read back" );
            else
            {
                report_text( "dedicated 1280x720 image, render pass clear", "read back wrong" );
                report( "  pixel bytes B G R A (little-endian)", seen );
                if (!later_failure) later_failure = 65;
            }
            {
                DWORD full_failure = full_frame_readback( device, queue, pool, &memory_props, big,
                                                          big_view, timeline, &value );
                if (full_failure && !later_failure) later_failure = full_failure;
            }
            vkDestroyImageView( device, big_view, NULL );
            vkDestroyImage( device, big, NULL );
            vkFreeMemory( device, big_memory, NULL );
        }

        vkUnmapMemory( device, chunk );
        vkDestroyImageView( device, view, NULL );
        vkDestroyImage( device, image, NULL );
        vkFreeMemory( device, image_chunk, NULL );
        vkDestroySemaphore( device, timeline, NULL );
        vkDestroyBuffer( device, src, NULL );
        vkDestroyBuffer( device, dst, NULL );
        vkFreeMemory( device, chunk, NULL );
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
    if (!failure) failure = later_failure;

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
