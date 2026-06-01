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

基于 Vulkan 的广义相对论克尔黑洞光线追踪仿真器。测地线积分运行在 GPU compute shader 上——CPU 端积分器仅用于验证算法正确性（守恒量测试、解析解对标），验证通过后移植到 GLSL compute shader。所有渲染通过 Vulkan 完成（graphics pipeline + compute pipeline），GLFW 仅负责窗口管理和输入。

架构采用 **RAII 分层设计**（参考 `~/learn/sph_vulkan/plan.md`），从底到顶三层：

```
┌─────────────────────────────────────────────────────┐
│                    VulkanApp                         │
│  (顶层编排：持有所有组件，初始化 + 每帧循环)           │
├─────────────────────────────────────────────────────┤
│  VulkanContext  │ GPUBuffer │ ComputePipeline       │
│  (底层硬件)      │ (缓冲 RAII)│         │ GraphicsPipeline │
│                 │           │ CommandRecord         │
├─────────────────────────────────────────────────────┤
│              vulkan 工具命名空间 (纯函数)              │
└─────────────────────────────────────────────────────┘

[物理层 — 独立于 Vulkan 抽象]
Camera (ZAMO tetrad) → Scene (黑洞参数, 吸积盘)
  → CPU Integrator (验证用) → GPU geodesic.comp (运行时)
```

### 设计哲学

- **不做过度设计**：组件在"够用"和"可扩展"之间取平衡。当前只需要一个 render pass、一个 compute pipeline——不为此预拆 SwapchainManager / RenderPassFactory 等子组件。架构良好的代码扩展起来不复杂。
- **RAII 全覆盖**：每个 VkHandle 都有明确的 owner 类，构造时创建、析构时销毁、只允许 move。不存在裸 handle 游离在类外部。
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
VkCommandPool create_command_pool(VkDevice device, uint32_t queue_family_index,
                                   VkCommandPoolCreateFlags flags);
VkCommandBuffer allocate_command_buffer(VkDevice device, VkCommandPool pool);
std::vector<VkCommandBuffer> allocate_command_buffers(VkDevice device,
                                                       VkCommandPool pool, uint32_t count);
VkSemaphore create_semaphore(VkDevice device);
VkFence create_fence(VkDevice device, VkFenceCreateFlags flags);

} // namespace vulkan_util
```

---

### 2. VulkanContext — 底层硬件抽象

**职责**：管理所有"应用全局唯一"的底层硬件资源。其他类通过它查询 device/handle，自行调用 Vulkan API 创建上层对象。

**内部创建的资源**：
- VkInstance（含 debug messenger / validation layers）
- VkPhysicalDevice（查询但不持有所有权）
- VkDevice（logical device）+ queue handles
- VkSurfaceKHR
- VkSwapchainKHR + VkImage[] + VkImageView[] + VkFramebuffer[]
- VkRenderPass
- VkPipelineCache
- VkDescriptorPool（共享分配器）

**接口**（const getter，不暴露 mutable handle）：
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
    VkRenderPass   render_pass()            const;
    VkFramebuffer  framebuffer(uint32_t i)  const;
    uint32_t       framebuffer_count()      const;
    VkPipelineCache pipeline_cache()        const;
    VkDescriptorPool descriptor_pool()      const;

    const VkPhysicalDeviceMemoryProperties& memory_properties() const;

    uint32_t window_width()  const;
    uint32_t window_height() const;
};
```

**设计决策**：
- Swapchain 信息（format、extent、images、image_views、framebuffers）作为内部结构体存储，通过 getter 暴露
- RenderPass + Framebuffer 放在 VulkanContext（与 swapchain 属于同一逻辑组——当前只有一个 render pass，不拆）
- DescriptorPool 放在 VulkanContext（作为共享分配器，避免每个上层组件各自维护一个 pool）
- **不**持有 descriptor set layout 或 pipeline —— 这些是上层应用细节
- 析构顺序：由内而外（framebuffers → image views → swapchain → surface → device → instance）

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
- 本项目中的用途：vertex buffer（全屏四边形）、staging buffer（上传用）、uniform buffer（相机/场景参数）、storage buffer（光线初始状态）

