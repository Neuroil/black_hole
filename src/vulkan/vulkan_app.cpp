//
// Created by neuroil on 2026/6/1.
//

#include "vulkan_app.h"
#include "vulkan_util.h"
#include "command_buffer.h"
#include <cstring>

VulkanApp::VulkanApp(uint32_t w, uint32_t h) : context_(nullptr) {

    // ── 1. 硬件初始化 ──
    context_ = std::make_unique<VulkanContext>(w, h);
    window_  = context_->window();

    // ── 2. GPU Buffer ──
    camera_ubo_ = std::make_unique<GPUBuffer>(
        context_.get(), 64,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    spacetime_ubo_ = std::make_unique<GPUBuffer>(
        context_.get(), 64,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // ── 3. Storage Image ──
    storage_image_ = std::make_unique<GPUImage>(
        context_.get(), w, h,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    // ── 4. Sampler（graphics pass 采样 storage image 用）──
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType     = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(context_->device(), &sampler_info, nullptr,
                        &storage_sampler_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create storage sampler!");
    }

    // ── 5. Descriptor Set Layout ──

    // Compute：binding 0=CameraUBO, 1=SpacetimeUBO, 2=storage image
    {
        VkDescriptorSetLayoutBinding bindings[3]{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[2].binding         = 2;
        bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

        create_descriptor_set_layout(
            &compute_descriptor_set_layout_, bindings, 3);
    }

    // Graphics：binding 0=storage image（fragment shader 采样）
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding         = 0;
        binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        create_descriptor_set_layout(
            &graphics_descriptor_set_layout_, &binding, 1);
    }

    // ── 6. Pipeline Layout ──
    {
        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts    = &compute_descriptor_set_layout_;
        if (vkCreatePipelineLayout(context_->device(), &layout_info, nullptr,
                                   &compute_pipeline_layout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create compute pipeline layout!");
        }
    }
    {
        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts    = &graphics_descriptor_set_layout_;
        if (vkCreatePipelineLayout(context_->device(), &layout_info, nullptr,
                                   &graphics_pipeline_layout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create graphics pipeline layout!");
        }
    }

    // ── 7. Pipeline ──
    compute_pipeline_ = std::make_unique<ComputePipeline>(
        context_.get(),
        "shaders/geodesic.comp.spv",
        compute_pipeline_layout_);

    {
        GraphicsPipelineConfig config{};
        config.vertex_shader_path        = "shaders/fullscreen.vert.spv";
        config.fragment_shader_path      = "shaders/fullscreen.frag.spv";
        config.color_attachment_formats  = { context_->swapchain_format() };
        config.pipeline_layout           = graphics_pipeline_layout_;
        config.pipeline_cache            = context_->pipeline_cache();
        config.viewport_extent           = context_->swapchain_extent();
        // fullscreen triangle：无顶点输入，vertex_bindings / vertex_attributes 留空
        graphics_pipeline_ = std::make_unique<GraphicsPipeline>(
            context_.get(), config);
    }

    // ── 8. Descriptor Pool ──
    {
        VkDescriptorPoolSize pool_sizes[3]{};
        pool_sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = 2;  // camera + spacetime
        pool_sizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[1].descriptorCount = 1;  // compute 写
        pool_sizes[2].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[2].descriptorCount = 1;  // graphics 读

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 3;
        pool_info.pPoolSizes    = pool_sizes;
        pool_info.maxSets       = 2;  // compute set + graphics set

        if (vkCreateDescriptorPool(context_->device(), &pool_info, nullptr,
                                   &descriptor_pool_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool!");
        }
    }

    // ── 9. Descriptor Set ──
    create_descriptor_sets();

    // ── 10. 同步原语 ──
    create_sync_objects();
}

VulkanApp::~VulkanApp() {
    vkDeviceWaitIdle(context_->device());
    clean_up();
    if (storage_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(context_->device(), storage_sampler_, nullptr);
    }
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(context_->device(), image_available_semaphores_[i], nullptr);
        vkDestroySemaphore(context_->device(), render_finished_semaphores_[i], nullptr);
        vkDestroyFence(context_->device(), frame_fences_[i], nullptr);
    }
}

void VulkanApp::run() {
    main_loop();
}

void VulkanApp::create_sync_objects() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        image_available_semaphores_[i] = vulkan_util::create_semaphore(context_->device());
        render_finished_semaphores_[i] = vulkan_util::create_semaphore(context_->device());
        frame_fences_[i] = vulkan_util::create_fence(
            context_->device(),
            VK_FENCE_CREATE_SIGNALED_BIT // 首帧直接通过
        );
    }
    uint32_t image_count = context_->swapchain_view_count();
    images_in_flight_.resize(image_count, VK_NULL_HANDLE);
}

void VulkanApp::create_descriptor_set_layout(VkDescriptorSetLayout* layout,
                                              const VkDescriptorSetLayoutBinding* bindings,
                                              uint32_t binding_count) {
    VkDescriptorSetLayoutCreateInfo layout_create_info{};
    layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_create_info.bindingCount = binding_count;
    layout_create_info.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(context_->device(), &layout_create_info, nullptr, layout)
        != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

void VulkanApp::create_descriptor_sets() {
    VkDevice device = context_->device();

    // 分配与更新 compute 描述集
    {
        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = descriptor_pool_;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &compute_descriptor_set_layout_;
        if (vkAllocateDescriptorSets(device, &alloc_info,
                                     &compute_descriptor_set_) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate compute descriptor set!");
        }

        VkDescriptorBufferInfo camera_info{};
        camera_info.buffer = camera_ubo_->buffer();
        camera_info.offset = 0;
        camera_info.range  = camera_ubo_->size();

        VkDescriptorBufferInfo spacetime_info{};
        spacetime_info.buffer = spacetime_ubo_->buffer();
        spacetime_info.offset = 0;
        spacetime_info.range  = spacetime_ubo_->size();

        VkDescriptorImageInfo storage_info{};
        storage_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storage_info.imageView   = storage_image_->image_view();
        storage_info.sampler     = VK_NULL_HANDLE;

        VkWriteDescriptorSet writes[3]{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = compute_descriptor_set_;
        writes[0].dstBinding      = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo     = &camera_info;

        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = compute_descriptor_set_;
        writes[1].dstBinding      = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].pBufferInfo     = &spacetime_info;

        writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = compute_descriptor_set_;
        writes[2].dstBinding      = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[2].pImageInfo      = &storage_info;

        vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
    }

    // 分配与更新 graphics 描述集
    {
        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = descriptor_pool_;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &graphics_descriptor_set_layout_;
        if (vkAllocateDescriptorSets(device, &alloc_info,
                                     &graphics_descriptor_set_) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate graphics descriptor set!");
        }

        VkDescriptorImageInfo storage_info{};
        storage_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        storage_info.imageView   = storage_image_->image_view();
        storage_info.sampler     = storage_sampler_;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = graphics_descriptor_set_;
        write.dstBinding      = 0;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo      = &storage_info;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
}

void VulkanApp::update_ubos() {
    void* data = camera_ubo_->map();
    std::memset(data, 0, camera_ubo_->size());
    camera_ubo_->unmap();

    data = spacetime_ubo_->map();
    std::memset(data, 0, spacetime_ubo_->size());
    spacetime_ubo_->unmap();
}

void VulkanApp::main_loop() {
    while (!glfwWindowShouldClose(window_)) {
        step_frame();
        glfwPollEvents();
    }
}

void VulkanApp::clean_up() {
    VkDevice device = context_->device();

    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }
    if (compute_descriptor_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, compute_descriptor_set_layout_, nullptr);
        compute_descriptor_set_layout_ = VK_NULL_HANDLE;
    }
    if (graphics_descriptor_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, graphics_descriptor_set_layout_, nullptr);
        graphics_descriptor_set_layout_ = VK_NULL_HANDLE;
    }
    if (compute_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, compute_pipeline_layout_, nullptr);
        compute_pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (graphics_pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, graphics_pipeline_layout_, nullptr);
        graphics_pipeline_layout_ = VK_NULL_HANDLE;
    }

    images_in_flight_.clear();
}

void VulkanApp::step_frame() {
    VkDevice device = context_->device();
    VkFence   fence  = frame_fences_[current_frame_];

    // 等待 GPU 完成上一帧
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &fence);

    // CPU 开始准备资源
    context_->reset_command_pool();
    update_ubos();
    storage_image_->set_layout(VK_IMAGE_LAYOUT_UNDEFINED);

    // 请求 swapchain 图像
    uint32_t image_index =
        context_->acquire_next_image(image_available_semaphores_[current_frame_]);

    if (images_in_flight_[image_index] != VK_NULL_HANDLE) {
        vkWaitForFences(device, 1, &images_in_flight_[image_index],
                        VK_TRUE, UINT64_MAX);
    }
    images_in_flight_[image_index] = fence;

    // 录制命令
    CommandBuffer cmd(context_->allocate_command_buffer());
    cmd.begin();

    // GPU Compute Pass
    // Storage image: UNDEFINED → GENERAL (compute shader write)
    {
        VkImageMemoryBarrier2 b = storage_image_->create_barrier(
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_NONE,
            VK_ACCESS_2_SHADER_WRITE_BIT);
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        cmd.pipeline_barrier2(dep);
        storage_image_->set_layout(VK_IMAGE_LAYOUT_GENERAL);
    }

    cmd.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE,
                      compute_pipeline_->pipeline());
    cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_COMPUTE,
                             compute_pipeline_layout_, 0,
                             {compute_descriptor_set_});
    cmd.dispatch((context_->window_width()  + 15) / 16,
                 (context_->window_height() + 15) / 16, 1);

    // Barrier: COMPUTE → GRAPHICS
    {
        VkImageMemoryBarrier2 b = storage_image_->create_barrier(
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_WRITE_BIT,
            VK_ACCESS_2_SHADER_READ_BIT);
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        cmd.pipeline_barrier2(dep);
        storage_image_->set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }


    // Swapchain image: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
    {
        VkImageMemoryBarrier2 b{};
        b.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        b.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        b.srcAccessMask = VK_ACCESS_2_NONE;
        b.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        b.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b.image         = context_->swapchain_image(image_index);
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        cmd.pipeline_barrier2(dep);
    }

    // GPU Graphics Pass
    VkRenderingAttachmentInfo color_attachment{};
    color_attachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView   = context_->swapchain_view(image_index);
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue  = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

    VkRenderingInfo rendering_info{};
    rendering_info.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea           = {{0, 0}, context_->swapchain_extent()};
    rendering_info.layerCount           = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments    = &color_attachment;

    cmd.begin_rendering(rendering_info);
    cmd.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphics_pipeline_->pipeline());
    cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                             graphics_pipeline_layout_, 0,
                             {graphics_descriptor_set_});
    cmd.draw(3);  // fullscreen triangle
    cmd.end_rendering();

    // Swapchain image: COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR
    {
        VkImageMemoryBarrier2 b{};
        b.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        b.dstStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_2_NONE;
        b.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b.image         = context_->swapchain_image(image_index);
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        cmd.pipeline_barrier2(dep);
    }

    cmd.end();

    // Submit and present
    context_->submit(cmd.handle(),
                     image_available_semaphores_[current_frame_],
                     render_finished_semaphores_[current_frame_],
                     fence);
    context_->present(image_index, render_finished_semaphores_[current_frame_]);

    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}
