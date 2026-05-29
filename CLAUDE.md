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

**依赖关系图（自底向上）：**

```
[Vulkan 基础设施]
VulkanContext (instance, device, validation layers)
  → Surface + Swapchain + ImageViews
  → Graphics Pipeline (全屏四边形) + Compute Pipeline (测地线积分)
  → Buffers (vertex, staging, uniform) + Storage Image (GPU 端像素缓冲)

[GPU 计算层 — 运行时路径]
Camera (CPU: ZAMO tetrad, 生成光线初始状态)  →  Scene (黑洞参数, 吸积盘)
  → GPU Compute: geodesic.comp (每像素一个 work item, RK4 积分)
  → GPU Storage Image: 每像素 vec4 颜色
  → GPU Graphics Pipeline: 全屏四边形采样 → present

[CPU 侧 — 运行时仅负责上传]
Uniform Buffer (相机/场景参数) → 每帧 upload
Vertex Buffer (全屏四边形) → 初始化时 upload

[验证用 — 非运行时路径]
CPU Integrator (RK4/RK45) → 守恒量测试 → 确认正确 → 移植到 geodesic.comp

[交互层]
Window (GLFW: 窗口 + surface + 输入)  ←  Control (输入 → 相机)
  →  main.cpp (主循环: 更新 UBO → dispatch compute → graphics draw → present)
```

**数据流（运行时）：**

```
[CPU: Camera 生成每像素初始光线状态 → upload to GPU storage buffer]
  → [GPU Compute: geodesic.comp — 每像素并行积分测地线方程]
  → [终止判定 + 颜色映射 (compute shader 内部)]
  → [GPU Storage Image: vec4 颜色]
  → [GPU Graphics Pipeline: 全屏四边形采样 → swapchain present]
```

## Key conventions

- **GLM** 是数学库，列主序。GPU 端 GLSL 用相同语法，减少 CPU/GPU 代码分歧。
- **不使用自定义 Color 类型** —— 颜色直接用 `glm::vec4`，与 GPU 端 `vec4` 和 Vulkan 纹理格式 `VK_FORMAT_R32G32B32A32_SFLOAT` 天然对齐，无需包装层。
- **测地线积分跑在 GPU 上** —— 运行时：CPU 生成初始光线状态 → upload → GPU compute shader 并行积分 → storage image → graphics pipeline 显示。CPU 端积分器仅用于验证，不进入运行时热路径。
- **GLFW 仅管理窗口和输入** —— 创建窗口、提供 Vulkan surface（`glfwCreateWindowSurface`）、转发键盘/鼠标事件。不创建 OpenGL 上下文，不参与渲染。
- **物理对象无状态** —— Metric、Integrator、Camera 将状态作为参数传入，不存储可变状态。
- **`std::function` 用于可扩展性** —— 积分器调用可替换的步进函数（RK4 → RK45 无需修改调用方代码）。
- **构建时编译 shader** —— CMake `add_custom_command` 调用 `glslc` 将 `.vert`/`.frag`/`.comp` 编译为 `.spv`。需要 Vulkan SDK 在 PATH 中。
- **`docs/devlog.md`** 记录各阶段实现笔记。新计划和实现笔记追加在文件末尾。

## Current state

项目正在从旧的单体 Vulkan 实现（`src/engine/vk.cpp`，约 1500 行）重写为模块化架构。CMake 骨架已搭好（Vulkan + GLFW + GLM）。

| 阶段 | 描述 | 状态 |
|------|------|------|
| Stage 1 | Vulkan 基础设施: instance、device、validation layer、debug messenger | 待实现 |
| Stage 2 | 窗口表面与交换链: GLFW window、Vulkan surface、swapchain、image views | 待实现 |
| Stage 3 | 图形管线与帧缓冲: render pass、shader modules、graphics pipeline、framebuffers | 待实现 |
| Stage 4 | 缓冲区: vertex buffer（全屏四边形）、staging buffer、uniform buffer | 待实现 |
| Stage 5 | 纹理: GPU texture（image + imageView + sampler）、staging upload、descriptor set | 待实现 |
| Stage 6 | 物理基础验证（CPU 端）: Kerr 度规、Christoffel 符号、RK4/RK45 积分器，守恒量测试 | 待实现 |
| Stage 7 | GPU 计算着色器与渲染集成: geodesic.comp、compute pipeline、storage image、camera、scene、disk、renderer | 待实现 |
| Stage 8 | 主循环/交互: 输入处理、渐进式渲染、高分辨率 PNG 导出 | 待实现 |

## Planning workflow

当用户要求规划下一阶段的开发时：

1. **只读分析**：阅读当前源文件以理解代码库状态。不要修改任何代码。
2. **更新 docs/devlog.md**：在 `docs/devlog.md` 末尾追加新阶段计划，遵循已有格式（时间戳标题 `## YYYY-MM-DD`，然后是目标、设计理由、文件变更、关键要点、验证方法等部分）。
3. **向用户汇报**：输出简洁的总结，说明计划内容。

**重要**：规划阶段只能修改 `docs/devlog.md` 和 `CLAUDE.md`。在用户明确要求实现之前，不得修改 `include/` 和 `src/` 下的源文件。
