#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "scene.glsl"

layout(set=2, binding=0) uniform PerObject
{
	mat4 u_model_matrix;
	vec3 u_emissive_mtl;
};
layout(set=2, binding=1) uniform sampler2D u_albedo;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 texcoord;
layout(location = 4) in vec3 tangent;
layout(location = 5) in vec3 bitangent;

layout(location = 0) out vec4 f_color;

void main() 
{
	vec3 eye_vec = normalize(u_eye_position - position);
	vec3 albedo = texture(u_albedo, texcoord).rgb * color;
	vec3 normal_vec = normalize(normal);

	vec3 light = u_ambient_light + u_emissive_mtl;

	// directional light (using shadow map)
	{
		float lit = textureProj(u_shadow_map, u_shadow_map_matrix * vec4(position, 1)).r;
		vec3 light_vec = normalize(u_shadow_light_pos - position); //u_light_direction;
		float diffuse = max(dot(normal_vec, light_vec), 0) * lit;
		light += albedo * u_light_color * diffuse;
	}

	for(int i=0; i<u_num_point_lights; ++i)
	{
		vec3 light_vec = u_point_lights[i].position - position;
		float light_dist = length(light_vec);
		light_vec /= light_dist;
		float diffuse = max(dot(normal_vec, light_vec), 0) / light_dist; // Note: Linear falloff
		light += albedo * u_point_lights[i].color * diffuse;
	}

	f_color = vec4(light,1);
}