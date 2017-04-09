#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "scene.glsl"

layout(set=2, binding=0) uniform PerSkinnedObject
{
	mat4 u_bone_matrices[64];
};

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_texcoord;
layout(location = 3) in vec3 v_tangent;
layout(location = 4) in vec3 v_bitangent;

layout(location = 5) in uvec4 v_bone_indices;
layout(location = 6) in vec4 v_bone_weights;

layout(location = 0) out vec3 position;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec2 texcoord;
layout(location = 3) out vec3 tangent;
layout(location = 4) out vec3 bitangent;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
    mat4 model_matrix = u_bone_matrices[v_bone_indices.x] * v_bone_weights.x
		        	  + u_bone_matrices[v_bone_indices.y] * v_bone_weights.y
					  + u_bone_matrices[v_bone_indices.z] * v_bone_weights.z
					  + u_bone_matrices[v_bone_indices.w] * v_bone_weights.w;
	position = (model_matrix * vec4(v_position, 1)).xyz;
	normal = normalize((model_matrix * vec4(v_normal, 0)).xyz);
    texcoord = v_texcoord;
	tangent = normalize((model_matrix * vec4(v_tangent, 0)).xyz);
    bitangent = normalize((model_matrix * vec4(v_bitangent, 0)).xyz);
    gl_Position = u_view_proj_matrix * vec4(position, 1);	
}