#ifndef SPRITE_H
#define SPRITE_H

#include "renderer.h"

struct rect 
{ 
    int x0, y0, x1, y1; 

    int width() const { return x1 - x0; }
    int height() const { return y1 - y0; }
    int2 dims() const { return {width(), height()}; }
    float aspect_ratio() const { return (float)width()/height(); }

    rect adjusted(int dx0, int dy0, int dx1, int dy1) const { return {x0+dx0, y0+dy0, x1+dx1, y1+dy1}; }

    rect take_x0(int x) { rect r {x0, y0, x0+x, y1}; x0 = r.x1; return r; }
    rect take_x1(int x) { rect r {x1-x, y0, x1, y1}; x1 = r.x0; return r; }
    rect take_y0(int y) { rect r {x0, y0, x1, y0+y}; y0 = r.y1; return r; }
    rect take_y1(int y) { rect r {x0, y1-y, x1, y1}; y1 = r.y0; return r; }
};

// sprite_sheet is a simple class which packs a collection of 2D images into a single atlas texture
struct sprite
{
    image img;              // The contents of the sprite
    int border;             // Number of pixels from the edge of the image which are not considered part of the sprite (but should be copied to the atlas anyway)
    float s0, t0, s1, t1;   // The subrect of this sprite within the texture atlas
};
struct sprite_sheet
{
    image sheet;
    std::shared_ptr<texture> texture;
    std::vector<sprite> sprites;

    size_t add_sprite(image img, int border);
    void prepare_sheet();
};

// font_face rasterizes a collection of text glyphs into a sprite_sheet
struct glyph_info
{
    size_t sprite_index;
    int2 offset;
    int advance;
};
struct font_face
{
    sprite_sheet & sheet;
    std::map<int, glyph_info> glyphs;

    font_face(sprite_sheet & sheet, const char * filepath, float pixel_height);
};

// gui_sprites rasterizes a collection of useful shapes into a sprite_sheet
struct gui_sprites
{
    sprite_sheet & sheet;
    size_t solid_pixel;
    std::map<int, size_t> corner_sprites;

    gui_sprites(sprite_sheet & sheet);
};

struct image_vertex { float2 position, texcoord; float4 color; };
struct gui_context
{
    gui_sprites & sprites;
    draw_list & list;
    uint2 dims;
    uint32_t num_quads;

    gui_context(gui_sprites & sprites, draw_list & list, const uint2 & dims);

    void begin_frame();

    void draw_sprite(const rect & r, float s0, float t0, float s1, float t1, const float4 & color);

    void draw_rect(const rect & r, const float4 & color);
    void draw_rounded_rect(rect r, int radius, const float4 & color);
    void draw_partial_rounded_rect(rect r, int radius, const float4 & color, bool tl, bool tr, bool bl, bool br);
    
    void draw_text(const font_face & font, const float4 & color, int x, int y, std::string_view text);
    void draw_shadowed_text(const font_face & font, const float4 & color, int x, int y, std::string_view text);
    void end_frame(const scene_material & mtl, const sampler & samp);
};

#endif
