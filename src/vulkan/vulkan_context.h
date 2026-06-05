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
#include <glm/ext/scalar_uint_sized.hpp>
#include <vulkan/vulkan.h>
#include "queue_family_indices.h"
#include "swap_chain_support_details.h"

const std::vector<const char*> validation_layers = {"VK_LAYER_KHRONOS_validation"};

const std::vector<const char*> device_extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
constexpr bool enable_validation_layers = false;
#else
constexpr bool enable_validation_layers = true;
#endif

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance_,
                                      const VkDebugUtilsMessengerCreateInfoEXT* p_create_info,
                                      const VkAllocationCallbacks* p_allocator,
                                      VkDebugUtilsMessengerEXT* p_debug_messenger);
void DestroyDebugUtilsMessengerEXT(VkInstance instance_, VkDebugUtilsMessengerEXT debug_messenger_,
                                   const VkAllocationCallbacks* p_allocator);


// ###############################################################


class VulkanContext {
    uint32_t WIDTH, HEIGHT;
    GLFWwindow* window_;
    VkInstance instance_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue compute_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    VkSwapchainKHR swap_chain_;
    std::vector<VkImage> swap_chain_images_;
    VkFormat swap_chain_image_format_;
    VkExtent2D swap_chain_extent_;
    std::vector<VkImageView> swap_chain_image_views_;

    uint32_t graphics_queue_family_index_ = 0;
    uint32_t compute_queue_family_index_      = 0;
    uint32_t present_queue_family_index_     = 0;
    VkPhysicalDeviceMemoryProperties memory_properties_{};
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;

    // ============== helper ==============
    void init_window();
    bool check_validation_layer_support();
    void create_vulkan_instance();
    bool is_physical_device_ok(VkPhysicalDevice physical_device);
    bool check_device_extension_support(VkPhysicalDevice physical_device);
    void pickup_physical_device();
    QueueFamilyIndices find_queue_families(VkPhysicalDevice device);
    void create_surface();
    void create_logical_device();
    swap_chain_support_details query_swap_chain_support(VkPhysicalDevice device);
    VkSurfaceFormatKHR choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>& available_formats);
    VkPresentModeKHR choose_swap_present_mode(const std::vector<VkPresentModeKHR>& available_present_modes);
    VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities);
    void create_swap_chain(VkSwapchainKHR old_swapchain = VK_NULL_HANDLE);
    void create_image_views();
    void cache_queue_family_index();
    void cache_memory_properties();
    void init_command_pool();
    void init_descriptor_pool();
    void init_pipeline_cache();

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
    explicit VulkanContext(uint32_t w, uint32_t h);
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
    uint32_t graphics_queue_family()     const;
    uint32_t compute_queue_family()      const;
    uint32_t presentation_queue_family() const;

    // Swapchain
    VkSwapchainKHR swapchain() const;
    VkImage swapchain_image(uint32_t i) const;
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
    void submit(VkCommandBuffer cmd, VkSemaphore wait, VkSemaphore signal, VkFence signal_fence);
    void present(uint32_t image_index, VkSemaphore wait_semaphore);

    // window
    GLFWwindow* window() const;
};

#endif // BLACK_HOLE_PRAC_VULKANCONTEXT_H
