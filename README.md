Blackhole_prac — 黑洞光线追踪（实验性）

简介
---
这是一个基于 Vulkan 的黑洞（Geodesic）光线追踪试验项目，使用 Compute Shader 执行光线积分（GRRT），并通过图形管线将结果呈现在屏幕上。项目使用 GLFW 创建窗口，GLM 做数学运算，并依赖 Vulkan SDK 提供的运行时与 glslangValidator 用于着色器编译。

主要目录
---
- src/: 源代码、着色器与构建脚本
  - src/engine/vk.cpp: 主 Vulkan 初始化、资源管理、管线与渲染流程
  - src/shader/: GLSL 源文件与已编译的 SPIR‑V 文件（.spv）
  - src/CMakeLists.txt: 构建目标与 post-build 步骤（会把 SPV 复制到可执行目录）
- include/: 头文件（如 vk.h、object.h 等）
- build/: 构建输出目录（由 CMake 生成）

依赖
---
- Vulkan SDK
- GLFW（CMake find_package 已配置）
- GLM
- CMake >= 3.10

构建
---
1. 生成构建文件（如果尚未生成）：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

2. 构建可执行：

```powershell
cmake --build build --config Debug --target main
```

3. 运行：

```powershell
& 'C:\Users\Neuroil\Documents\Code\Blackhole_prac\build\src\Debug\main.exe'
```

说明与注意事项
---
- 着色器文件：项目在构建后会将 `src/shader/*.spv` 复制到可执行目录下的 `src/shader/`，以避免相对路径在不同工作目录下引起的找不到文件错误。
- 验证层警告：当前会看到验证层警告提示“顶点属性未被顶点着色器使用”，这是因为图形管线声明了顶点属性但顶点着色器使用了硬编码的顶点（gl_VertexIndex）。该问题已在代码中注释并提供两个修复方案（删除顶点输入或更新 shader 以使用顶点缓冲）。
- Compute -> Graphics 流：如果希望使用 Compute Shader 写入 SSBO，并在随后由图形管线读取，务必在命令缓冲中先记录 compute dispatch，再插入内存屏障（pipeline barrier），然后再执行 render pass。代码中包含相关注释与伪代码示例。
- SSBO 分辨率：为避免越界或浪费显存，请确保 SSBO 大小基于实际的渲染分辨率（例如 swapChainImageExtent 或 UBO 提供的屏幕尺寸），而不是硬编码值。

后续工作建议
---
- 选择并实现顶点输入修复（推荐先使用方案 A：全屏三角 + 移除顶点输入）。
- 统一命令缓冲分配策略（每个 swapchain 图像一个命令缓冲或每帧一个命令缓冲）。
- 将 compute dispatch 与相应的 barrier 真正加入到命令缓冲录制流程并验证结果。

作者
---
Neuroil


