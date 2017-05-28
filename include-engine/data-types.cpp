#include "data-types.h"
#include <string>

size_t compute_image_size(int2 dims, VkFormat format)
{
    if(format <= VK_FORMAT_UNDEFINED) throw std::logic_error("unknown format");
    if(format <= VK_FORMAT_R4G4_UNORM_PACK8) return product(dims)*1;
    if(format <= VK_FORMAT_A1R5G5B5_UNORM_PACK16) return product(dims)*2;
    if(format <= VK_FORMAT_R8_SRGB) return product(dims)*1;
    if(format <= VK_FORMAT_R8G8_SRGB) return product(dims)*2;
    if(format <= VK_FORMAT_B8G8R8_SRGB) return product(dims)*3;
    if(format <= VK_FORMAT_A2B10G10R10_SINT_PACK32) return product(dims)*4;
    if(format <= VK_FORMAT_R16_SFLOAT) return product(dims)*2;
    if(format <= VK_FORMAT_R16G16_SFLOAT) return product(dims)*4;
    if(format <= VK_FORMAT_R16G16B16_SFLOAT) return product(dims)*6;
    if(format <= VK_FORMAT_R16G16B16A16_SFLOAT) return product(dims)*8;
    if(format <= VK_FORMAT_R32_SFLOAT) return product(dims)*4;
    if(format <= VK_FORMAT_R32G32_SFLOAT) return product(dims)*8;
    if(format <= VK_FORMAT_R32G32B32_SFLOAT) return product(dims)*12;
    if(format <= VK_FORMAT_R32G32B32A32_SFLOAT) return product(dims)*16;
    if(format <= VK_FORMAT_E5B9G9R9_UFLOAT_PACK32) return product(dims)*4;
    throw std::logic_error("unknown format");
}

image::image(int2 dims, VkFormat format) : dims{dims}, format{format}
{
    auto p = std::malloc(compute_image_size(dims,format));
    if(!p) throw std::bad_alloc();
    pixels.reset(reinterpret_cast<byte *>(p));
}

image::image(int2 dims, VkFormat format, std::unique_ptr<byte, std_free_deleter> pixels) : dims{dims}, format{format}, pixels{move(pixels)} 
{

}