---

### 4. ComputePipeline — Compute Pipeline RAII 包装

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
- 本项目只需要 **1 个** compute pipeline（测地线积分），比 sph_vulkan 的 3 个更简单

---

### 5. GraphicsPipeline — Graphics Pipeline RAII 包装

**职责**：封装 graphics pipeline 的创建。通过配置结构体避免超长构造函数。

```cpp
struct GraphicsPipelineConfig {
    std::string vertex_shader_path;
    std::string fragment_shader_path;

    VkRenderPass     render_pass;
    uint32_t         subpass = 0;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipelineCache  pipeline_cache  = VK_NULL_HANDLE;

    std::vector<VkVertexInputBindingDescription>   vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> vertex_attributes;

    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkBool32            primitive_restart = VK_FALSE;

    VkExtent2D viewport_extent{0, 0};

    VkPolygonMode   polygon_mode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cull_mode    = VK_CULL_MODE_NONE;
    VkFrontFace     front_face   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

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
- 配置结构体有合理默认值，本项目场景只需填 6-7 个字段（shader 路径、render pass、viewport extent、vertex layout）
- 内部同样 transient 使用 shader module（创建后销毁）
- 本项目用于渲染全屏四边形（采样 storage image → swapchain）

---

### 6. CommandRecord — 命令录制与提交

**职责**：管理 command pool 和 command buffer，处理录制和每帧提交。

```cpp
class CommandRecord {
public:
    explicit CommandRecord(const VulkanContext& context);
    ~CommandRecord();

    // --- 初始化期录制（调用一次） ---

    // 录制 compute dispatch：bind descriptor set → dispatch geodesic.comp
    void record_compute_dispatch(VkPipeline compute_pipeline,
                                 VkPipelineLayout pipeline_layout,
                                 VkDescriptorSet descriptor_set,
                                 uint32_t width, uint32_t height);

    // 为每个 swapchain image 录制 graphics 命令缓冲（全屏四边形采样 storage image）
    void record_graphics_commands(const VulkanContext& context,
                                  VkPipeline graphics_pipeline,
                                  VkBuffer vertex_buffer,
                                  uint32_t vertex_count,
                                  VkExtent2D viewport_extent);

    // --- 每帧提交 ---

    void submit_compute(VkQueue compute_queue);

    uint32_t submit_graphics(VkQueue graphics_queue,
                             VkSwapchainKHR swapchain,
                             VkSemaphore image_available,
                             VkSemaphore render_finished);

    void present(VkQueue presentation_queue,
                 VkSwapchainKHR swapchain,
                 uint32_t image_index,
                 VkSemaphore wait_semaphore);
};
```

**设计决策**：
- 持有 compute + graphics 两个 command pool 和所有 command buffer
- `VkSubmitInfo` / `VkPresentInfoKHR` 在提交函数内栈上构建（每帧重新填写），不在 header 中预初始化
- 命令缓冲使用 `VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT`，一次录制多帧复用
- 同步行为：`vkQueueWaitIdle` 串行化（与当前需求匹配；后续可升级为 fence-based 异步帧同步）
- 与 sph_vulkan 的差异：compute dispatch 和 graphics draw 之间需要 pipeline barrier（storage image: compute write → fragment shader read），由 VulkanApp 在录制时插入

---

### 7. VulkanApp — 顶层编排

**职责**：持有所有组件，编排初始化和每帧流程。替代旧架构中散落在 main.cpp 的裸 handle 管理。

```cpp
class VulkanApp {
public:
    explicit VulkanApp();
    ~VulkanApp();
    void run();

private:
    // 窗口
    GLFWwindow* m_window = nullptr;

    // Vulkan 抽象组件
    std::unique_ptr<VulkanContext>    m_context;
    std::unique_ptr<GPUBuffer>        m_vertex_buffer;      // 全屏四边形
    std::unique_ptr<GPUBuffer>        m_uniform_buffer;     // 相机 + 场景参数
    std::unique_ptr<ComputePipeline>  m_compute_pipeline;   // geodesic.comp
    std::unique_ptr<GraphicsPipeline> m_graphics_pipeline;  // 全屏四边形
    std::unique_ptr<CommandRecord>    m_command_record;

