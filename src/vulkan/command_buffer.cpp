//
// Created by neuroil on 2026/6/5.
//

#include "command_buffer.h"

CommandBuffer::CommandBuffer(VkCommandBuffer handle) : handle_(handle) {}

CommandBuffer::CommandBuffer(CommandBuffer&& other) noexcept : handle_(other.handle_) {
    other.handle_ = VK_NULL_HANDLE;
}

CommandBuffer& CommandBuffer::operator=(CommandBuffer&& other) noexcept {
    if (this != &other) {
        handle_       = other.handle_;
        other.handle_ = VK_NULL_HANDLE;
    }
    return *this;
}

VkCommandBuffer CommandBuffer::handle() const { return handle_; }

void CommandBuffer::begin(VkCommandBufferUsageFlags flags) {
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = flags;
    vkBeginCommandBuffer(handle_, &begin_info);
}

void CommandBuffer::end() {
    vkEndCommandBuffer(handle_);
}

void CommandBuffer::bind_pipeline(VkPipelineBindPoint bind_point, VkPipeline pipeline) {
    vkCmdBindPipeline(handle_, bind_point, pipeline);
}

void CommandBuffer::bind_descriptor_sets(VkPipelineBindPoint bind_point,
                                          VkPipelineLayout layout,
                                          uint32_t first_set,
                                          const std::vector<VkDescriptorSet>& sets) {
    vkCmdBindDescriptorSets(handle_, bind_point, layout, first_set,
                            static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
}

void CommandBuffer::bind_vertex_buffer(VkBuffer buffer, VkDeviceSize offset) {
    vkCmdBindVertexBuffers(handle_, 0, 1, &buffer, &offset);
}

void CommandBuffer::dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
    vkCmdDispatch(handle_, group_count_x, group_count_y, group_count_z);
}

void CommandBuffer::begin_rendering(const VkRenderingInfo& rendering_info) {
    vkCmdBeginRendering(handle_, &rendering_info);
}

void CommandBuffer::end_rendering() {
    vkCmdEndRendering(handle_);
}

void CommandBuffer::draw(uint32_t vertex_count, uint32_t instance_count,
                          uint32_t first_vertex, uint32_t first_instance) {
    vkCmdDraw(handle_, vertex_count, instance_count, first_vertex, first_instance);
}

void CommandBuffer::set_viewport(const VkViewport& viewport) {
    vkCmdSetViewport(handle_, 0, 1, &viewport);
}

void CommandBuffer::set_scissor(const VkRect2D& scissor) {
    vkCmdSetScissor(handle_, 0, 1, &scissor);
}

void CommandBuffer::pipeline_barrier2(const VkDependencyInfo& dependency_info) {
    vkCmdPipelineBarrier2(handle_, &dependency_info);
}
