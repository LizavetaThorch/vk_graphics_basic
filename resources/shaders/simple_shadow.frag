#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "common.h"

layout(location = 0) out vec4 out_fragColor;

layout(push_constant) uniform params_t {
  mat4 mProjView;
  mat4 mModel;
  vec3 mColor;
  int sssOn;
} params;



layout (location = 0 ) in VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec3 wTangent;
  vec2 texCoord;
} surf;

layout(binding = 0, set = 0) uniform AppData
{
  UniformParams Params;
};

layout (binding = 1) uniform sampler2D shadowMap;

vec3 subsurfaceScattering(float s)
{
    return vec3(0.9f, 0.2f, 0.2f) * exp(-s*s / 0.0064f) +
           vec3(0.6f, 0.1f, 0.1f) * exp(-s*s / 0.0484f) +
           vec3(0.3f, 0.05f, 0.05f) * exp(-s*s / 0.187f) +
           vec3(0.1f, 0.02f, 0.02f) * exp(-s*s / 0.567f);
}

float computeDepth(vec2 texCoord)
{
    float depth = texture(shadowMap, texCoord).x;
    vec4 transformedPos = Params.lightMatrix * vec4(texCoord, depth, 1.0f);
    vec3 posNDC = transformedPos.xyz / transformedPos.w;
    float depthShadowMap = texture(shadowMap, posNDC.xy * 0.5).x;
    return abs(depthShadowMap - posNDC.z);
}

vec4 applySSS(vec2 texCoord, vec3 lightDir, vec4 baseColor)
{
    float depthDiff = computeDepth(texCoord);
    float s = depthDiff * 1.5f;
    float intensity = max(0.3f + dot(-surf.wNorm, lightDir), 0.0f);
    return vec4(subsurfaceScattering(s), 1.0f) * baseColor * intensity;
}


void main()
{
    const vec4 posLightClipSpace = Params.lightMatrix * vec4(surf.wPos, 1.0f);
    const vec3 posLightSpaceNDC = posLightClipSpace.xyz / posLightClipSpace.w;
    const vec2 shadowTexCoord = posLightSpaceNDC.xy * 0.5f + vec2(0.5f, 0.5f);
    
    const bool outOfView = (shadowTexCoord.x < 0.0001f || shadowTexCoord.x > 0.9999f || shadowTexCoord.y < 0.0091f || shadowTexCoord.y > 0.9999f);
    const float shadow = ((posLightSpaceNDC.z < textureLod(shadowMap, shadowTexCoord, 0).x + 0.001f) || outOfView) ? 1.0f : 0.0f;
    
    vec4 baseColor = vec4(Params.baseColor, 1.0f);
    vec3 lightDir = normalize(Params.lightPos - surf.wPos);
    vec4 lightColor = max(dot(surf.wNorm, lightDir), 0.0f) * baseColor;
    out_fragColor = (lightColor * shadow + vec4(0.1f)) * baseColor;
    
    if (Params.sssOn) {
        out_fragColor += applySSS(shadowTexCoord, lightDir, baseColor);
    }
}

