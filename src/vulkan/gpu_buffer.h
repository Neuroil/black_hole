//
// Created by neuroil on 2026/6/3.
//

#ifndef BLACK_HOLE_PRAC_GPUBUFFER_H
#define BLACK_HOLE_PRAC_GPUBUFFER_H

#include "vulkan_context.h"
#include <vulkan/vulkan_core.h>

class GPUBuffer {
    const VulkanContext* context_ = nullptr;
    VkDeviceSize size_ = 0;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;

public:
    GPUBuffer(const VulkanContext* p_context,
              VkDeviceSize size,
              VkBufferUsageFlags usage,
              VkMemoryPropertyFlags memory_properties);
    ~GPUBuffer();

    GPUBuffer(GPUBuffer&& other) noexcept;
    GPUBuffer& operator=(GPUBuffer&& other) noexcept;

    GPUBuffer(const GPUBuffer&) = delete;
    GPUBuffer& operator=(const GPUBuffer&) = delete;

    VkBuffer     buffer() const;
    VkDeviceSize size()   const;

    // host-visible 内存映射
    void* map(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
    void  unmap();
};

#endif //BLACK_HOLE_PRAC_GPUBUFFER_H
