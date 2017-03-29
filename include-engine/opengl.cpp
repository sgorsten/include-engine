#include "opengl.h"
#include <stdexcept>
#include <algorithm>
#pragma comment(lib, "opengl32.lib")

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

GLuint compile_shader(GLenum type, const char * source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if(status != GL_TRUE)
    {
        GLint length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

        std::string log(length, ' ');
        glGetShaderInfoLog(shader, length, nullptr, &log[0]);
        glDeleteShader(shader);
        throw std::runtime_error("compile error " + log);
    }

    return shader;
}

GLuint link_program(std::initializer_list<GLuint> shaders)
{
    GLuint program = glCreateProgram();
    for(auto shader : shaders) glAttachShader(program, shader);
    glLinkProgram(program);

    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if(status != GL_TRUE)
    {
        GLint length;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);

        std::string log(length, ' ');
        glGetProgramInfoLog(program, length, nullptr, &log[0]);
        glDeleteProgram(program);
        throw std::runtime_error("link error " + log);
    }

    return program;
}



static int compute_max_mipmap_levels(int max_edge_length) { return 1 + static_cast<int>(std::floor(std::log2(max_edge_length))); }

texture_2d::texture_2d(GLenum internal_format, int width, int height)
{
    glCreateTextures(GL_TEXTURE_2D, 1, &tex_name);
    glTextureStorage2D(tex_name, compute_max_mipmap_levels(std::max(width, height)), internal_format, width, height);
}

void texture_2d::upload(const image & image)
{
    switch(image.get_channels())
    {
    case 1: glTextureSubImage2D(tex_name, 0, 0, 0, image.get_width(), image.get_height(), GL_LUMINANCE, GL_UNSIGNED_BYTE, image.get_pixels()); break;
    case 2: glTextureSubImage2D(tex_name, 0, 0, 0, image.get_width(), image.get_height(), GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, image.get_pixels()); break;
    case 3: glTextureSubImage2D(tex_name, 0, 0, 0, image.get_width(), image.get_height(), GL_RGB, GL_UNSIGNED_BYTE, image.get_pixels()); break;
    case 4: glTextureSubImage2D(tex_name, 0, 0, 0, image.get_width(), image.get_height(), GL_RGBA, GL_UNSIGNED_BYTE, image.get_pixels()); break;
    }
}

void texture_2d::generate_mipmaps()
{ 
    glGenerateTextureMipmap(tex_name); 
}

void texture_2d::set_parameter(GLenum pname, GLint param)
{ 
    glTextureParameteri(tex_name, pname, param); 
}



texture_cube::texture_cube(GLenum internal_format, int edge_length) : tex_name{}, array_view{}
{
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &tex_name);
    glTextureStorage2D(tex_name, compute_max_mipmap_levels(edge_length), internal_format, edge_length, edge_length);

    glGenTextures(1, &array_view);
    glTextureView(array_view, GL_TEXTURE_2D_ARRAY, tex_name, internal_format, 0, 1, 0, 6);
}

void texture_cube::upload_face(int face, const image & image)
{
    switch(image.get_channels())
    {
    case 1: glTextureSubImage3D(array_view, 0, 0, 0, face, image.get_width(), image.get_height(), 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, image.get_pixels()); break;
    case 2: glTextureSubImage3D(array_view, 0, 0, 0, face, image.get_width(), image.get_height(), 1, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, image.get_pixels()); break;
    case 3: glTextureSubImage3D(array_view, 0, 0, 0, face, image.get_width(), image.get_height(), 1, GL_RGB, GL_UNSIGNED_BYTE, image.get_pixels()); break;
    case 4: glTextureSubImage3D(array_view, 0, 0, 0, face, image.get_width(), image.get_height(), 1, GL_RGBA, GL_UNSIGNED_BYTE, image.get_pixels()); break;
    }
}

void texture_cube::generate_mipmaps() 
{ 
    glGenerateTextureMipmap(tex_name); 
}

void texture_cube::set_parameter(GLenum pname, GLint param) 
{ 
    glTextureParameteri(tex_name, pname, param); 
}