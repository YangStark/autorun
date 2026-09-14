/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 * With sdmc:/switch/wine/vulkan-probe.txt containing 1, report what the
 * loaderless NVK from build-mesa-switch.sh offers on this console, as [NXVK]
 * lines: the instance, the GPU, each requirement of DXVK's d3d9 baseline
 * profile (VP_DXVK_requirements.json in DXVK), what Wine needs to map Vulkan
 * memory for 32-bit programs, and whether a device can be created. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

void wine_nx_runtime_trace( const char *msg );
void wine_nx_vulkan_probe( void );

/* Past the global functions, everything goes through vkGetInstanceProcAddr,
 * as a loader would, whatever else the archive exports. */
#define VK_FUNCS \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkEnumerateDeviceExtensionProperties) \
    X(vkGetPhysicalDeviceProperties2) X(vkGetPhysicalDeviceFeatures2) X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkCreateDevice) X(vkDestroyDevice)
#define X(name) static __typeof__(name) *p_##name;
VK_FUNCS
#undef X

struct requirement
{
    const char *name;
    int met;
};

#define FEATURE(s, member) { #member, !!(s)->member }
#define LIMIT(s, member, min) { #member, (s)->member >= (min) }

static void vk_log( const char *format, ... ) __attribute__((format(printf, 1, 2)));
static void vk_log( const char *format, ... )
{
    char buf[1024];
    va_list args;

    va_start( args, format );
    vsnprintf( buf, sizeof(buf), format, args );
    va_end( args );
    wine_nx_runtime_trace( buf );
}

static int has_extension( const VkExtensionProperties *props, uint32_t count, const char *name )
{
    uint32_t i;

    for (i = 0; i < count; i++) if (!strcmp( props[i].extensionName, name )) return 1;
    return 0;
}

/* One line per group: all present, or the names that are not. */
static unsigned int report( const char *group, const struct requirement *reqs, unsigned int count )
{
    char names[768] = "";
    unsigned int i, missing = 0;
    int len = 0;

    for (i = 0; i < count; i++)
    {
        if (reqs[i].met) continue;
        if (len < (int)sizeof(names))
            len += snprintf( names + len, sizeof(names) - len, "%s%s", missing ? " " : "", reqs[i].name );
        missing++;
    }
    if (missing) vk_log( "[NXVK] d3d9 baseline %s: %u of %u missing: %s", group, missing, count, names );
    else vk_log( "[NXVK] d3d9 baseline %s: all %u present", group, count );
    return missing;
}

