#ifndef OPENGL_H
#define OPENGL_H

#include "data-types.h"
#include <memory>
#include <initializer_list>

#define GLEW_STATIC
#include <GL/glew.h>
#include <GLFW/glfw3.h>

GLuint compile_shader(GLenum type, const char * source);
GLuint link_program(std::initializer_list<GLuint> shaders);

class texture_2d
{
    GLuint tex_name;
public:
    texture_2d(GLenum internal_format, int width, int height);
    texture_2d(const texture_2d &) = delete;
    texture_2d(texture_2d &&) = delete;
    texture_2d & operator = (const texture_2d &) = delete;
    texture_2d & operator = (texture_2d &&) = delete;

    GLuint get_texture_name() const { return tex_name; }

    void upload(const image & image);
    void generate_mipmaps();
    void set_parameter(GLenum pname, GLint param);
};

class texture_cube
{
    GLuint tex_name, array_view;
public:
    texture_cube(GLenum internal_format, int edge_length);
    texture_cube(const texture_cube &) = delete;
    texture_cube(texture_cube &&) = delete;
    texture_cube & operator = (const texture_cube &) = delete;
    texture_cube & operator = (texture_cube &&) = delete;

    GLuint get_texture_name() const { return tex_name; }

    void upload_face(int face, const image & image);
    void generate_mipmaps();
    void set_parameter(GLenum pname, GLint param);
};

inline std::unique_ptr<texture_2d> load_texture_2d(GLenum internal_format, const image & image)
{
    auto tex = std::make_unique<texture_2d>(internal_format, image.get_width(), image.get_height());
    tex->upload(image);
    tex->generate_mipmaps();
    tex->set_parameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    tex->set_parameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return tex;
}

inline std::unique_ptr<texture_cube> load_texture_cube(GLenum internal_format, const image & posx, const image & negx, const image & posy, const image & negy, const image & posz, const image & negz)
{
    auto tex = std::make_unique<texture_cube>(internal_format, posx.get_width());
    tex->upload_face(0, posx);
    tex->upload_face(1, negx);
    tex->upload_face(2, posy);
    tex->upload_face(3, negy);
    tex->upload_face(4, posz);
    tex->upload_face(5, negz);
    tex->generate_mipmaps();
    tex->set_parameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    tex->set_parameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    return tex;
}

#endif