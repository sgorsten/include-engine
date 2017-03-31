#include "data-types.h"
#include <string>

image::image(int width, int height) : width{width}, height{height}
{
    auto p = std::malloc(width*height*4);
    if(!p) throw std::bad_alloc();
    pixels = std::unique_ptr<void, std_free_deleter>{p};
}

image::image(int width, int height, std::unique_ptr<void, std_free_deleter> pixels) : width{width}, height{height}, pixels{move(pixels)} 
{

}
