# 开发日志

## 2026-05-29 — 项目整体规划（修订）

### 修订说明

上一版规划存在严重架构错误：将 Vulkan 视为"后期附加品"（Stage 3），且自定义了 Types.h 做不必要的类型包装。Vulkan 本身拥有完善的类型体系和初始化序列，项目应遵循 Vulkan 的原生开发顺序，而非在 Vulkan 之上叠加自定义抽象层。

**核心修正：**

1. **移除 Types.h** —— `Color` 直接使用 `glm::vec4`，与 Vulkan 纹理格式 `VK_FORMAT_R32G32B32A32_SFLOAT` 天然对应，无需额外包装层。`glm` 的语法与 GLSL 一致，CPU/GPU 两侧类型对齐，不存在迁移负担。
2. **Vulkan 优先** —— 先按 Vulkan 原生初始化顺序搭建渲染基础设施（instance → validation layer → surface → swapchain → imageView → pipeline → framebuffer → vertex buffer → uniform buffer → texture），在此基础上逐步添加功能。
3. **测地线积分器独立验证** —— 积分器本质上是纯数学函数，可通过守恒量测试和解析解对标来独立验证，不依赖任何渲染管线。

### 背景

基于 Vulkan 的广义相对论克尔黑洞光线追踪仿真器。目标：
- **克尔度规**（旋转黑洞），而非仅史瓦西
- **可交互** —— 通过 GLFW 窗口实时控制相机
- **Vulkan 原生** —— 遵循 Vulkan 标准初始化序列，所有渲染通过 graphics pipeline 完成
- **物理模块独立可测** —— 测地线积分器通过守恒量测试独立验证，不绑定渲染管线

### 整体架构

```
[Vulkan 基础设施]
VulkanContext (instance, device, validation layers)
  → Surface + Swapchain + ImageViews
  → Graphics Pipeline (全屏四边形) + Compute Pipeline (测地线积分)
  → Buffers (vertex, staging, uniform) + Storage Image (GPU 端像素缓冲)
  → Command pool + command buffers

[GPU 计算层 — 运行时路径]
Camera (CPU 端生成光线初始条件 → upload to GPU storage buffer)
  → GPU Compute: geodesic.comp (每条光线一个 work item, RK4/RK45 积分)
  → GPU Storage Image: 每像素 vec4 颜色
  → GPU Graphics Pipeline: 全屏四边形采样 storage image → swapchain present

[CPU 侧 — 运行时仅负责数据上传与调度]
Uniform Buffer (相机参数, 分辨率, 黑洞参数) → 每帧 upload
Vertex Buffer (全屏四边形) → 初始化时一次性 upload

[验证用 — 非运行时路径]
CPU Integrator (RK4/RK45) → 守恒量测试 (E, L_z, Q) → 确认算法正确 → 移植到 geodesic.comp

[交互层]
Window (GLFW: 窗口 + surface + 输入)  ←  Control (输入 → 相机)
  →  main.cpp (主循环组装: 更新 UBO → dispatch compute → graphics draw → present)
```

**数据流（运行时）：**

```
[CPU: Camera 生成每个像素的初始光线状态 → upload to GPU storage buffer]
  → [GPU Compute: geodesic.comp — 每条光线一个 work item, 并行执行]
  → [RK4/RK45 逐步积分测地线方程 (GPU 并行)]
  → [终止判定 + 颜色映射 (compute shader 内部完成)]
  → [GPU Storage Image: 每像素 vec4 颜色]
  → [GPU Graphics Pipeline: 全屏四边形 + texture.frag 采样 storage image]
  → [swapchain present]
```

### 核心设计决策

- **不使用自定义类型包装** —— `Color` = `glm::vec4`，直接对应 Vulkan 纹理格式。`glm` 语法与 GLSL 一致，CPU/GPU 代码无分歧。
- **Vulkan 原生初始化顺序** —— 严格遵循 instance → device → surface → swapchain → imageView → pipeline → framebuffer → buffer → texture 的顺序，每一步在上一步验证通过后才继续。
- **物理对象无状态** —— Metric、Integrator、Camera 将状态作为参数传入，不存储可变状态。
- **`std::function` 用于可扩展性** —— 积分器调用可替换的步进函数（RK4 → RK45 无需修改调用方代码）。
- **GLFW 仅管理窗口和输入** —— 创建窗口、提供 Vulkan surface（`glfwCreateWindowSurface`）、转发键盘/鼠标事件。不创建 OpenGL 上下文，不参与渲染。
- **GLM 作为数学库** —— 列主序，与 GLSL 语法一致。
- **Boyer-Lindquist 坐标系** —— Kerr 度规的标准坐标，物理意义直观（r, θ, φ, t）。
- **构建时编译 shader** —— CMake `add_custom_command` 调用 `glslc` 将 `.vert`/`.frag`/`.comp` 编译为 `.spv`。
- **测地线积分跑在 GPU 上** —— 运行时路径：CPU 生成光线初始条件 → upload → GPU compute shader 并行积分 → storage image → graphics pipeline 采样显示。CPU 端积分器仅用于验证算法正确性（守恒量测试、解析解对标），验证通过后移植到 GLSL compute shader，不进入运行时热路径。
- **Vulkan 1.3+** —— 使用 dynamic rendering，减少样板代码。
- **volk** —— meta-loader，避免 SDK 依赖，链接更简洁。

