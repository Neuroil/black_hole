//
// Created by neuroil on 2026/6/3.
//

#include "compute_pipeline.h"
#include "vulkan_util.h"
#include <stdexcept>

ComputePipeline::ComputePipeline(const VulkanContext* p_context,
                                 const std::string& shader_path,
                                 VkPipelineLayout pipeline_layout,
                                 VkPipelineCache pipeline_cache) {
    context_ = p_context;

    VkShaderModule shader_module = VK_NULL_HANDLE;

    // 创建 shader module
    shader_module = vulkan_util::create_shader_module(context_->device(), shader_path);

    // 配置 compute pipeline
    VkPipelineShaderStageCreateInfo shader_stage{};
    shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shader_stage.module = shader_module;
    shader_stage.pName = "main";

    VkComputePipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.stage = shader_stage;
    pipeline_info.layout = pipeline_layout;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    VkResult result = vkCreateComputePipelines(
        context_->device(), pipeline_cache, 1, &pipeline_info, nullptr, &pipeline_);

    // pipeline 创建后 shader module 可立即销毁
    vkDestroyShaderModule(context_->device(), shader_module, nullptr);

    if (result != VK_SUCCESS) {
        pipeline_ = VK_NULL_HANDLE;
        throw std::runtime_error("failed to create compute pipeline!");
    }
}

ComputePipeline::~ComputePipeline() {
    if (context_ && pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_->device(), pipeline_, nullptr);
    }
}

ComputePipeline::ComputePipeline(ComputePipeline&& other) noexcept
    : context_(other.context_)
      , pipeline_(other.pipeline_) {
    other.context_ = nullptr;
    other.pipeline_ = VK_NULL_HANDLE;
}

ComputePipeline& ComputePipeline::operator=(ComputePipeline&& other) noexcept {
    if (this != &other) {
        // 先销毁自身资源
        if (context_ && pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(context_->device(), pipeline_, nullptr);
        }

        // 转移
        context_ = other.context_;
        pipeline_ = other.pipeline_;

        // 源置空
        other.context_ = nullptr;
        other.pipeline_ = VK_NULL_HANDLE;
    }
    return *this;
}

VkPipeline ComputePipeline::pipeline() const { return pipeline_; }
