#ifndef LOAD_H
#define LOAD_H

#include "data-types.h"

image load_image(const char * filename);
std::vector<mesh> load_meshes_from_fbx(const char * filename);
std::vector<uint32_t> load_spirv_binary(const char * filename);

class shader_compiler
{
    std::unique_ptr<struct shader_compiler_impl> impl;
public:
    shader_compiler();
    ~shader_compiler();

    std::vector<uint32_t> compile_glsl(const char * filename);
};

#endif
