#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "scene.glsl"

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

	vec3 light = u_ambient_light;

	vec3 light_vec = u_light_direction;
	light += albedo * u_light_color * max(dot(normal_vec, light_vec), 0);
	vec3 half_vec = normalize(light_vec + eye_vec);                
	light += u_light_color * pow(max(dot(normal_vec, half_vec), 0), 128);

	f_color = vec4(light,1);
}