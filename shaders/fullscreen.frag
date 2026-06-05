#version 450

layout(binding = 0, rgba32f) uniform readonly image2D inputImage;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = imageLoad(inputImage, ivec2(gl_FragCoord.xy));
}
