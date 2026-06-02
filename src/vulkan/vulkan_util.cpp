//
// Created by neuroil on 2026/6/1.
//

#include "vulkan_util.h"
#include <fstream>
#include <stdexcept>
#include <vector>

namespace vulkan_util {

uint32_t find_memory_type(const VkPhysicalDeviceMemoryProperties& props,
                          uint32_t type_bits, VkMemoryPropertyFlags required) {
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if ((type_bits & (1 << i)) &&
            (props.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

VkBuffer create_raw_buffer(VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage) {
    VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer buffer;
    if (vkCreateBuffer(device, &buffer_info, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }
    return buffer;
}

VkShaderModule create_shader_module(VkDevice device, const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open shader file: " + path);
    }

    size_t file_size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(file_size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(file_size));
    file.close();

    VkShaderModuleCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = buffer.size(),
        .pCode = reinterpret_cast<const uint32_t*>(buffer.data()),
    };

    VkShaderModule shader_module;
    if (vkCreateShaderModule(device, &create_info, nullptr, &shader_module) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }
    return shader_module;
}

VkSemaphore create_semaphore(VkDevice device) {
    VkSemaphoreCreateInfo semaphore_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkSemaphore semaphore;
    if (vkCreateSemaphore(device, &semaphore_info, nullptr, &semaphore) != VK_SUCCESS) {
        throw std::runtime_error("failed to create semaphore!");
    }
    return semaphore;
}

VkFence create_fence(VkDevice device, VkFenceCreateFlags flags) {
    VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = flags,
    };
    VkFence fence;
    if (vkCreateFence(device, &fence_info, nullptr, &fence) != VK_SUCCESS) {
        throw std::runtime_error("failed to create fence!");
    }
    return fence;
}

VkCommandPool create_command_pool(VkDevice device, uint32_t queue_family_index,
                                   VkCommandPoolCreateFlags flags) {
    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = flags,
        .queueFamilyIndex = queue_family_index,
    };
    VkCommandPool command_pool;
    if (vkCreateCommandPool(device, &pool_info, nullptr, &command_pool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
    return command_pool;
}

VkCommandBuffer allocate_command_buffer(VkDevice device, VkCommandPool pool) {
    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(device, &alloc_info, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffer!");
    }
    return cmd;
}

} // namespace vulkan_util
