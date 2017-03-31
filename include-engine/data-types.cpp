#include "data-types.h"
#include <string>
#include <stb_image.h>

image::image(const char * filename) 
{ 
    pixels = stbi_load(filename, &width, &height, nullptr, 4);
    if(!pixels) throw std::runtime_error(std::string("failed to load ") + filename);
}

image::~image() 
{ 
    stbi_image_free(pixels); 
}