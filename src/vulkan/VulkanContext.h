//
// Created by neuroil on 2026/6/1.
//

#ifndef BLACK_HOLE_PRAC_VULKANCONTEXT_H
#define BLACK_HOLE_PRAC_VULKANCONTEXT_H

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

class VulkanContext {
    VkInstance instance_;
    VkPhysicalDevice physical_device_;
    VkDevice device_;

    // ============== helper ==============
    VkInstance create_vulkan_instance();
    bool is_physical_device_ok();
    void choose_physical_device();
    void create_physical_device();

public:
    explicit VulkanContext(GLFWwindow* window);
    ~VulkanContext();
};

#endif // BLACK_HOLE_PRAC_VULKANCONTEXT_H