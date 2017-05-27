#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec2 v_position;
layout(location = 1) in vec2 v_texcoord;
layout(location = 2) in vec4 v_color;

layout(location = 0) out vec2 texcoord;
layout(location = 1) out vec4 color;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
	gl_Position = vec4(v_position, 0, 1);
	texcoord = v_texcoord;
	color = v_color;
}