---

### Stage 1: Vulkan 基础设施 — 实例、设备、验证层

#### 目标
创建 Vulkan 实例，启用验证层（debug messenger），选择物理设备并创建逻辑设备。此阶段只做硬件初始化，不涉及窗口或渲染——验证层干净无报错即为通过。

#### 新建文件
- `include/vulkan/context.h` + `src/vulkan/context.cpp` —— `VulkanContext` RAII 封装：
  - **Instance 创建**：指定应用信息、引擎信息、所需的 instance extension（`VK_KHR_surface` + 平台 surface extension）。启用 validation layer（`VK_LAYER_KHRONOS_validation`），注册 debug messenger 回调。
  - **物理设备选择**：枚举物理设备，按以下条件筛选——(1) 支持 graphics queue、(2) 支持 `VK_KHR_swapchain`、(3) 支持 `VK_KHR_shader_float64`（为 Stage 7 compute shader 双精度计算预留）、(4) 离散 GPU 优先于集成 GPU。
  - **逻辑设备创建**：获取 graphics queue family index，创建 device 和 queue。启用所需 device extension：`VK_KHR_swapchain`。
- `include/vulkan/debug.h` + `src/vulkan/debug.cpp` —— Debug messenger：将 validation layer 消息输出到 stderr，对 Error/Warning 级别加前缀标记。

#### 验证方法
- 编译通过，程序启动后 validation layer 无报错
- `vkEnumeratePhysicalDevices` 至少返回一个支持 swapchain 的设备
- Device 创建成功，queue handle 非空

#### 关键设计决策
- **单 graphics queue** —— 初期 graphics 和 present 共用一个 queue，减少复杂度。等 compute 加入后再评估是否需要独立 queue。
- **RAII 封装** —— 构造时创建、析构时销毁。每个 Vulkan 对象的 create/destroy 成对管理。
- **volk** —— 在 `VulkanContext` 构造函数中调用 `volkInitialize()` 和 `volkLoadInstance()`，避免静态链接 Vulkan loader。

---

### Stage 2: 窗口表面、交换链与图像视图

#### 目标
创建 GLFW 窗口并获取 Vulkan surface，建立交换链和图像视图。此阶段完成后，可获取 swapchain image 但暂不渲染——验证 swapchain 创建和重建（resize）逻辑正确。

#### 新建文件
- `include/display/window.h` + `src/display/window.cpp` —— GLFW 窗口管理：
  - 创建窗口（不初始化 OpenGL：`glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)`）
  - 创建 Vulkan surface（`glfwCreateWindowSurface`）
  - 提供窗口尺寸查询（用于 swapchain extent）
  - 注册键盘/鼠标回调（回调存储到 Control 对象，Stage 6 使用）
  - **职责严格限定**：窗口创建、surface 提供、输入事件转发。不参与任何渲染。
- `include/vulkan/swapchain.h` + `src/vulkan/swapchain.cpp` —— 交换链管理：
  - 查询 surface capabilities（min/max image count、extent、supported formats/present modes）
  - 选择 surface format（优先 `VK_FORMAT_B8G8R8A8_SRGB` + `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`）
  - 选择 present mode（优先 `VK_PRESENT_MODE_MAILBOX_KHR`，回退到 `VK_PRESENT_MODE_FIFO_KHR`）
  - 选择 extent（与窗口 client size 对齐，clamp 到 surface capabilities 范围）
  - 创建 swapchain + 获取 images + 为每个 image 创建 `VkImageView`
  - 重建逻辑：`recreate()` 函数，窗口 resize 时调用（销毁旧 swapchain → 重建新 swapchain + image views）

#### 验证方法
- 窗口出现（空白/黑色），validation layer 无报错
- 手动 resize 窗口 → swapchain 重建不崩溃
- `vkGetSwapchainImagesKHR` 返回至少 2 个 image

---

### Stage 3: 图形管线与帧缓冲

#### 目标
构建渲染通道（render pass）、着色器模块、管线布局、图形管线、帧缓冲区。此阶段管线就绪但尚未提交绘制命令——validation layer 在管线创建时无报错即为通过。

#### 新建文件
- `shaders/fullscreen.vert` —— 全屏四边形顶点着色器：
  - 硬编码 6 个顶点（两个三角形覆盖 NDC $[-1,1]^2$）
  - 输出 UV 坐标给片元着色器
- `shaders/texture.frag` —— 片元着色器：
  - 绑定 combined image sampler（descriptor set 0, binding 0）
  - 采样纹理 → 输出 `vec4` 颜色
