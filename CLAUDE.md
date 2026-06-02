# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build commands

```bash
# Configure (from repo root, debug preset via vcpkg)
cmake --preset debug

# Build
cmake --build build/debug

# Run
./build/debug/src/simulation
```

There are no tests or linting setup yet.

## Architecture

> **架构真相源**：`docs/architecture.md`。以下为摘要，完整设计（组件接口、所有权关系、初始化/析构顺序、每帧伪代码、文件布局、开发阶段）见该文件。

基于 Vulkan 1.3 Dynamic Rendering 的克尔黑洞光线追踪仿真器。架构采用 **RAII 分层 + 名词/动词分离**：

```
[名词层 — 资源 RAII]              [动词层 — 无状态]
VulkanContext (硬件)              CommandBuffer
GPUBuffer     (缓冲)              (纯 VkCmd 封装，零业务耦合)
GPUImage      (图像)
ComputePipeline
GraphicsPipeline
        ↓
vulkan_util 工具命名空间 (纯函数)
        ↓
[物理层] Camera → Scene → Integrator (CPU 验证) → geodesic.comp (GPU 运行时)
        ↓
[编排层] VulkanApp — 持有所有名词，每帧指挥动词，数据流 100% 可见
```

**核心原则**：
- 名词类（VulkanContext / GPUBuffer / GPUImage / *Pipeline）持有 VkHandle、管理生命周期、只允许 move
- 动词类（CommandBuffer）是纯 Vulkan 指令薄封装，方法签名只含 `Vk*` 原生类型——不知"顶点缓冲"、"测地线积分"等业务概念
- 不用 VkRenderPass / VkFramebuffer（Vulkan 1.3 Dynamic Rendering）
- 不做过度设计：当前一个 compute pipeline、一个 graphics pipeline、一个 storage image——不预拆子组件

**数据流**：CPU 生成光线初始状态 → upload → GPU compute (geodesic.comp) → storage image → pipeline barrier → GPU graphics (全屏四边形, Dynamic Rendering) → swapchain present

**初始化顺序**：GLFW window → VulkanContext → GPUBuffer×3 → GPUImage → descriptor sets → pipeline layouts → ComputePipeline → GraphicsPipeline → Semaphore×2

**组件清单**（`src/vulkan/`，头文件与源文件同目录）：
`vulkan_util.h/.cpp` / `vulkan_context.h/.cpp` / `gpu_buffer.h/.cpp` / `gpu_image.h/.cpp` / `compute_pipeline.h/.cpp` / `graphics_pipeline.h/.cpp` / `command_buffer.h/.cpp` / `vulkan_app.h/.cpp`

## Key conventions

- **Vulkan 1.3 Dynamic Rendering**：不使用 VkRenderPass / VkFramebuffer。管线创建用 `VkPipelineRenderingCreateInfo`，绘制时用 `vkCmdBeginRendering` / `vkCmdEndRendering`。
- **GLM** 是数学库，列主序。GPU 端 GLSL 用相同语法，减少 CPU/GPU 代码分歧。
- **颜色直接用 `glm::vec4`**，与 GPU 端 `vec4` 和 Vulkan 纹理格式 `VK_FORMAT_R32G32B32A32_SFLOAT` 天然对齐，无需包装层。
- **测地线积分跑在 GPU 上** —— 运行时：CPU 生成初始光线状态 → upload → GPU compute shader 并行积分 → storage image → graphics pipeline 显示。CPU 端积分器仅用于验证，不进入运行时热路径。
- **GLFW 仅管理窗口和输入** —— 创建窗口、提供 Vulkan surface（`glfwCreateWindowSurface`）、转发键盘/鼠标事件。不创建 OpenGL 上下文，不参与渲染。
- **物理对象无状态** —— Metric、Integrator、Camera 将状态作为参数传入，不存储可变状态。
- **`std::function` 用于可扩展性** —— 积分器调用可替换的步进函数（RK4 → RK45 无需修改调用方代码）。
- **构建时编译 shader** —— CMake `add_custom_command` 调用 `glslc` 将 `.vert`/`.frag`/`.comp` 编译为 `.spv`。需要 Vulkan SDK 在 PATH 中。
- **名词/动词严格分离** —— 资源类持有 VkHandle、不包含命令录制逻辑。CommandBuffer 是纯 Vulkan 指令封装，方法签名只有 `Vk*` 类型。上层编排代码即文档。
- **RAII 全覆盖** —— 所有 VkHandle 由明确的 owner 类持有，构造创建、析构销毁、只允许 move。
- **VulkanContext 是硬件查询的唯一入口** —— 其他组件通过 `const VulkanContext&` 获取 device、memory properties。
- **Pipeline 配置用 struct + 默认值** —— `GraphicsPipelineConfig` 所有字段有合理默认值，调用方只填需要覆盖的字段。
- **不做过度设计** —— 不为未来需求预拆组件。名词/动词分离的架构扩展时改编排逻辑即可，不动封装类。
- **`docs/architecture.md` 是架构真相源** —— 完整组件接口、初始化/析构顺序、每帧伪代码、文件布局均在其中。`docs/devlog.md` 仅记录开发过程。

