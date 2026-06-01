# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build commands

```bash
# Configure (from repo root)
cmake -S . -B build -G Ninja

# Build
cmake --build build

# Run
./build/src/simulation
```

There are no tests or linting setup yet.

## Architecture

基于 Vulkan 1.3 的广义相对论克尔黑洞光线追踪仿真器。测地线积分运行在 GPU compute shader 上——CPU 端积分器仅用于验证算法正确性（守恒量测试、解析解对标），验证通过后移植到 GLSL compute shader。所有渲染通过 Vulkan 完成（Dynamic Rendering + compute pipeline），GLFW 仅负责窗口管理和输入。

架构采用 **RAII 分层 + 名词/动词分离**（参考 `~/learn/sph_vulkan/plan.md`，经两轮演进）：

```
┌──────────────────────────────────────────────────────────┐
│                       VulkanApp                           │
│  (顶层编排：持有所有名词，每帧指挥动词，数据流 100% 可见)    │
├──────────────────────────────────────────────────────────┤
│  名词层（资源 RAII）          │  动词层（无状态）            │
│  VulkanContext (硬件)        │  CommandBuffer             │
│  GPUBuffer     (缓冲)        │  (纯指令封装，不知业务)      │
│  GPUImage      (图像)        │                            │
│  ComputePipeline             │                            │
│  GraphicsPipeline            │                            │
├──────────────────────────────────────────────────────────┤
│               vulkan 工具命名空间 (纯函数)                  │
└──────────────────────────────────────────────────────────┘

[物理层 — 独立于 Vulkan 抽象]
Camera (ZAMO tetrad) → Scene (黑洞参数, 吸积盘)
  → CPU Integrator (验证用) → GPU geodesic.comp (运行时)
```

### 设计哲学

- **名词/动词严格分离**：资源类（名词）持有 VkHandle、管理生命周期；CommandBuffer（动词）是纯指令封装，不知道"顶点缓冲"、"descriptor set"等业务概念。换个项目——名词复用、动词复用、编排逻辑重写即可。
- **Vulkan 1.3 Dynamic Rendering**：不用 VkRenderPass / VkFramebuffer。管线创建用 `VkPipelineRenderingCreateInfo`，绘制时用 `vkCmdBeginRendering`。架构中不应出现 RenderPass 和 Framebuffer 这两个名词。
- **不做过度设计**：当前一个 compute pipeline、一个 graphics pipeline——不预拆子组件。真需要扩展时，名词/动词分离的架构改起来不痛苦。
- **RAII 全覆盖**：每个 VkHandle 都有明确的 owner 类，构造时创建、析构时销毁、只允许 move。
- **所有权单向无环**：VulkanApp 唯一持有所有组件，下层组件通过 const reference 引用上层（如 GPUBuffer 构造时接收 `const VulkanContext&`）。

---

### 1. vulkan 工具命名空间 — 纯函数层

无状态，不持有任何 VkHandle。所有函数显式接收 `VkDevice`。

```cpp
namespace vulkan_util {

VkShaderModule create_shader_module(VkDevice device, const std::string& path);
uint32_t find_memory_type(const VkPhysicalDeviceMemoryProperties& props,
                          uint32_t type_bits, VkMemoryPropertyFlags required);
VkBuffer create_raw_buffer(VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage);
VkSemaphore create_semaphore(VkDevice device);
VkFence create_fence(VkDevice device, VkFenceCreateFlags flags);

// Command pool 创建与 buffer 分配（供 VulkanContext 内部使用，也可被上层直接调用）
VkCommandPool create_command_pool(VkDevice device, uint32_t queue_family_index,
                                   VkCommandPoolCreateFlags flags);
VkCommandBuffer allocate_command_buffer(VkDevice device, VkCommandPool pool);

} // namespace vulkan_util
```

---

### 2. VulkanContext — 底层硬件抽象（Dynamic Rendering 瘦身版）

