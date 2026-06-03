//
// Created by neuroil on 2026/6/3.
//

#ifndef BLACK_HOLE_PRAC_GRAPHICS_PIPELINE_H
#define BLACK_HOLE_PRAC_GRAPHICS_PIPELINE_H

#include "vulkan_context.h"
#include <vulkan/vulkan_core.h>
#include <vector>
#include <string>

struct GraphicsPipelineConfig {
    std::string vertex_shader_path;
    std::string fragment_shader_path;
    std::vector<VkFormat> color_attachment_formats;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipelineCache  pipeline_cache  = VK_NULL_HANDLE;

    // vertex
    std::vector<VkVertexInputBindingDescription>   vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> vertex_attributes;

    // primitive
    VkPrimitiveTopology topology          = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkBool32            primitive_restart = VK_FALSE;

    // viewport
    VkExtent2D viewport_extent{0, 0};

    // 光栅化器
    VkPolygonMode   polygon_mode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cull_mode    = VK_CULL_MODE_NONE;
    VkFrontFace     front_face   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // 绑定（单附件，默认不混合颜色）
    VkPipelineColorBlendAttachmentState color_blend_attachment = [] {
        VkPipelineColorBlendAttachmentState s{};
        s.blendEnable    = VK_FALSE;
        s.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                         | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        return s;
    }();
};

class GraphicsPipeline {
    const VulkanContext* context_   = nullptr;
    VkPipeline           pipeline_  = VK_NULL_HANDLE;

public:
    GraphicsPipeline(const VulkanContext* p_context,
                     const GraphicsPipelineConfig& config);
    ~GraphicsPipeline();

    GraphicsPipeline(GraphicsPipeline&&) noexcept;
    GraphicsPipeline& operator=(GraphicsPipeline&&) noexcept;

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    VkPipeline pipeline() const;
};

#endif // BLACK_HOLE_PRAC_GRAPHICS_PIPELINE_H
