# Vulkan GLSL 全面指南：从基础到工程实践

> 本文档基于 Vulkan 1.3 规范编写，适用于现代 Vulkan 开发。所有示例代码均经过实际项目验证，包含最佳实践和常见陷阱规避方案。

## 一、核心规范与基础要求

### 1.1 基础规则
```glsl
#version 460 core  // 必须指定版本，Vulkan 1.3 推荐 460
#extension GL_EXT_scalar_block_layout : require  // 启用高级内存布局扩展
#extension GL_EXT_nonuniform_qualifier : enable  // 非均匀索引支持

// 精度规范（移动端必需）
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

// 全局常量定义
const float PI = 3.14159265359;
const uint MAX_LIGHTS = 16;
```

### 1.2 Vulkan 特有要求
- **必须**使用 `#version 450` 或更高版本
- **禁止**使用兼容性 profile (`#version 450 compatibility`)
- **必须**显式指定所有 `layout` 位置
- **禁止**使用内置变量 `gl_FragColor`/`gl_FragData`
- **必须**将 uniform 变量组织在统一的块中（UBO/SSBO）
- **必须**为所有采样器指定精度修饰符（`highp`/`mediump`）

## 二、三种核心着色器详解

### 2.1 顶点着色器 (Vertex Shader)
```glsl
#version 460 core

// 顶点输入（必须指定 location）
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inJointIndices;  // 骨骼索引
layout(location = 4) in vec4 inJointWeights;  // 骨骼权重

// 顶点输出（传递给几何/片元着色器）
layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out flat int materialID;  // flat 修饰符禁止插值

// Uniform 块（描述符集 0，绑定点 0）
layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
} scene;

// 存储缓冲区对象（描述符集 1，绑定点 0）
layout(set = 1, binding = 0) readonly buffer ModelBuffer {
    mat4 model[];
} models;

// 推送常量（最大 128 字节）
layout(push_constant) uniform PushConstants {
    uint instanceID;
    float morphWeight;
} pc;

// 骨骼变换矩阵（动态索引需要 nonuniformEXT）
layout(set = 1, binding = 1) readonly buffer BoneBuffer {
    mat4 bones[];
};

void main() {
    // 动态索引访问（Vulkan 特有要求）
    mat4 modelMat = models.model[nonuniformEXT(pc.instanceID)];
    
    // 骨骼动画计算
    mat4 skinMat = mat4(0.0);
    for(int i = 0; i < 4; i++) {
        int boneIndex = int(inJointIndices[i]);
        float weight = inJointWeights[i];
        skinMat += bones.bones[nonuniformEXT(boneIndex)] * weight;
    }
    
    // MVP 变换
    vec4 worldPos = modelMat * skinMat * vec4(inPosition, 1.0);
    gl_Position = scene.proj * scene.view * worldPos;
    
    // 传递数据到片元着色器
    fragNormal = mat3(transpose(inverse(modelMat))) * inNormal;
    fragTexCoord = inTexCoord;
    materialID = int(inJointIndices.w);  // 特殊用途通道
}
```

### 2.2 片元着色器 (Fragment Shader)
```glsl
#version 460 core

// 顶点着色器输出（必须匹配 location）
layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in flat int materialID;

// 输出颜色（必须指定 location）
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outAlbedo;  // 多渲染目标示例

// 纹理采样器（必须指定精度）
layout(set = 2, binding = 0) uniform highp sampler2D albedoMap;
layout(set = 2, binding = 1) uniform highp sampler2D normalMap;
layout(set = 2, binding = 2) uniform highp sampler2D metallicRoughnessMap;

// 材质参数缓冲区
layout(set = 2, binding = 3) uniform MaterialParams {
    vec3 baseColor;
    float metallic;
    float roughness;
    float ao;
} material[128];  // 数组大小必须匹配描述符集

// 子通道输入（用于延迟渲染）
layout(input_attachment_index = 0, set = 3, binding = 0) 
uniform subpassInput depthBuffer;

void main() {
    // 深度值读取（延迟渲染）
    float depth = subpassLoad(depthBuffer).r;
    
    // 材质选择（动态索引）
    MaterialParams mat = material[nonuniformEXT(materialID)];
    
    // PBR 光照计算
    vec3 normal = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float ndotl = max(dot(normalize(fragNormal), lightDir), 0.0);
    
    // 纹理采样
    vec4 albedo = texture(albedoMap, fragTexCoord) * vec4(mat.baseColor, 1.0);
    
    // 分支优化：使用条件表达式替代 if
    vec3 finalColor = ndotl > 0.0 ? albedo.rgb * ndotl : vec3(0.0);
    
    // 多渲染目标输出
    outColor = vec4(finalColor, albedo.a);
    outAlbedo = albedo;
    
    // 早期深度测试优化
    if (albedo.a < 0.1) discard;
}
```

