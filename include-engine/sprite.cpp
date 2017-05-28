#include "sprite.h"

gui_context::gui_context(sprite_sheet & sprites, draw_list & list, const uint2 & dims) : sprites{sprites}, list{list}, dims{dims} 
{

}

void gui_context::begin_frame()
{
    list.begin_vertices();
    list.begin_indices();
    num_quads = 0;
}

void gui_context::draw_rect(const float4 & color, int x0, int y0, int x1, int y1, float s0, float t0, float s1, float t1)
{
    const float fx0 = x0*2.0f/dims.x-1;
    const float fy0 = y0*2.0f/dims.y-1;
    const float fx1 = x1*2.0f/dims.x-1;
    const float fy1 = y1*2.0f/dims.y-1;
    list.write_vertex(image_vertex{{fx0,fy0},{s0,t0},color});
    list.write_vertex(image_vertex{{fx0,fy1},{s0,t1},color});
    list.write_vertex(image_vertex{{fx1,fy1},{s1,t1},color});
    list.write_vertex(image_vertex{{fx1,fy0},{s1,t0},color});
    list.write_indices(num_quads*4+uint3{0,1,2});
    list.write_indices(num_quads*4+uint3{0,2,3});
    ++num_quads;
}

void gui_context::draw_text(const float4 & color, int x, int y, std::string_view text)
{
    for(auto ch : text)
    {
        auto it = sprites.glyphs.find(ch);
        if(it == sprites.glyphs.end()) continue;
        auto & b = it->second;
        const int x0 = x + b.xoff, y0 = y + b.yoff, x1 = x0 + b.x1 - b.x0, y1 = y0 + b.y1 - b.y0;
        const float s0 = (float)b.x0/512, t0 = (float)b.y0/512, s1 = (float)b.x1/512, t1 = (float)b.y1/512;
        draw_rect(color, x0, y0, x1, y1, s0, t0, s1, t1);
        x += b.xadvance;
    }
}

void gui_context::draw_shadowed_text(const float4 & color, int x, int y, std::string_view text)
{
    draw_text({0,0,0,color.w},x+1,y+1,text);
    draw_text(color,x,y,text);
}

void gui_context::end_frame(const scene_material & mtl, const sampler & samp)
{
    auto vertex_info = list.end_vertices();
    auto index_info = list.end_indices();
    auto desc = list.descriptor_set(mtl);
    desc.write_combined_image_sampler(0, 0, samp, *sprites.texture);
    list.draw(desc, {vertex_info}, index_info, num_quads*6, 1);
}

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

sprite_sheet bake_font_bitmap(float pixel_height, int first_char, int num_chars)
{
    sprite_sheet sprites;
    sprites.sheet = image{{512,512}, VK_FORMAT_R8_UNORM};
    auto data = load_binary_file("C:/windows/fonts/arial.ttf");
    stbtt_fontinfo f {};
    if(!stbtt_InitFont(&f, data.data(), 0)) throw std::runtime_error("stbtt_InitFont(...) failed");
    memset(sprites.sheet.get_pixels(), 0, sprites.sheet.get_width()*sprites.sheet.get_height());
    const float scale = stbtt_ScaleForPixelHeight(&f, pixel_height);
    int x=1, y=1, bottom_y=1;
    for(int i=0; i<num_chars; ++i)
    {
        const int g = stbtt_FindGlyphIndex(&f, first_char+i);
        int advance, lsb, x0,y0,x1,y1;
        stbtt_GetGlyphHMetrics(&f, g, &advance, &lsb);
        stbtt_GetGlyphBitmapBox(&f, g, scale,scale, &x0,&y0,&x1,&y1);

        const int gw = x1-x0, gh = y1-y0;
        if(x + gw + 1 >= sprites.sheet.get_width()) y = bottom_y, x = 1; // advance to next row
        if(y + gh + 1 >= sprites.sheet.get_height()) throw std::runtime_error("out of space in image");
        STBTT_assert(x+gw < pw);
        STBTT_assert(y+gh < ph);

        stbtt_MakeGlyphBitmap(&f, reinterpret_cast<uint8_t *>(sprites.sheet.get_pixels()+x+y*sprites.sheet.get_width()), gw,gh, sprites.sheet.get_width(), scale,scale, g);
        sprites.glyphs[first_char+i].x0 = x;
        sprites.glyphs[first_char+i].y0 = y;
        sprites.glyphs[first_char+i].x1 = x + gw;
        sprites.glyphs[first_char+i].y1 = y + gh;
        sprites.glyphs[first_char+i].xoff = x0;
        sprites.glyphs[first_char+i].yoff = y0;
        sprites.glyphs[first_char+i].xadvance = static_cast<int>(std::round(scale * advance));
        x += gw + 1;
        if(y+gh+1 > bottom_y) bottom_y = y+gh+1;
    }
    return sprites;
}