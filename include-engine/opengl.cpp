#include "opengl.h"
#include <stdexcept>
#pragma comment(lib, "opengl32.lib")

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

GLuint load_texture(const char * filename)
{
    int width, height, comp;
    auto image = stbi_load(filename, &width, &height, &comp, 3);
    if(!image) throw std::runtime_error(std::string("failed to load ")+filename);
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    switch(comp)
    {
    case 1: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,  width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, image); break;
    case 2: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, image); break;
    case 3: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,  width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image); break;
    case 4: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image); break;
    }
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(image);
    return tex;
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