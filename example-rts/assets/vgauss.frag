#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

layout(set=0, binding=0) uniform sampler2D u_texture;

layout(location = 0) in vec2 texcoord;

layout(location = 0) out vec4 f_color;

void main() 
{
	vec2 step = vec2(0,1.0/textureSize(u_texture,0).y);
	f_color = texture(u_texture, texcoord-step*3) * ( 1.0/64)
			+ texture(u_texture, texcoord-step*2) * ( 6.0/64)
			+ texture(u_texture, texcoord-step*1) * (15.0/64)
			+ texture(u_texture, texcoord+step*0) * (20.0/64)
			+ texture(u_texture, texcoord+step*1) * (15.0/64)
			+ texture(u_texture, texcoord+step*2) * ( 6.0/64)
			+ texture(u_texture, texcoord+step*3) * ( 1.0/64);
}