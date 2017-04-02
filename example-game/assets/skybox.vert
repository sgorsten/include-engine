#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "scene.glsl"

layout(location = 0) in vec3 v_position;

layout(location = 0) out vec3 direction;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
	direction = v_position;
    gl_Position = u_rotation_only_view_proj_matrix * vec4(v_position, 1);	
}