

## 🛠️ I. 着色器环境准备

### 1. 着色器语言与编译器

* **语言：** 使用 **$\text{GLSL}$** ($\text{OpenGL}$ Shading Language) 编写着色器代码。
* **版本：** 推荐使用 $\text{GLSL}$ **$450$** 或更高版本，并明确指定 $\text{Vulkan}$ 核心配置文件。
* **编译：** $\text{Vulkan}$ 不直接使用 $\text{GLSL}$ 源代码，你需要使用 **$\text{glslangValidator}$** 工具（通常集成在 $\text{Vulkan SDK}$ 中）将其编译成 **$\text{SPIR-V}$** 中间二进制代码 ($\text{.spv}$ 文件)。

### 2. 着色器文件清单

你需要三个着色器文件来完成你的流程：

| 文件名 | 阶段 | 职责 |
| :--- | :--- | :--- |
| `grrt.comp` | **Compute Shader** | **核心算法**：执行 $\text{GRRT}$ (光线积分、黑洞交互)，将最终的颜色和深度结果 **写入 $\text{SSBO}$**。 |
| `fullscreen.vert` | **Vertex Shader** | **渲染输入**：不接收传统顶点数据，而是通过 `gl_VertexIndex` **读取 $\text{SSBO}$** 中的颜色和位置数据，生成 $2\text{D}$ 屏幕坐标。 |
| `passthrough.frag` | **Fragment Shader** | **渲染输出**：接收 $\text{Vertex Shader}$ 传递的颜色，直接作为最终像素颜色输出。 |

---

## 💻 II. 着色器代码编写关键点

### 1. Compute Shader (`grrt.comp`)

这是你 $\text{GRRT}$ 算法发生的地方，必须明确线程组织和数据写入。

| 元素 | 代码示例 ($\text{GLSL}$) | 关键点 |
| :--- | :--- | :--- |
| **线程组织** | `layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;` | 必须与你在 $\text{vkCmdDispatch}$ 中计算的 `groupCount` 保持一致（例如 $16 \times 16$）。 |
| **UBO 声明** | `layout(std140, binding = 0) uniform CameraParams { ... } ubo;` | 绑定 $\text{Binding 0}$。用于读取摄像机位置、时间、黑洞参数等。 |
| **SSBO 声明** | `layout(std430, binding = 1) buffer PixelDataSSBO { PixelResult pixels[]; } pixelBuffer;` | 绑定 $\text{Binding 1}$。用于 **写入** 颜色结果。必须使用 **$\text{std430}$ 布局**。 |
| **索引计算** | `uint index = gl_GlobalInvocationID.y * screenWidth + gl_GlobalInvocationID.x;` | 将 $2\text{D}$ 线程索引转换为 $1\text{D}$ 数组索引，用于访问 $\text{SSBO}$。 |
| **数据写入** | `pixelBuffer.pixels[index].finalColor = calculatedColor;` | 计算结果直接写入 $\text{SSBO}$。 |

---

### 2. Vertex Shader (`fullscreen.vert`)

该 $\text{Shader}$ 的职责是读取 $\text{SSBO}$ 的计算结果，并将它们映射到屏幕上。

| 元素 | 代码示例 ($\text{GLSL}$) | 关键点 |
| :--- | :--- | :--- |
| **SSBO 声明** | `layout(std430, binding = 1) readonly buffer PixelDataSSBO { ... } pixelBuffer;` | 绑定 $\text{Binding 1}$。使用 `readonly` 修饰符。 |
| **UBO 声明** | `layout(std140, binding = 0) uniform CameraParams { ... } ubo;` | 绑定 $\text{Binding 0}$。用于读取 $\text{Projection}$ 矩阵等。 |
| **顶点索引** | `uint index = gl_VertexIndex;` | 使用内置变量 $\text{gl\_VertexIndex}$ 或计算索引来访问 $\text{SSBO}$ 中的像素数据。 |
| **输出到 Frag** | `layout(location = 0) out vec4 fragColor;` | 将从 $\text{SSBO}$ 读取的颜色传递给 $\text{Fragment Shader}$。 |
| **设置位置** | `gl_Position = vec4(pos, 0.0, 1.0);` | 设置输出位置。如果是全屏四边形，这里只需硬编码 $3$ 或 $6$ 个顶点来覆盖屏幕。 |

### 3. Fragment Shader (`passthrough.frag`)

这是最简单的部分，它只是将颜色结果输出到最终的图像。

| 元素 | 代码示例 ($\text{GLSL}$) | 关键点 |
| :--- | :--- | :--- |
| **输入颜色** | `layout(location = 0) in vec4 fragColor;` | 接收来自 $\text{Vertex Shader}$ 的颜色。 |
| **输出颜色** | `layout(location = 0) out vec4 color;` | 将颜色写入 $\text{Render Pass}$ 的第一个颜色附件。 |
| **逻辑** | `void main() { color = fragColor; }` | 直接将输入颜色赋给输出颜色。 |

---

## ⚙️ III. $\text{SPIR-V}$ 编译流程

编写完 `.comp`, `.vert`, `.frag` 文件后，你需要在终端或构建系统中运行编译命令，将它们转化为 $\text{.spv}$ 文件：

| Shader 文件 | 编译命令 |
| :--- | :--- |
| `grrt.comp` | `glslangValidator -V grrt.comp -o grrt.spv` |
| `fullscreen.vert` | `glslangValidator -V fullscreen.vert -o fullscreen.spv` |
| `passthrough.frag` | `glslangValidator -V passthrough.frag -o passthrough.spv` |

这些 $\text{.spv}$ 文件就是你在 $\text{Vulkan}$ 初始化函数中加载并创建 $\text{VkShaderModule}$ 的二进制数据。

你可以从 $\text{Compute Shader}$ 的 $\text{GLSL}$ 代码开始编写。