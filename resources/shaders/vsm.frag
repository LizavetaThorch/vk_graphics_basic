#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec2 fragColor;

layout(location = 0) in VS_OUT
{
    vec3 wPos;
    vec3 wNorm;
    vec3 wTangent;
    vec2 texCoord;
} surf;

void main()
{
    float zDepth = gl_FragCoord.z;

    vec2 moments;
    moments.x = zDepth;
    moments.y = pow(zDepth, 2);

    fragColor = moments;
}
