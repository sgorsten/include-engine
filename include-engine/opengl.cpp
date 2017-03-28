#include "opengl.h"
#include <stdexcept>
#include <algorithm>
#pragma comment(lib, "opengl32.lib")

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

GLuint load_texture(const char * filename, GLenum internal_format)
{
    int width, height, comp;
    auto image = stbi_load(filename, &width, &height, &comp, 3);
    if(!image) throw std::runtime_error(std::string("failed to load ")+filename);
    GLuint tex;
    glCreateTextures(GL_TEXTURE_2D, 1, &tex);
    glTextureStorage2D(tex, static_cast<GLsizei>(std::floor(std::log2(std::max(width,height)))), internal_format, width, height);
    switch(comp)
    {
    case 1: glTextureSubImage2D(tex, 0, 0, 0, width, height, GL_LUMINANCE, GL_UNSIGNED_BYTE, image); break;
    case 2: glTextureSubImage2D(tex, 0, 0, 0, width, height, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, image); break;
    case 3: glTextureSubImage2D(tex, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, image); break;
    case 4: glTextureSubImage2D(tex, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image); break;
    }
    glGenerateTextureMipmap(tex);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(image);
    return tex;
}

void load_cube_face(GLuint tex, GLint face, const char * filename, GLenum internal_format)
{
    int width, height, comp;
    auto image = stbi_load(filename, &width, &height, &comp, 3);
    if(!image) throw std::runtime_error(std::string("failed to load ")+filename);
    GLuint view;
    glGenTextures(1, &view);
    glTextureView(view, GL_TEXTURE_2D, tex, internal_format, 0, 1, face, 1);
    switch(comp)
    {
    case 1: glTextureSubImage2D(view, 0, 0, 0, width, height, GL_LUMINANCE, GL_UNSIGNED_BYTE, image); break;
    case 2: glTextureSubImage2D(view, 0, 0, 0, width, height, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, image); break;
    case 3: glTextureSubImage2D(view, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, image); break;
    case 4: glTextureSubImage2D(view, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image); break;
    }
    stbi_image_free(image);
    glDeleteTextures(1, &view);
}

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