**职责**：管理所有"应用全局唯一"的底层硬件资源。不持有 RenderPass / Framebuffer（Dynamic Rendering 不需要这些对象）。内置 command pool 用于分配 CommandBuffer。

**内部创建的资源**：
- VkInstance（含 debug messenger / validation layers）
- VkPhysicalDevice（查询但不持有所有权）
- VkDevice（logical device）+ queue handles
- VkSurfaceKHR
- VkSwapchainKHR + VkImage[] + VkImageView[]（不含 VkFramebuffer[]）
- VkCommandPool（用于分配 CommandBuffer）
- VkPipelineCache
- VkDescriptorPool（共享分配器）

**接口**（const getter）：
```cpp
class VulkanContext {
public:
    explicit VulkanContext(GLFWwindow* window);
    ~VulkanContext();
    VulkanContext(const VulkanContext&) = delete;

    VkInstance       instance()        const;
    VkPhysicalDevice physical_device() const;
    VkDevice         device()          const;
    VkSurfaceKHR     surface()         const;

    VkQueue graphics_queue()     const;
    VkQueue compute_queue()      const;
    VkQueue presentation_queue() const;
    uint32_t queue_family_index() const;

    VkSwapchainKHR swapchain()              const;
    VkImageView    swapchain_view(uint32_t i) const;
    uint32_t       swapchain_view_count()   const;
    VkFormat       swapchain_format()       const;
    VkExtent2D     swapchain_extent()       const;

    VkPipelineCache pipeline_cache()   const;
    VkDescriptorPool descriptor_pool() const;

    const VkPhysicalDeviceMemoryProperties& memory_properties() const;

    uint32_t window_width()  const;
    uint32_t window_height() const;

    // --- Command buffer 分配（每帧从内部 pool 分配） ---
    VkCommandBuffer allocate_command_buffer();
    void reset_command_pool();  // 每帧开始时调用，回收所有 buffer

    // --- Swapchain 帧循环 ---
    uint32_t acquire_next_image(VkSemaphore signal_semaphore);
    void submit(VkCommandBuffer cmd, VkSemaphore wait, VkSemaphore signal);
    void present(uint32_t image_index, VkSemaphore wait_semaphore);
};
```

**设计决策**：
- **没有** VkRenderPass、VkFramebuffer：Vulkan 1.3 Dynamic Rendering 不需要这些对象，帧缓冲在 `vkCmdBeginRendering` 时动态指定
- Command pool 内置：`allocate_command_buffer()` 和 `reset_command_pool()` 由 VulkanContext 管理，CommandBuffer 是每帧临时分配的"速记员"
- Swapchain image views 通过 `swapchain_view(i)` 暴露，供 `vkCmdBeginRendering` 直接使用
- DescriptorPool 共享：避免每个上层组件各自维护 pool
- **不**持有 descriptor set layout 或 pipeline —— 这些是上层应用细节
- 析构顺序：由内而外（command pool → image views → swapchain → surface → device → instance）

---

### 3. GPUBuffer — VkBuffer RAII 包装

**职责**：封装 VkBuffer + VkDeviceMemory，自动管理生命周期。

```cpp
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
```

**设计决策**：
- 构造时通过 `VulkanContext::memory_properties()` 查询合适的内存类型，一步完成 buffer 创建 + 内存分配 + 绑定
- `upload_from_host` 封装 staging buffer → vkCmdCopyBuffer → vkQueueWaitIdle 完整流程
- 被 move 后的对象处于安全空状态（`VK_NULL_HANDLE`）
- 本项目中的用途：vertex buffer（全屏四边形）、uniform buffer（相机/场景参数）、storage buffer（光线初始状态）

---

### 4. GPUImage — VkImage RAII 包装

**职责**：封装 VkImage + VkImageView + VkDeviceMemory，追踪当前布局，提供 barrier 辅助方法。黑洞仿真中 storage image（Compute Write → Graphics Read）是核心数据通道，必须有 RAII 封装。

