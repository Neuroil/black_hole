#version 450

layout(binding = 0) uniform sampler2D inputImage;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(inputImage, 0));
    outColor = texture(inputImage, uv);
}