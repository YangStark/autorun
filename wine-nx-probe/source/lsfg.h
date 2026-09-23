#ifndef WINE_NX_LSFG_H
#define WINE_NX_LSFG_H

#include "lsfg_config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct wine_nx_lsfg;
void wine_nx_lsfg_device_features(VkInstance instance, VkPhysicalDevice physical,
                                 VkPhysicalDeviceFeatures *features);
int wine_nx_lsfg_prepare_swapchain(VkInstance instance, VkPhysicalDevice physical,
                                   VkSwapchainCreateInfoKHR *info);
struct wine_nx_lsfg *wine_nx_lsfg_create(VkInstance instance, VkPhysicalDevice physical,
                                        VkDevice device, VkSwapchainKHR swapchain,
                                        const VkSwapchainCreateInfoKHR *info);
void wine_nx_lsfg_destroy(struct wine_nx_lsfg *state);
int wine_nx_lsfg_present(struct wine_nx_lsfg *state, VkQueue queue, unsigned int family,
                         int generate, const VkPresentInfoKHR *info, VkResult *result);

#ifdef __cplusplus
}
#endif
#endif