```cpp
class GPUImage {
public:
    GPUImage(const VulkanContext& context,
             uint32_t width, uint32_t height,
             VkFormat format,
             VkImageUsageFlags usage,
             VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ~GPUImage();

    GPUImage(GPUImage&& other) noexcept;
    GPUImage& operator=(GPUImage&& other) noexcept;

    VkImage     image()     const;
    VkImageView image_view() const;
    VkFormat    format()    const;
    VkExtent2D  extent()    const;

    // 追踪当前布局（构造时 = UNDEFINED 或传入的 initialLayout）
    VkImageLayout current_layout() const;

    // 生成从当前布局到目标布局的 VkImageMemoryBarrier（调用方在 CommandBuffer::PipelineBarrier 中使用）
    // 注意：不会自动更新 m_currentLayout —— 调用方在 PipelineBarrier 执行后手动 update_layout()
    VkImageMemoryBarrier create_barrier(VkImageLayout new_layout,
                                         VkAccessFlags src_access,
                                         VkAccessFlags dst_access) const;

    // 便捷方法：调用 create_barrier 后更新内部布局追踪
    VkImageMemoryBarrier transition_barrier(VkImageLayout new_layout,
                                             VkAccessFlags src_access,
                                             VkAccessFlags dst_access);
    void update_layout(VkImageLayout new_layout);

private:
    VkImage        m_image        = VK_NULL_HANDLE;
    VkImageView    m_image_view   = VK_NULL_HANDLE;
    VkDeviceMemory m_memory       = VK_NULL_HANDLE;
    VkFormat       m_format;
    VkExtent2D     m_extent;
    VkImageLayout  m_current_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkDevice       m_device;  // 析构时需要
};
```

**设计决策**：
- 不内置 VMA（Vulkan Memory Allocator）—— 先用手动 `vkAllocateMemory`，后续按需引入
- `create_barrier()` 不修改内部状态（纯查询），`transition_barrier()` 同步更新布局追踪
- barrier 的执行由 CommandBuffer::PipelineBarrier 完成，GPUImage 不直接操作命令流——保持名词/动词分离
- 本项目用途：storage image（`VK_FORMAT_R32G32B32A32_SFLOAT`，`VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT`）

---

### 5. ComputePipeline — Compute Pipeline RAII 包装

**职责**：封装单个 compute pipeline 的创建和销毁。

```cpp
class ComputePipeline {
public:
    ComputePipeline(const VulkanContext& context,
                    const std::string& shader_path,
                    VkPipelineLayout pipeline_layout,
                    VkPipelineCache pipeline_cache = VK_NULL_HANDLE);
    ~ComputePipeline();

    ComputePipeline(ComputePipeline&&) noexcept;
    ComputePipeline& operator=(ComputePipeline&&) noexcept;

    VkPipeline pipeline() const;
};
```

**设计决策**：
- shader module 在构造函数内部创建 → 用于 pipeline 创建 → 立即销毁（pipeline 内部已保留编译后的 shader）
- 不持有 pipeline layout 或 descriptor set —— 这些由 VulkanApp 统一管理
- 本项目只需要 **1 个** compute pipeline（geodesic.comp）

---

### 6. GraphicsPipeline — Graphics Pipeline RAII 包装（Dynamic Rendering）

**职责**：封装 graphics pipeline 的创建。使用 Vulkan 1.3 Dynamic Rendering——配置中不传 VkRenderPass，改用 color attachment format(s)。