- `include/vulkan/pipeline.h` + `src/vulkan/pipeline.cpp` —— 管线构建：
  - Shader module 加载：从文件读取 SPIR-V（`.spv`），创建 `VkShaderModule`
  - Descriptor set layout：单个 combined image sampler（fragment stage）
  - Pipeline layout：绑定 descriptor set layout
  - Render pass：单个 color attachment（对应 swapchain image format），load op = clear，store op = store
  - Graphics pipeline：hardcode 全屏四边形顶点格式（无需 vertex input），input assembly = triangle list，viewport/scissor 动态设置，无 depth/stencil
  - Framebuffers：为每个 swapchain image view 创建 framebuffer（绑定到 render pass）
- CMake 构建规则：`add_custom_command` 调用 `glslc` 编译 `.vert`/`.frag` → `.spv`

#### 验证方法
- Pipeline 创建成功，validation layer 无报错
- Framebuffer 数量与 swapchain image 数量一致

#### 关键设计决策
- **dynamic rendering (Vulkan 1.3)** —— 使用 `VK_KHR_dynamic_rendering` 替代传统 render pass + framebuffer 对象。在 `vkCmdBeginRendering` 时指定 color attachment，减少样板代码。如果使用传统 render pass，则 framebuffer 按上述方案。
- **暂不需要深度/模板** —— 全屏四边形绘制只有一层，无需深度测试。

---

### Stage 4: 顶点缓冲区、统一缓冲区

#### 目标
创建顶点缓冲区（硬编码全屏四边形）、暂存缓冲区（CPU→GPU 数据中转）、统一缓冲区（传递分辨率等参数给着色器）。此阶段可录制并提交第一帧绘制命令——屏幕上出现纯色（clear color）。

#### 新建文件
- `include/vulkan/buffer.h` + `src/vulkan/buffer.cpp` —— GPU buffer RAII 封装：
  - `createBuffer(size, usage, memoryType)` —— 分配 `VkBuffer` + `VkDeviceMemory`
  - `copyBuffer(src, dst, size)` —— 用 command buffer 做 device-to-device 拷贝
  - 内存类型选择：host-visible + coherent（staging）、device-local（vertex、uniform）
- `include/vulkan/commands.h` + `src/vulkan/commands.cpp` —— Command pool + command buffer 管理：
  - 创建 command pool（graphics queue family）
  - 分配 command buffer（单帧用，暂不做 double-buffering）
  - 录制命令：begin rendering → bind pipeline → bind vertex buffer → bind descriptor set → draw → end rendering
  - Submit + present

#### 具体操作
- **Vertex buffer**：6 个顶点（`glm::vec2` position + `glm::vec2` texCoord），两个三角形覆盖全屏。数据写死在代码中（NDC 坐标），分配 device-local buffer 并通过 staging buffer 上传。
- **Uniform buffer**：`UniformBufferObject { glm::vec2 resolution; float time; }`。每个 swapchain image 对应一个 uniform buffer（或单帧共享），host-visible。
- **Staging buffer**：通用 CPU→GPU 中转，host-visible + coherent，用于顶点数据上传和后续纹理数据上传。

#### 验证方法
- 屏幕显示 clear color（纯色，由 render pass clear value 决定）
- 验证顶点着色器被调用（修改 clear color 可观察变化）
- Validation layer 无报错

---

### Stage 5: 纹理

#### 目标
创建 GPU 纹理（image + image view + sampler），通过 staging buffer 上传像素数据，绑定到 descriptor set 供片元着色器采样。此阶段显示一张 CPU 生成的彩色测试渐变——验证从 CPU 像素缓冲到 GPU 纹理再到 Vulkan 绘制的完整数据通路。

#### 新建文件
- `include/vulkan/texture.h` + `src/vulkan/texture.cpp` —— GPU 纹理封装：
  - 创建 `VkImage` + `VkDeviceMemory`（device-local，`VK_FORMAT_R32G32B32A32_SFLOAT`，与 `glm::vec4` 内存布局一致）
  - 创建 `VkImageView`
  - 创建 `VkSampler`（linear filtering, clamp to edge）
  - 通过 staging buffer 上传像素数据：`vkCmdCopyBufferToImage`
  - Layout transition helper：undefined → transfer dst（上传阶段）、transfer dst → shader read only（采样阶段）
  - 创建 descriptor pool + descriptor set（combined image sampler，绑定到 fragment shader）

#### 验证方法
- 屏幕上显示 CPU 生成的彩色渐变纹理（如左上红→右上绿→左下蓝的 bilinear 渐变）
- 验证纹理坐标正确（渐变位置与预期一致）
- Validation layer 无报错

