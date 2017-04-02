/////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-scene uniforms: Used to define the lighting environment in which all scene rendering occurs //
/////////////////////////////////////////////////////////////////////////////////////////////////////

layout(set=0, binding=0) uniform PerScene
{
	vec3 u_ambient_light;
	vec3 u_light_direction;
	vec3 u_light_color;
};
layout(set=0, binding=1) uniform samplerCube u_env;

vec3 sample_environment(vec3 direction)
{
	return texture(u_env, direction*vec3(1,-1,-1)).rgb;
}

////////////////////////////////////////////////////////////////////////////
// Per-view uniforms: Used to define the world-to-viewport transformation //
////////////////////////////////////////////////////////////////////////////

layout(set=1, binding=0) uniform PerView
{
	mat4 u_view_proj_matrix;
	mat4 u_rotation_only_view_proj_matrix;
	vec3 u_eye_position;
};