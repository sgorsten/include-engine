#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set=1, binding=1) uniform sampler2D u_albedo;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord;

layout(location = 0) out vec4 f_color;

void main() 
{
    f_color = texture(u_albedo, texcoord) + vec4(0.2f,0,0,1); //vec4(normal, 1.0);
}