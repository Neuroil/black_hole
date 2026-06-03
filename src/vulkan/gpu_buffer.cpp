//
// Created by neuroil on 2026/6/3.
//

#include "gpu_buffer.h"
#include "vulkan_util.h"
#include <cstring>
#include <stdexcept>

GPUBuffer::GPUBuffer(const VulkanContext* p_context, VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags memory_properties) {
    if (size == 0) {
        throw std::invalid_argument("buffer size must be > 0");
    }

    context_ = p_context;
    size_ = size;

    // 创建缓冲区
    buffer_ = vulkan_util::create_raw_buffer(context_->device(), size, usage);

    // 申请内存
    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(context_->device(), buffer_, &mem_requirements);
    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = vulkan_util::find_memory_type(
        context_->memory_properties(),
        mem_requirements.memoryTypeBits,
        memory_properties);

    if (vkAllocateMemory(context_->device(), &alloc_info, nullptr, &memory_) != VK_SUCCESS) {
        vkDestroyBuffer(context_->device(), buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    // 绑定内存
    if (vkBindBufferMemory(context_->device(), buffer_, memory_, 0) != VK_SUCCESS) {
        vkFreeMemory(context_->device(), memory_, nullptr);
        vkDestroyBuffer(context_->device(), buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
        throw std::runtime_error("failed to bind buffer memory!");
    }
}

GPUBuffer::~GPUBuffer() {
    if (context_ && buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(context_->device(), buffer_, nullptr);
    }
    if (context_ && memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(context_->device(), memory_, nullptr);
    }
}

GPUBuffer::GPUBuffer(GPUBuffer&& other) noexcept
    : context_(other.context_)
      , size_(other.size_)
      , buffer_(other.buffer_)
      , memory_(other.memory_) {
    other.context_ = nullptr;
    other.size_ = 0;
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
}

GPUBuffer& GPUBuffer::operator=(GPUBuffer&& other) noexcept {
    if (this != &other) {
        // 先销毁自身资源
        if (context_ && buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(context_->device(), buffer_, nullptr);
        }
        if (context_ && memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(context_->device(), memory_, nullptr);
        }

        // 转移
        context_ = other.context_;
        size_ = other.size_;
        buffer_ = other.buffer_;
        memory_ = other.memory_;

        // 源置空
        other.context_ = nullptr;
        other.size_ = 0;
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
    }
    return *this;
}

VkBuffer GPUBuffer::buffer() const { return buffer_; }

VkDeviceSize GPUBuffer::size() const { return size_; }

void* GPUBuffer::map(VkDeviceSize offset, VkDeviceSize size) {
    void* data;
    if (vkMapMemory(context_->device(), memory_, offset, size, 0, &data) != VK_SUCCESS) {
        throw std::runtime_error("failed to map buffer memory!");
    }
    return data;
}

void GPUBuffer::unmap() {
    vkUnmapMemory(context_->device(), memory_);
}
