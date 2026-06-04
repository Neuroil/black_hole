# 架构设计文档

> 本文档是项目的唯一架构真相源（single source of truth）。CLAUDE.md 引用本文档，devlog.md 仅记录开发过程。

## 概述

基于 Vulkan 1.3 的广义相对论克尔黑洞光线追踪仿真器。渲染采用三阶段管线：CPU 准备（相机更新 + UBO 上传）→ GPU Compute Pass（geodesic.comp 每像素并行积分测地线，写 Storage Image）→ GPU Graphics Pass（fragment shader 读取 Storage Image 呈递到 swapchain）。CPU 端积分器仅用于验证算法正确性（守恒量测试、解析解对标），不进入运行时热路径。所有渲染通过 Vulkan 完成（Dynamic Rendering + compute pipeline），GLFW 仅负责窗口管理和输入。

架构采用 **RAII 分层 + 名词/动词分离**，从底到顶：

```
┌───────────────────────────────────────────────────────────────┐
│                           VulkanApp                           │
│  (顶层编排：持有所有名词，每帧指挥动词，数据流 100% 可见)     │
├───────────────────────────────────────────────────────────────┤
│  名词层（资源 RAII）          │  动词层（无状态）             │
│  VulkanContext (硬件)         │  CommandBuffer                │
│  GPUBuffer     (缓冲)         │  (纯指令封装，不知业务)       │
│  GPUImage      (图像)         │                               │
│  ComputePipeline              │                               │
│  GraphicsPipeline             │                               │
├───────────────────────────────────────────────────────────────┤
│               vulkan_util 工具命名空间 (纯函数)               │
└───────────────────────────────────────────────────────────────┘

[物理层 — 独立于 Vulkan 抽象]
Camera (ZAMO tetrad) → Scene (黑洞参数 M, a, 吸积盘)
  → CPU Integrator (验证用) → GPU geodesic.comp (运行时，计算 Christoffel、
     积分测地线、终止判定、颜色映射)
```

## 设计哲学

1. **名词/动词严格分离**：资源类（名词）持有 VkHandle、管理生命周期；CommandBuffer（动词）是纯指令封装，方法签名只含 Vulkan 原生类型，不知道"顶点缓冲"、"descriptor set"等业务概念。换个项目——名词复用、动词复用、编排逻辑重写即可。
2. **Vulkan 1.3 Dynamic Rendering**：不使用 VkRenderPass / VkFramebuffer。管线创建用 `VkPipelineRenderingCreateInfo`，绘制时用 `vkCmdBeginRendering` / `vkCmdEndRendering`。架构中不应出现 RenderPass 和 Framebuffer 这两个名词。
3. **不做过度设计**：当前一个 compute pipeline、一个 graphics pipeline、一个 storage image——不为此预拆子组件。真需要扩展时，名词/动词分离的架构改编排逻辑即可，不动封装类。
4. **RAII 全覆盖**：每个 VkHandle 都有明确的 owner 类，构造时创建、析构时销毁、只允许 move。裸 handle 仅在 VulkanApp 中少量存在（descriptor set layout / pipeline layout / semaphore / fence）。
5. **所有权单向无环**：VulkanApp 唯一持有所有组件，下层组件通过 const reference 引用上层（如 GPUBuffer 构造时接收 `const VulkanContext&`）。

---

## 组件详细设计

### 1. vulkan_util — 工具命名空间（纯函数层）

无状态，不持有任何 VkHandle。所有函数显式接收 `VkDevice`。

```cpp
namespace vulkan_util {

VkShaderModule create_shader_module(VkDevice device, const std::string& path);
uint32_t find_memory_type(const VkPhysicalDeviceMemoryProperties& props,
                          uint32_t type_bits, VkMemoryPropertyFlags required);
VkBuffer create_raw_buffer(VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage);
VkSemaphore create_semaphore(VkDevice device);
VkFence create_fence(VkDevice device, VkFenceCreateFlags flags);

VkCommandPool create_command_pool(VkDevice device, uint32_t queue_family_index,
                                   VkCommandPoolCreateFlags flags);
VkCommandBuffer allocate_command_buffer(VkDevice device, VkCommandPool pool);

} // namespace vulkan_util
```

