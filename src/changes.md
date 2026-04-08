Created on 2026-04-08


项目修改与说明（汇总）

概述
---
本文件总结了在 `src/engine/vk.cpp` 与 `src/CMakeLists.txt` 中所做的可见改动、注意事项与建议修复流程，便于后续维护与功能扩展。

已完成的改动
---
1. 构建后复制 SPIR‑V 文件
   - 在 `src/CMakeLists.txt` 中添加了 POST_BUILD 命令，将 `src/shader/comp.spv`, `src/shader/vert.spv`, `src/shader/frag.spv` 复制到可执行目录的 `src/shader/` 子目录，解决了相对路径导致的运行时找不到着色器文件问题。

2. 代码中添加注释
   - 添加补充说明，涉及内容包括：
     - 管线创建与执行顺序（创建顺序 != 执行顺序）
     - 描述符绑定与 GLSL 布局一致性
     - 着色器文件相对路径与运行时工作目录（CWD）问题及解决方法
     - 顶点属性声明与顶点着色器不匹配的修复建议
     - SSBO 分辨率硬编码的问题与修复建议
     - command buffer 分配策略与 imageIndex 越界风险
     - 如何在命令缓冲中实现 compute -> barrier -> graphics 的录制顺序（含伪代码示例，伪代码保留原样）

3. SSBO 分辨率改写
   - `createShaderStorageBuffers()` 中将原先硬编码的宽高替换为 `swapChainImageExtent.width/height` 并添加了注释，提醒应与 UBO 或实际窗口分辨率保持一致。

需要开发者注意（技术债 / 待修复）
---
1. 顶点输入与顶点着色器不匹配（验证层警告）
   - 问题：当前 pipeline 声明了顶点输入（location 0/1），但 `shader.vert` 使用硬编码三角或 `gl_VertexIndex`，并没有声明对应的输入变量，导致验证层警告。
   - 修复选项：
     - 方案 A（推荐用于全屏三角）：移除顶点输入描述（vertexBindingDescriptionCount = 0，attributeCount = 0），让顶点着色器通过 `gl_VertexIndex` 生成全屏顶点。
     - 方案 B（使用顶点缓冲）：修改 `shader.vert` 添加 `layout(location=0) in vec2 inPos; layout(location=1) in vec3 inColor;` 并在 C++ 端准备并绑定对应的顶点缓冲。

2. 命令缓冲与 swapchain imageIndex 索引不一致（潜在越界）
   - 问题：当前代码将命令缓冲数量设置为 `MAX_FRAMES_IN_FLIGHT`，但在提交时使用 `commandBuffers[imageIndex]`。若 `swapChainImages.size() != MAX_FRAMES_IN_FLIGHT`，可能发生越界。
   - 修复选项：
     - 方案 1（推荐）：为每个 swapchain 图像分配并记录一个命令缓冲（commandBuffers.resize(swapChainImages.size())），并在提交时使用 `commandBuffers[imageIndex]`。
     - 方案 2：保持每帧一命令缓冲（与 MAX_FRAMES_IN_FLIGHT 对齐），在提交时使用 `commandBuffers[currentFrame]`（并按 frame 录制/重录命令缓冲）。

3. compute -> graphics 的数据可见性
   - 若 compute 着色器写入 SSBO 并希望随后图形着色器读取，必须在同一或后续命令缓冲中插入合适的 pipeline barrier（或使用跨队列同步），以保证写入对读操作可见。伪代码示例已加入 `createCommandBuffers()` 的注释中。


修改方向
---
- 自动修复顶点输入（方案 A 或 B）并运行验证。
- 将命令缓冲分配策略改为“每个 swapchain 图像一个命令缓冲”，并相应地记录命令缓冲。
- 把 compute dispatch 的录制（含 barrier）真正写入命令缓冲并测试完整的 compute->graphics 流。


