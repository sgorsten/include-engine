#ifndef LOAD_H
#define LOAD_H

#include "data-types.h"
#include <vulkan/vulkan.h>

#include "fbx.h"

image generate_single_color_image(const byte4 & color);
image load_image(const char * filename);

mesh generate_fullscreen_quad();
mesh generate_box_mesh(const float3 & bmin, const float3 & bmax);
mesh apply_vertex_color(mesh m, const float3 & color);
mesh invert_faces(mesh m);
std::vector<mesh> load_meshes_from_fbx(coord_system target, const char * filename);
mesh load_mesh_from_obj(coord_system target, const char * filename);

struct shader_info
{
    struct type;
    enum scalar_type { uint_, int_, float_, double_ };
    struct matrix_layout { uint32_t stride; bool row_major; };
    struct structure_member { std::string name; std::unique_ptr<const type> type; std::optional<uint32_t> offset; };
    struct numeric { scalar_type scalar; uint32_t row_count, column_count; std::optional<matrix_layout> matrix_layout; };
    struct sampler { scalar_type channel; VkImageViewType view_type; bool multisampled, shadow; };
    struct array { std::unique_ptr<const type> element; uint32_t length; std::optional<uint32_t> stride; };
    struct structure { std::string name; std::vector<structure_member> members; };
    struct type { std::variant<sampler, numeric, array, structure> contents; };
    struct descriptor { uint32_t set, binding; std::string name; type type; };

    VkShaderStageFlagBits stage;
    std::string name;
    std::vector<descriptor> descriptors;

    shader_info(array_view<uint32_t> words);
};

class shader_compiler
{
    std::unique_ptr<struct shader_compiler_impl> impl;
public:
    shader_compiler();
    ~shader_compiler();

    std::vector<uint32_t> compile_glsl(VkShaderStageFlagBits stage, const char * filename);
};

#endif
