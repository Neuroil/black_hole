//
// Created by neuroil on 2026/6/1.
//

#include "VulkanContext.h"
#include "vulkan_util.h"

VkInstance VulkanContext::create_vulkan_instance() {
    VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Blackhole Simulation",
        .applicationVersion = VK_MAKE_VERSION(0,1,0),
        .pEngineName = "Blackhole Simulation",
        .apiVersion = VK_API_VERSION_1_3
    };

    uint32_t glfw_extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    VkInstanceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = glfw_extension_count,
        .ppEnabledExtensionNames = glfw_extensions,
        .enabledLayerCount = 0
    };

    VkResult result = vkCreateInstance(&create_info, nullptr, &instance_);

}

VulkanContext::VulkanContext(GLFWwindow* window) {
    instance_ = create_vulkan_instance();

}

VulkanContext::~VulkanContext() {
    vkDestroyInstance(instance_, nullptr);
}