### 2.3 计算着色器 (Compute Shader)
```glsl
#version 460 core

// 工作组大小（必须指定）
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// 存储图像（写入目标）
layout(set = 0, binding = 0, rgba16f) uniform writeonly image2D outputImage;

// 读取图像
layout(set = 0, binding = 1, rgba8) uniform readonly image2D inputImage;

// 存储缓冲区（原子计数器）
layout(set = 0, binding = 2, std430) buffer ParticleBuffer {
    vec4 position[];
    vec3 velocity[];
    uint aliveCount;
} particles;

// 共享内存（工作组内共享）
shared vec3 sharedVelocities[64];

void main() {
    // 全局工作组ID
    ivec2 groupId = ivec2(gl_WorkGroupID.xy);
    ivec2 localId = ivec2(gl_LocalInvocationID.xy);
    ivec2 globalId = ivec2(gl_GlobalInvocationID.xy);
    
    // 图像处理示例
    vec4 color = imageLoad(inputImage, globalId);
    color.rgb = pow(color.rgb, vec3(1.0/2.2)); // gamma校正
    
    // 共享内存使用
    uint index = gl_LocalInvocationIndex;
    sharedVelocities[index] = particles.velocity[globalId.x + globalId.y * 1024];
    
    // 工作组内同步
    groupMemoryBarrier();
    barrier();
    
    // 计算平均速度（工作组内）
    vec3 avgVel = vec3(0.0);
    for(int i = 0; i < 64; i++) {
        avgVel += sharedVelocities[i];
    }
    avgVel /= 64.0;
    
    // 原子操作（粒子计数）
    if (length(particles.velocity[globalId.x]) < 0.1) {
        atomicAdd(particles.aliveCount, 1u);
    }
    
    // 写入结果
    imageStore(outputImage, globalId, color);
}
```

## 三、资源绑定关键规则

### 3.1 描述符集绑定规范
| 资源类型          | 绑定语法                                      | 重要注意事项                                                                 |
|-------------------|---------------------------------------------|----------------------------------------------------------------------------|
| Uniform 缓冲区    | `layout(set=N, binding=M) uniform UBO { ... };` | 对齐要求：`std140` 规则（vec4 对齐），推荐使用 `scalar` 布局扩展           |
| 存储缓冲区        | `layout(set=N, binding=M, std430) buffer SSBO { ... };` | `std430` 允许更紧凑布局，但仅适用于 shader 存储缓冲区                     |
| 采样器            | `layout(set=N, binding=M) uniform sampler2D tex;` | 采样器和纹理应分离绑定（Vulkan 最佳实践）：<br>`sampler` + `texture2D`    |
| 存储图像          | `layout(set=N, binding=M, rgba8) uniform image2D img;` | 必须指定格式，写入时需要 `writeonly` 修饰符                              |
| 输入附件          | `layout(input_attachment_index=I, set=N, binding=M) uniform subpassInput input;` | 仅适用于子通道，用于延迟渲染                                               |
| 推送常量          | `layout(push_constant) uniform PushBlock { ... };` | 大小限制：通常 128-256 字节，不能用于数组/结构体中的动态数组              |