## Tool usage

- **文件修改**：使用 `Read` / `Edit` / `Write` 工具。不得通过 Bash 调用 `sed` / `awk` / `perl` 等文本处理命令修改仓库文件。
- **文件搜索**：使用 `Glob` 或 Bash `find` 查找文件，使用 LSP（`findReferences` / `goToDefinition` / `hover`）查找符号引用和定义。Bash `grep` 仅用于快速定位行号，不用于批量替换。
- **Bash**：专用于构建（cmake / ninja）、运行、git 操作、文件列表（`find` / `ls`）、项目状态查询。
- **并发**：相互独立的工具调用应在单次响应中并行发出，减少往返延迟。
- **构建前确认**：`cmake --build build` 和 `rm` 等破坏性或耗时操作，若涉及清理或全量重编译，需先确认。

## Current state

项目正在从旧的单体 Vulkan 实现重写为 RAII 分层 + Dynamic Rendering 架构。CMake 骨架已搭好（Vulkan + GLFW + GLM + vcpkg），VulkanContext 初始化链已完成到 swapchain + image views。

| 阶段 | 描述 | 状态 |
|------|------|------|
| Stage 1 | VulkanContext + vulkan_util: instance, device, validation layers, debug messenger, surface, swapchain (Dynamic Rendering), command pool, descriptor pool | **进行中** — instance ✓, validation layers ✓, debug messenger ✓, surface ✓, physical device ✓, logical device + 3 queues ✓, vulkan_util (7个纯函数) ✓, swapchain ✓, image views ✓; command pool / descriptor pool 待实现 |
| Stage 2 | GPUBuffer + GPUImage: RAII buffer 和 image 包装, staging upload, layout transition + barrier | 待实现 |
| Stage 3 | ComputePipeline + GraphicsPipeline: RAII pipeline 包装, GraphicsPipelineConfig (VkPipelineRenderingCreateInfo) | 待实现 |
| Stage 4 | CommandBuffer: 通用命令缓冲包装 (begin/end, bind_pipeline, bind_descriptor_sets, dispatch, begin_rendering/end_rendering, draw, pipeline_barrier) | 待实现 |
| Stage 5 | VulkanApp: 顶层编排, 组件持有与初始化, 每帧 Dynamic Rendering 流程 | 待实现 |
| Stage 6 | 物理基础验证 (CPU 端): Kerr 度规, Christoffel, RK4/RK45, 守恒量测试 | 待实现 |
| Stage 7 | GPU compute 集成: geodesic.comp, storage image, descriptor sets, camera/scene UBO, ray buffer | 待实现 |
| Stage 8 | 主循环/交互: 输入处理, 渐进式渲染, PNG 导出 | 待实现 |

## Planning workflow

当用户要求规划下一阶段的开发时：

1. **只读分析**：阅读当前源文件以理解代码库状态。不要修改任何代码。
2. **更新 docs/devlog.md**：在 `docs/devlog.md` 末尾追加新阶段计划，遵循已有格式（时间戳标题 `## YYYY-MM-DD`，然后是目标、设计理由、文件变更、关键要点、验证方法等部分）。
3. **向用户汇报**：输出简洁的总结，说明计划内容。

**重要**：规划阶段只能修改 `docs/devlog.md` 和 `CLAUDE.md`。在用户明确要求实现之前，不得修改 `include/` 和 `src/` 下的源文件。