**设计决策**：
- 纯函数，零项目依赖——任何 Vulkan 项目可直接复用
- 不持有 VkDevice 或任何状态——调用方传什么 device 就用什么
- Command pool / buffer 函数包含在内（VulkanContext 内部使用），但也可被上层直接调用

---

### 2. VulkanContext — 底层硬件抽象（Dynamic Rendering 瘦身版）

**职责**：管理所有"应用全局唯一"的底层硬件资源。不持有 VkRenderPass / VkFramebuffer（Dynamic Rendering 不需要这些对象）。内置 command pool 用于分配 CommandBuffer。

**GLFW window 所有权**：`VulkanContext` 在构造函数内部调用 GLFW 创建窗口并持有 `GLFWwindow*` 的所有权，析构时负责销毁。`VulkanApp` 通过 `VulkanContext::window()` 获取裸指针引用，仅用于注册输入回调，不负责销毁。

**内部创建的资源**：
- GLFWwindow（持有所有权）
- VkInstance（含 debug messenger / validation layers）
- VkPhysicalDevice（查询但不持有所有权）
- VkDevice（logical device）+ queue handles
- VkSurfaceKHR
- VkSwapchainKHR + VkImage[] + VkImageView[]（不含 VkFramebuffer[]）
- VkCommandPool（用于分配 CommandBuffer）
- VkPipelineCache
- VkDescriptorPool（共享分配器）

**接口**（const getter + 帧操作）：

```cpp
class VulkanContext {
public:
    explicit VulkanContext(uint32_t w, uint32_t h);
    ~VulkanContext();
    VulkanContext(const VulkanContext&) = delete;

    // ── GLFW window ──
    GLFWwindow* window() const;

    // ── 硬件查询（const getter） ──
    VkInstance       instance()        const;
    VkPhysicalDevice physical_device() const;
    VkDevice         device()          const;
    VkSurfaceKHR     surface()         const;

    VkQueue graphics_queue()     const;
    VkQueue compute_queue()      const;
    VkQueue presentation_queue() const;

    // 各 queue 对应的 family index（三者可能相同，也可能不同，取决于硬件）
    uint32_t graphics_queue_family()     const;
    uint32_t compute_queue_family()      const;
    uint32_t presentation_queue_family() const;

    // ── Swapchain（Dynamic Rendering：用 swapchain_view 取代 framebuffer） ──
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

    // ── Command buffer 分配（每帧从内部 pool 分配，pool reset 统一回收） ──
    VkCommandBuffer allocate_command_buffer();
    void reset_command_pool();

    // ── Swapchain 帧循环（semaphore + fence 同步） ──
    uint32_t acquire_next_image(VkSemaphore signal_semaphore);
    void submit(VkCommandBuffer cmd,
                VkSemaphore wait, VkSemaphore signal,
                VkFence signal_fence);          // fence 用于 CPU 侧帧完成等待
    void present(uint32_t image_index, VkSemaphore wait_semaphore);

    // ── Swapchain 重建（窗口 resize 时调用） ──
    // 重建 swapchain + image views；storage image、pipeline、descriptor binding 由 VulkanApp 负责
    void recreate_swapchain(uint32_t new_w, uint32_t new_h);
};
```