#### 关键设计决策
- **纹理格式与 `glm::vec4` 对齐** —— `VK_FORMAT_R32G32B32A32_SFLOAT` 的 16 字节布局与 `glm::vec4` 完全一致。测试用的 CPU 端像素缓冲（`std::vector<glm::vec4>`，仅在 Stage 5 验证阶段使用）可直接 `memcpy` 到 staging buffer，无需格式转换。运行时路径中像素数据由 GPU compute shader 直接写入 storage image，不经 CPU。
- **`glm::vec4` 即颜色** —— 不使用自定义 `Color` 类型。`.r`/`.g`/`.b`/`.a` 访问通过 `glm::vec4` 的 swizzle（`.x`/`.y`/`.z`/`.w`）或直接用下标 `[0]`/`[1]`/`[2]`/`[3]`。片元着色器中 `vec4` 原生支持 `.rgba` swizzle。

---

### Stage 6: 物理基础验证（CPU 端，仅验证用）

#### 目标
实现 Kerr 时空度规、Christoffel 符号计算、测地线方程和数值积分器——**全部在 CPU 端，仅用于验证积分器算法的正确性**。此模块为纯数学库，不依赖 Vulkan、不依赖渲染管线，通过单元测试独立验证。验证通过后，算法逻辑移植到 GLSL compute shader（Stage 7），CPU 积分器不进入运行时热路径。

#### 新建文件
- `include/physics/constants.h` —— G、c、M_sun、几何单位制转换常量
- `include/physics/metric.h` —— 抽象基类，接口包含：
  - `christoffel(i, j, k, position)` → float（即 $\Gamma^i_{jk}$）
  - `g_dn(i, j, position)` → float（协变度规 $g_{\mu\nu}$）
  - `g_up(i, j, position)` → float（逆变度规 $g^{\mu\nu}$）
- `include/physics/kerr.h` + `src/physics/kerr.cpp` —— Kerr 度规（Boyer-Lindquist 坐标），参数为质量 M 和自旋 a
- `include/physics/geodesic.h` + `src/physics/geodesic.cpp` —— 测地线方程：
  - `GeodesicState { glm::vec4 position, glm::vec4 momentum }`（位置 $x^\mu$ 和四动量 $p^\mu$）
  - `GeodesicDerivatives { glm::vec4 dPos, glm::vec4 dMom }`
  - 方程：$\frac{dx^\mu}{d\lambda} = p^\mu$， $\frac{dp^\mu}{d\lambda} = -\Gamma^\mu_{\alpha\beta} p^\alpha p^\beta$
- `include/physics/integrator.h` + `src/physics/integrator.cpp` —— RK4 积分器，`integrateStep(metric, state, dλ)` → 新状态。另外实现 RK45（Dormand-Prince），利用嵌入误差估计做自适应步长。
- `test/test_kerr.cpp` —— Kerr 度规单元测试
- `test/test_geodesic.cpp` —— 测地线守恒量测试
- `test/test_integrator.cpp` —— 积分器精度测试

#### 验证方法（独立，无需渲染管线）
- **守恒量测试**：在 Kerr 时空中，能量 $E = -p_t$、角动量 $L_z = p_\phi$、Carter 常数 Q 应沿测地线保持恒定（每积分步 < 1e-6，双精度）
- **光子球半径对标**：赤道面轨道，解析公式 $r_{\text{ph}} = 2M\left(1 + \cos\left(\frac{2}{3}\arccos\left(-\frac{|a|}{M}\right)\right)\right)$，与数值结果比较
- **史瓦西极限**：a=0 时，轨道应对称于赤道面
- **ISCO 对标**：顺行/逆行 ISCO 解析值与数值结果比较

#### 关键物理知识

**Boyer-Lindquist 坐标下的 Kerr 度规**（几何单位制 $G=c=1$）：

$$
\begin{aligned}
\Sigma &= r^2 + a^2\cos^2\theta \\
\Delta &= r^2 - 2Mr + a^2 \\[4pt]
g_{tt} &= -\left(1 - \frac{2Mr}{\Sigma}\right) \\
g_{t\phi} &= -\frac{2Mra\sin^2\theta}{\Sigma} \\
g_{rr} &= \frac{\Sigma}{\Delta} \\
g_{\theta\theta} &= \Sigma \\
g_{\phi\phi} &= \left(r^2 + a^2 + \frac{2Mra^2\sin^2\theta}{\Sigma}\right)\sin^2\theta
\end{aligned}
$$

线元：

$$
ds^2 = g_{tt}\,dt^2 + 2g_{t\phi}\,dt\,d\phi + g_{rr}\,dr^2 + g_{\theta\theta}\,d\theta^2 + g_{\phi\phi}\,d\phi^2
$$

外视界：$r_+ = M + \sqrt{M^2 - a^2}$，内视界：$r_- = M - \sqrt{M^2 - a^2}$

光子球（赤道面，顺行）：$r_{\text{ph}}^+ = 2M\left(1 + \cos\left(\frac{2}{3}\arccos\left(-\frac{|a|}{M}\right)\right)\right)$，逆行：$r_{\text{ph}}^- = 2M\left(1 + \cos\left(\frac{2}{3}\arccos\left(\frac{|a|}{M}\right)\right)\right)$

