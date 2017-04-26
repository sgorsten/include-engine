 #version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "scene.glsl"

layout(location = 0) in vec3 v_offset;
layout(location = 1) in vec2 v_texcoord;
layout(location = 2) in vec3 i_position;
layout(location = 3) in vec3 i_color;

layout(location = 0) out vec2 texcoord;
layout(location = 1) out vec3 color;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
	vec3 position = i_position + u_eye_x_axis*v_offset.x + u_eye_y_axis*v_offset.y;
	texcoord = v_texcoord;
	color = i_color;    
    gl_Position = u_view_proj_matrix * vec4(position, 1);	
}