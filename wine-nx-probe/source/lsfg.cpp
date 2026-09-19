/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <vulkan/vulkan.h>
#include "lsfg.h"
#include "lsfg-vk-backend/lsfgvk.hpp"
#include "lsfg-vk-common/vulkan/command_buffer.hpp"
#include "lsfg-vk-common/vulkan/fence.hpp"
#include "lsfg-vk-common/vulkan/semaphore.hpp"
#include "lsfg-vk-common/vulkan/vulkan.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

extern "C" void wine_nx_runtime_trace(const char *message);

namespace {

constexpr const char *shader_path = "sdmc:/switch/wine/lsfg/Lossless.dll";
constexpr const char *cache_path = "sdmc:/switch/wine/cache/lsfg-vk.bin";
bool enabled, performance = true;
float flow_scale = 0.25f;

void trace(const char *message)
{
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), "[LSFG] %s", message);
    wine_nx_runtime_trace(buffer);
}

VkImageMemoryBarrier barrier(VkImage image, VkAccessFlags src, VkAccessFlags dst,
                             VkImageLayout before, VkImageLayout after)
{
    return {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, src, dst, before, after,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
}

struct FrameSlot {
    explicit FrameSlot(const vk::Vulkan& vk)
        : frame(vk), restore(vk), frame_done(vk), restore_done(vk), acquired(vk) {}
    vk::CommandBuffer frame, restore;
    vk::Fence frame_done, restore_done;
    vk::Semaphore acquired;
    bool frame_pending{}, restore_pending{};

    void wait(const vk::Vulkan& vk)
    {
        if ((frame_pending && !frame_done.wait(vk)) ||
            (restore_pending && !restore_done.wait(vk)))
            throw std::runtime_error("frame completion timed out");
        frame_pending = restore_pending = false;
        frame_done.reset(vk);
        restore_done.reset(vk);
    }
};

class Presentation {
public:
    Presentation(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                 VkQueue queue, uint32_t family, VkSwapchainKHR swapchain, VkExtent2D extent)
        : swapchain(swapchain), extent(extent),
          backend(lsfgvk::backend::BorrowedDevice{
              instance, physical, device, family, queue, vkGetInstanceProcAddr, cache_path},
              shader_path, false),
          context(backend.openLocalContext(extent.width, extent.height,
                                           false, 1.0f / flow_scale, performance, 1)),
          vk(backend.vulkan())
    {
        uint32_t count{};
        auto result = vk.df().GetSwapchainImagesKHR(device, swapchain, &count, nullptr);
        if (result != VK_SUCCESS || !count)
            throw std::runtime_error("cannot enumerate swapchain images");
        images.resize(count);
        result = vk.df().GetSwapchainImagesKHR(device, swapchain, &count, images.data());
        if (result != VK_SUCCESS)
            throw std::runtime_error("cannot read swapchain images");
        images.resize(count);
        for (uint32_t i = 0; i < count; ++i)
            presented.emplace_back(vk);
        for (uint32_t i = 0; i < 3; ++i)
            slots.emplace_back(vk);
    }

    ~Presentation()
    {
        vk.df().DeviceWaitIdle(vk.dev());
    }

    bool uses_queue(VkQueue queue) const { return queue == vk.queue(); }
    void reset_history() { warmup = 2; }

    VkResult present(const VkPresentInfoKHR& info)
    {
        uint32_t index = info.pImageIndices[0];
        if (index >= images.size())
            return VK_ERROR_OUT_OF_DATE_KHR;

        auto& slot = slots.at(frame_index % slots.size());
        slot.wait(vk);
        const size_t source_index = frame_index & 1;
        const VkImage source = backend.sourceImage(context, source_index);
        std::vector<VkSemaphore> waits;
        if (info.waitSemaphoreCount)
            waits.assign(info.pWaitSemaphores, info.pWaitSemaphores + info.waitSemaphoreCount);

        slot.frame.begin(vk);
        slot.frame.memoryBarrier(vk);
        capture(slot.frame, images[index], source, frame_index != 0);
        if (!frame_index)
            capture(slot.frame, images[index], backend.sourceImage(context, 1), false);
        backend.recordFrame(context, slot.frame);
        if (!warmup)
            output(slot.frame, backend.destinationImage(context, 0), images[index],
                   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_SHADER_WRITE_BIT);
        slot.frame.end(vk);
        VkSemaphore signal = presented[index].handle();
        slot.frame.submit(vk, std::move(waits), VK_NULL_HANDLE, 0,
                          {signal}, VK_NULL_HANDLE, 0, slot.frame_done.handle());
        slot.frame_pending = true;
        ++frame_index;

        if (warmup)
        {
            --warmup;
            return show(info, index, signal, false);
        }

        /* VI allows one acquired image: present the interpolation before reacquiring. */
        VkResult generated = show(info, index, signal, true);
        if (generated < VK_SUCCESS)
            return generated;
        VkResult acquired = vk.df().AcquireNextImageKHR(vk.dev(), swapchain, UINT64_MAX,
                                                       slot.acquired.handle(), VK_NULL_HANDLE, &index);
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
            return acquired;
        if (index >= images.size())
            return VK_ERROR_OUT_OF_DATE_KHR;

        slot.restore.begin(vk);
        output(slot.restore, source, images[index], VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_SHADER_READ_BIT);
        slot.restore.end(vk);
        signal = presented[index].handle();
        slot.restore.submit(vk, {slot.acquired.handle()}, VK_NULL_HANDLE, 0,
                            {signal}, VK_NULL_HANDLE, 0, slot.restore_done.handle());
        slot.restore_pending = true;
        VkResult result = show(info, index, signal, false);
        if (result >= VK_SUCCESS && !generated_logged)
        {
            trace("first interpolated frame presented");
            generated_logged = true;
        }
        if (result == VK_SUCCESS && (generated == VK_SUBOPTIMAL_KHR || acquired == VK_SUBOPTIMAL_KHR))
            result = VK_SUBOPTIMAL_KHR;
        return result;
    }

private:
    void capture(const vk::CommandBuffer& command, VkImage image, VkImage source, bool initialized)
    {
        command.blitImage(vk, {
            barrier(image, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
            barrier(source, initialized ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT : 0,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)},
            {image, source}, extent, {
            barrier(image, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),
            barrier(source, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL)});
    }

    void output(const vk::CommandBuffer& command, VkImage source, VkImage image,
                VkImageLayout layout, VkAccessFlags access)
    {
        command.blitImage(vk, {
            barrier(source, access, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
            barrier(image, layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_READ_BIT,
                    VK_ACCESS_TRANSFER_WRITE_BIT, layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)},
            {source, image}, extent, {
            barrier(source, VK_ACCESS_TRANSFER_READ_BIT, access,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
            barrier(image, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)});
    }

    VkResult show(const VkPresentInfoKHR& original, uint32_t index, VkSemaphore wait, bool generated)
    {
        VkPresentInfoKHR info = original;
        info.pNext = generated ? nullptr : original.pNext;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &wait;
        info.pImageIndices = &index;
        info.pResults = generated ? nullptr : original.pResults;
        return vk.df().QueuePresentKHR(vk.queue(), &info);
    }

    VkSwapchainKHR swapchain;
    VkExtent2D extent;
    lsfgvk::backend::Instance backend;
    lsfgvk::backend::Context& context;
    const vk::Vulkan& vk;
    std::vector<VkImage> images;
    std::vector<vk::Semaphore> presented;
    std::vector<FrameSlot> slots;
    uint64_t frame_index{};
    unsigned int warmup{2};
    bool generated_logged{};
};
}

struct wine_nx_lsfg {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkSwapchainKHR swapchain;
    VkExtent2D extent;
    std::unique_ptr<Presentation> presentation;
    bool failed{};
    VkResult error{VK_SUCCESS};
};

extern "C" void wine_nx_lsfg_configure(int active, int perf, int flow)
{
    constexpr float scales[] = {0.125f, 0.25f, 0.5f};
    enabled = active != 0;
    performance = perf != 0;
    flow_scale = scales[flow >= 0 && flow < 3 ? flow : 1];
    if (!enabled) return;
    FILE *file = std::fopen(shader_path, "rb");
    if (!file)
    {
        trace("disabled: copy your own Lossless.dll to sdmc:/switch/wine/lsfg/Lossless.dll");
        enabled = false;
        return;
    }
    std::fclose(file);
}

extern "C" void wine_nx_lsfg_device_features(VkInstance instance, VkPhysicalDevice physical,
                                             VkPhysicalDeviceFeatures *features)
{
    if (!enabled) return;
    auto get = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures"));
    VkPhysicalDeviceFeatures supported{};
    get(physical, &supported);
    if (!supported.shaderStorageImageExtendedFormats)
    {
        trace("disabled: storage image extended formats are unavailable");
        enabled = false;
        return;
    }
    features->shaderStorageImageExtendedFormats |= supported.shaderStorageImageExtendedFormats;
}

extern "C" int wine_nx_lsfg_prepare_swapchain(VkInstance instance, VkPhysicalDevice physical,
                                             VkSwapchainCreateInfoKHR *info)
{
    if (!enabled) return 0;
    if (info->imageExtent.width * flow_scale < 64 || info->imageExtent.height * flow_scale < 64)
    {
        trace("disabled for this swapchain: motion resolution must be at least 64x64");
        return 0;
    }
    if (info->imageArrayLayers != 1 || info->flags ||
        info->imageColorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ||
        (info->imageFormat != VK_FORMAT_B8G8R8A8_UNORM && info->imageFormat != VK_FORMAT_R8G8B8A8_UNORM))
    {
        trace("disabled for this swapchain: only SDR RGBA8/BGRA8 UNORM is supported");
        return 0;
    }
    auto caps_fn = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    auto format_fn = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFormatProperties"));
    VkSurfaceCapabilitiesKHR caps{};
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const VkFormatFeatureFlags blit = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
    VkFormatProperties host{}, source{};
    format_fn(physical, info->imageFormat, &host);
    format_fn(physical, VK_FORMAT_R8G8B8A8_UNORM, &source);
    if (caps_fn(physical, info->surface, &caps) != VK_SUCCESS ||
        (caps.supportedUsageFlags & usage) != usage ||
        (host.optimalTilingFeatures & blit) != blit ||
        (source.optimalTilingFeatures & blit) != blit)
    {
        trace("disabled for this swapchain: required image transfers are unavailable");
        return 0;
    }
    info->imageUsage |= usage;
    info->presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info->minImageCount = std::max(info->minImageCount, std::max(caps.minImageCount, 3u));
    if (caps.maxImageCount)
        info->minImageCount = std::min(info->minImageCount, caps.maxImageCount);
    return 1;
}

extern "C" wine_nx_lsfg *wine_nx_lsfg_create(VkInstance instance, VkPhysicalDevice physical,
                                            VkDevice device, VkSwapchainKHR swapchain,
                                            const VkSwapchainCreateInfoKHR *info)
{
    try {
        return new wine_nx_lsfg{instance, physical, device, swapchain, info->imageExtent, {}, false};
    } catch (const std::exception& e) {
        trace(e.what());
        return nullptr;
    }
}

extern "C" void wine_nx_lsfg_destroy(wine_nx_lsfg *state)
{
    delete state;
}

extern "C" int wine_nx_lsfg_present(wine_nx_lsfg *state, VkQueue queue, unsigned int family,
                                    int generate, const VkPresentInfoKHR *info, VkResult *result)
{
    if (!state || state->failed) return 0;
    if (state->error != VK_SUCCESS)
    {
        *result = state->error;
        if (info->pResults) info->pResults[0] = *result;
        return 1;
    }
    if (!generate || info->swapchainCount != 1 || info->pSwapchains[0] != state->swapchain)
    {
        if (state->presentation) state->presentation->reset_history();
        return 0;
    }
    if (!state->presentation)
    {
        try {
            auto props_fn = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
                vkGetInstanceProcAddr(state->instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
            uint32_t count{};
            props_fn(state->physical, &count, nullptr);
            std::vector<VkQueueFamilyProperties> props(count);
            props_fn(state->physical, &count, props.data());
            const VkQueueFlags flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if (family >= props.size() || (props[family].queueFlags & flags) != flags)
                throw std::runtime_error("presentation queue must support graphics and compute");
            trace("compiling frame-generation pipelines");
            state->presentation = std::make_unique<Presentation>(
                state->instance, state->physical, state->device, queue, family, state->swapchain, state->extent);
            char message[160];
            std::snprintf(message, sizeof(message), "2x active, %ux%u, motion %.1f%%, %s",
                          state->extent.width, state->extent.height, flow_scale * 100,
                          performance ? "performance" : "quality");
            trace(message);
        } catch (const std::exception& e) {
            trace(e.what());
            state->failed = true;
            return 0;
        }
    }
    if (!state->presentation->uses_queue(queue))
    {
        state->presentation->reset_history();
        return 0;
    }
    try {
        *result = state->presentation->present(*info);
    } catch (const std::exception& e) {
        trace(e.what());
        *result = VK_ERROR_INITIALIZATION_FAILED;
    }
    if (*result < VK_SUCCESS) state->error = *result;
    if (info->pResults) info->pResults[0] = *result;
    return 1;
}
