//
// Created by neuroil on 2026/6/3.
//

#include "gpu_image.h"
#include "vulkan_context.h"
#include "vulkan_util.h"
#include <stdexcept>

GPUImage::GPUImage(const VulkanContext* context,
                   uint32_t width, uint32_t height,
                   VkFormat format,
                   VkImageUsageFlags usage,
                   VkMemoryPropertyFlags memory_properties) {
    if (width == 0 || height == 0) {
        throw std::invalid_argument("image dimensions must be > 0");
    }

    context_ = context;
    format_ = format;
    extent_ = {width, height};

    // 创建 VkImage
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(context_->device(), &image_info, nullptr, &image_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image!");
    }

    // 申请内存
    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(context_->device(), image_, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = vulkan_util::find_memory_type(
        context_->memory_properties(),
        mem_requirements.memoryTypeBits,
        memory_properties);

    if (vkAllocateMemory(context_->device(), &alloc_info, nullptr, &memory_) != VK_SUCCESS) {
        vkDestroyImage(context_->device(), image_, nullptr);
        image_ = VK_NULL_HANDLE;
        throw std::runtime_error("failed to allocate image memory!");
    }

    // 绑定内存
    if (vkBindImageMemory(context_->device(), image_, memory_, 0) != VK_SUCCESS) {
        vkDestroyImage(context_->device(), image_, nullptr);
        vkFreeMemory(context_->device(), memory_, nullptr);
        image_ = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
        throw std::runtime_error("failed to bind image memory!");
    }

    // 创建 Image View
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format_;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(context_->device(), &view_info, nullptr, &image_view_) != VK_SUCCESS) {
        vkDestroyImage(context_->device(), image_, nullptr);
        vkFreeMemory(context_->device(), memory_, nullptr);
        image_ = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
        image_view_ = VK_NULL_HANDLE;
        throw std::runtime_error("failed to create image view!");
    }
}

GPUImage::~GPUImage() {
    if (context_ && image_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(context_->device(), image_view_, nullptr);
    }
    if (context_ && image_ != VK_NULL_HANDLE) {
        vkDestroyImage(context_->device(), image_, nullptr);
    }
    if (context_ && memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(context_->device(), memory_, nullptr);
    }
}

GPUImage::GPUImage(GPUImage&& other) noexcept
    : context_(other.context_)
    , format_(other.format_)
    , extent_(other.extent_)
    , current_layout_(other.current_layout_)
    , image_(other.image_)
    , image_view_(other.image_view_)
    , memory_(other.memory_) {

    other.context_ = nullptr;
    other.format_ = VK_FORMAT_UNDEFINED;
    other.extent_ = {};
    other.current_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    other.image_ = VK_NULL_HANDLE;
    other.image_view_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
}

GPUImage& GPUImage::operator=(GPUImage&& other) noexcept {
    if (this != &other) {
        // 先销毁自身资源
        if (context_ && image_view_ != VK_NULL_HANDLE) {
            vkDestroyImageView(context_->device(), image_view_, nullptr);
        }
        if (context_ && image_ != VK_NULL_HANDLE) {
            vkDestroyImage(context_->device(), image_, nullptr);
        }
        if (context_ && memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(context_->device(), memory_, nullptr);
        }

        // 转移
        context_ = other.context_;
        format_ = other.format_;
        extent_ = other.extent_;
        current_layout_ = other.current_layout_;
        image_ = other.image_;
        image_view_ = other.image_view_;
        memory_ = other.memory_;

        // 源置空
        other.context_ = nullptr;
        other.format_ = VK_FORMAT_UNDEFINED;
        other.extent_ = {};
        other.current_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
        other.image_ = VK_NULL_HANDLE;
        other.image_view_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
    }
    return *this;
}

// getter

VkImage GPUImage::image() const { return image_; }

VkImageView GPUImage::image_view() const { return image_view_; }

VkFormat GPUImage::format() const { return format_; }

VkExtent2D GPUImage::extent() const { return extent_; }

VkImageLayout GPUImage::current_layout() const { return current_layout_; }

// barrier

VkImageMemoryBarrier GPUImage::create_barrier(VkImageLayout new_layout,
                                              VkAccessFlags src_access,
                                              VkAccessFlags dst_access) const {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = current_layout_;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    return barrier;
}

VkImageMemoryBarrier GPUImage::transition_barrier(VkImageLayout new_layout,
                                                  VkAccessFlags src_access,
                                                  VkAccessFlags dst_access) {
    VkImageLayout old_layout = current_layout_;
    current_layout_ = new_layout;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    return barrier;
}
