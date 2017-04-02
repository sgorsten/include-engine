#ifndef LOAD_H
#define LOAD_H

#include "data-types.h"
#include <vulkan/vulkan.h>

image load_image(const char * filename);

mesh generate_box_mesh(const float3 & bmin, const float3 & bmax);
std::vector<mesh> load_meshes_from_fbx(const char * filename);

class shader_compiler
{
    std::unique_ptr<struct shader_compiler_impl> impl;
public:
    shader_compiler();
    ~shader_compiler();

    std::vector<uint32_t> compile_glsl(VkShaderStageFlagBits stage, const char * filename);
};

#endif