**四个守恒量**：
- 能量 $E = -p_t$（由度规的时间平移不变性保证）
- 角动量 $L_z = p_\phi$（由度规的轴对称性保证）
- Carter 常数 $Q = p_\theta^2 + \cos^2\theta\left(a^2(\mu^2 - p_t^2) + \frac{p_\phi^2}{\sin^2\theta}\right)$（Kerr 独有的 Killing 张量保证）
- 静质量 $\mu^2 = -p_\mu p^\mu$（光子为 0）

这四个量是验证积分精度的核心工具。

**Christoffel 符号**：对 Kerr 度规，$\Gamma^\mu_{\alpha\beta}$ 有约 40 个非零分量。建议用数值差分做交叉验证。

**ISCO（最内稳定圆轨道）**：
- 顺行：$r_{\text{ISCO}}^+ = M\left(3 + Z_2 - \sqrt{(3 - Z_1)(3 + Z_1 + 2Z_2)}\right)$，其中
  $$
  \begin{aligned}
  Z_1 &= 1 + \left(1 - \frac{a^2}{M^2}\right)^{1/3}\left[\left(1 + \frac{a}{M}\right)^{1/3} + \left(1 - \frac{a}{M}\right)^{1/3}\right] \\
  Z_2 &= \sqrt{\frac{3a^2}{M^2} + Z_1^2}
  \end{aligned}
  $$
- 逆行：$r_{\text{ISCO}}^- = M\left(3 + Z_2 + \sqrt{(3 - Z_1)(3 + Z_1 + 2Z_2)}\right)$
- $a=0$ 时退化为 $6M$；$a=M$（极端旋转）时，顺行 ISCO = $M$，逆行 ISCO = $9M$

---

### Stage 7: GPU 计算着色器与渲染集成

#### 目标
将 Stage 6 验证通过的积分器算法移植到 GLSL compute shader（`geodesic.comp`），构建完整的 GPU 运行时管线：CPU 生成光线初始条件并上传 → GPU compute shader 并行积分测地线 → storage image 输出 → graphics pipeline 采样显示。同时实现相机 tetrad、场景描述和吸积盘模型。

**关键分工**：
- **CPU**（轻量）: 更新 uniform buffer（相机参数、分辨率、黑洞参数）、Camera 为每个像素生成初始光线状态（`GeodesicState`，一次 `vec4` 乘法）、数据上传
- **GPU Compute**（重负载）: 每条光线一个 work item，RK4 并行积分测地线方程 → 终止判定 → 颜色映射 → 写入 storage image
- **GPU Graphics**（显示）: 全屏四边形采样 storage image → present

#### 新建/修改文件
- `shaders/geodesic.comp` —— GPU 测地线积分 compute shader：
  - Dispatch dimensions: `ceil(width / 16) × ceil(height / 16)`，每个 work group 16×16 像素
  - 从 storage buffer 读取初始光线状态（CPU 端 Camera 生成并上传）
  - 内联 Kerr 度规 Christoffel 符号（从 C++ `kerr.cpp` 移植到 GLSL，双精度 `float64`）
  - RK4 固定步长积分（固定步数保证 GPU 线程间无 divergence）
  - 终止判定：$r < r_{\text{horizon}}$（捕获）、$r > r_{\text{max}}$（逃逸）、击中赤道面吸积盘、达到最大步数
  - 颜色映射（compute shader 内部完成，避免 CPU readback）：
    - 捕获 → `vec4(0, 0, 0, 1)`
    - 逃逸 → 天空渐变（基于最终动量方向）
    - 击中吸积盘 → 红/橙色 × 辐射率（$\propto r^{-3}$）× 多普勒频移因子
  - 输出：storage image `VK_FORMAT_R32G32B32A32_SFLOAT`（`image2D`，`writeonly`）
- 扩展 `include/vulkan/pipeline.h` / `src/vulkan/pipeline.cpp` —— 增加 compute pipeline 创建：
  - Descriptor set layout：storage buffer（initial ray states）+ storage image（output）+ uniform buffer（scene params）
  - Pipeline layout + `vkCreateComputePipelines`
- 扩展 `include/vulkan/buffer.h` —— 增加 storage buffer（`VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`，device-local）
- 扩展 `include/vulkan/texture.h` —— 增加 storage image 支持（`VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT`），compute 写入后 pipeline barrier transition 到 shader read
- `include/scene/camera.h` + `src/scene/camera.cpp` —— ZAMO tetrad 轨道相机（Boyer-Lindquist 坐标）：
  - 以球坐标 (θ, φ, r) 围绕黑洞
  - 每帧在 CPU 端为所有像素生成初始光线状态（`vector<GeodesicState>`），upload 到 GPU storage buffer
  - 每像素仅一次 `vec4` 线性组合——CPU 端开销可忽略
