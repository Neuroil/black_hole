//
// Created by neuroil on 2026/6/1.
//

#include "VulkanContext.h"

#include <algorithm>

#include "vulkan_util.h"
#include <cstring>
#include <limits>
#include <set>

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance_,
                                      const VkDebugUtilsMessengerCreateInfoEXT* p_create_info,
                                      const VkAllocationCallbacks* p_allocator,
                                      VkDebugUtilsMessengerEXT* p_debug_messenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance_, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance_, p_create_info, p_allocator, p_debug_messenger);
    }
    else {
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

void VulkanContext::init_window() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window_ = glfwCreateWindow(static_cast<int>(WIDTH), static_cast<int>(HEIGHT), "Blackhole GRRT Simulation", nullptr,
                               nullptr);
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

    if (enable_validation_layers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

void VulkanContext::create_vulkan_instance() {
    if (enable_validation_layers && !check_validation_layer_support()) {
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
    if (!enable_validation_layers) return;
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

    bool extension_support = check_device_extension_support(physical_device);
    bool swap_chain_adequate = false;
    if (extension_support) {
        SwapChainSupportDetails swapChainSupport = query_swap_chain_support(physical_device);
        swap_chain_adequate = !swapChainSupport.formats_.empty() && !swapChainSupport.present_modes_.empty();
    }

    return indices.is_complete() && extension_support && swap_chain_adequate;
}

bool VulkanContext::check_device_extension_support(VkPhysicalDevice physical_device) {
    uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, nullptr);

    std::vector<VkExtensionProperties> available_extensions(extension_count);
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, available_extensions.data());

    std::set<std::string> required_extension(device_extensions.begin(), device_extensions.end());

    for (const auto& extension : available_extensions) {
        required_extension.erase(extension.extensionName);
    }

    return required_extension.empty();
}

void VulkanContext::pickup_physical_device() {
    std::cout << "Done.\nNow picking up the physical device..." << std::endl;
    uint32_t physical_device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &physical_device_count, nullptr);
    if (physical_device_count == 0) throw std::runtime_error("failed to find GPUs with Vulkan support!");

    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    vkEnumeratePhysicalDevices(instance_, &physical_device_count, physical_devices.data());

    for (auto physical_device : physical_devices) {
        if (is_physical_device_ok(physical_device)) {
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

void VulkanContext::create_surface() {
    std::cout << "Done.\nNow creating the window surface..." << std::endl;
    if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) != VK_SUCCESS) {
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

SwapChainSupportDetails VulkanContext::query_swap_chain_support(VkPhysicalDevice device) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.capabilities_);

    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &format_count, nullptr);
    if (format_count != 0) {
        details.formats_.resize(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &format_count, details.formats_.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.present_modes_.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, details.present_modes_.data());
    }

    return details;
}

VkSurfaceFormatKHR VulkanContext::
choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>& available_formats) {
    for (const auto& available_format : available_formats) {
        if (available_format.format == VK_FORMAT_B8G8R8A8_SRGB && available_format.colorSpace ==
            VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return available_format;
        }
    }
    return available_formats[0];
}

VkPresentModeKHR VulkanContext::
choose_swap_present_mode(const std::vector<VkPresentModeKHR>& available_present_modes) {
    for (const auto& availablePresentMode : available_present_modes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanContext::choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    else {
        int width, height;
        glfwGetFramebufferSize(window_, &width, &height);

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width,
                                        capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height,
                                         capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

void VulkanContext::create_swap_chain() {
    std::cout << "Done.\nNow creating swap chain..." << std::endl;
    SwapChainSupportDetails swap_chain_support_details = query_swap_chain_support(physical_device_);

    VkSurfaceFormatKHR surface_format = choose_swap_surface_format(swap_chain_support_details.formats_);
    VkPresentModeKHR present_mode = choose_swap_present_mode(swap_chain_support_details.present_modes_);
    VkExtent2D extent = choose_swap_extent(swap_chain_support_details.capabilities_);

    uint32_t image_count = swap_chain_support_details.capabilities_.minImageCount + 1;

    if (swap_chain_support_details.capabilities_.maxImageCount > 0 && image_count > swap_chain_support_details.
        capabilities_.maxImageCount) {
        image_count = swap_chain_support_details.capabilities_.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface_,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
    };

    QueueFamilyIndices indices = find_queue_families(physical_device_);
    uint32_t queueFamilyIndices[] = {indices.graphic_family_.value(), indices.present_family_.value()};

    if (indices.graphic_family_ != indices.present_family_) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queueFamilyIndices;
    }
    else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.queueFamilyIndexCount = 0;
        create_info.pQueueFamilyIndices = nullptr;
    }
    create_info.preTransform = swap_chain_support_details.capabilities_.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device_, &create_info, nullptr, &swap_chain_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swap chain!");
    }
    vkGetSwapchainImagesKHR(device_, swap_chain_, &image_count, nullptr);
    swap_chain_images_.resize(image_count);
    vkGetSwapchainImagesKHR(device_, swap_chain_, &image_count, swap_chain_images_.data());

    swap_chain_image_format_ = surface_format.format;
    swap_chain_extent_ = extent;
}

void VulkanContext::create_image_views() {
    std::cout << "Done.\nNow creating image views..." << std::endl;
    swap_chain_image_views_.resize(swap_chain_images_.size());
    for (size_t i = 0; i < swap_chain_images_.size(); i++) {
        VkImageViewCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        create_info.image = swap_chain_images_[i];
        create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        create_info.format = swap_chain_image_format_;
        create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        create_info.subresourceRange.baseMipLevel = 0;
        create_info.subresourceRange.levelCount = 1;
        create_info.subresourceRange.baseArrayLayer = 0;
        create_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &create_info, nullptr, &swap_chain_image_views_[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image views!");
        }
    }
}

// ##########################################################

VulkanContext::VulkanContext(uint32_t w, uint32_t h) : WIDTH(w), HEIGHT(h) {
    init_window();
    create_vulkan_instance();
    setup_debug_messenger();
    create_surface();
    pickup_physical_device();
    create_logical_device();
    create_swap_chain();
    create_image_views();
}

VulkanContext::~VulkanContext() {
    vkDestroySwapchainKHR(device_, swap_chain_, nullptr);
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (enable_validation_layers) {
        DestroyDebugUtilsMessengerEXT(instance_, debug_messenger_, nullptr);
    }
    vkDestroyInstance(instance_, nullptr);

    glfwDestroyWindow(window_);
    glfwTerminate();
}
