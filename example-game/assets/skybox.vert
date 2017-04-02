#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "scene.glsl"

layout(set=2, binding=0) uniform PerObject
{
	mat4 u_model_matrix;
};

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_texcoord;
layout(location = 3) in vec3 v_tangent;
layout(location = 4) in vec3 v_bitangent;

layout(location = 0) out vec3 direction;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
	direction = v_position;
    gl_Position = u_rotation_only_view_proj_matrix * vec4(v_position, 1);	
}