**设计决策**：
- **没有** VkRenderPass、VkFramebuffer：Dynamic Rendering 下帧缓冲在 `vkCmdBeginRendering` 时动态指定，swapchain image view 直接作为渲染目标
- **三个独立的 queue family index getter**：`graphics_queue_family()`、`compute_queue_family()`、`presentation_queue_family()`——当前假设三者同 family（多数桌面 GPU），但接口语义明确，支持异构情况而无需改调用方
- Command pool 内置：`allocate_command_buffer()` 和 `reset_command_pool()` 由 VulkanContext 管理，CommandBuffer 是每帧临时分配、pool reset 统一回收的"速记员"
- `submit()` 增加 `VkFence` 参数：调用方（VulkanApp）在帧首 `vkWaitForFences` + `vkResetFences`，确保 GPU 完成后再调用 `reset_command_pool()`
- DescriptorPool 共享：避免每个上层组件各自维护 pool
- `recreate_swapchain()` 预留接口：Stage 8 实现；重建顺序为 `vkDeviceWaitIdle` → 销毁旧 image views / swapchain → 重建；storage image 尺寸跟进、pipeline viewport 更新、descriptor binding 刷新由 VulkanApp 负责
- **不**持有 descriptor set layout 或 pipeline —— 这些是上层应用细节
- 析构顺序（由内而外）：command pool → image views → swapchain → surface → device → instance → GLFW window

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
};
```

**设计决策**：
- 构造时持有 `const VulkanContext*` ——析构时通过 `context_->device()` 获取 device，不再重复持有裸 `VkDevice`
- 构造时通过 `VulkanContext::memory_properties()` 查询合适的内存类型，一步完成 buffer 创建 + 内存分配 + 绑定
- 被 move 后的对象处于安全空状态（指针置 `nullptr`，handle 置 `VK_NULL_HANDLE`）
- 本项目中的用途：`camera_ubo_`（CameraUBO）和 `spacetime_ubo_`（SpacetimeUBO）各一个，具名成员由 VulkanApp 持有

> 不提供 `upload_from_host` —— staging upload 是上层编排职责，不属于 buffer RAII 容器。GPUBuffer 只提供 `map()` / `unmap()`（host-visible buffer），数据上传由 VulkanApp 或独立动词类编排

---

### 4. GPUImage — VkImage RAII 包装

**职责**：封装 VkImage + VkImageView + VkDeviceMemory，追踪当前布局，提供 barrier 辅助方法。黑洞仿真中 storage image 是 compute pass 与 graphics pass 之间的数据通道（compute shader 写入颜色 → fragment shader 读取并输出到 swapchain）。

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
    VkImageLayout current_layout() const;

    // ── Barrier 辅助 ──
    // 纯查询：生成 barrier 结构体，不修改内部状态
    VkImageMemoryBarrier2 create_barrier(VkImageLayout new_layout,
                                         VkPipelineStageFlags2 src_stage,
                                         VkPipelineStageFlags2 dst_stage,
                                         VkAccessFlags2 src_access,
                                         VkAccessFlags2 dst_access) const;

    // 显式更新布局追踪（在对应的 pipeline_barrier2 提交后调用）
    void set_layout(VkImageLayout new_layout);

private:
    VkImage        image_        = VK_NULL_HANDLE;
    VkImageView    image_view_   = VK_NULL_HANDLE;
    VkDeviceMemory memory_       = VK_NULL_HANDLE;
    VkFormat       format_;
    VkExtent2D     extent_;
    VkImageLayout  current_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    const VulkanContext* context_ = nullptr;
};
```

**设计决策**：
- 构造时持有 `const VulkanContext*`，与 GPUBuffer 保持一致
- 不内置 VMA（Vulkan Memory Allocator）——先用手动 `vkAllocateMemory`，后续按需引入
- **布局更新两步分离**：`create_barrier()` 只生成结构体（不修改状态）；调用方在执行完 `cmd.pipeline_barrier2()` 后，显式调用 `set_layout(new_layout)` 更新追踪——确保"内部布局状态与 GPU 实际布局一致"这个不变量在代码层面可见
- 去掉了旧的 `transition_barrier()`（既生成又修改状态，调用方无法感知副作用）
- barrier 的执行由 CommandBuffer::pipeline_barrier2 负责——GPUImage 不直接操作命令流（名词/动词分离）
- **barrier 接口升级到 synchronization2**（`VkImageMemoryBarrier2` + `VkPipelineStageFlags2`）：与架构的 Vulkan 1.3 选型一致，stage/access 粒度更精确

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

    // Dynamic Rendering：不传 VkRenderPass，传颜色附件格式列表
    std::vector<VkFormat> color_attachment_formats;

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
- **没有** `VkRenderPass render_pass` 字段：使用 `VkPipelineRenderingCreateInfo`（pNext 链）传入颜色格式列表
- 内部自动从 `color_attachment_formats` 构建 `VkPipelineRenderingCreateInfo`，附加到 `VkGraphicsPipelineCreateInfo::pNext`
- 配置结构体所有字段有合理默认值，本项目场景只需填 shader 路径、color_attachment_formats、viewport_extent、vertex layout
- 本项目用途：presentation pass —— fragment shader 读取 storage image（compute 输出），写入 swapchain color attachment（Dynamic Rendering）。顶点数据的生成方式为实现细节，不在此设计层规定