```cpp
struct GraphicsPipelineConfig {
    std::string vertex_shader_path;
    std::string fragment_shader_path;

    // Dynamic Rendering: 不传 VkRenderPass，传颜色附件格式列表
    std::vector<VkFormat> color_attachment_formats;  // e.g. { swapchain_format }

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipelineCache  pipeline_cache  = VK_NULL_HANDLE;

    // 顶点输入
    std::vector<VkVertexInputBindingDescription>   vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> vertex_attributes;

    // 图元
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkBool32            primitive_restart = VK_FALSE;

    // 视口
    VkExtent2D viewport_extent{0, 0};

    // 光栅化
    VkPolygonMode   polygon_mode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cull_mode    = VK_CULL_MODE_NONE;
    VkFrontFace     front_face   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // 混合（单 attachment，默认不混合）
    VkPipelineColorBlendAttachmentState color_blend_attachment{};
};

class GraphicsPipeline {
public:
    GraphicsPipeline(const VulkanContext& context,
                     const GraphicsPipelineConfig& config);
    ~GraphicsPipeline();

    GraphicsPipeline(GraphicsPipeline&&) noexcept;
    GraphicsPipeline& operator=(GraphicsPipeline&&) noexcept;

    VkPipeline pipeline() const;
};
```

**设计决策**：
- **没有** `VkRenderPass render_pass` 字段——Dynamic Rendering 下管线创建通过 `VkPipelineRenderingCreateInfo`（pNext 链）传入颜色格式
- 内部使用 `VkPipelineRenderingCreateInfo` 填充格式列表，附加到 `VkGraphicsPipelineCreateInfo::pNext`
- 配置结构体有合理默认值，本项目场景只需填 shader 路径、color_attachment_formats、viewport_extent、vertex layout
- 本项目用于渲染全屏四边形（采样 storage image → swapchain）

---

### 7. CommandBuffer — 通用命令缓冲包装（动词类）

**职责**：纯粹的、无状态的 Vulkan 命令封装。不持有任何业务逻辑——不知道什么是"顶点缓冲"、"descriptor set"、"测地线积分"。只提供 Vulkan API 的薄封装，让上层编排逻辑 100% 可见。

```cpp
class CommandBuffer {
public:
    // 从 VkCommandBuffer handle 构造（由 VulkanContext::allocate_command_buffer 调用）
    explicit CommandBuffer(VkCommandBuffer handle);
    ~CommandBuffer() = default;  // 不释放 handle —— 由 pool reset 统一回收

    CommandBuffer(CommandBuffer&&) noexcept;
    CommandBuffer& operator=(CommandBuffer&&) noexcept;

    VkCommandBuffer handle() const;

    // --- 生命周期 ---
    void begin(VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    void end();

    // --- 管线绑定 ---
    void bind_pipeline(VkPipelineBindPoint bind_point, VkPipeline pipeline);
    void bind_descriptor_sets(VkPipelineBindPoint bind_point,
                               VkPipelineLayout layout,
                               uint32_t first_set,
                               const std::vector<VkDescriptorSet>& sets);

    // --- Compute ---
    void dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);

    // --- Dynamic Rendering（替代 RenderPass Begin/End） ---
    void begin_rendering(const VkRenderingInfo& rendering_info);
    void end_rendering();

    // --- Graphics ---
    void bind_vertex_buffer(VkBuffer buffer, VkDeviceSize offset = 0);
    void draw(uint32_t vertex_count, uint32_t instance_count = 1,
              uint32_t first_vertex = 0, uint32_t first_instance = 0);
    void set_viewport(const VkViewport& viewport);
    void set_scissor(const VkRect2D& scissor);

    // --- 同步屏障（黑洞仿真核心：Compute Write → Graphics Read） ---
    void pipeline_barrier(VkPipelineStageFlags src_stage,
                          VkPipelineStageFlags dst_stage,
                          const std::vector<VkBufferMemoryBarrier>& buffer_barriers,
                          const std::vector<VkImageMemoryBarrier>& image_barriers);

private:
    VkCommandBuffer m_handle = VK_NULL_HANDLE;
};
```

**设计决策**：
- **完全通用**：一个纯函数集合，可复用到任何 Vulkan 项目（流体、布料、光线追踪……）
- **不知业务**：方法签名只有 Vulkan 原生类型——`bind_vertex_buffer(VkBuffer)` 而非 `bind_vertex_buffer(GPUBuffer&)`
- 析构时不释放 VkCommandBuffer（handle 由 command pool 的 reset 统一回收）
- 每帧从 VulkanContext 分配新的 CommandBuffer，录制完成后通过 `vkResetCommandPool` 回收——不需要 `VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT`
- `begin_rendering` / `end_rendering` 直接映射 `vkCmdBeginRendering` / `vkCmdEndRendering`，调用方在栈上构建 `VkRenderingInfo`

