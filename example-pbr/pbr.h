#ifndef PBR_H
#define PBR_H

#include <vector>
#include <optional>
#include <string_view>

#include "linalg.h"
using namespace linalg::aliases;

#define GLEW_STATIC
#include "GL/glew.h"
#include "GLFW/glfw3.h"
#pragma comment(lib, "opengl32.lib")

extern std::string_view preamble;
extern std::string_view pbr_lighting;

class gl_program
{
    GLuint program = 0;
public:
    gl_program() = default;
    gl_program(std::initializer_list<GLuint> shader_stages);
    gl_program(gl_program && r) : gl_program() { *this = std::move(r); }
    ~gl_program();

    void use() const;
    std::optional<GLint> get_uniform_location(const char * name) const;

    void bind_texture(GLint location, GLuint texture) const;
    void bind_texture(const char * name, GLuint texture) const { if(auto loc = get_uniform_location(name)) bind_texture(*loc, texture); }

    void uniform(GLint location, float scalar);
    void uniform(GLint location, const float2 & vec);
    void uniform(GLint location, const float3 & vec);
    void uniform(GLint location, const float4 & vec);
    void uniform(GLint location, const float4x4 & mat);
    template<class T> void uniform(const char * name, const T & value) { if(auto loc = get_uniform_location(name)) uniform(*loc, value); }

    gl_program & operator = (gl_program && r) { std::swap(program, r.program); return *this; }
};

GLuint compile_shader(GLenum type, std::initializer_list<std::string_view> sources);

class pbr_tools
{
    mutable gl_program spheremap_skybox_prog;
    mutable gl_program cubemap_skybox_prog;
    mutable gl_program irradiance_prog;
    mutable gl_program reflectance_prog;
    mutable gl_program brdf_integration_prog;
public:
    pbr_tools();

    GLuint convert_spheremap_to_cubemap(GLenum internal_format, GLsizei width, GLuint spheremap) const;
    GLuint compute_irradiance_map(GLuint cubemap) const;
    GLuint compute_reflectance_map(GLuint cubemap) const;
    GLuint compute_brdf_integration_map() const;

    void draw_skybox(GLuint cubemap, const float4x4 & skybox_view_proj_matrix) const;
};

#endif