//
// Created by neuroil on 2026/6/1.
//

#include "vulkan_app.h"
#include "vulkan_util.h"
#include "command_buffer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>

VulkanApp::VulkanApp(uint32_t w, uint32_t h) : context_(nullptr) {
    // 硬件初始化
    context_ = std::make_unique<VulkanContext>(w, h);
    window_ = context_->window();

    // GLFW 输入回调 — 用 user pointer 回找 VulkanApp 实例
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, key_callback);
    glfwSetMouseButtonCallback(window_, mouse_button_callback);
    glfwSetCursorPosCallback(window_, cursor_pos_callback);
    glfwSetScrollCallback(window_, scroll_callback);

    // GPU Buffer
    // CameraUBO: vec4 camPos(16) + mat4 invViewProj(64) + float stepSize(4) +
    // int maxSteps(4) + float escapeRadius(4) + float pad(4) = 96 bytes
    camera_ubo_ = std::make_unique<GPUBuffer>(
        context_.get(), 96,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // SpacetimeUBO: vec4 bhParams(16) + vec4 integratorParams(16) +
    // vec4 diskParams(16) = 48 bytes
    spacetime_ubo_ = std::make_unique<GPUBuffer>(
        context_.get(), 48,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Storage Image
    storage_image_ = std::make_unique<GPUImage>(
        context_.get(), w, h,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    // Sampler（graphics pass 采样 storage image 用）
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(context_->device(), &sampler_info, nullptr,
                        &storage_sampler_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create storage sampler!");
    }

    // Descriptor Set Layout

    // Compute：binding 0=CameraUBO, 1=SpacetimeUBO, 2=StorageImage
    {
        VkDescriptorSetLayoutBinding bindings[3]{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; // CameraUBO
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; // SpacetimeUBO
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; // outImage
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        create_descriptor_set_layout(
            &compute_descriptor_set_layout_, bindings, 3); // ← 改为 3
    }

    // Graphics：binding 0=CombinedImageSampler（fragment shader 采样）
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        create_descriptor_set_layout(
            &graphics_descriptor_set_layout_, &binding, 1);
    }
    // Pipeline Layout
    {
        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &compute_descriptor_set_layout_;
        if (vkCreatePipelineLayout(context_->device(), &layout_info, nullptr,
                                   &compute_pipeline_layout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create compute pipeline layout!");
        }
    }
    {
        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &graphics_descriptor_set_layout_;
        if (vkCreatePipelineLayout(context_->device(), &layout_info, nullptr,
                                   &graphics_pipeline_layout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create graphics pipeline layout!");
        }
    }

    // Pipeline
    compute_pipeline_ = std::make_unique<ComputePipeline>(
        context_.get(),
        "shaders/geodesic_minimal.comp.spv",
        compute_pipeline_layout_);

    {
        GraphicsPipelineConfig config{};
        config.vertex_shader_path = "shaders/fullscreen.vert.spv";
        config.fragment_shader_path = "shaders/fullscreen.frag.spv";
        config.color_attachment_formats = {context_->swapchain_format()};
        config.pipeline_layout = graphics_pipeline_layout_;
        config.pipeline_cache = context_->pipeline_cache();
        config.viewport_extent = context_->swapchain_extent();
        // fullscreen triangle：无顶点输入，vertex_bindings / vertex_attributes 留空
        graphics_pipeline_ = std::make_unique<GraphicsPipeline>(
            context_.get(), config);
    }

    // Descriptor Pool
    {
        VkDescriptorPoolSize pool_sizes[3]{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = 2; // camera + spacetime
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[1].descriptorCount = 1; // compute 写
        pool_sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[2].descriptorCount = 1; // graphics 读

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 3;
        pool_info.pPoolSizes = pool_sizes;
        pool_info.maxSets = 2; // compute set + graphics set

        if (vkCreateDescriptorPool(context_->device(), &pool_info, nullptr,
                                   &descriptor_pool_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool!");
        }
    }

    // 描述集
    create_descriptor_sets();

    // 同步原语
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
        vkDestroyFence(context_->device(), frame_fences_[i], nullptr);
    }
    for (VkSemaphore sem : render_finished_semaphores_) {
        vkDestroySemaphore(context_->device(), sem, nullptr);
    }
}

void VulkanApp::run() {
    main_loop();
}

void VulkanApp::create_sync_objects() {
    uint32_t image_count = context_->swapchain_view_count();

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        image_available_semaphores_[i] = vulkan_util::create_semaphore(context_->device());
        frame_fences_[i] = vulkan_util::create_fence(
            context_->device(),
            VK_FENCE_CREATE_SIGNALED_BIT // 首帧直接通过
        );
    }

    // render_finished: 每 swapchain image 一个，避免 present 异步持有时复用
    render_finished_semaphores_.resize(image_count);
    for (uint32_t i = 0; i < image_count; i++) {
        render_finished_semaphores_[i] = vulkan_util::create_semaphore(context_->device());
    }

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
        camera_info.range = camera_ubo_->size();

        VkDescriptorBufferInfo spacetime_info{};
        spacetime_info.buffer = spacetime_ubo_->buffer();
        spacetime_info.offset = 0;
        spacetime_info.range = spacetime_ubo_->size();

        VkDescriptorImageInfo storage_info{};
        storage_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storage_info.imageView = storage_image_->image_view();
        storage_info.sampler = VK_NULL_HANDLE;

        VkWriteDescriptorSet writes[3]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = compute_descriptor_set_;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &camera_info;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = compute_descriptor_set_;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].pBufferInfo = &spacetime_info;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = compute_descriptor_set_;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[2].pImageInfo = &storage_info;

        vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
    }

    // 分配与更新 graphics 描述符集
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
        storage_info.imageView = storage_image_->image_view();
        storage_info.sampler = storage_sampler_;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = graphics_descriptor_set_;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &storage_info;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
}

void VulkanApp::update_ubos() {
    float w = static_cast<float>(context_->window_width());
    float h = static_cast<float>(context_->window_height());
    float aspect = (h > 0.0f) ? (w / h) : 1.0f;

    // ── 场景参数 ──
    float M = 1.0f;
    float a = 0.0f;
    float a2 = a * a;
    float horizon_r = M + std::sqrt(M * M - a2);
    float inner_r   = M - std::sqrt(M * M - a2);

    // ── FPV Camera ──
    // forward from yaw/pitch: 绕 +z 的方位角 + 离 xy-plane 的抬角
    float cy = std::cos(camera_.pitch);
    glm::vec3 forward(
        cy * std::cos(camera_.yaw),
        cy * std::sin(camera_.yaw),
        std::sin(camera_.pitch)
    );
    glm::vec3 worldUp(0.0f, 0.0f, 1.0f);  // 黑洞自转轴
    glm::vec3 eye = camera_.position;
    glm::vec3 center = eye + forward;       // 看向前方

    // ── 相机笛卡尔坐标 (shader 接受 Cartesian) ──
    glm::vec3 cam_cartesian = eye;

    // ── 视图 + 投影矩阵 ──
    glm::mat4 view = glm::lookAt(eye, center, worldUp);
    glm::mat4 proj = glm::perspective(glm::radians(camera_.fov), aspect, 0.1f, 1000.0f);
    // Vulkan NDC: y 轴向下 (与 OpenGL 相反)，翻转 y
    proj[1][1] *= -1.0f;
    glm::mat4 invVP = glm::inverse(proj * view);

    // camera r (到原点的距离，用于 stepSize / escapeRadius)
    float cam_r = glm::length(cam_cartesian);

    // CameraUBO (std140): vec4 camPos | mat4 invViewProj | float stepSize |
    // int maxSteps | float escapeRadius | float pad
    {
        void* data = camera_ubo_->map();
        float* f = static_cast<float*>(data);

        // camPos (offset 0, 16B): 相机笛卡尔坐标 (x, y, z, 0)
        f[0] = cam_cartesian.x;
        f[1] = cam_cartesian.y;
        f[2] = cam_cartesian.z;
        f[3] = 0.0f;

        // invViewProj (offset 16, 64B): 逆 VP 矩阵 (列主序)
        std::memcpy(f + 4, glm::value_ptr(invVP), 64 * sizeof(float));

        // stepSize (offset 80, 4B)
        f[20] = 0.1f * glm::max(1.0f, cam_r / 15.0f);

        // maxSteps (offset 84, 4B)
        *reinterpret_cast<int32_t*>(&f[21]) = 1024;

        // escapeRadius (offset 88, 4B)
        f[22] = glm::max(50.0f, cam_r * 1.5f);

        // pad (offset 92, 4B)
        f[23] = 0.0f;

        camera_ubo_->unmap();
    }

    // SpacetimeUBO (std140): vec4 bhParams | vec4 integratorParams |
    // vec4 diskParams
    {
        void* data = spacetime_ubo_->map();
        float* f = static_cast<float*>(data);

        // bhParams: M, a, horizon_radius, inner_horizon_radius
        f[0] = M;
        f[1] = a;
        f[2] = horizon_r;
        f[3] = inner_r;

        // integratorParams: tolerance, minStepSize, maxStepSize, safety
        f[4] = 1e-6f;
        f[5] = 0.001f;
        f[6] = 1.0f;
        f[7] = 0.9f;

        // diskParams: inner_r, outer_r, height, pad
        // ISCO prograde ≈ 4.6M for a=0.5, 保守取 5M
        f[8]  = 5.0f;
        f[9]  = 25.0f;
        f[10] = 0.02f;
        f[11] = 0.0f;

        spacetime_ubo_->unmap();
    }
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
    VkFence fence = frame_fences_[current_frame_];

    // 等待 GPU 完成上一帧
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    // CPU 开始准备资源
    context_->reset_command_pool(current_frame_);
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

    // 此时 images_in_flight_ 已更新，安全 reset fence
    vkResetFences(device, 1, &fence);

    // 录制命令
    CommandBuffer cmd(context_->allocate_command_buffer(current_frame_));
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
    cmd.dispatch((context_->window_width() + 15) / 16,
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
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        b.srcAccessMask = VK_ACCESS_2_NONE;
        b.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b.image = context_->swapchain_image(image_index);
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        cmd.pipeline_barrier2(dep);
    }

    // GPU Graphics Pass
    VkRenderingAttachmentInfo color_attachment{};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = context_->swapchain_view(image_index);
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

    VkRenderingInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea = {{0, 0}, context_->swapchain_extent()};
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;

    cmd.begin_rendering(rendering_info);
    cmd.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphics_pipeline_->pipeline());

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(context_->window_width());
    viewport.height = static_cast<float>(context_->window_height());
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd.handle(), 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = context_->swapchain_extent();
    vkCmdSetScissor(cmd.handle(), 0, 1, &scissor);

    cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                             graphics_pipeline_layout_, 0,
                             {graphics_descriptor_set_});
    cmd.draw(3); // fullscreen triangle
    cmd.end_rendering();

    // Swapchain image: COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR
    {
        VkImageMemoryBarrier2 b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_2_NONE;
        b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b.image = context_->swapchain_image(image_index);
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        cmd.pipeline_barrier2(dep);
    }

    cmd.end();

    // Submit and present
    // render_finished 按 image_index 索引: 每个 swapchain image 有独立 semaphore，
    // 避免 current_frame_ 回绕时 presentation engine 还持有旧 semaphore
    context_->submit(cmd.handle(),
                     image_available_semaphores_[current_frame_],
                     render_finished_semaphores_[image_index],
                     fence);
    context_->present(image_index, render_finished_semaphores_[image_index]);

    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

// ══════════════════════════════════════════════════════════════
// GLFW 回调 — FPV Camera 控制
// ══════════════════════════════════════════════════════════════

void VulkanApp::key_callback(GLFWwindow* w, int key, int scancode, int action, int mods) {
    auto* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(w));
    if (!app) return;

    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    // 相机局部坐标系 (forward/right/up in world space)
    float cy = std::cos(app->camera_.pitch);
    glm::vec3 forward(
        cy * std::cos(app->camera_.yaw),
        cy * std::sin(app->camera_.yaw),
        std::sin(app->camera_.pitch)
    );
    glm::vec3 worldUp(0.0f, 0.0f, 1.0f);
    glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    glm::vec3 up    = glm::cross(right, forward);

    float s = app->camera_.move_speed;
    switch (key) {
        case GLFW_KEY_W: app->camera_.position += forward * s; break;
        case GLFW_KEY_S: app->camera_.position -= forward * s; break;
        case GLFW_KEY_A: app->camera_.position -= right   * s; break;
        case GLFW_KEY_D: app->camera_.position += right   * s; break;
        case GLFW_KEY_LEFT_SHIFT: app->camera_.position -= worldUp * s; break;  // 降
        case GLFW_KEY_SPACE: app->camera_.position += worldUp * s; break;  // 升
        default: break;
    }
}