---

### 7. CommandBuffer — 通用命令缓冲包装（动词类）

**职责**：纯粹的、无状态的 Vulkan 命令封装。不持有任何业务逻辑——不知道什么是"测地线积分"、"顶点缓冲"、"descriptor set"。只提供 Vulkan API 的薄封装，让上层编排逻辑 100% 可见。

```cpp
class CommandBuffer {
public:
    explicit CommandBuffer(VkCommandBuffer handle);
    ~CommandBuffer() = default;  // 不释放 handle —— 由 pool reset 统一回收

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
```

**设计决策**：
- **完全通用**：方法签名只含 Vulkan 原生类型（`VkBuffer` 而非 `GPUBuffer&`）——零业务耦合，任何 Vulkan 项目直接复用
- 析构时不释放 VkCommandBuffer（handle 由 command pool 的 reset 统一回收）——不持有 pool 引用
- 每帧从 VulkanContext 分配新 CommandBuffer，pool reset 时回收
- `begin_rendering` / `end_rendering` 直接映射 `vkCmdBeginRendering` / `vkCmdEndRendering`
- **`pipeline_barrier2`** 替代旧 `pipeline_barrier`：使用 `VkDependencyInfo` + `VkImageMemoryBarrier2`，与 Vulkan 1.3 synchronization2 对齐，stage/access 粒度更精确，不易写错

---

### 8. VulkanApp — 顶层编排

**职责**：持有所有名词（资源），每帧指挥动词（CommandBuffer），数据流 100% 可见。替代旧架构中散落在 main.cpp 的裸 handle 管理。

```cpp
class VulkanApp {
public:
    explicit VulkanApp();
    ~VulkanApp();
    void run();

private:
    // ── 名词层：所有 GPU 资源 ──
    std::unique_ptr<VulkanContext>    context_;          // 持有 GLFWwindow（窗口所有权在此）

    // UBO 具名成员，意图明确
    std::unique_ptr<GPUBuffer>        camera_ubo_;       // CameraUBO（tetrad, 轨道参数）
    std::unique_ptr<GPUBuffer>        spacetime_ubo_;    // SpacetimeUBO（M, a）

    std::unique_ptr<GPUImage>         storage_image_;    // Compute Write → Graphics Read 数据通道
    std::unique_ptr<ComputePipeline>  compute_pipeline_; // geodesic.comp
    std::unique_ptr<GraphicsPipeline> graphics_pipeline_;// presentation pass → swapchain

    // 少数裸 handle（量太少，不值得单独包装类；VulkanApp 负责生命周期）
    VkDescriptorSetLayout compute_descriptor_set_layout_  = VK_NULL_HANDLE;
    VkDescriptorSet       compute_descriptor_set_         = VK_NULL_HANDLE;
    VkDescriptorSetLayout graphics_descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet       graphics_descriptor_set_        = VK_NULL_HANDLE;
    VkPipelineLayout      compute_pipeline_layout_        = VK_NULL_HANDLE;
    VkPipelineLayout      graphics_pipeline_layout_       = VK_NULL_HANDLE;
    VkSemaphore           image_available_semaphore_      = VK_NULL_HANDLE;
    VkSemaphore           render_finished_semaphore_      = VK_NULL_HANDLE;
    VkFence               frame_fence_                    = VK_NULL_HANDLE; // 帧完成 fence

    // 物理层
    Camera camera_;
    Scene  scene_;
};
```

**每帧流程**（即架构文档——意图直接写在代码里）：

