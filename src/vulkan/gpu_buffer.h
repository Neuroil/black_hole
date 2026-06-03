//
// Created by neuroil on 2026/6/3.
//

#ifndef BLACK_HOLE_PRAC_GPUBUFFER_H
#define BLACK_HOLE_PRAC_GPUBUFFER_H

#include "vulkan_context.h"
#include <vulkan/vulkan_core.h>

class GPUBuffer {
public:
    GPUBuffer(const VulkanContext& context,
              VkDeviceSize size,
              VkBufferUsageFlags usage,
              VkMemoryPropertyFlags memory_properties);
    ~GPUBuffer();

    GPUBuffer(GPUBuffer&& other) noexcept;
    GPUBuffer& operator=(GPUBuffer&& other) noexcept;

    VkBuffer     buffer() const;
    VkDeviceSize size()   const;

    // host-visible 内存映射
    void* map(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
    void  unmap();

    // 通过 staging buffer 上传数据（初始化期专用，内部同步等待）
    void upload_from_host(const VulkanContext& context,
                          const void* data,
                          VkDeviceSize offset,
                          VkDeviceSize size,
                          VkCommandPool command_pool,
                          VkQueue queue);
};



#endif //BLACK_HOLE_PRAC_GPUBUFFER_H