- `include/scene/scene.h` + `src/scene/scene.cpp` —— 场景描述：黑洞 (M, a)、背景天空渐变
- `include/scene/accretion_disk.h` + `src/scene/accretion_disk.cpp` —— 赤道面薄盘模型（ISCO ~ 20M），辐射率 $\propto r^{-3}$，提供多普勒频移计算（GPU 端在 intersection 处调用）
- `include/render/renderer.h` + `src/render/renderer.cpp` —— 渲染编排器：
  - 持有 Vulkan 资源引用（compute pipeline、graphics pipeline、storage image、storage buffer、uniform buffer、command pool、descriptor sets）
  - `renderFrame(scene, camera)` 流程：
    1. CPU: 更新 uniform buffer（相机参数、分辨率、M、a、disk 参数）→ upload
    2. CPU: Camera 生成本帧所有像素的初始光线状态 → upload to GPU storage buffer
    3. GPU: pipeline barrier（storage image: undefined → general，确保 compute 可写）
    4. GPU: `vkCmdDispatch` 调度 `geodesic.comp`
    5. GPU: pipeline barrier（storage image: compute write → fragment shader read）
    6. GPU: graphics pipeline 全屏四边形采样 storage image → `vkCmdDraw`
    7. Present

#### 颜色映射
- 被捕获的光线（落入视界）→ 纯黑 `(0, 0, 0, 1)`
- 逃逸的光线 → 天空色（基于最终动量方向的渐变）
- 击中吸积盘 → 红/橙色，加权辐射率 × 多普勒频移因子（蓝移侧偏亮偏蓝白，红移侧偏暗偏红）
- 光子球贡献 → 亮环（在 $r_{\text{ph}}$ 附近多次轨道绕行后逃逸的光线）

#### ZAMO tetrad 构建（CPU 端，每帧一次）
1. ZAMO 四速度：$u^\mu = (u^t, 0, 0, u^\phi)$，其中 $u^\phi = \omega u^t$，$\omega = -g_{t\phi}/g_{\phi\phi}$ 为参考系拖曳角速度
2. 由归一化条件 $u^\mu u_\mu = -1$ 确定 $u^t$
3. 三空间基矢 $e_{\hat{i}}^\mu$ 通过 Gram-Schmidt 正交化得到

#### 光线初始条件（CPU 端，每像素）
1. 从相机 tetrad 出发，每个像素 $(u, v)$ 对应空间方向 $\mathbf{d} = \mathrm{normalize}(u\cdot\mathbf{e}_{\hat{1}} + v\cdot\mathbf{e}_{\hat{2}} + f\cdot\mathbf{e}_{\hat{3}})$，$f$ 为焦距
2. 光子四动量：$p^\mu = E_{\text{obs}}(u^\mu + d^\mu)$，$u^\mu$ 为相机四速度，$d^\mu$ 为纯空间方向
3. 类光条件验证：$p^\mu p_\mu = 0$

#### 光线终止条件（GPU compute shader 内部）
- $r < r_{\text{horizon}}$（被捕获）
- $r > r_{\text{max}}$（逃逸至无穷远）
- 达到最大积分步数（固定值，保证 GPU 一致性）
- 击中吸积盘（赤道面，$r_{\text{ISCO}} < r < r_{\text{outer}}$）

#### 渲染分辨率策略
- 相机移动时：storage image 使用低分辨率（如 320×240）+ 较少积分步数 → GPU compute 负载低，帧率流畅
- 相机静止时：渐进式提升 storage image 分辨率 + 更多步数，逐帧累积
- 按空格键：触发一次全分辨率渲染（如 1920×1080），compute shader 输出到高分辨率 storage image → graphics pipeline 绘制 → 导出 PNG

#### 验证方法
- 渲染一张 640×480 的史瓦西黑洞 (a=0) 图像——应看到对称的黑色圆盘（阴影）+ 光子环
- 阴影半径对标解析值 $r_{\text{shadow}} \approx \sqrt{27}M$（$a=0$ 时）
- 与 Stage 6 CPU 积分器的输出逐像素对比（低分辨率下），二者差异应在浮点精度范围内
- 设置 a=0.9M，验证阴影不对称性（D 形阴影）与文献一致

---

### Stage 8: 主循环与交互

#### 目标
组装所有模块，实现交互式相机控制和实时 GPU 渲染。

#### 新建文件
- `src/main.cpp` —— 组装所有组件：Window、Renderer、Camera、Control。主循环：处理输入 → 更新相机 → Renderer.renderFrame()（更新 UBO → 生成光线初始状态 → upload → dispatch compute → graphics draw → present）。
- `include/control.h` + `src/control.cpp` —— 输入派发：
  - 鼠标拖拽 = 轨道旋转相机
  - 滚轮 = 缩放
  - 按键：R 重置视角，1-2-3 切换颜色模式，A 切换自动旋转，空格键触发全分辨率离屏渲染并保存 PNG

#### 交互策略
- 相机移动时：自动降低分辨率和积分步数以保证流畅帧率
- 相机静止时：渐进式超采样，逐步提升画质
- 与 DNGR 等真实 GR 光线追踪器的交互模式一致

---

### 已确定的开发细节