---

### 8. VulkanApp — 顶层编排

**职责**：持有所有名词（资源），每帧指挥动词（CommandBuffer），数据流 100% 可见。

```cpp
class VulkanApp {
public:
    explicit VulkanApp();
    ~VulkanApp();
    void run();

private:
    // 窗口
    GLFWwindow* m_window = nullptr;

    // ── 名词层：所有 GPU 资源 ──
    std::unique_ptr<VulkanContext>    m_context;
    std::unique_ptr<GPUBuffer>        m_vertex_buffer;         // 全屏四边形 (6 vertices)
    std::unique_ptr<GPUBuffer>        m_uniform_buffer;        // 相机 + 场景参数 UBO
    std::unique_ptr<GPUBuffer>        m_ray_buffer;            // 光线初始状态 storage buffer
    std::unique_ptr<GPUImage>         m_storage_image;         // 黑洞画布: Compute Write → Graphics Read
    std::unique_ptr<ComputePipeline>  m_compute_pipeline;      // geodesic.comp
    std::unique_ptr<GraphicsPipeline> m_graphics_pipeline;     // 全屏四边形采样 → swapchain

    // 少数裸 handle（量太少，不值得单独包装类；VulkanApp 负责生命周期）
    VkDescriptorSetLayout m_compute_descriptor_set_layout;
    VkDescriptorSet       m_compute_descriptor_set;       // bind: ray buffer + storage image + UBO
    VkDescriptorSetLayout m_graphics_descriptor_set_layout;
    VkDescriptorSet       m_graphics_descriptor_set;       // bind: storage image sampler
    VkPipelineLayout      m_compute_pipeline_layout;
    VkPipelineLayout      m_graphics_pipeline_layout;
    VkSemaphore           m_image_available_semaphore;
    VkSemaphore           m_render_finished_semaphore;

    // 物理层
    Camera m_camera;
    Scene  m_scene;
};
```

**每帧流程**——名词/动词分离后，编排逻辑即文档：

```cpp
void VulkanApp::step_frame() {
    // 0. 准备
    m_context->reset_command_pool();
    update_uniform_buffer_and_ray_data();    // CPU: 更新 UBO + 光线初始状态

    CommandBuffer cmd(m_context->allocate_command_buffer());
    cmd.begin();

    // ═══════════ PASS 1: COMPUTE ═══════════
    // Storage image: UNDEFINED → GENERAL (compute write)
    cmd.pipeline_barrier(
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        {},
        { m_storage_image->transition_barrier(
            VK_IMAGE_LAYOUT_GENERAL,
            0,
            VK_ACCESS_SHADER_WRITE_BIT) });

    cmd.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, m_compute_pipeline->pipeline());
    cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_COMPUTE,
                              m_compute_pipeline_layout, 0,
                              { m_compute_descriptor_set });
    cmd.dispatch(ceil_div(m_context->window_width(),  16),
                 ceil_div(m_context->window_height(), 16), 1);

    // ═══════════ PASS 2: GRAPHICS (Dynamic Rendering) ═══════════
    // Storage image: GENERAL → SHADER_READ_ONLY_OPTIMAL (fragment shader sample)
    cmd.pipeline_barrier(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        {},
        { m_storage_image->transition_barrier(
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT) });

    uint32_t image_index = m_context->acquire_next_image(m_image_available_semaphore);

    // Dynamic Rendering: 直接在 swapchain image view 上渲染，没有 RenderPass/Framebuffer
    VkRenderingInfo rendering_info = build_rendering_info(
        m_context->swapchain_view(image_index),
        m_context->swapchain_extent());
    cmd.begin_rendering(rendering_info);

    cmd.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphics_pipeline->pipeline());
    cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_graphics_pipeline_layout, 0,
                              { m_graphics_descriptor_set });
    cmd.bind_vertex_buffer(m_vertex_buffer->buffer());
    cmd.draw(6);  // 全屏四边形

    cmd.end_rendering();
    cmd.end();

    // 3. 提交与呈现（semaphore 同步，无 vkQueueWaitIdle）
    m_context->submit(cmd.handle(), m_image_available_semaphore, m_render_finished_semaphore);
    m_context->present(image_index, m_render_finished_semaphore);
}
```

