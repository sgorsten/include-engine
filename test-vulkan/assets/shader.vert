#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set=0, binding=0) uniform PerView
{
	mat4 u_view_proj_matrix;
};

layout(set=1, binding=0) uniform PerObject
{
	mat4 u_model_matrix;
};

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
    fragColor = inColor;
    gl_Position = u_view_proj_matrix * u_model_matrix * vec4(inPosition, 0.0, 1.0);	
}