- **吸积盘模型**（已确定）：薄盘 + 多普勒频移着色。赤道面薄盘（ISCO ~ 20M），在交点处计算多普勒因子——盘面物质绕黑洞高速旋转，朝向观测者一侧因相对论多普勒效应更亮更蓝，远离侧更暗更红。
- **渲染交互模型**（已确定）：渐进式细化 + 手动高分辨率导出。日常交互时用低分辨率（~320×240）保持流畅，相机静止后渐进式超采样逐帧累积。按空格键触发全分辨率离屏渲染（1920×1080）并导出 PNG。
- **文档习惯**（已确定）：项目只使用一个 `docs/devlog.md`，每个阶段实现完成后追加一段总结，说明该阶段做了什么、有什么改动和新增、以及设计原因。不贴大段代码，保持简洁。

---

### 项目文件布局（全部阶段完成后）

```
blackhole_prac/
├── CMakeLists.txt
├── .gitignore
├── CLAUDE.md
├── CMakePresets.json
├── docs/
│   └── devlog.md               # 本文件
├── include/
│   ├── physics/
│   │   ├── constants.h
│   │   ├── metric.h
│   │   ├── kerr.h
│   │   ├── geodesic.h
│   │   └── integrator.h
│   ├── scene/
│   │   ├── camera.h
│   │   ├── scene.h
│   │   └── accretion_disk.h
│   ├── render/
│   │   └── renderer.h
│   ├── vulkan/
│   │   ├── context.h
│   │   ├── debug.h
│   │   ├── swapchain.h
│   │   ├── pipeline.h
│   │   ├── buffer.h
│   │   ├── texture.h
│   │   └── commands.h
│   └── display/
│       └── window.h
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp              # Stage 8
│   ├── control.cpp           # Stage 8
│   ├── physics/
│   │   ├── kerr.cpp
│   │   ├── geodesic.cpp
│   │   └── integrator.cpp
│   ├── scene/
│   │   ├── camera.cpp
│   │   ├── scene.cpp
│   │   └── accretion_disk.cpp
│   ├── render/
│   │   └── renderer.cpp
│   ├── vulkan/
│   │   ├── context.cpp
│   │   ├── debug.cpp
│   │   ├── swapchain.cpp
│   │   ├── pipeline.cpp
│   │   ├── buffer.cpp
│   │   ├── texture.cpp
│   │   └── commands.cpp
│   └── display/
│       └── window.cpp
├── test/
│   ├── CMakeLists.txt
│   ├── test_kerr.cpp
│   ├── test_geodesic.cpp
│   └── test_integrator.cpp
└── shaders/
    ├── fullscreen.vert       # Stage 3
    ├── texture.frag          # Stage 3
    └── geodesic.comp         # Stage 7
```

---

## 2026-06-01 — 架构重设计：采用 RAII 分层架构

### 背景

项目处于 Stage 0（所有阶段待实现），原有架构描述本质上是数据流图而非类设计——列出了组件和依赖关系，但没有明确 RAII 所有权、初始化/析构顺序、类边界。

### 决策

参考 `~/learn/sph_vulkan/plan.md` 的抽象设计，将架构替换为三层 RAII 分层模型：

1. **工具层**（vulkan 工具命名空间）：纯函数，无状态，显式传 `VkDevice`
2. **资源层**（VulkanContext / GPUBuffer / ComputePipeline / GraphicsPipeline / CommandRecord）：每个类封装一组 VkHandle，构造创建、析构销毁、只允许 move
3. **编排层**（VulkanApp）：持有所有组件，编排初始化顺序和每帧流程

### 与旧架构的关键差异

| 旧架构 | 新架构 |
|--------|--------|
| 数据流图（组件列表 + 依赖箭头） | 分层类设计（RAII 所有权 + 接口 + 初始化顺序） |
| 每个 Vulkan 子系统独立文件（context/debug/swapchain/pipeline/buffer/texture/commands） | 6 个内聚类 + 1 个工具命名空间 |
| 无明确的析构顺序保证 | 成员声明逆序自动保证 |
| RenderPass/Framebuffer 位置模糊 | 放入 VulkanContext（与 swapchain 同逻辑组） |
| DescriptorPool 归属不明 | 放入 VulkanContext（共享分配器） |
| ShaderModule 生命周期模糊 | pipeline 创建后立即销毁（transient） |

### 不做过度设计

- VulkanContext 不拆出 SwapchainManager / RenderPassFactory（当前只有一个 render pass）
- 不预建 GPUImage RAII 类（storage image 以 raw handle 起步）
- 不为未来多 pass 渲染预留抽象

### 文件布局变化

旧的 `include/vulkan/{context,debug,swapchain,pipeline,buffer,texture,commands}.h` → 新的 `include/vulkan/{vulkan_util,vulkan_context,gpu_buffer,compute_pipeline,graphics_pipeline,command_record,vulkan_app}.h`

物理层（`include/physics/`、`include/scene/`）不变。

### 参考

- `~/learn/sph_vulkan/plan.md` — 原始架构设计文档
- `CLAUDE.md` — 更新后的完整架构描述

---

