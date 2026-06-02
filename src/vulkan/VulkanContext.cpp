//
// Created by neuroil on 2026/6/1.
//

#include "VulkanContext.h"
#include "vulkan_util.h"
#include <cstring>
#include <set>

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance_,
                                      const VkDebugUtilsMessengerCreateInfoEXT* p_create_info,
                                      const VkAllocationCallbacks* p_allocator,
                                      VkDebugUtilsMessengerEXT* p_debug_messenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance_, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance_, p_create_info, p_allocator, p_debug_messenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance_, VkDebugUtilsMessengerEXT debug_messenger_,
                                   const VkAllocationCallbacks* p_allocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance_, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance_, debug_messenger_, p_allocator);
    }
}

bool VulkanContext::check_validation_layer_support() {
    std::cout << "Now checking the validation layers..." << std::endl;
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    for (const char* layer_name : validation_layers) {
        bool layerFound = false;

        for (const auto& layer_properties : available_layers) {
            if (strcmp(layer_name, layer_properties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }
        if (!layerFound) return false;
    }
    return true;
}

std::vector<const char*> VulkanContext::get_required_extensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

void VulkanContext::create_vulkan_instance() {
    if (enableValidationLayers && !check_validation_layer_support()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

#ifndef NDEBUG

    std::cout << "Done.\nNow creating the instance..." << std::endl;
#endif

    VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Blackhole Simulation",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "Blackhole Simulation",
        .apiVersion = VK_API_VERSION_1_3,
    };

    auto extensions = get_required_extensions();
    VkInstanceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = 0,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    VkResult result = vkCreateInstance(&create_info, nullptr, &instance_);
}


void VulkanContext::setup_debug_messenger() {
    if (!enableValidationLayers) return;
    std::cout << "Done.\nNow setting up the debug messenger..." << std::endl;
    VkDebugUtilsMessengerCreateInfoEXT create_info{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
        .pUserData = nullptr,
    };
    if (CreateDebugUtilsMessengerEXT(instance_, &create_info, nullptr, &debug_messenger_) !=
        VK_SUCCESS) {
        throw std::runtime_error("failed to set up the debug messenger!");
    }
}

bool VulkanContext::is_physical_device_ok(VkPhysicalDevice physical_device) {
    QueueFamilyIndices indices = find_queue_families(physical_device);

    return indices.is_complete();
}

void VulkanContext::pickup_physical_device() {
    std::cout << "Done.\nNow picking up the physical device..." << std::endl;
    uint32_t physical_device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &physical_device_count, nullptr);
    if (physical_device_count == 0) throw std::runtime_error("failed to find GPUs with Vulkan support!");

    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    vkEnumeratePhysicalDevices(instance_, &physical_device_count, physical_devices.data());

    for (auto physical_device : physical_devices) {
        if (is_physical_device_ok(physical_device)){
            physical_device_ = physical_device;
            break;
        }
    }

    if (physical_device_ == VK_NULL_HANDLE) throw std::runtime_error("there's no suitable GPU!");
}

QueueFamilyIndices VulkanContext::find_queue_families(VkPhysicalDevice physical_device) {
    std::cout << "...finding the queue families..." << std::endl;
    QueueFamilyIndices indices;

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
    if (queue_family_count == 0) throw std::runtime_error("there's no queue_family in need!");

    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families.data());

    int i = 0;
    for (const auto& qf : queue_families) {
        // graphics
        if (qf.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            indices.graphic_family_ = i;

        // compute: 优先专用 compute 族（无 GRAPHICS 位），可异步并行
        if ((qf.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(qf.queueFlags & VK_QUEUE_GRAPHICS_BIT))
            indices.compute_family_ = i;

        i++;
    }

    // compute 回退：无专用 compute 族时，用 graphics 族
    if (!indices.compute_family_.has_value()) {
        i = 0;
        for (const auto& qf : queue_families) {
            if (qf.queueFlags & VK_QUEUE_COMPUTE_BIT) {
                indices.compute_family_ = i;
                break;
            }
            i++;
        }
    }

    // present: 需要 surface，逐族查询
    i = 0;
    for (const auto& qf : queue_families) {
        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface_, &present_support);
        if (present_support) {
            indices.present_family_ = i;
            break;
        }
        i++;
    }

    return indices;
}

void VulkanContext::create_surface(GLFWwindow* window) {
    std::cout << "Done.\nNow creating the window surface..." << std::endl;
    if (glfwCreateWindowSurface(instance_, window, nullptr, &surface_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
}

void VulkanContext::create_logical_device() {
    std::cout << "Done.\nNow creating the logical device..." << std::endl;

    QueueFamilyIndices indices = find_queue_families(physical_device_);

    // 去重
    std::set<uint32_t> unique_families = {
        indices.graphic_family_.value(),
        indices.compute_family_.value(),
        indices.present_family_.value(),
    };

    float queue_priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queue_infos;
    for (uint32_t family : unique_families) {
        queue_infos.push_back({
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = family,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        });
    }

    // 设备扩展
    std::vector<const char*> device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    // Dynamic Rendering
    VkPhysicalDeviceVulkan13Features features_13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE,
    };

    VkDeviceCreateInfo device_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features_13,
        .queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size()),
        .pQueueCreateInfos = queue_infos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
    };

    if (vkCreateDevice(physical_device_, &device_info, nullptr, &device_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device!");
    }

    // 取回队列句柄
    vkGetDeviceQueue(device_, indices.graphic_family_.value(), 0, &graphics_queue_);
    vkGetDeviceQueue(device_, indices.compute_family_.value(), 0, &compute_queue_);
    vkGetDeviceQueue(device_, indices.present_family_.value(), 0, &present_queue_);
}

// ##########################################################

VulkanContext::VulkanContext(GLFWwindow* window) {
    create_vulkan_instance();
    setup_debug_messenger();
    create_surface(window);
    pickup_physical_device();
    create_logical_device();
}

VulkanContext::~VulkanContext() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(instance_, debug_messenger_, nullptr);
    }
    vkDestroyInstance(instance_, nullptr);
}
