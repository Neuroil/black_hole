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
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class VulkanApp {
    std::unique_ptr<VulkanContext> context_;
    GLFWwindow* window_ = nullptr;

    // ── FPV Camera 状态 ──
    // 第一人称: position + yaw/pitch, 无 target 无轨道
    struct FPCamera {
        glm::vec3 position = {0.0f, 15.0f, 5.0f};   // 世界坐标
        float yaw   = -1.5708f;   // 绕 +z 轴旋转 (rad), 初始朝向 -y (黑洞)
        float pitch = -0.316f;    // 抬角 from xy-plane (rad), 初始略向下看
        float fov   = 60.0f;      // 垂直视场角 (度)
        float move_speed = 0.3f;  // 每击移动量
        float look_sensitivity = 0.003f;
    } camera_;

    // 鼠标状态
    bool looking_ = false;               // 左键: 环视
    double last_mouse_x_ = 0.0;
    double last_mouse_y_ = 0.0;

    // GLFW 回调 (static + glfwSetWindowUserPointer 回找 this)
    static void key_callback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void mouse_button_callback(GLFWwindow* w, int button, int action, int mods);
    static void cursor_pos_callback(GLFWwindow* w, double xpos, double ypos);
    static void scroll_callback(GLFWwindow* w, double xoffset, double yoffset);

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
    // image_available: 每 in-flight frame 一个 (acquire 时消费，submit 后安全重用)
    VkSemaphore image_available_semaphores_[MAX_FRAMES_IN_FLIGHT];
    // render_finished: 每 swapchain image 一个 (present 可能异步持有，
    //   按 image_index 索引避免旧 image 的 present 未完成时复用 semaphore)
    std::vector<VkSemaphore> render_finished_semaphores_;
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
