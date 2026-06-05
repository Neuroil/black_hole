//
// Created by neuroil on 2026/6/5.
//

#ifndef BLACK_HOLE_PRAC_COMMANDBUFFER_H
#define BLACK_HOLE_PRAC_COMMANDBUFFER_H

#include <vulkan/vulkan_core.h>
#include <vector>

class CommandBuffer {
public:
    explicit CommandBuffer(VkCommandBuffer handle);
    ~CommandBuffer() = default;  // 由 pool reset 统一回收

    CommandBuffer(CommandBuffer&&) noexcept;
    CommandBuffer& operator=(CommandBuffer&&) noexcept;

    VkCommandBuffer handle() const;

    // ── 生命周期 ──
    void begin(VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    void end();

    // ── 管线绑定 ──
    void bind_pipeline(VkPipelineBindPoint bind_point, VkPipeline pipeline);
    void bind_descriptor_sets(VkPipelineBindPoint bind_point,
                               VkPipelineLayout layout,
                               uint32_t first_set,
                               const std::vector<VkDescriptorSet>& sets);
    void bind_vertex_buffer(VkBuffer buffer, VkDeviceSize offset = 0);

    // ── Compute ──
    void dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);

    // ── Dynamic Rendering（替代 RenderPass Begin/End） ──
    void begin_rendering(const VkRenderingInfo& rendering_info);
    void end_rendering();

    // ── Graphics ──
    void draw(uint32_t vertex_count, uint32_t instance_count = 1,
              uint32_t first_vertex = 0, uint32_t first_instance = 0);
    void set_viewport(const VkViewport& viewport);
    void set_scissor(const VkRect2D& scissor);

    // ── 同步屏障（Vulkan 1.3 synchronization2） ──
    void pipeline_barrier2(const VkDependencyInfo& dependency_info);

private:
    VkCommandBuffer handle_ = VK_NULL_HANDLE;
};

#endif // BLACK_HOLE_PRAC_COMMANDBUFFER_H
