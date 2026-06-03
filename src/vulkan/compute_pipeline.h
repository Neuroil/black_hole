//
// Created by neuroil on 2026/6/3.
//

#ifndef BLACK_HOLE_PRAC_COMPUTE_PIPELINE_H
#define BLACK_HOLE_PRAC_COMPUTE_PIPELINE_H

#include "vulkan_context.h"
#include <vulkan/vulkan_core.h>

class ComputePipeline {
    const VulkanContext* context_ = nullptr;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

public:
    ComputePipeline(const VulkanContext* p_context,
                    const std::string& shader_path,
                    VkPipelineLayout pipeline_layout,
                    VkPipelineCache pipeline_cache = VK_NULL_HANDLE);
    ~ComputePipeline();

    ComputePipeline(ComputePipeline&&) noexcept;
    ComputePipeline& operator=(ComputePipeline&&) noexcept;

    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    VkPipeline pipeline() const;
};

#endif //BLACK_HOLE_PRAC_COMPUTE_PIPELINE_H