```cpp
void VulkanApp::step_frame() {
    // ═══════════ 帧同步：等待上一帧 GPU 完成 ═══════════
    // 确保 GPU 已完成上一帧，再安全 reset command pool
    vkWaitForFences(context_->device(), 1, &frame_fence_, VK_TRUE, UINT64_MAX);
    vkResetFences(context_->device(), 1, &frame_fence_);

    // ═══════════ 阶段一：CPU 端准备 ═══════════
    context_->reset_command_pool();  // GPU 已确认完成，safe
    // GLFW 捕获输入 → 更新 Camera 状态 → 将 CameraUBO / SpacetimeUBO 写入 GPUBuffer
    update_ubos();

    CommandBuffer cmd(context_->allocate_command_buffer());
    cmd.begin();

    // ═══════════ 阶段二：GPU Compute Pass（测地线积分） ═══════════
    // Storage image: UNDEFINED → GENERAL (compute shader 写入)
    {
        VkImageMemoryBarrier2 b = storage_image_->create_barrier(
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_NONE,                       VK_ACCESS_2_SHADER_WRITE_BIT);
        VkDependencyInfo dep = {};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        cmd.pipeline_barrier2(dep);
        storage_image_->set_layout(VK_IMAGE_LAYOUT_GENERAL);
    }

    cmd.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_->pipeline());
    cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_COMPUTE,
                              compute_pipeline_layout_, 0, { compute_descriptor_set_ });
    // 每像素一个 work item：CameraUBO 生成初始光线 → RK4 积分（查 SpacetimeUBO 计算 Christoffel）
    // → 终止判定（捕获/逃逸/盘面命中）→ imageStore 写 Storage Image
    cmd.dispatch(ceil_div(context_->window_width(),  16),
                 ceil_div(context_->window_height(), 16), 1);

    // ═══════════ 阶段三：GPU Graphics Pass（屏幕呈递） ═══════════
    // Storage image: GENERAL → SHADER_READ_ONLY_OPTIMAL (fragment shader 读取)
    {
        VkImageMemoryBarrier2 b = storage_image_->create_barrier(
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_WRITE_BIT,              VK_ACCESS_2_SHADER_READ_BIT);
        VkDependencyInfo dep = {};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        cmd.pipeline_barrier2(dep);
        storage_image_->set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    uint32_t image_index = context_->acquire_next_image(image_available_semaphore_);

    // Swapchain image: UNDEFINED/PRESENT_SRC_KHR → COLOR_ATTACHMENT_OPTIMAL
    {
        VkImageMemoryBarrier2 b = {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        b.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        b.srcAccessMask = VK_ACCESS_2_NONE;
        b.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        b.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b.image         = context_->swapchain_image(image_index); // 见注①
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkDependencyInfo dep = {};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        cmd.pipeline_barrier2(dep);
    }

    VkRenderingAttachmentInfo color_attachment = {};
    color_attachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView   = context_->swapchain_view(image_index);
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue  = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

    VkRenderingInfo rendering_info = {};
    rendering_info.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea           = {{0, 0}, context_->swapchain_extent()};
    rendering_info.layerCount           = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments    = &color_attachment;

    cmd.begin_rendering(rendering_info);

    // Fragment shader 读取 Storage Image → 输出到 swapchain（当前直通，后续可在此加后处理）
    cmd.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_->pipeline());
    cmd.bind_descriptor_sets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                              graphics_pipeline_layout_, 0, { graphics_descriptor_set_ });
    cmd.draw(vertex_count);

    cmd.end_rendering();

    // Swapchain image: COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR
    {
        VkImageMemoryBarrier2 b = {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        b.dstStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_2_NONE;
        b.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b.image         = context_->swapchain_image(image_index);
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkDependencyInfo dep = {};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        cmd.pipeline_barrier2(dep);
    }

    cmd.end();

    // 提交与呈现（semaphore + fence 同步）
    context_->submit(cmd.handle(),
                     image_available_semaphore_,
                     render_finished_semaphore_,
                     frame_fence_);                // GPU 完成后 signal fence
    context_->present(image_index, render_finished_semaphore_);
}
```

> **注①**：需在 `VulkanContext` 上新增 `VkImage swapchain_image(uint32_t i) const`，暴露 swapchain 的裸 VkImage（用于构造 barrier），与已有的 `swapchain_view()` 并列。

