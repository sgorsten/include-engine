#version 450

uniform mat4 u_model_matrix;
uniform mat4 u_view_proj_matrix;

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_texcoord;
layout(location = 3) in vec3 v_tangent;
layout(location = 4) in vec3 v_bitangent;

out vec3 position;
out vec3 normal;
out vec2 texcoord;
out vec3 tangent;
out vec3 bitangent;

void main() 
{ 
    position = (u_model_matrix * vec4(v_position, 1)).xyz;
    normal = normalize((u_model_matrix * vec4(v_normal, 0)).xyz);
    texcoord = v_texcoord;
    tangent = normalize((u_model_matrix * vec4(v_tangent, 0)).xyz);
    bitangent = normalize((u_model_matrix * vec4(v_bitangent, 0)).xyz);
    gl_Position = u_view_proj_matrix * vec4(position, 1); 
}