**关键点**：
- 整个渲染管线的数据流在上层代码中一目了然——compute 写什么 image、barrier 怎么转、graphics 读什么 image
- 没有哪个"封装类"藏了业务逻辑——`CommandBuffer` 只是把 Vulkan API 调用翻译成方法调用
- 将来换一个物理仿真项目，`VulkanContext`、`GPUBuffer`、`GPUImage`、`GraphicsPipeline`、`ComputePipeline`、`CommandBuffer` 全部原样复用

---

### 所有权关系

```
VulkanApp ──owns──► VulkanContext
                 ├─ GPUBuffer × 3 (vertex + uniform + ray storage)
                 ├─ GPUImage  × 1 (storage image — 黑洞画布)
                 ├─ ComputePipeline
                 ├─ GraphicsPipeline
                 ├─ VkDescriptorSetLayout × 2 (raw)
                 ├─ VkDescriptorSet × 2 (raw)
                 ├─ VkPipelineLayout × 2 (raw)
                 ├─ VkSemaphore × 2 (raw)
                 ├─ Camera
                 └─ Scene

CommandBuffer ── 临时对象，每帧从 VulkanContext command pool 分配，pool reset 时回收
```

### 初始化顺序

```
 1. GLFW window
 2. VulkanContext              ─── 底层硬件全部就绪（含 command pool, descriptor pool）
 3. GPUBuffer (vertex)         ─── 全屏四边形顶点数据 + upload
 4. GPUBuffer (uniform)        ─── 相机/场景 UBO
 5. GPUBuffer (ray storage)    ─── 光线初始状态 storage buffer
 6. GPUImage (storage)         ─── 黑洞画布
 7. 创建 compute descriptor set layout + 分配 descriptor set + 更新
 8. 创建 compute pipeline layout
 9. ComputePipeline            ─── geodesic.comp
10. 创建 graphics descriptor set layout + 分配 descriptor set
11. 创建 graphics pipeline layout
12. GraphicsPipeline           ─── 全屏四边形采样（Dynamic Rendering）
13. 创建 2 个 VkSemaphore
```

### 析构顺序（`~VulkanApp` 成员声明逆序自动保证）

```
 1. vkDeviceWaitIdle
 2. 销毁 semaphore
 3. 销毁 descriptor set layout + pipeline layout
 4. 销毁 pipeline（ComputePipeline / GraphicsPipeline 析构）
 5. 销毁 GPUImage
 6. 销毁 GPUBuffer × 3
 7. 销毁 VulkanContext（command pool → image views → swapchain
                        → surface → device → instance）
```

RAII 保证初始化中途异常时已创建的资源被正确清理。

---

### 物理层（独立于 Vulkan 抽象）

物理层不依赖任何 Vulkan handle，可独立开发和测试：

- **Metric**（纯虚接口）：定义度规张量 $g_{\mu\nu}$ 及其逆 $g^{\mu\nu}$
- **Kerr**（实现 Metric）：Kerr 时空度规，参数为质量 $M$ 和角动量 $a$
- **Christoffel**：从度规计算 Christoffel 符号 $\Gamma^\mu_{\alpha\beta}$（解析式 + 数值差分交叉验证）
- **Integrator**（CPU 端）：RK4/RK45 积分器，通过 `std::function` 注入步进函数，用于验证守恒量
- **Camera**（ZAMO tetrad）：Boyer-Lindquist 坐标下的轨道相机，为每像素生成初始光线四动量
- **Scene**：黑洞参数 (M, a)、吸积盘参数、天空背景

