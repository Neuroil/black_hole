//
// Created by neuroil on 2026/6/3.
//

#ifndef BLACK_HOLE_PRAC_GPUIMAGE_H
#define BLACK_HOLE_PRAC_GPUIMAGE_H

#include <vulkan/vulkan_core.h>
#include <cstdint>

class VulkanContext;

// GPUImage — VkImage + VkImageView + VkDeviceMemory RAII 包装
//
// 设计决策：
// - 构造时持有 const VulkanContext* ——与 GPUBuffer 统一，不重复持有裸 VkDevice
// - create_barrier() 纯查询、不修改状态；transition_barrier() 同步更新布局追踪
// - barrier 执行由 CommandBuffer 负责——GPUImage 不操作命令流（名词/动词分离）
// - 不内置 VMA——先用手动 vkAllocateMemory，后续按需引入
class GPUImage {
    const VulkanContext* context_        = nullptr;
    VkFormat             format_         = VK_FORMAT_UNDEFINED;
    VkExtent2D           extent_         = {};
    VkImageLayout        current_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage              image_          = VK_NULL_HANDLE;
    VkImageView          image_view_     = VK_NULL_HANDLE;
    VkDeviceMemory       memory_         = VK_NULL_HANDLE;

public:
    GPUImage(const VulkanContext* context,
             uint32_t width, uint32_t height,
             VkFormat format,
             VkImageUsageFlags usage,
             VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ~GPUImage();

    GPUImage(GPUImage&& other) noexcept;
    GPUImage& operator=(GPUImage&& other) noexcept;

    GPUImage(const GPUImage&) = delete;
    GPUImage& operator=(const GPUImage&) = delete;

    VkImage      image()          const;
    VkImageView  image_view()     const;
    VkFormat     format()         const;
    VkExtent2D   extent()         const;
    VkImageLayout current_layout() const;

    // 纯查询：生成 barrier，不修改内部布局追踪
    VkImageMemoryBarrier create_barrier(VkImageLayout new_layout,
                                         VkAccessFlags src_access,
                                         VkAccessFlags dst_access) const;

    // 生成 barrier 并更新内部布局追踪
    VkImageMemoryBarrier transition_barrier(VkImageLayout new_layout,
                                             VkAccessFlags src_access,
                                             VkAccessFlags dst_access);
};

#endif //BLACK_HOLE_PRAC_GPUIMAGE_H
