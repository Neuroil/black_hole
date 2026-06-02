//
// Created by neuroil on 2026/6/1.
//

#include "VulkanContext.h"
#include "vulkan_util.h"
#include <cstring>

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
    VkPhysicalDeviceProperties device_properties;
    VkPhysicalDeviceFeatures device_features;
    vkGetPhysicalDeviceProperties(physical_device, &device_properties);
    vkGetPhysicalDeviceFeatures(physical_device, &device_features);

    return device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
}

void VulkanContext::pickup_physical_device() {
    std::cout << "Done.\nNow picking up the physical device..." << std::endl;
    uint32_t physical_device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &physical_device_count, nullptr);
    if (physical_device_count == 0) throw std::runtime_error("failed to enumerate physical deivces!");

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

VulkanContext::VulkanContext(GLFWwindow* window) {
    create_vulkan_instance();
    setup_debug_messenger();
    pickup_physical_device();
}

VulkanContext::~VulkanContext() {
    if (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(instance_, debug_messenger_, nullptr);
    }
    vkDestroyInstance(instance_, nullptr);
    //
}