---

## 所有权关系

```
VulkanApp ──owns──► VulkanContext (含 GLFWwindow)
                 ├─ GPUBuffer: camera_ubo_    (CameraUBO)
                 ├─ GPUBuffer: spacetime_ubo_ (SpacetimeUBO)
                 ├─ GPUImage  (storage image — compute → graphics 数据通道)
                 ├─ ComputePipeline
                 ├─ GraphicsPipeline
                 ├─ VkDescriptorSetLayout × 2 (raw)
                 ├─ VkDescriptorSet × 2 (raw)
                 ├─ VkPipelineLayout × 2 (raw)
                 ├─ VkSemaphore × 2 (raw)
                 ├─ VkFence × 1 (frame_fence_, raw)
                 ├─ Camera
                 └─ Scene

CommandBuffer ── 临时对象，每帧从 VulkanContext command pool 分配，pool reset 时回收
```

## 初始化顺序

```
 1. VulkanContext              ─── 底层硬件全部就绪（含 GLFW window、command pool、descriptor pool）
 2. GPUBuffer: camera_ubo_    ─── CameraUBO（host-visible）
 3. GPUBuffer: spacetime_ubo_ ─── SpacetimeUBO（host-visible）
 4. GPUImage (storage)         ─── compute → graphics 数据通道
 5. 创建 compute descriptor set layout + 分配 descriptor set + 更新
 6. 创建 compute pipeline layout
 7. ComputePipeline            ─── geodesic.comp
 8. 创建 graphics descriptor set layout + 分配 descriptor set
 9. 创建 graphics pipeline layout
10. GraphicsPipeline           ─── presentation pass（Dynamic Rendering, 传 swapchain format）
11. 创建 2 个 VkSemaphore + 1 个 VkFence（VK_FENCE_CREATE_SIGNALED_BIT，首帧直接通过）
```

## 析构顺序

由 C++ 成员声明逆序自动保证（`~VulkanApp`）：

```
 1. vkDeviceWaitIdle
 2. 销毁 fence + semaphore
 3. 销毁 descriptor set layout + pipeline layout
 4. 销毁 pipeline（ComputePipeline / GraphicsPipeline 析构）
 5. 销毁 GPUImage
 6. 销毁 GPUBuffer（camera_ubo_ / spacetime_ubo_）
 7. 销毁 VulkanContext（command pool → image views → swapchain
                        → surface → device → instance → GLFW window）
```

RAII 保证初始化中途异常时已创建的资源被正确清理。

---

## 物理层（独立于 Vulkan 抽象）

物理层不依赖任何 Vulkan handle，可独立开发和测试：

| 组件 | 职责 |
|------|------|
| Metric（纯虚接口） | 定义度规张量 g_μν 及其逆 g^μν |
| Kerr（实现 Metric） | Kerr 时空度规，参数为质量 M 和角动量 a |
| Christoffel | 从度规计算 Γ^μ_αβ（解析式 + 数值差分交叉验证） |
| Integrator（CPU 端） | RK4/RK45 积分器，std::function 注入步进函数，验证守恒量 |
| Camera（ZAMO tetrad） | Boyer-Lindquist 坐标下的轨道相机，为每像素生成初始光线四动量 |
| Scene | 黑洞参数 (M, a)、吸积盘参数、天空背景 |

### 数据流（三阶段管线）

```
阶段一 [CPU]
  GLFW 输入 → Camera 更新 → 写入 SpacetimeUBO (M, a) + CameraUBO (tetrad, 轨道参数)
  → GPUBuffer map/unmap 上传到 GPU 侧

阶段二 [GPU Compute Pass]
  Barrier: Storage Image UNDEFINED → GENERAL (synchronization2)
  → bind ComputePipeline (geodesic.comp) + descriptor sets
  → dispatch (每像素一个 work item):
      CameraUBO 生成初始光线 x^μ, p^μ (BL 坐标)
      → RK4 积分循环 (查 SpacetimeUBO 实时计算 Christoffel)
      → 坐标转换 BL ↔ 局部欧几里得 (相交检测)
      → 终止判定 (r ≤ r_horizon 捕获 / 逃逸 / 盘面命中)
      → imageStore 写 Storage Image

阶段三 [GPU Graphics Pass]
  Barrier: Storage Image GENERAL → SHADER_READ_ONLY_OPTIMAL (synchronization2)
  Barrier: Swapchain Image UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
  → vkCmdBeginRendering 绑定 swapchain image view 为 color attachment
  → bind GraphicsPipeline + descriptor sets
  → fragment shader 读取 Storage Image → 输出到 swapchain
      (当前直通，后续可在此阶段加多普勒频移等后处理)
  → vkCmdEndRendering
  Barrier: Swapchain Image COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR
  → submit（带 frame_fence）→ present
```

