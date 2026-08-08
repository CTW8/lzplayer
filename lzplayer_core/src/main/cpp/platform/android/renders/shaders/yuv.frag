#version 450
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D yTexture;
layout(set = 0, binding = 1) uniform sampler2D uTexture;
layout(set = 0, binding = 2) uniform sampler2D vTexture;

// 色彩系数与量程偏移由 CPU 按帧的 color_range/colorspace 算好传进来，
// 与 GLES 渲染器同一套判定(BT.601/709 x full/limited)
layout(push_constant) uniform ColorParams {
    mat3 colorMat;
    vec3 colorOffset;
} params;

void main() {
    vec3 yuv = vec3(texture(yTexture, vTexCoord).r,
                    texture(uTexture, vTexCoord).r,
                    texture(vTexture, vTexCoord).r) - params.colorOffset;
    fragColor = vec4(clamp(params.colorMat * yuv, 0.0, 1.0), 1.0);
}
