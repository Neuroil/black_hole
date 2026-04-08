#version 450

layout(location = 0) in vec3 fragColor;
// 输出到屏幕的颜色（通常是 location 0）
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}