---

## 项目文件布局

本项目是独立应用而非库——头文件和源文件统一放在 `src/` 下，`include/` 仅保留给第三方头文件（如需）。

```
blackhole_prac/
├── CMakeLists.txt
├── CLAUDE.md                     # 开发指引（引用本文档）
├── docs/
│   ├── architecture.md           # 本文件 — 唯一架构真相源
│   └── devlog.md                 # 开发流程记录（不决定方向）
├── include/                      # 第三方头文件（如需）
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── control.h / control.cpp
│   ├── vulkan/
│   │   ├── vulkan_util.h / vulkan_util.cpp
│   │   ├── vulkan_context.h / vulkan_context.cpp
│   │   ├── gpu_buffer.h / gpu_buffer.cpp
│   │   ├── gpu_image.h / gpu_image.cpp
│   │   ├── compute_pipeline.h / compute_pipeline.cpp
│   │   ├── graphics_pipeline.h / graphics_pipeline.cpp
│   │   ├── command_buffer.h / command_buffer.cpp
│   │   └── vulkan_app.h / vulkan_app.cpp
│   ├── physics/
│   │   ├── metric.h              # 度规纯虚接口（header-only）
│   │   ├── kerr.h / kerr.cpp
│   │   ├── geodesic.h / geodesic.cpp
│   │   └── integrator.h / integrator.cpp
│   └── scene/
│       ├── camera.h / camera.cpp
│       ├── scene.h / scene.cpp
│       └── accretion_disk.h / accretion_disk.cpp
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

---

## 开发阶段

| 阶段 | 描述 | 新增文件 |
|------|------|----------|
| Stage 1 | VulkanContext + vulkan_util: instance, device, validation layers, debug messenger, surface, swapchain (Dynamic Rendering), command pool, descriptor pool | `vulkan_util.h/cpp`, `vulkan_context.h/cpp` |
| Stage 2 | GPUBuffer + GPUImage: RAII buffer 和 image 包装, staging upload, layout transition + barrier (synchronization2) | `gpu_buffer.h/cpp`, `gpu_image.h/cpp` |
| Stage 3 | ComputePipeline + GraphicsPipeline: RAII pipeline 包装, GraphicsPipelineConfig (VkPipelineRenderingCreateInfo) | `compute_pipeline.h/cpp`, `graphics_pipeline.h/cpp` |
| Stage 4 | CommandBuffer: 通用命令缓冲包装，pipeline_barrier2 (synchronization2) | `command_buffer.h/cpp` |
| Stage 5 | VulkanApp: 顶层编排, 组件持有与初始化, 每帧 Dynamic Rendering 流程, frame fence 同步 | `vulkan_app.h/cpp` |
| Stage 6 | 物理基础验证 (CPU 端): Kerr, Christoffel, RK4/RK45, 守恒量测试 | `physics/` 下全部, `test/` 下全部 |
| Stage 7 | GPU compute 集成: geodesic.comp, storage image, descriptor sets, camera/scene UBO | shaders 全部, `camera.h/cpp`, `scene.h/cpp`, `accretion_disk.h/cpp` |
| Stage 8 | 主循环/交互: 输入处理, swapchain 重建 (resize), 渐进式渲染, PNG 导出 | `control.h/cpp`, 扩展 `vulkan_app.cpp` + `main.cpp` |

---

## 命名规范

| 元素 | 风格 | 示例 |
|------|------|------|
| 文件 | 蛇形 (snake_case) | `vulkan_context.h`, `queue_family_indices.h` |
| 类 / struct | 大驼峰 (PascalCase) | `VulkanContext`, `GraphicsPipelineConfig` |
| 函数 / 方法 | 蛇形 (snake_case) | `create_shader_module()`, `allocate_command_buffer()` |
| lambda | 蛇形 (snake_case) | `auto upload_data = [&]() { ... }` |
| 变量（局部、全局） | 蛇形 (snake_case) | `image_index`, `command_pool` |
| 命名空间 | 蛇形 (snake_case) | `vulkan_util` |
| 类成员变量 | 蛇形 + 尾下划线 (`_`) | `device_`, `current_layout_`, `vertex_buffer_` |
| 枚举值 | 蛇形 (snake_case) | （项目中暂未出现） |

**注意**：不使用 `m_` 前缀——尾下划线足以区分成员变量和局部变量，且与 C++ 标准库保留标识符规则兼容（`_` 后缀不在保留之列，`_` 前缀 + 大写字母在）。

---

## 关键规范

- **Vulkan 1.3 Dynamic Rendering**：不使用 VkRenderPass / VkFramebuffer
- **Vulkan 1.3 synchronization2**：barrier 统一使用 `vkCmdPipelineBarrier2` + `VkImageMemoryBarrier2`，不使用旧版 `vkCmdPipelineBarrier`
- **frame fence 保证 reset 安全**：每帧开头 `vkWaitForFences` + `vkResetFences`，确认 GPU 完成后再 `reset_command_pool()`
- **swapchain image barrier 完整**：每帧对 swapchain image 执行 `UNDEFINED → COLOR_ATTACHMENT_OPTIMAL`（渲染前）和 `COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR`（present 前）两次 barrier
- **GLM** 是数学库，列主序；GPU 端 GLSL 用相同语法
- **颜色直接用 glm::vec4**，对应 `VK_FORMAT_R32G32B32A32_SFLOAT`
- **物理对象无状态**：Metric、Integrator、Camera 将状态作为参数传入
- **std::function 用于可扩展性**：积分器步进函数可替换（RK4 → RK45）
- **构建时编译 shader**：CMake add_custom_command + glslc → .spv
- **RAII 全覆盖**：所有 VkHandle 有明确 owner
- **VulkanContext 是硬件查询的唯一入口**：其他组件通过 const reference 获取
- **Pipeline 配置用 struct + 默认值**：GraphicsPipelineConfig 只填需要覆盖的字段
- **不做过度设计**：架构在"够用"和"可扩展"之间取平衡

---

## 设计演进记录

| 日期 | 变更 | 说明 |
|------|------|------|
| 2026-05-29 | 初始架构 | 数据流图式架构（VulkanContext → Surface → Pipeline → Buffer） |
| 2026-06-01 v1 | RAII 分层 | 参考 sph_vulkan/plan.md，引入三层 RAII 模型，但 CommandRecord 含业务逻辑、VulkanContext 含 RenderPass |
| 2026-06-01 v2 | 名词/动词分离 + Dynamic Rendering | CommandRecord → CommandBuffer（纯动词），新增 GPUImage，砍掉 RenderPass/Framebuffer，Vulkan 1.3 Dynamic Rendering 全覆盖 |
| 2026-06-03 v3 | 名词类 context 指针统一 + 职责边界修正 | GPUBuffer/GPUImage 统一持有 `const VulkanContext*` 替代裸 `VkDevice`，砍掉 GPUBuffer 的 `upload_from_host`（越界），VulkanContext 构造函数内置窗口创建 |
| 2026-06-04 v4 | 同步模型补全 + 接口精确化 | 新增 frame fence（CPU/GPU 帧同步），补全 swapchain image barrier（渲染前后各一次），barrier 接口升级到 synchronization2，queue family index 拆分为三个独立 getter，GPUImage 布局更新改为显式两步（create_barrier + set_layout），VulkanContext 明确 GLFW window 所有权，UBO 改为具名成员，预留 recreate_swapchain 接口 |

当前版本：**v4**（2026-06-04）
