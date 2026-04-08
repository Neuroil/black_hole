#version 450

// 输出到屏幕的颜色（通常是 location 0）
layout(location = 0) out vec4 outColor;

void main() {
    // 输出橘黄色（或者任何你喜欢的颜色），方便在黑色背景中看清
    outColor = vec4(1.0, 0.5, 0.0, 1.0);
}