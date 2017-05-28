#ifndef SPRITE_H
#define SPRITE_H

#include "renderer.h"

struct glyph_info
{
   int x0,y0,x1,y1; // coordinates of bbox in bitmap
   int xoff,yoff;
   int xadvance;
};

struct sprite_sheet
{
    image sheet;
    std::shared_ptr<texture> texture;
    std::map<int, glyph_info> glyphs;
};
sprite_sheet bake_font_bitmap(float pixel_height, int first_char, int num_chars);

struct image_vertex { float2 position, texcoord; float4 color; };
struct gui_context
{
    sprite_sheet & sprites;
    draw_list & list;
    uint2 dims;
    uint32_t num_quads;

    gui_context(sprite_sheet & sprites, draw_list & list, const uint2 & dims);

    void begin_frame();
    void draw_rect(const float4 & color, int x0, int y0, int x1, int y1, float s0, float t0, float s1, float t1);
    void draw_text(const float4 & color, int x, int y, std::string_view text);
    void draw_shadowed_text(const float4 & color, int x, int y, std::string_view text);
    void end_frame(const scene_material & mtl, const sampler & samp);
};

#endif
