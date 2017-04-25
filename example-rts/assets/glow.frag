#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "scene.glsl"

layout(location = 1) in vec3 color;

layout(location = 0) out vec4 f_color;

void main() 
{
	f_color = vec4(color,1);
}