static void probe_gpu( VkPhysicalDevice gpu )
{
    VkPhysicalDeviceVulkan11Properties props11 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES };
    VkPhysicalDeviceVulkan12Properties props12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES, .pNext = &props11 };
    VkPhysicalDeviceVulkan13Properties props13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES, .pNext = &props12 };
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT host = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT };
    VkPhysicalDeviceProperties2 props = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &props13 };
    VkPhysicalDeviceVulkan11Features f11 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    VkPhysicalDeviceVulkan12Features f12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &f11 };
    VkPhysicalDeviceVulkan13Features f13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &f12 };
    VkPhysicalDeviceMaintenance5FeaturesKHR m5 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR };
    VkPhysicalDeviceMaintenance6FeaturesKHR m6 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES_KHR };
    VkPhysicalDeviceRobustness2FeaturesEXT r2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT };
    VkPhysicalDeviceDepthClipEnableFeaturesEXT dc = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT };
    VkPhysicalDeviceFeatures2 features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f13 };
    const VkPhysicalDeviceFeatures *f = &features.features;
    const VkPhysicalDeviceLimits *limits = &props.properties.limits;
    VkPhysicalDeviceMemoryProperties memory;
    VkQueueFamilyProperties families[8];
    VkExtensionProperties *ext;
    uint32_t ext_count = 0, family_count = 8, family, i, v;
    int has_m5, has_m6, has_r2, has_dc, has_host;
    unsigned int missing = 0;
    char heaps[256] = "";
    VkDevice device;
    VkResult res;
    int len = 0;

    p_vkEnumerateDeviceExtensionProperties( gpu, NULL, &ext_count, NULL );
    if (!(ext = calloc( ext_count ? ext_count : 1, sizeof(*ext) ))) return;
    p_vkEnumerateDeviceExtensionProperties( gpu, NULL, &ext_count, ext );
    has_m5 = has_extension( ext, ext_count, VK_KHR_MAINTENANCE_5_EXTENSION_NAME );
    has_m6 = has_extension( ext, ext_count, VK_KHR_MAINTENANCE_6_EXTENSION_NAME );
    has_r2 = has_extension( ext, ext_count, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME );
    has_dc = has_extension( ext, ext_count, VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME );
    has_host = has_extension( ext, ext_count, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME );

    /* Only structures of extensions the device has may be chained. */
    if (has_host) { host.pNext = props.pNext; props.pNext = &host; }
    if (has_m5) { m5.pNext = features.pNext; features.pNext = &m5; }
    if (has_m6) { m6.pNext = features.pNext; features.pNext = &m6; }
    if (has_r2) { r2.pNext = features.pNext; features.pNext = &r2; }
    if (has_dc) { dc.pNext = features.pNext; features.pNext = &dc; }
    p_vkGetPhysicalDeviceProperties2( gpu, &props );
    p_vkGetPhysicalDeviceFeatures2( gpu, &features );
    p_vkGetPhysicalDeviceMemoryProperties( gpu, &memory );

    v = props.properties.apiVersion;
    vk_log( "[NXVK] GPU %s: Vulkan %u.%u.%u, driver %s %s, conformance %u.%u.%u.%u, %u device extensions",
            props.properties.deviceName, VK_API_VERSION_MAJOR( v ), VK_API_VERSION_MINOR( v ), VK_API_VERSION_PATCH( v ),
            props12.driverName, props12.driverInfo, props12.conformanceVersion.major, props12.conformanceVersion.minor,
            props12.conformanceVersion.subminor, props12.conformanceVersion.patch, ext_count );
    for (i = 0; i < memory.memoryHeapCount && len < (int)sizeof(heaps); i++)
        len += snprintf( heaps + len, sizeof(heaps) - len, "%s%llu MB%s", i ? ", " : "",
                         (unsigned long long)(memory.memoryHeaps[i].size >> 20),
                         memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ? " device-local" : "" );
    vk_log( "[NXVK] memory: %u heaps (%s), %u types", memory.memoryHeapCount, heaps, memory.memoryTypeCount );
    vk_log( "[NXVK] 32-bit mapping for Wine: VK_EXT_map_memory_placed %s, VK_EXT_external_memory_host %s (alignment %llu); VK_KHR_swapchain %s",
            has_extension( ext, ext_count, VK_EXT_MAP_MEMORY_PLACED_EXTENSION_NAME ) ? "yes" : "no",
            has_host ? "yes" : "no", (unsigned long long)host.minImportedHostPointerAlignment,
            has_extension( ext, ext_count, VK_KHR_SWAPCHAIN_EXTENSION_NAME ) ? "yes" : "no" );

    {
        const struct requirement version[] =
        {
            { "apiVersion>=1.3.204", v >= VK_MAKE_API_VERSION( 0, 1, 3, 204 ) },
        };
        const struct requirement extensions[] =
        {
            { VK_KHR_LOAD_STORE_OP_NONE_EXTENSION_NAME, has_extension( ext, ext_count, VK_KHR_LOAD_STORE_OP_NONE_EXTENSION_NAME ) },
            { VK_KHR_MAINTENANCE_5_EXTENSION_NAME, has_m5 },
            { VK_KHR_MAINTENANCE_6_EXTENSION_NAME, has_m6 },
            { VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME, has_dc },
            { VK_EXT_ROBUSTNESS_2_EXTENSION_NAME, has_r2 },
        };
        const struct requirement feature_list[] =
        {
            FEATURE(f, robustBufferAccess), FEATURE(f, fragmentStoresAndAtomics), FEATURE(f, samplerAnisotropy),
            FEATURE(f, shaderInt16), FEATURE(f, shaderSampledImageArrayDynamicIndexing), FEATURE(f, geometryShader),
            FEATURE(f, imageCubeArray), FEATURE(f, depthClamp), FEATURE(f, depthBiasClamp), FEATURE(f, fillModeNonSolid),
            FEATURE(f, sampleRateShading), FEATURE(f, shaderClipDistance), FEATURE(f, shaderCullDistance),
            FEATURE(f, textureCompressionBC), FEATURE(f, occlusionQueryPrecise), FEATURE(f, independentBlend),
            FEATURE(f, fullDrawIndexUint32), FEATURE(f, shaderImageGatherExtended),
            FEATURE(&f11, multiview), FEATURE(&f11, storageBuffer16BitAccess), FEATURE(&f11, shaderDrawParameters),
            FEATURE(&f12, uniformBufferStandardLayout), FEATURE(&f12, subgroupBroadcastDynamicId),
            FEATURE(&f12, imagelessFramebuffer), FEATURE(&f12, separateDepthStencilLayouts), FEATURE(&f12, hostQueryReset),
            FEATURE(&f12, timelineSemaphore), FEATURE(&f12, shaderSubgroupExtendedTypes), FEATURE(&f12, vulkanMemoryModel),
            FEATURE(&f12, vulkanMemoryModelDeviceScope), FEATURE(&f12, bufferDeviceAddress),
            FEATURE(&f12, storageBuffer8BitAccess), FEATURE(&f12, shaderInt8), FEATURE(&f12, descriptorIndexing),
            FEATURE(&f12, descriptorBindingSampledImageUpdateAfterBind),
            FEATURE(&f12, descriptorBindingUpdateUnusedWhilePending), FEATURE(&f12, descriptorBindingPartiallyBound),
            FEATURE(&f12, runtimeDescriptorArray), FEATURE(&f12, samplerMirrorClampToEdge),
            FEATURE(&f13, robustImageAccess), FEATURE(&f13, shaderTerminateInvocation),
            FEATURE(&f13, shaderZeroInitializeWorkgroupMemory), FEATURE(&f13, synchronization2),
            FEATURE(&f13, shaderIntegerDotProduct), FEATURE(&f13, maintenance4), FEATURE(&f13, pipelineCreationCacheControl),
            FEATURE(&f13, subgroupSizeControl), FEATURE(&f13, computeFullSubgroups),
            FEATURE(&f13, shaderDemoteToHelperInvocation), FEATURE(&f13, inlineUniformBlock), FEATURE(&f13, dynamicRendering),
            FEATURE(&m5, maintenance5), FEATURE(&m6, maintenance6), FEATURE(&r2, nullDescriptor),
            FEATURE(&r2, robustBufferAccess2), FEATURE(&dc, depthClipEnable),
        };
        const struct requirement limit_list[] =
        {
            LIMIT(limits, maxPushConstantsSize, 256u),
            LIMIT(&props11, maxMultiviewViewCount, 6u),
            LIMIT(&props11, maxMultiviewInstanceIndex, 134217727u),
            LIMIT(&props12, maxTimelineSemaphoreValueDifference, 2147483647u),
            LIMIT(&props13, maxBufferSize, 1073741824u),
            LIMIT(&props13, maxInlineUniformBlockSize, 256u),
            LIMIT(&props13, maxPerStageDescriptorInlineUniformBlocks, 4u),
            LIMIT(&props13, maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks, 4u),
            LIMIT(&props13, maxDescriptorSetInlineUniformBlocks, 4u),
            LIMIT(&props13, maxDescriptorSetUpdateAfterBindInlineUniformBlocks, 4u),
            LIMIT(&props13, maxInlineUniformTotalSize, 4u),
        };

        missing += report( "version", version, sizeof(version) / sizeof(version[0]) );
        missing += report( "extensions", extensions, sizeof(extensions) / sizeof(extensions[0]) );
        missing += report( "features", feature_list, sizeof(feature_list) / sizeof(feature_list[0]) );
        missing += report( "limits", limit_list, sizeof(limit_list) / sizeof(limit_list[0]) );
    }
    vk_log( "[NXVK] DXVK d3d9 baseline %s", missing ? "NOT met" : "met" );
    free( ext );

    p_vkGetPhysicalDeviceQueueFamilyProperties( gpu, &family_count, families );
    for (family = 0; family < family_count; family++)
        if (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) break;
    if (family == family_count)
    {
        vk_log( "[NXVK] no graphics queue among %u families", family_count );
        return;
    }
    {
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queue = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                          .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority };
        VkDeviceCreateInfo info = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                    .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue };

        res = p_vkCreateDevice( gpu, &info, NULL, &device );
        vk_log( "[NXVK] vkCreateDevice with a graphics queue (family %u of %u): %d", family, family_count, res );
        if (res == VK_SUCCESS) p_vkDestroyDevice( device, NULL );
    }
}