## 2026-06-01（续）— 第二轮架构演进：名词/动词分离 + Dynamic Rendering + GPUImage

### 触发

审阅了第一版 RAII 分层设计后，指出三个硬伤：

1. **CommandRecord 伪解耦**：`record_compute_dispatches(...)` 和 `record_graphics_commands(...)` 将业务逻辑写死在封装类内部。换一个物理仿真项目必须改类源码——这违反"可复用"原则。
2. **Vulkan 1.3 Dynamic Rendering 利用不彻底**：VulkanContext 仍持有 VkRenderPass 和 VkFramebuffer[]，GraphicsPipelineConfig 仍要求传入 VkRenderPass。既然用了 Dynamic Rendering，这两个对象应该从整个架构里消失。
3. **Storage Image 裸奔**：黑洞画布（Compute Write → Graphics Read）是核心数据通道，却以 raw handle 形式存在。每帧手动写 VkImageMemoryBarrier 不仅打破封装一致性，还极其容易出错。

### 决策

三个痛点指向同一个根因：**"业务"被写进了"封装类"**。需要将参数化架构升级为完全的名词/动词分离：

| 改动 | 旧设计 | 新设计 |
|------|--------|--------|
| 命令封装 | CommandRecord（持有 command pool + buffer，内部写死业务录制逻辑） | CommandBuffer（纯 VkCommandBuffer 薄包装，不知道顶点缓冲/descriptor set 等业务概念，不持有 pool） |
| 图像封装 | 无（raw handle） | GPUImage（VkImage + VkImageView + VkDeviceMemory RAII，内建 layout 追踪 + barrier 辅助方法） |
| 渲染模型 | 传统 RenderPass/Framebuffer | Vulkan 1.3 Dynamic Rendering（vkCmdBeginRendering，管线用 VkPipelineRenderingCreateInfo） |
| Context 胖瘦 | 持有 VkRenderPass + VkFramebuffer[] | 移除这两个对象，新增 command pool 管理 + AllocateCommandBuffer |
| 每帧同步 | vkQueueWaitIdle 串行化 | semaphore-based（image_available → render_finished） |

### CommandBuffer 设计原则

- 方法签名只有 Vulkan 原生类型（`VkBuffer` 而非 `GPUBuffer&`）——零业务耦合
- `begin_rendering(VkRenderingInfo)` / `end_rendering()` 直接映射 Dynamic Rendering API
- `pipeline_barrier()` 接收 `VkImageMemoryBarrier` 列表——不知道 barrier 的业务语义，只管执行
- 每帧从 VulkanContext command pool 分配，pool reset 时统一回收

### GPUImage 设计原则

- `create_barrier()` 纯查询（不修改内部状态），`transition_barrier()` 同步更新布局追踪
- 不直接操作命令流（barrier 的录制由 CommandBuffer 完成）——保持名词/动词分离
- 暂不引入 VMA（先用手动 vkAllocateMemory，后续按需迁移）

### 新旧架构三层对比

| 层 | 旧（第一版 RAII） | 新（名词/动词分离） |
|----|-------------------|---------------------|
| 工具层 | vulkan 工具 namespace（不变） | 同左 |
| 资源层（名词） | VulkanContext, GPUBuffer, ComputePipeline, GraphicsPipeline, CommandRecord | VulkanContext(瘦身), GPUBuffer, **GPUImage(新)**, ComputePipeline, GraphicsPipeline(Dynamic Rendering) |
| 动词层 | （无——CommandRecord 混了业务和指令） | **CommandBuffer(新)** — 纯指令，不知业务 |
| 编排层 | VulkanApp（调度 CommandRecord 的高层方法） | VulkanApp（直接指挥 CommandBuffer，每帧数据流 100% 可见） |

### 复用性

整个名词层 + 动词层（VulkanContext, GPUBuffer, GPUImage, ComputePipeline, GraphicsPipeline, CommandBuffer）可在任何 Vulkan 物理仿真项目中**原样复用**。唯一项目特定的代码在 VulkanApp::step_frame() 的编排逻辑中。

### 参考

- `CLAUDE.md` — 更新后的完整架构描述（含每帧伪代码）

---

## 2026-06-01（续）— 架构文档独立

### 决策

将架构设计从 CLAUDE.md 和 devlog.md 中分离出来，写入独立的 `docs/architecture.md` 作为**唯一架构真相源**（single source of truth）。

职责分工：
- `docs/architecture.md` — 架构真相源。完整组件接口、所有权关系、初始化/析构顺序、每帧伪代码、文件布局、开发阶段、设计演进记录。开发时以此为准。
- `CLAUDE.md` — 开发指引。引用 architecture.md，保留摘要 + 关键规范 + 当前状态 + 规划流程。给 Claude Code 提供上下文。
- `docs/devlog.md` — 开发流程记录。只记录"做了什么、为什么"，不决定架构方向。

### 文件变更

- 新建 `docs/architecture.md`（完整架构文档）
- 更新 `CLAUDE.md`（Architecture 节精简为引用 + 摘要）