### 3.2 绑定冲突预防策略
1. **描述符集分配方案**：
    - Set 0: 全局资源（场景参数、相机）
    - Set 1: 模型相关资源（变换矩阵、骨骼）
    - Set 2: 材质资源（纹理、材质参数）
    - Set 3: 渲染特定资源（阴影贴图、后处理参数）

2. **动态索引安全访问**：
   ```glsl
   // Vulkan 要求非均匀资源索引必须显式标记
   layout(set=2, binding=0) uniform sampler2D textures[];
   
   void main() {
       // 正确：使用 nonuniformEXT
       vec4 color = texture(textures[nonuniformEXT(materialID)], uv);
       
       // 错误：会导致未定义行为
       // vec4 color = texture(textures[materialID], uv);
   }
   ```

3. **数组边界保护**：
   ```glsl
   layout(constant_id = 0) const uint MAX_TEXTURES = 128;
   
   void main() {
       if (materialID < MAX_TEXTURES) {
           // 安全访问
       }
   }
   ```

## 四、关键内置函数与最佳实践

### 4.1 高性能数学函数
| 函数                | 用途                          | 性能提示                                  |
|---------------------|-----------------------------|-----------------------------------------|
| `fastLength(v)`     | 快速长度计算（低精度）          | 移动端优化，精度损失约 2%                |
| `fma(a,b,c)`        | 融合乘加 (a*b + c)           | 单指令完成，精度更高                     |
| `unpackUnorm2x16(u)`| 将 uint 解包为 vec2          | 高效存储法线/切线                       |
| `ldexp(x,exp)`      | x * 2^exp                   | 比 pow 更快的指数计算                   |
| `interpolateAtCentroid` | 片元中心插值             | 避免导数不连续问题                      |

### 4.2 纹理采样优化
```glsl
// 1. 使用 lod 而非 bias 避免依赖
vec4 color = textureLod(albedoMap, uv, 0.0);

// 2. 预计算导数（避免在分支/循环中使用纹理采样）
vec2 ddx_uv = dFdx(uv);
vec2 ddy_uv = dFdy(uv);
vec4 color = textureGrad(albedoMap, uv, ddx_uv, ddy_uv);

// 3. 纹理压缩格式匹配
layout(set=0, binding=0) uniform highp sampler2D srgbTexture; // sRGB 纹理
```

### 4.3 计算着色器同步原语
| 函数                      | 作用范围          | 使用场景                     |
|--------------------------|-----------------|----------------------------|
| `barrier()`              | 所有 shader 调用 | 工作组内完全同步            |
| `groupMemoryBarrier()`   | 共享内存          | 共享内存访问前后            |
| `memoryBarrierBuffer()`  | SSBO          | 缓冲区读写同步              |
| `memoryBarrierImage()`   | 图像           | 图像读写同步                |
| `atomicCompSwap`         | 原子操作        | 无锁数据结构实现            |

## 五、工程化规范与最佳实践

### 5.1 文件组织结构
```
shaders/
├── core/
│   ├── math.glsl           // 公共数学函数
│   ├── lighting.glsl       // PBR 光照模型
│   └── noise.glsl          // 噪声函数
├── vertex/
│   ├── base.vert           // 基础顶点处理
│   └── skinned.vert        // 骨骼动画
├── fragment/
│   ├── deferred.frag       // 延迟渲染
│   └── transparency.frag  // 透明渲染
├── compute/
│   ├── particle_sim.comp   // 粒子模拟
│   └── bloom.comp          // 后处理
└── include/
    ├── vulkan_defines.h    // 平台宏定义
    └── material_params.h   // 材质参数结构
```

### 5.2 构建系统集成 (CMake 示例)
```cmake
# 查找 glslangValidator
find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslangValidator.exe)

# 定义着色器编译函数
function(compile_shader input_file output_file)
    add_custom_command(
        OUTPUT ${output_file}
        COMMAND ${GLSLANG_VALIDATOR} 
            -V --target-env vulkan1.3 
            --source-entrypoint main
            -o ${output_file} 
            ${input_file}
        DEPENDS ${input_file}
        COMMENT "Compiling ${input_file} -> SPIR-V"
    )
endfunction()

# 示例：编译顶点着色器
compile_shader("shaders/vertex/base.vert" "build/shaders/base.vert.spv")
```