void VulkanApp::mouse_button_callback(GLFWwindow* w, int button, int action, int mods) {
    auto* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(w));
    if (!app) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        app->looking_ = (action == GLFW_PRESS);
        if (action == GLFW_PRESS) {
            glfwGetCursorPos(w, &app->last_mouse_x_, &app->last_mouse_y_);
        }
    }
}

void VulkanApp::cursor_pos_callback(GLFWwindow* w, double xpos, double ypos) {
    auto* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(w));
    if (!app || !app->looking_) return;

    double dx = xpos - app->last_mouse_x_;
    double dy = ypos - app->last_mouse_y_;
    app->last_mouse_x_ = xpos;
    app->last_mouse_y_ = ypos;

    float s = app->camera_.look_sensitivity;
    app->camera_.yaw   -= static_cast<float>(dx) * s;
    app->camera_.pitch -= static_cast<float>(dy) * s;  // GLFW +y=下, 上推鼠标=抬头=pitch+

    // clamp pitch to avoid flip
    const float limit = glm::radians(89.0f);
    app->camera_.pitch = std::max(-limit, std::min(limit, app->camera_.pitch));
}

void VulkanApp::scroll_callback(GLFWwindow* w, double xoffset, double yoffset) {
    auto* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(w));
    if (!app) return;

    app->camera_.fov -= static_cast<float>(yoffset) * 2.0f;
    app->camera_.fov = std::max(10.0f, std::min(120.0f, app->camera_.fov));
}