**数据流（运行时）**：

```
[CPU: Camera 生成每像素初始光线状态 → upload to GPUBuffer (ray storage)]
  → [GPU Compute: geodesic.comp — 每像素并行积分测地线方程]
  → [终止判定 + 颜色映射 (compute shader 内部)]
  → [GPU Storage Image: vec4 颜色]
  → [GPU Graphics: Dynamic Rendering 全屏四边形采样 → swapchain present]
```

## Key conventions

- **Vulkan 1.3 Dynamic Rendering**：不使用 VkRenderPass / VkFramebuffer。管线创建用 `VkPipelineRenderingCreateInfo`，绘制时用 `vkCmdBeginRendering` / `vkCmdEndRendering`。
- **GLM** 是数学库，列主序。GPU 端 GLSL 用相同语法，减少 CPU/GPU 代码分歧。
- **颜色直接用 `glm::vec4`**，与 GPU 端 `vec4` 和 Vulkan 纹理格式 `VK_FORMAT_R32G32B32A32_SFLOAT` 天然对齐，无需包装层。
- **测地线积分跑在 GPU 上** —— 运行时：CPU 生成初始光线状态 → upload → GPU compute shader 并行积分 → storage image → graphics pipeline 显示。CPU 端积分器仅用于验证，不进入运行时热路径。
- **GLFW 仅管理窗口和输入** —— 创建窗口、提供 Vulkan surface（`glfwCreateWindowSurface`）、转发键盘/鼠标事件。不创建 OpenGL 上下文，不参与渲染。
- **物理对象无状态** —— Metric、Integrator、Camera 将状态作为参数传入，不存储可变状态。
- **`std::function` 用于可扩展性** —— 积分器调用可替换的步进函数（RK4 → RK45 无需修改调用方代码）。
- **构建时编译 shader** —— CMake `add_custom_command` 调用 `glslc` 将 `.vert`/`.frag`/`.comp` 编译为 `.spv`。需要 Vulkan SDK 在 PATH 中。
- **名词/动词严格分离** —— 资源类（名词）持有 VkHandle、管理生命周期、不包含命令录制逻辑。CommandBuffer（动词）是纯 Vulkan 指令封装，不包含业务语义。上层编排代码即文档。
- **RAII 全覆盖** —— 所有 VkHandle 由明确的 owner 类持有，构造创建、析构销毁、只允许 move。裸 handle 仅在 VulkanApp 中少量存在（descriptor set layout / pipeline layout / semaphore）。
- **VulkanContext 是硬件查询的唯一入口** —— 其他组件通过 `const VulkanContext&` 获取 device、memory properties 等，不自行查询物理设备。
- **Pipeline 配置用 struct + 默认值** —— `GraphicsPipelineConfig` 所有字段有合理默认值，调用方只填需要覆盖的字段。
- **不做过度设计** —— 当前只有一个 storage image、一个 compute pipeline，不为未来多 pass 预留抽象。名词/动词分离的架构扩展时改编排逻辑即可，不动封装类。
- **`docs/devlog.md`** 记录各阶段实现笔记。新计划和实现笔记追加在文件末尾。

## Current state

项目正在从旧的单体 Vulkan 实现重写为 RAII 分层 + Dynamic Rendering 架构。CMake 骨架已搭好（Vulkan + GLFW + GLM），源文件仅有 hello world 的 `main.cpp`。