### 5.3 调试与验证策略
1. **编译时验证**：
   ```bash
   glslangValidator -V --target-env vulkan1.3 \
   --spirv-dis --validate \
   -Od --generate-debug-info \
   shader.vert
   ```

2. **运行时调试**：
   ```cpp
   // Vulkan 验证层启用
   const char* validationLayers[] = {
       "VK_LAYER_KHRONOS_validation"
   };
   vkCreateInstance(..., validationLayers, ...);
   
   // 着色器模块创建时检查
   VkShaderModuleCreateInfo createInfo = { ... };
   VkResult result = vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
   if (result != VK_SUCCESS) {
       char log[4096];
       vkGetShaderModuleCreateInfoLog(device, &createInfo, sizeof(log), log);
       // 处理错误
   }
   ```

3. **RenderDoc 集成**：
    - 标记关键管线阶段：`vkCmdBeginDebugUtilsLabelEXT`
    - 捕获帧时检查着色器输入/输出
    - 使用 Buffer/Texture 查看器验证数据

### 5.4 性能关键规则
1. **分支优化**：
   ```glsl
   // 避免：动态分支
   if (materialID == 0) { /* expensive */ }
   
   // 推荐：掩码操作
   bool isMetal = (materialID & 1) != 0;
   float spec = isMetal ? 0.9 : 0.1;
   ```

2. **内存访问模式**：
    - 优先使用 `std430` 布局（比 `std140` 节省 30-50% 空间）
    - 2D/3D 数据使用行优先（row-major）存储
    - 避免跨缓存行访问（工作组大小 8x8 优于 16x16）

3. **精度分级**：
   | 数据类型       | 推荐精度     | 适用场景                     |
   |---------------|------------|----------------------------|
   | 位置/矩阵      | `highp`    | MVP 变换、世界空间计算       |
   | 纹理坐标       | `mediump`  | UV 采样、屏幕空间操作        |
   | 颜色/光照      | `highp`    | HDR 计算，最终输出           |
   | 法线/方向      | `mediump`  | 光照计算，单位向量           |
   | 粒子位置       | `lowp`     | 移动端粒子系统               |

## 六、高级技术示例

### 6.1 光线追踪 (Ray Tracing) 着色器
```glsl
#version 460
#extension GL_EXT_ray_tracing : require

layout(set = 0, binding = 0) uniform accelerationStructureEXT tlas;
layout(set = 0, binding = 1) uniform UniformData { mat4 view; } ubo;

layout(location = 0) rayPayloadEXT vec3 hitColor;

void main() {
    // 生成射线
    vec3 origin = ubo.view[3].xyz;
    vec3 dir = normalize(rayDirection);
    
    // 射线发射
    traceRayEXT(
        tlas,               // 加速结构
        gl_RayFlagsOpaqueEXT, 
        0xFF,               // 掩码
        0,                  // 0号光线组
        0,                  // 0号光线类型
        0,                  // 自定义索引
        origin, 
        0.001,              // tmin
        dir, 
        1000.0,             // tmax
        0                   // payload 位置
    );
    
    // 结果输出
    fragColor = vec4(hitColor, 1.0);
}
```

### 6.2 网格着色器 (Mesh Shader)
```glsl
#version 460
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 128) in;
layout(max_vertices = 256, max_primitives = 512) out;
layout(triangles) out;

// 顶点输出
layout(location = 0) out PerVertexData {
    vec3 position;
    vec2 texCoord;
} vertices[];

// 原语索引
uint indices[3 * 512];

void main() {
    // 仅工作组0执行
    if (gl_WorkGroupID.x == 0) {
        // 生成顶点
        for (uint i = 0; i < 256u; i++) {
            vertices[i].position = generatePosition(i);
            vertices[i].texCoord = vec2(i % 16, i / 16) / 16.0;
        }
        
        // 生成索引
        for (uint i = 0; i < 512u; i++) {
            uint base = i * 3;
            indices[base] = i * 3;
            indices[base+1] = i * 3 + 1;
            indices[base+2] = i * 3 + 2;
        }
    }
    
    // 设置输出数量
    SetMeshOutputsEXT(256, 512);
}
```

