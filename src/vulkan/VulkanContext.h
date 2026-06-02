//
// Created by neuroil on 2026/6/1.
//

#ifndef BLACK_HOLE_PRAC_VULKANCONTEXT_H
#define BLACK_HOLE_PRAC_VULKANCONTEXT_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.h>
#include "QueueFamilyIndices.h"

const std::vector<const char*> validation_layers = {"VK_LAYER_KHRONOS_validation"};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance_,
                                      const VkDebugUtilsMessengerCreateInfoEXT* p_create_info,
                                      const VkAllocationCallbacks* p_allocator,
                                      VkDebugUtilsMessengerEXT* p_debug_messenger);
void DestroyDebugUtilsMessengerEXT(VkInstance instance_, VkDebugUtilsMessengerEXT debug_messenger_,
                                   const VkAllocationCallbacks* p_allocator);



// ###############################################################



class VulkanContext {
    VkInstance instance_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue compute_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;

    // ============== helper ==============
    bool check_validation_layer_support();
    void create_vulkan_instance();
    bool is_physical_device_ok(VkPhysicalDevice physical_device);
    void pickup_physical_device();
    QueueFamilyIndices find_queue_families(VkPhysicalDevice device);
    void create_surface(GLFWwindow* window);
    void create_logical_device();

    // ============== debuger ==============
    std::vector<const char*> get_required_extensions();
    static VKAPI_ATTR VkBool32 VKAPI_CALL
    debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                   VkDebugUtilsMessageTypeFlagsEXT messageType,
                   const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

        return VK_FALSE;
    }
    void setup_debug_messenger();

  public:
    explicit VulkanContext(GLFWwindow* window);
    ~VulkanContext();

    // 禁止拷贝构造
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // 允许移动构造
    VulkanContext(VulkanContext&&) = default;
    VulkanContext& operator=(VulkanContext&&) = default;

    // 硬件查询
    VkInstance instance() const;
    VkPhysicalDevice physical_device() const;
    VkDevice device() const;
    VkSurfaceKHR surface() const;

    VkQueue graphics_queue() const;
    VkQueue compute_queue() const;
    VkQueue presentation_queue() const;
    uint32_t queue_family_index() const;

    // Swapchain
    VkSwapchainKHR swapchain() const;
    VkImageView swapchain_view(uint32_t i) const;
    uint32_t swapchain_view_count() const;
    VkFormat swapchain_format() const;
    VkExtent2D swapchain_extent() const;

    VkPipelineCache pipeline_cache() const;
    VkDescriptorPool descriptor_pool() const;

    uint32_t window_width() const;
    uint32_t window_height() const;

    const VkPhysicalDeviceMemoryProperties& memory_properties() const;

    // Command buffer 分配
    // 从内部 pool 分配，pool reset 统一回收
    VkCommandBuffer allocate_command_buffer();
    void reset_command_pool();

    // Swapchain 帧循环
    uint32_t acquire_next_image(VkSemaphore signal_semaphore);
    void submit(VkCommandBuffer cmd, VkSemaphore wait, VkSemaphore signal);
    void present(uint32_t image_index, VkSemaphore wait_semaphore);
};

#endif // BLACK_HOLE_PRAC_VULKANCONTEXT_H