void wine_nx_vulkan_probe( void )
{
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "Wine-NX",
                              .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkExtensionProperties *ext = NULL;
    VkPhysicalDevice gpus[4];
    uint32_t version = 0, ext_count = 0, gpu_count = 4, i;
    VkInstance instance;
    VkResult res;

    vkEnumerateInstanceVersion( &version );
    vkEnumerateInstanceExtensionProperties( NULL, &ext_count, NULL );
    if ((ext = calloc( ext_count ? ext_count : 1, sizeof(*ext) )))
        vkEnumerateInstanceExtensionProperties( NULL, &ext_count, ext );
    vk_log( "[NXVK] instance: Vulkan %u.%u.%u, %u extensions, VK_KHR_surface %s, VK_NN_vi_surface %s",
            VK_API_VERSION_MAJOR( version ), VK_API_VERSION_MINOR( version ), VK_API_VERSION_PATCH( version ), ext_count,
            ext && has_extension( ext, ext_count, VK_KHR_SURFACE_EXTENSION_NAME ) ? "yes" : "no",
            ext && has_extension( ext, ext_count, "VK_NN_vi_surface" ) ? "yes" : "no" );
    free( ext );

    if ((res = vkCreateInstance( &info, NULL, &instance )) != VK_SUCCESS)
    {
        vk_log( "[NXVK] vkCreateInstance failed: %d", res );
        return;
    }
#define X(name) p_##name = (__typeof__(p_##name))vkGetInstanceProcAddr( instance, #name );
    VK_FUNCS
#undef X
    if (!p_vkDestroyInstance || !p_vkEnumeratePhysicalDevices || !p_vkEnumerateDeviceExtensionProperties ||
        !p_vkGetPhysicalDeviceProperties2 || !p_vkGetPhysicalDeviceFeatures2 || !p_vkGetPhysicalDeviceMemoryProperties ||
        !p_vkGetPhysicalDeviceQueueFamilyProperties || !p_vkCreateDevice || !p_vkDestroyDevice)
    {
        vk_log( "[NXVK] vkGetInstanceProcAddr left a function unresolved" );
        return;
    }
    res = p_vkEnumeratePhysicalDevices( instance, &gpu_count, gpus );
    if (res < 0 || !gpu_count)
        vk_log( "[NXVK] no GPU: vkEnumeratePhysicalDevices %d, %u devices", res, gpu_count );
    else
        for (i = 0; i < gpu_count; i++) probe_gpu( gpus[i] );
    p_vkDestroyInstance( instance, NULL );
}