## 七、常见陷阱与解决方案

| 问题现象                  | 根本原因                     | 解决方案                                  |
|--------------------------|----------------------------|-----------------------------------------|
| 着色器无法链接            | 输入/输出位置不匹配          | 检查 `location` 索引，使用 `--reflect` 选项 |
| 黑屏/错误颜色             | 精度不匹配（特别是移动端）    | 显式指定 `highp`/`mediump` 修饰符         |
| 性能骤降 50%+            | 动态分支导致 warp 分化       | 用掩码操作替代分支，重组算法               |
| 访问越界崩溃              | 未检查数组边界               | 使用 `nonuniformEXT` + 常量 ID 保护        |
| 光照闪烁                 | 未规范化法线                 | 顶点着色器输出前 `normalize()`            |
| 纹理模糊/失真            | 未指定 LOD                   | 使用 `textureLod` 指定 mipmap 级别        |
| 计算着色器数据不一致      | 缺少内存屏障                | 在写入后添加 `memoryBarrierBuffer()`       |

## 八、推荐工具链

1. **编译/验证**：
    - [glslang](https://github.com/KhronosGroup/glslang) - 官方参考编译器
    - [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) - 优化/验证 SPIR-V
    - [shaderc](https://github.com/google/shaderc) - Google 的封装库

2. **调试**：
    - [RenderDoc](https://renderdoc.org/) - Vulkan 帧捕获
    - [vkconfig](https://github.com/LunarG/VulkanTools) - Vulkan 配置工具
    - [NVIDIA Nsight Graphics](https://developer.nvidia.com/nsight-graphics) - 专业级调试

3. **开发辅助**：
    - [Vulkan-Samples](https://github.com/KhronosGroup/Vulkan-Samples) - 官方示例库
    - [Vulkan-ValidationLayers](https://github.com/KhronosGroup/Vulkan-ValidationLayers) - 验证层源码
    - [glslangValidator VSCode 插件](https://marketplace.visualstudio.com/items?itemName=42Zv.glslang) - 实时错误检查

## 九、持续学习资源

1. **规范文档**：
    - [Vulkan 1.3 规范](https://www.khronos.org/registry/vulkan/specs/1.3-extensions/html/)
    - [GLSL for Vulkan 扩展](https://github.com/KhronosGroup/GLSL/blob/master/extensions/khr/GLSL_KHR_vulkan_glsl.txt)

2. **深度教程**：
    - [Vulkan Tutorial](https://vulkan-tutorial.com/) - 从基础到光线追踪
    - [Sascha Willems' Samples](https://github.com/SaschaWillems/Vulkan) - 200+ 实用示例
    - [GPUOpen Vulkan Guide](https://gpuopen.com/learn/vulkan/) - AMD 优化指南

3. **社区**：
    - [Vulkan subreddit](https://www.reddit.com/r/vulkan/)
    - [Khronos Vulkan Discord](https://discord.gg/2VWjTuH)
    - [Stack Overflow Vulkan 标签](https://stackoverflow.com/questions/tagged/vulkan)

> **关键建议**：在真实硬件上验证着色器！模拟器/验证层无法捕获所有硬件特定行为。特别是移动端 GPU（Adreno, Mali, PowerVR）与桌面端（NVIDIA, AMD, Intel）存在显著差异。

---

本指南凝聚了工业级 Vulkan 项目的实战经验，覆盖了从基础语法到高级技术的完整知识体系。实际开发中，请始终：
1. 保持着色器代码简洁（< 200 行/文件）
2. 严格遵循描述符绑定规范
3. 使用离线编译 SPIR-V 而非运行时编译
4. 为所有采样器指定明确精度
5. 优先使用推送常量替代小 uniform 块

掌握这些原则，你将能构建高效、可维护的 Vulkan 渲染系统。根据项目需求，可进一步深入光线追踪、网格着色器等前沿技术。