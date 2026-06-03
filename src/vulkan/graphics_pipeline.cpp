//
// Created by neuroil on 2026/6/3.
//

#include "graphics_pipeline.h"
#include "vulkan_util.h"
#include <stdexcept>

GraphicsPipeline::GraphicsPipeline(const VulkanContext* p_context,
                                   const GraphicsPipelineConfig& config) {
    context_ = p_context;

    VkShaderModule vert_module = VK_NULL_HANDLE;
    VkShaderModule frag_module = VK_NULL_HANDLE;

    vert_module = vulkan_util::create_shader_module(context_->device(), config.vertex_shader_path);


    try {
        frag_module = vulkan_util::create_shader_module(context_->device(), config.fragment_shader_path);
    }
    catch (...) {
        vkDestroyShaderModule(context_->device(), vert_module, nullptr);
        throw;
    }

    // shader stages
    VkPipelineShaderStageCreateInfo vert_stage{};
    vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_module;
    vert_stage.pName = "main";

    VkPipelineShaderStageCreateInfo frag_stage{};
    frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_module;
    frag_stage.pName = "main";

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage, frag_stage};

    // vertex input state
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = static_cast<uint32_t>(config.vertex_bindings.size());
    vertex_input.pVertexBindingDescriptions = config.vertex_bindings.data();
    vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(config.vertex_attributes.size());
    vertex_input.pVertexAttributeDescriptions = config.vertex_attributes.data();

    // input assembly state
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = config.topology;
    input_assembly.primitiveRestartEnable = config.primitive_restart;

    // viewport state 动态剪切
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    // rasterization state
    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = config.polygon_mode;
    rasterization.cullMode = config.cull_mode;
    rasterization.frontFace = config.front_face;
    rasterization.lineWidth = 1.0f;

    // multisample state
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // color blend state （单附件）
    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &config.color_blend_attachment;

    // dynamic state
    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    // Dynamic Rendering create info
    VkPipelineRenderingCreateInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_info.colorAttachmentCount = static_cast<uint32_t>(config.color_attachment_formats.size());
    rendering_info.pColorAttachmentFormats = config.color_attachment_formats.data();

    // graphics pipeline create info
    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &rendering_info;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = config.pipeline_layout;
    pipeline_info.renderPass = VK_NULL_HANDLE;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    VkResult result = vkCreateGraphicsPipelines(
        context_->device(), config.pipeline_cache, 1, &pipeline_info, nullptr, &pipeline_);

    vkDestroyShaderModule(context_->device(), vert_module, nullptr);
    vkDestroyShaderModule(context_->device(), frag_module, nullptr);

    if (result != VK_SUCCESS) {
        pipeline_ = VK_NULL_HANDLE;
        throw std::runtime_error("failed to create graphics pipeline!");
    }
}

GraphicsPipeline::~GraphicsPipeline() {
    if (context_ && pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_->device(), pipeline_, nullptr);
    }
}

GraphicsPipeline::GraphicsPipeline(GraphicsPipeline&& other) noexcept
    : context_(other.context_)
      , pipeline_(other.pipeline_) {
    other.context_ = nullptr;
    other.pipeline_ = VK_NULL_HANDLE;
}

GraphicsPipeline& GraphicsPipeline::operator=(GraphicsPipeline&& other) noexcept {
    if (this != &other) {
        if (context_ && pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(context_->device(), pipeline_, nullptr);
        }
        context_ = other.context_;
        pipeline_ = other.pipeline_;

        other.context_ = nullptr;
        other.pipeline_ = VK_NULL_HANDLE;
    }
    return *this;
}

VkPipeline GraphicsPipeline::pipeline() const { return pipeline_; }
