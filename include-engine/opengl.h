#ifndef OPENGL_H
#define OPENGL_H

#define GLEW_STATIC
#include <GL/glew.h>
#include <GLFW/glfw3.h>

GLuint load_texture(const char * filename);

#endif