    // 少数裸 handle（量太少，不值得单独包装类；VulkanApp 负责其生命周期）
    VkDescriptorSetLayout m_compute_descriptor_set_layout;
    VkDescriptorSet       m_compute_descriptor_set;
    VkDescriptorSetLayout m_graphics_descriptor_set_layout;
    VkDescriptorSet       m_graphics_descriptor_set;
    VkPipelineLayout      m_compute_pipeline_layout;
    VkPipelineLayout      m_graphics_pipeline_layout;
    VkSemaphore           m_image_available_semaphore;
    VkSemaphore           m_render_finished_semaphore;

    // 物理层
    Camera m_camera;
    Scene  m_scene;

    // 渲染状态
    uint32_t m_frame_number = 0;
};
```

**每帧流程**（从散落的 Vulkan API 调用缩减为核心编排）：

```cpp
void VulkanApp::main_loop() {
    glfwPollEvents();
    update_uniform_buffer();            // CPU: 更新相机/场景 UBO
    m_command_record->submit_compute(m_context->compute_queue());
    uint32_t idx = m_command_record->submit_graphics(
        m_context->graphics_queue(), m_context->swapchain(),
        m_image_available_semaphore, m_render_finished_semaphore);
    m_command_record->present(
        m_context->presentation_queue(), m_context->swapchain(),
        idx, m_render_finished_semaphore);
    vkQueueWaitIdle(m_context->presentation_queue());
}
```

---

### 所有权关系

```
VulkanApp ──owns──► VulkanContext
                 ├─ GPUBuffer × 2 (vertex + uniform)
                 ├─ ComputePipeline
                 ├─ GraphicsPipeline
                 ├─ CommandRecord
                 ├─ VkDescriptorSetLayout × 2 (raw)
                 ├─ VkDescriptorSet × 2 (raw)
                 ├─ VkPipelineLayout × 2 (raw)
                 ├─ VkSemaphore × 2 (raw)
                 ├─ Camera
                 └─ Scene
```

### 初始化顺序

```
 1. GLFW window
 2. VulkanContext          ─── 底层硬件全部就绪
 3. GPUBuffer (vertex)     ─── 全屏四边形顶点数据 + upload
 4. GPUBuffer (uniform)    ─── 相机/场景 UBO
 5. 创建 compute descriptor set layout + 分配 descriptor set + 更新
 6. 创建 compute pipeline layout
 7. ComputePipeline        ─── geodesic.comp
 8. 创建 graphics descriptor set layout + 分配 descriptor set
 9. 创建 graphics pipeline layout
10. GraphicsPipeline       ─── 全屏四边形采样
11. CommandRecord          ─── 录制所有 command buffer
12. 创建 2 个 VkSemaphore
```

### 析构顺序（`~VulkanApp` 成员声明逆序自动保证）

```
 1. vkDeviceWaitIdle
 2. 销毁 semaphore
 3. 销毁 CommandRecord（释放 command buffer + 销毁 pool）
 4. 销毁 descriptor set layout + pipeline layout
 5. 销毁 pipeline（ComputePipeline / GraphicsPipeline 析构）
 6. 销毁 GPUBuffer
 7. 销毁 VulkanContext（framebuffers → image views → swapchain → render pass
                        → surface → device → instance）
```

RAII 保证初始化中途异常时已创建的资源被正确清理（`std::unique_ptr` 自动析构）。

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
[CPU: Camera 生成每像素初始光线状态 → upload to GPU storage buffer]
  → [GPU Compute: geodesic.comp — 每像素并行积分测地线方程]
  → [终止判定 + 颜色映射 (compute shader 内部)]
  → [GPU Storage Image: vec4 颜色]
  → [GPU Graphics Pipeline: 全屏四边形采样 → swapchain present]
```

## Key conventions

