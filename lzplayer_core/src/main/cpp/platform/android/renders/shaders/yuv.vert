#version 450
// 顶点位置与纹理坐标都由 CPU 算好：位置里含 fit-inside 缩放，
// 纹理坐标里含画面旋转(在无量纲的纹理空间转，不会被视口宽高比拉变形)
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
