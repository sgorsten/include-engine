/////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-scene uniforms: Used to define the lighting environment in which all scene rendering occurs //
/////////////////////////////////////////////////////////////////////////////////////////////////////

struct point_light
{
	vec3 position;
	vec3 color;
};

layout(set=0, binding=0) uniform PerScene
{
	mat4 u_shadow_map_matrix;
	vec3 u_shadow_light_pos;
	vec3 u_ambient_light;
	vec3 u_light_direction;
	vec3 u_light_color;
	point_light u_point_lights[64];
	int u_num_point_lights;
};
layout(set=0, binding=1) uniform sampler2D u_shadow_map;

////////////////////////////////////////////////////////////////////////////
// Per-view uniforms: Used to define the world-to-viewport transformation //
////////////////////////////////////////////////////////////////////////////

layout(set=1, binding=0) uniform PerView
{
	mat4 u_view_proj_matrix;
	vec3 u_eye_position;
	vec3 u_eye_x_axis;
	vec3 u_eye_y_axis;
};