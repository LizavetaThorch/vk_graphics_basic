#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "common.h"

layout(location = 0) out vec4 out_normal;
layout(location = 1) out vec4 out_flux;

layout(push_constant) uniform params_t
{
    mat4 mProjView;
    mat4 mModel;
    vec4 objectColor;
} params;

layout (location = 0 ) in VS_OUT
{
    vec3 wPos;
    vec3 wNorm;
    vec3 wTangent;
    vec2 texCoord;
} surf;

void main()
{
    vec3 lightDirection = (inverse(params.mProjView) * vec4(0.0, 0.0, 1.0, 0.0)).xyz;

    vec3 normal = normalize(surf.wNorm.xyz);
    float flux = max(0.0, dot(-lightDirection, normal));

    out_normal = vec4(normal, 1.0);
    out_flux = vec4(flux * params.objectColor.rgb, 1.0);
}
