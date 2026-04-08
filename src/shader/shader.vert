//#version 450
//#extension GL_ARB_separate_shader_objects : enable
//#extension GL_ARB_shading_language_420pack : enable
//
//// --- C++ 端定义的 PixelResult 结构体在 GLSL 中的等效声明 ---
//// 必须确保与 C++ 端（以及 Compute Shader）的内存布局和大小一致。
//struct PixelResult {
//    // Compute Shader 计算出的最终颜色
//    vec4 finalColor;
//    // Compute Shader 计算出的击中点位置（用于深度测试）
//    vec4 hitPosition_and_padding;
//    // ... 其他数据 ...
//};
//
//// --- Binding 1: SSBO (Shader Storage Buffer Object) ---
//// 用于从 Compute Shader 读取计算结果。
//layout(std430, binding = 1) readonly buffer PixelDataSSBO {
//    PixelResult pixels[];
//} pixelBuffer;
//
///*
//// --- Binding 0: UBO (Uniform Buffer Object) ---
//// 用于读取全局参数，例如投影矩阵和 Viewport 大小。
//layout(std140, binding = 0) uniform CameraParams {
//    mat4 view;
//    mat4 proj;
//    // ... 其他参数 ...
//    float screenWidth;
//    float screenHeight;
//} ubo;
//*/
//
//// --- 输出：传递给 Fragment Shader 的数据 ---
//// 传递颜色结果
//layout(location = 0) out vec4 fragColor;
//// 传递深度值
//layout(location = 1) out float fragDepth;
//
//
//void main() {
//    // 1. 获取 SSBO 索引
//    // 由于我们只使用 vkCmdDraw(3, 1, 0, 0) 来绘制一个三角形，gl_VertexIndex 将依次是 0, 1, 2。
//    // 我们在这里使用 gl_VertexIndex 来生成一个覆盖整个屏幕的四边形坐标。
//    // 这种方法避免了绑定顶点缓冲区。
//
//    vec2 pos;
//    // 硬编码全屏四边形的三个顶点（在裁剪空间 [-1, 1] x [-1, 1]）
//    switch (gl_VertexIndex) {
//        case 0: pos = vec2(-1.0, -1.0); break;
//        case 1: pos = vec2( 3.0, -1.0); break; // 扩大范围以覆盖屏幕
//        case 2: pos = vec2(-1.0,  3.0); break;
//    }
//
//    // 2. 将裁剪空间坐标 (pos) 转换回像素坐标
//    // 屏幕像素坐标 (UV = [0, 1] x [0, 1])
//    vec2 uv = (pos + 1.0) / 2.0;
//
//    // 3. 计算 SSBO 数组的索引
//    // 注意：uv.y 必须反转，因为 Vulkan Y轴朝下，而 GLSL 纹理坐标 Y轴通常朝上。
//    // 但是在 Compute Shader 中通常也是 Y轴朝下计算，所以我们可能不需要反转。
//    // 假设 Compute Shader 是按 (x, y) 顺序写入的：
//    uint x = uint(uv.x * ubo.screenWidth);
//    uint y = uint(uv.y * ubo.screenHeight);
//
//    // 确保不越界 (通常不需要，但更安全)
//    x = clamp(x, 0u, uint(ubo.screenWidth) - 1u);
//    y = clamp(y, 0u, uint(ubo.screenHeight) - 1u);
//
//    uint index = y * uint(ubo.screenWidth) + x;
//
//    // 4. 从 SSBO 读取数据
//    PixelResult result = pixelBuffer.pixels[index];
//
//    // 5. 设置输出和传递数据
//    // 将读取到的颜色传递给 Fragment Shader
//    fragColor = result.finalColor;
//
//    // 将读取到的深度值（或路径长度）转换为裁剪空间 Z 值
//    // 你的 SSBO 结构体中需要包含一个 float 类型的深度/距离字段，这里使用 hitPosition 的 z 分量作为示例
//    float depthValue = result.hitPosition_and_padding.z;
//
//    // 设置最终位置（裁剪空间坐标）
//    gl_Position = vec4(pos, depthValue, 1.0);
//    // 注意：这里将深度值直接写入 Z，这取决于你是否使用深度测试，以及你的深度值范围。
//    // 理想情况下，Compute Shader 应该输出一个 [0, 1] 范围的深度值。
//}
#version 450

// 从 UBO 中读取顶点的齐次化坐标 (vec4)，注意使用 std140 布局以保证与 C++ 端的 std140/GLM 对齐
layout(std140, set = 0, binding = 0) uniform ObjectPositions {
    vec4 positions[3];
} ubo;

layout(location = 0) out vec3 fragColor;

void main() {
    // 直接使用 UBO 中的齐次坐标（glm::vec4 对应 GLSL 的 vec4）
    gl_Position = ubo.positions[gl_VertexIndex];

    // 简单根据顶点索引输出颜色以便观测
    if (gl_VertexIndex == 0) fragColor = vec3(1.0, 0.0, 0.0);
    else if (gl_VertexIndex == 1) fragColor = vec3(0.0, 1.0, 0.0);
    else fragColor = vec3(0.0, 0.0, 1.0);
}