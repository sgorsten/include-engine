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