- **GLM** 是数学库，列主序。GPU 端 GLSL 用相同语法，减少 CPU/GPU 代码分歧。
- **颜色直接用 `glm::vec4`**，与 GPU 端 `vec4` 和 Vulkan 纹理格式 `VK_FORMAT_R32G32B32A32_SFLOAT` 天然对齐，无需包装层。
- **测地线积分跑在 GPU 上** —— 运行时：CPU 生成初始光线状态 → upload → GPU compute shader 并行积分 → storage image → graphics pipeline 显示。CPU 端积分器仅用于验证，不进入运行时热路径。
- **GLFW 仅管理窗口和输入** —— 创建窗口、提供 Vulkan surface（`glfwCreateWindowSurface`）、转发键盘/鼠标事件。不创建 OpenGL 上下文，不参与渲染。
- **物理对象无状态** —— Metric、Integrator、Camera 将状态作为参数传入，不存储可变状态。
- **`std::function` 用于可扩展性** —— 积分器调用可替换的步进函数（RK4 → RK45 无需修改调用方代码）。
- **构建时编译 shader** —— CMake `add_custom_command` 调用 `glslc` 将 `.vert`/`.frag`/`.comp` 编译为 `.spv`。需要 Vulkan SDK 在 PATH 中。
- **RAII 全覆盖** —— 所有 VkHandle 由明确的 owner 类持有，构造创建、析构销毁、只允许 move。裸 handle 仅在 VulkanApp 中少量存在（descriptor set layout / pipeline layout / semaphore），且 VulkanApp 负责其生命周期。
- **VulkanContext 是硬件查询的唯一入口** —— 其他组件通过 `const VulkanContext&` 获取 device、memory properties 等，不自行查询物理设备。
- **Pipeline 配置用 struct + 默认值** —— `GraphicsPipelineConfig` 所有字段有合理默认值，调用方只填需要覆盖的字段。
- **命令缓冲一次录制、多帧复用** —— `VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT`，submit info 每帧栈上重建。
- **不做过度设计** —— 当前只有一个 render pass、一个 compute pipeline，不为未来需求预拆组件。架构良好的代码扩展时不会痛苦。
- **`docs/devlog.md`** 记录各阶段实现笔记。新计划和实现笔记追加在文件末尾。

## Current state

项目正在从旧的单体 Vulkan 实现重写为 RAII 分层架构。CMake 骨架已搭好（Vulkan + GLFW + GLM），源文件仅有 hello world 的 `main.cpp`。

| 阶段 | 描述 | 状态 |
|------|------|------|
| Stage 1 | VulkanContext + vulkan 工具命名空间: instance, device, validation layers, debug messenger, surface, swapchain, render pass, framebuffers, descriptor pool | 待实现 |
| Stage 2 | GPUBuffer: RAII VkBuffer 包装 (vertex buffer, staging buffer, uniform buffer 均复用同一类) | 待实现 |
| Stage 3 | ComputePipeline + GraphicsPipeline: RAII pipeline 包装, GraphicsPipelineConfig struct | 待实现 |
| Stage 4 | CommandRecord: command pool/buffer 管理, compute dispatch 录制, graphics 录制, submit/present | 待实现 |
| Stage 5 | VulkanApp: 顶层编排, 组件持有与初始化顺序, 每帧流程, 少数裸 handle 生命周期管理 | 待实现 |
| Stage 6 | 物理基础验证 (CPU 端): Kerr 度规, Christoffel 符号, RK4/RK45 积分器, 守恒量测试 | 待实现 |
| Stage 7 | GPU compute 集成: geodesic.comp, storage image, descriptor sets, camera/scene UBO, 全屏四边形采样管线 | 待实现 |
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
│   │   ├── vulkan_context.h       # 底层硬件抽象
│   │   ├── gpu_buffer.h           # VkBuffer RAII
│   │   ├── compute_pipeline.h     # Compute pipeline RAII
│   │   ├── graphics_pipeline.h    # Graphics pipeline RAII + config struct
│   │   ├── command_record.h       # 命令录制与提交
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
│   │   ├── compute_pipeline.cpp
│   │   ├── graphics_pipeline.cpp
│   │   ├── command_record.cpp
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
