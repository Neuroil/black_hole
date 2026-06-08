# BLACK_HOLE_PRAC

基于 Vulkan 1.3 的克尔黑洞光线追踪仿真器。GPU 端逐像素并行积分测地线方程（compute shader），通过 Dynamic Rendering 呈递到 swapchain。

## 文档索引

| 文件 | 内容 |
|------|------|
| `architecture.md` | 架构真相源：组件接口、RAII 分层、名词/动词分离、初始化/析构顺序、每帧伪代码、文件布局 |
| `devlog.md` | 开发日志：各阶段决策过程、遇到的问题与解决方案 |

## 技术栈

- **Vulkan 1.3** (Dynamic Rendering + synchronization2，无 VkRenderPass / VkFramebuffer)
- **GLFW** — 窗口 + 输入
- **GLM** — 数学库（列主序，与 GLSL 对齐）
- **glslc** — CMake 构建时编译 shader → SPIR-V
- **stb_image_write** — PNG 导出
- **vcpkg** — 依赖管理

## 构建

```bash
cmake --preset debug
cmake --build build/debug
./build/debug/src/simulation
```

依赖：Vulkan SDK、CMake ≥ 3.10、C++17 编译器。

## 架构概要

```
VulkanApp (顶层编排)
  ├─ 名词层: VulkanContext / GPUBuffer / GPUImage / ComputePipeline / GraphicsPipeline
  ├─ 动词层: CommandBuffer (纯 VkCmd 封装，零业务耦合)
  └─ 物理层: Camera → Scene → geodesic.comp (GPU 运行时)
```

三阶段管线：CPU 输入+UBO → GPU Compute (测地线积分，写 Storage Image) → GPU Graphics (呈递 swapchain)。

完整设计见 `architecture.md`。
