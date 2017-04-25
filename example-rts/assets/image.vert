#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec3 v_position;
layout(location = 3) in vec2 v_texcoord;

layout(location = 3) out vec2 texcoord;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
	texcoord = v_texcoord;
    gl_Position = vec4(v_position, 1);		
}