| 阶段 | 描述 | 状态 |
|------|------|------|
| Stage 1 | VulkanContext + vulkan 工具命名空间: instance, device, validation layers, debug messenger, surface, swapchain (Dynamic Rendering — 无 RenderPass/Framebuffer), command pool, descriptor pool | 待实现 |
| Stage 2 | GPUBuffer + GPUImage: RAII buffer 和 image 包装, staging upload, layout transition + barrier 辅助 | 待实现 |
| Stage 3 | ComputePipeline + GraphicsPipeline: RAII pipeline 包装, GraphicsPipelineConfig (VkPipelineRenderingCreateInfo — 不传 RenderPass) | 待实现 |
| Stage 4 | CommandBuffer: 通用命令缓冲包装 (begin/end, bind_pipeline, bind_descriptor_sets, dispatch, begin_rendering/end_rendering, draw, pipeline_barrier) | 待实现 |
| Stage 5 | VulkanApp: 顶层编排, 组件持有与初始化, 每帧 Dynamic Rendering 流程 (compute → barrier → graphics) | 待实现 |
| Stage 6 | 物理基础验证 (CPU 端): Kerr 度规, Christoffel 符号, RK4/RK45 积分器, 守恒量测试 | 待实现 |
| Stage 7 | GPU compute 集成: geodesic.comp, storage image, descriptor sets, camera/scene UBO, 光线初始状态 buffer | 待实现 |
| Stage 8 | 主循环/交互: 输入处理, 渐进式渲染, 高分辨率 PNG 导出 | 待实现 |

### 项目文件布局（全部阶段完成后）

```
blackhole_prac/
├── CMakeLists.txt
├── CLAUDE.md
├── docs/
│   └── devlog.md
├── include/
│   ├── vulkan/
│   │   ├── vulkan_util.h          # 纯函数工具命名空间
│   │   ├── vulkan_context.h       # 底层硬件抽象 (Dynamic Rendering)
│   │   ├── gpu_buffer.h           # VkBuffer RAII
│   │   ├── gpu_image.h            # VkImage RAII (storage image + barrier)
│   │   ├── compute_pipeline.h     # Compute pipeline RAII
│   │   ├── graphics_pipeline.h    # Graphics pipeline RAII + config struct
│   │   ├── command_buffer.h       # 通用命令缓冲包装 (纯动词, 不知业务)
│   │   └── vulkan_app.h           # 顶层编排
│   ├── physics/
│   │   ├── metric.h               # 度规纯虚接口
│   │   ├── kerr.h                 # Kerr 度规
│   │   ├── geodesic.h             # 测地线方程
│   │   └── integrator.h           # CPU 端积分器 (RK4/RK45)
│   ├── scene/
│   │   ├── camera.h               # ZAMO tetrad 相机
│   │   ├── scene.h                # 场景参数
│   │   └── accretion_disk.h       # 吸积盘模型
│   └── control.h                  # 输入处理
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── vulkan/
│   │   ├── vulkan_util.cpp
│   │   ├── vulkan_context.cpp
│   │   ├── gpu_buffer.cpp
│   │   ├── gpu_image.cpp
│   │   ├── compute_pipeline.cpp
│   │   ├── graphics_pipeline.cpp
│   │   ├── command_buffer.cpp
│   │   └── vulkan_app.cpp
│   ├── physics/
│   │   ├── kerr.cpp
│   │   ├── geodesic.cpp
│   │   └── integrator.cpp
│   ├── scene/
│   │   ├── camera.cpp
│   │   ├── scene.cpp
│   │   └── accretion_disk.cpp
│   └── control.cpp
├── test/
│   ├── CMakeLists.txt
│   ├── test_kerr.cpp
│   ├── test_geodesic.cpp
│   └── test_integrator.cpp
└── shaders/
    ├── fullscreen.vert
    ├── fullscreen.frag
    └── geodesic.comp
```

## Planning workflow

当用户要求规划下一阶段的开发时：

1. **只读分析**：阅读当前源文件以理解代码库状态。不要修改任何代码。
2. **更新 docs/devlog.md**：在 `docs/devlog.md` 末尾追加新阶段计划，遵循已有格式（时间戳标题 `## YYYY-MM-DD`，然后是目标、设计理由、文件变更、关键要点、验证方法等部分）。
3. **向用户汇报**：输出简洁的总结，说明计划内容。

**重要**：规划阶段只能修改 `docs/devlog.md` 和 `CLAUDE.md`。在用户明确要求实现之前，不得修改 `include/` 和 `src/` 下的源文件。
