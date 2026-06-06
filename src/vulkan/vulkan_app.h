//
// Created by neuroil on 2026/6/1.
//

#ifndef BLACK_HOLE_PRAC_VULKANAPP_H
#define BLACK_HOLE_PRAC_VULKANAPP_H

#include "vulkan_context.h"
#include "gpu_buffer.h"
#include "gpu_image.h"
#include "graphics_pipeline.h"
#include "compute_pipeline.h"
#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>

class VulkanApp {
    std::unique_ptr<VulkanContext> context_;
    GLFWwindow* window_ = nullptr;

    // UBO
    std::unique_ptr<GPUBuffer> camera_ubo_;
    std::unique_ptr<GPUBuffer> spacetime_ubo_;

    // 管线
    std::unique_ptr<GPUImage> storage_image_;
    std::unique_ptr<ComputePipeline> compute_pipeline_;
    std::unique_ptr<GraphicsPipeline> graphics_pipeline_;

    // handles
    // 描述集
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout compute_descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet compute_descriptor_set_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout graphics_descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet graphics_descriptor_set_ = VK_NULL_HANDLE;
    // 管线布局
    VkPipelineLayout compute_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout graphics_pipeline_layout_ = VK_NULL_HANDLE;
    VkSampler storage_sampler_;
    // 同步原语
    VkSemaphore image_available_semaphores_[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore render_finished_semaphores_[MAX_FRAMES_IN_FLIGHT];
    VkFence frame_fences_[MAX_FRAMES_IN_FLIGHT];
    std::vector<VkFence> images_in_flight_;
    uint32_t current_frame_ = 0;

    // helper
    void create_sync_objects();
    void create_descriptor_set_layout(VkDescriptorSetLayout* layout,
                                       const VkDescriptorSetLayoutBinding* bindings,
                                       uint32_t binding_count);
    void create_descriptor_sets();
    void update_ubos();

    void main_loop();
    void clean_up();

    // 处理逻辑
    void step_frame();

public:
    VulkanApp(uint32_t w, uint32_t h);
    ~VulkanApp();
    void run();


};


#endif //BLACK_HOLE_PRAC_VULKANAPP_H
