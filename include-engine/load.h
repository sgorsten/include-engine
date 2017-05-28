#ifndef LOAD_H
#define LOAD_H

#include "data-types.h"

std::vector<uint8_t> load_binary_file(const char * filename);
std::vector<char> load_text_file(const char * filename);

image generate_single_color_image(const byte4 & color);
image load_image(const char * filename, bool is_linear);

mesh generate_fullscreen_quad();
mesh generate_box_mesh(const float3 & bmin, const float3 & bmax);
mesh apply_vertex_color(mesh m, const float3 & color);
mesh invert_faces(mesh m);
std::vector<mesh> load_meshes_from_fbx(coord_system target, const char * filename);
mesh load_mesh_from_obj(coord_system target, const char * filename);
shader_info load_shader_info_from_spirv(array_view<uint32_t> words);

class shader_compiler
{
    std::unique_ptr<struct shader_compiler_impl> impl;
public:
    shader_compiler();
    ~shader_compiler();

    std::vector<uint32_t> compile_glsl(VkShaderStageFlagBits stage, const char * filename);
};

#endif
