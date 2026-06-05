//
// Created by neuroil on 2026/6/2.
//

#ifndef BLACK_HOLE_PRAC_QUEUEFAMILTINDICES_H
#define BLACK_HOLE_PRAC_QUEUEFAMILTINDICES_H

#include <optional>
#include <vulkan/vulkan.h>

struct QueueFamilyIndices {
    std::optional<uint32_t> graphic_family_;
    std::optional<uint32_t> compute_family_;
    std::optional<uint32_t> present_family_;

    bool is_complete() const {
        return graphic_family_.has_value() &&
               compute_family_.has_value() &&
               present_family_.has_value();
    }
};

// 注：find_queue_families 是 VulkanContext 的成员函数（需要访问 surface_ 查询 present 支持）
// 不再作为自由函数提供。

#endif // BLACK_HOLE_PRAC_QUEUEFAMILTINDICES_H
