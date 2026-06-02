//
// Created by neuroil on 2026/6/2.
//

#ifndef BLACK_HOLE_PRAC_QUEUEFAMILTINDICES_H
#define BLACK_HOLE_PRAC_QUEUEFAMILTINDICES_H

#include <optional>
#include <vulkan/vulkan.h>

struct QueueFamilyIndices {
    std::optional<uint32_t> graphic_family_;

    bool is_complete() { return graphic_family_.has_value(); }
};

QueueFamilyIndices find_queue_families(VkPhysicalDevice device);

#endif // BLACK_HOLE_PRAC_QUEUEFAMILTINDICES_H