#ifndef OPENGL_H
#define OPENGL_H

#include <initializer_list>

#define GLEW_STATIC
#include <GL/glew.h>
#include <GLFW/glfw3.h>

GLuint load_texture(const char * filename);
GLuint compile_shader(GLenum type, const char * source);
GLuint link_program(std::initializer_list<GLuint> shaders);

#endif