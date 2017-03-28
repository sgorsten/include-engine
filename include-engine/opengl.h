#ifndef OPENGL_H
#define OPENGL_H

#include <initializer_list>

#define GLEW_STATIC
#include <GL/glew.h>
#include <GLFW/glfw3.h>

GLuint load_texture(const char * filename, GLenum internal_format);
void load_cube_face(GLuint tex, GLint face, const char * filename, GLenum internal_format);
GLuint compile_shader(GLenum type, const char * source);
GLuint link_program(std::initializer_list<GLuint> shaders);

#endif