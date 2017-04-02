layout(set=0, binding=0) uniform PerScene
{
	vec3 u_ambient_light;
	vec3 u_light_direction;
	vec3 u_light_color;
};
layout(set=0, binding=1) uniform samplerCube u_env;

layout(set=1, binding=0) uniform PerView
{
	mat4 u_view_proj_matrix;
	mat4 u_rotation_only_view_proj_matrix;
	vec3 u_eye_position;
};