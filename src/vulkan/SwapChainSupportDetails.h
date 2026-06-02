//
// Created by neuroil on 2026/6/2.
//

#ifndef BLACK_HOLE_PRAC_SWAPCHAINSUPPORTDETAILS_H
#define BLACK_HOLE_PRAC_SWAPCHAINSUPPORTDETAILS_H

#include <vulkan/vulkan.h>
#include <vector>

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities_;
    std::vector<VkSurfaceFormatKHR> formats_;
    std::vector<VkPresentModeKHR> present_modes_;
};

#endif //BLACK_HOLE_PRAC_SWAPCHAINSUPPORTDETAILS_H