#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 color;

layout (binding = 0) uniform sampler2D colorTex;

layout (location = 0) in VS_OUT
{
  vec2 texCoord;
} surf;

void sort(inout vec4 values[9])
{
  for (int i = 0; i < 9; ++i)
  {
    for (int j = i + 1; j < 9; ++j)
    {
      float sumI = dot(values[i].rgb, vec3(0.299, 0.587, 0.114));
      float sumJ = dot(values[j].rgb, vec3(0.299, 0.587, 0.114));
      if (sumI > sumJ)
      {
        vec4 tmp = values[i];
        values[i] = values[j];
        values[j] = tmp;
      }
    }
  }
}

vec4 median(inout vec4 values[9])
{
  sort(values);
  return values[4];
}

void main()
{
  vec4 neighbourColors[9] = vec4[9](
    texture(colorTex, surf.texCoord + vec2(-1, -1)/512.0),
    texture(colorTex, surf.texCoord + vec2(-1,  0)/512.0),
    texture(colorTex, surf.texCoord + vec2(-1,  1)/512.0),
    texture(colorTex, surf.texCoord + vec2( 0, -1)/512.0),
    texture(colorTex, surf.texCoord + vec2( 0,  0)/512.0),
    texture(colorTex, surf.texCoord + vec2( 0,  1)/512.0),
    texture(colorTex, surf.texCoord + vec2( 1, -1)/512.0),
    texture(colorTex, surf.texCoord + vec2( 1,  0)/512.0),
    texture(colorTex, surf.texCoord + vec2( 1,  1)/512.0)
  );

  color = median(neighbourColors);
}