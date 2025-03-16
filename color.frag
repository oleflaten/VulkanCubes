#version 440

layout(push_constant) uniform PC {
    mat4 mvp;       //layout(offset = 64) was used instead of this
    vec3 color;
} pc;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vec4(pc.color, 1.0);
}
