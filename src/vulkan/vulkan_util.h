//
// Created by neuroil on 2026/6/1.
//

#ifndef BLACK_HOLE_PRAC_VULKAN_UTIL_H
#define BLACK_HOLE_PRAC_VULKAN_UTIL_H

#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>

// ============================================================
// vulkan_util — 纯函数工具命名空间
//
// 设计原则：
// - 零状态：所有函数显式接收 VkDevice，不持有任何 handle
// - 零决策：输入完全决定输出，不含业务偏好（如"选哪个 GPU"）
// - 零项目依赖：不依赖 VulkanContext / GPUBuffer / 任何项目类型
// - 任何 Vulkan 项目可直接复用
//
// 判断标准：函数签名里有业务判断？
//   有 → VulkanContext / 上层编排
//   没有 → vulkan_util
// ============================================================

namespace vulkan_util {

// ── 内存 ──
uint32_t find_memory_type(const VkPhysicalDeviceMemoryProperties& props,
                          uint32_t type_bits, VkMemoryPropertyFlags required);

// ── Buffer（不分配内存，仅创建 VkBuffer handle） ──
VkBuffer create_raw_buffer(VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage);

// ── Shader ──
VkShaderModule create_shader_module(VkDevice device, const std::string& path);

// ── 同步原语 ──
VkSemaphore create_semaphore(VkDevice device);
VkFence create_fence(VkDevice device, VkFenceCreateFlags flags);

// ── Command pool / buffer ──
VkCommandPool create_command_pool(VkDevice device, uint32_t queue_family_index,
                                   VkCommandPoolCreateFlags flags);
VkCommandBuffer allocate_command_buffer(VkDevice device, VkCommandPool pool);

} // namespace vulkan_util

#endif // BLACK_HOLE_PRAC_VULKAN_UTIL_H
