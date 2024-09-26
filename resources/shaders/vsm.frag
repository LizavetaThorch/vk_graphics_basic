#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec2 out_fragColor;

layout(location = 0) in VS_OUT
{
    vec3 wPos;
    vec3 wNorm;
    vec3 wTangent;
    vec2 texCoord;
} surf;

void main()
{
    float depth = gl_FragCoord.z;

    float moment1 = depth;
    float moment2 = depth * depth;
    out_fragColor = vec2(moment1, moment2);
}
