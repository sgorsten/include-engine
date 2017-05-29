#include "sprite.h"

//////////////////
// sprite_sheet //
//////////////////

size_t sprite_sheet::add_sprite(image img, int border)
{
    const size_t index = sprites.size();
    sprites.push_back({std::move(img), border});
    return index;
}

void sprite_sheet::prepare_sheet()
{
    // Sort glyphs by descending height, then descending width
    std::vector<sprite *> sorted_sprites;
    for(auto & g : sprites) sorted_sprites.push_back(&g);
    std::sort(begin(sorted_sprites), end(sorted_sprites), [](const sprite * a, const sprite * b)
    {
        return std::make_tuple(a->img.get_height(), a->img.get_width()) > std::make_tuple(b->img.get_height(), b->img.get_width());
    });

    int2 tex_dims = {64, 64};
    while(true)
    {
        sheet = image{tex_dims, VK_FORMAT_R8_UNORM};
        memset(sheet.get_pixels(), 0, sheet.get_width()*sheet.get_height());
        bool bad_pack = false;
        int2 used {0, 0};
        int next_y = 0;
        for(auto * s : sorted_sprites)
        {
            if(used.x + s->img.get_width() > sheet.get_width()) used = {0, next_y};
            if(used.x + s->img.get_width() > sheet.get_width() || used.y + s->img.get_height() > sheet.get_height()) 
            {
                bad_pack = true;
                break;
            }

            s->s0 = static_cast<float>(used.x+s->border)/sheet.get_width();
            s->t0 = static_cast<float>(used.y+s->border)/sheet.get_height();
            s->s1 = static_cast<float>(used.x+s->img.get_width()-s->border)/sheet.get_width();
            s->t1 = static_cast<float>(used.y+s->img.get_height()-s->border)/sheet.get_height();

            for(int i=0; i<s->img.get_height(); ++i)
            {
                memcpy(sheet.get_pixels()+sheet.get_width()*(used.y+i)+used.x, s->img.get_pixels()+s->img.get_width()*i, s->img.get_width());
            }

            used.x += s->img.get_width();
            next_y = std::max(next_y, used.y + s->img.get_height());
        }
        if(bad_pack)
        {
            if(tex_dims.x == tex_dims.y) tex_dims.x *= 2;
            else tex_dims.y *= 2;
        }
        else break;
    }
}

/////////////////
// gui_sprites //
/////////////////

static void compute_circle_quadrant_coverage(float coverage[], int radius)
{
    const float rr = static_cast<float>(radius * radius);
    auto function = [rr](float x) { return sqrt(rr - x*x); };
    auto antiderivative = [rr, function](float x) { return (x * function(x) + rr * atan(x/function(x))) / 2; };
    auto integral = [antiderivative](float x0, float x1) { return antiderivative(x1) - antiderivative(x0); };

    for(int i=0; i<radius; ++i)
    {
        const float x0 = i+0.0f, x1 = i+1.0f;
        const float y0 = function(x0);
        const float y1 = function(x1);
        const int y0i = (int)y0, y1i = (int)y1;

        for(int j=i; j<y1i; ++j)
        {
            coverage[i*radius+j] = coverage[j*radius+i] = 1.0f;
        }

        if(y0i == y1i)
        {
            float c = integral(x0, x1) - y1i*(x1-x0);
            coverage[i*radius+y1i] = c;
            coverage[y1i*radius+i] = c;
        }
        else
        {
            const float cross_x = function(static_cast<float>(y0i)); // X location where curve passes from pixel y0i to pixel y1i

            // Coverage for pixel at (i,y0i) is the area under the curve from x0 to cross_x
            if(y0i < radius) coverage[i*radius+y0i] = coverage[y0i*radius+i] = integral(x0, cross_x) - y0i*(cross_x-x0);

            // Coverage for pixel at (i,y1i) is the area of a rectangle from x0 to cross_x, and the area under the curve from cross_x to x1
            if(y1i == y0i - 1) coverage[i*radius+y1i] = coverage[y1i*radius+i] = (cross_x-x0) + integral(cross_x, x1) - y1i*(x1-cross_x);
            else break; // Stop after the first octant
        }
    }
}

image make_bordered_circle_quadrant(int radius)
{
    std::vector<float> coverage(radius*radius);
    compute_circle_quadrant_coverage(coverage.data(), radius);
    auto in = coverage.data();

    const int width = radius+2;
    image img {{width,width}, VK_FORMAT_R8_UNORM};
    auto out = reinterpret_cast<uint8_t *>(img.get_pixels()); 
    *out++ = 255;
    for(int i=0; i<radius; ++i) *out++ = 255;
    *out++ = 0;
    for(int i=0; i<radius; ++i)
    {
        *out++ = 255;
        for(int i=0; i<radius; ++i) *out++ = static_cast<uint8_t>(*in++ * 255);
        *out++ = 0;
    }
    for(int i=0; i<width; ++i) *out++ = 0;

    return std::move(img);
}

gui_sprites::gui_sprites(sprite_sheet & sheet) : sheet{sheet}
{
    image solid_pixel_img {{1,1}, VK_FORMAT_R8_UNORM};
    *solid_pixel_img.get_pixels() = (byte)0xFF;
    solid_pixel = sheet.add_sprite(std::move(solid_pixel_img), 0);
    for(int i=1; i<=32; ++i) corner_sprites[i] = sheet.add_sprite(make_bordered_circle_quadrant(i), 1);
}

/////////////////
// gui_context //
/////////////////

gui_context::gui_context(gui_sprites & sprites, draw_list & list, const uint2 & dims) : sprites{sprites}, list{list}, dims{dims} 
{

}

void gui_context::begin_frame()
{
    list.begin_vertices();
    list.begin_indices();
    num_quads = 0;
}

void gui_context::draw_sprite(const rect & r, float s0, float t0, float s1, float t1, const float4 & color)
{
    const float fx0 = r.x0*2.0f/dims.x-1;
    const float fy0 = r.y0*2.0f/dims.y-1;
    const float fx1 = r.x1*2.0f/dims.x-1;
    const float fy1 = r.y1*2.0f/dims.y-1;
    list.write_vertex(image_vertex{{fx0,fy0},{s0,t0},color});
    list.write_vertex(image_vertex{{fx0,fy1},{s0,t1},color});
    list.write_vertex(image_vertex{{fx1,fy1},{s1,t1},color});
    list.write_vertex(image_vertex{{fx1,fy0},{s1,t0},color});
    list.write_indices(num_quads*4+uint3{0,1,2});
    list.write_indices(num_quads*4+uint3{0,2,3});
    ++num_quads;
}

void gui_context::draw_sprite_sheet(const int2 & p)
{
    draw_sprite({p.x,p.y,p.x+sprites.sheet.sheet.get_width(),p.y+sprites.sheet.sheet.get_height()}, 0, 0, 1, 1, {1,1,1,1});
}

void gui_context::draw_rect(const rect & r, const float4 & color)
{
    const auto & solid = sprites.sheet.sprites[sprites.solid_pixel];
    const float s = (solid.s0 + solid.s1)/2, t = (solid.t0 + solid.t1)/2;
    draw_sprite(r, s, t, s, t, color);
}

void gui_context::draw_rounded_rect(rect r, int radius, const float4 & color)
{
    return draw_partial_rounded_rect(r, radius, color, true, true, true, true);
}

void gui_context::draw_partial_rounded_rect(rect r, int radius, const float4 & color, bool tl, bool tr, bool bl, bool br)
{
    auto it = sprites.corner_sprites.find(radius);
    if(it == end(sprites.corner_sprites)) return;
    const auto & sprite = sprites.sheet.sprites[it->second];
    
    if(tl || tr)
    {
        rect r2 = r.take_y0(radius);
        if(tl) draw_sprite(r2.take_x0(radius), sprite.s1, sprite.t1, sprite.s0, sprite.t0, color);    
        if(tr) draw_sprite(r2.take_x1(radius), sprite.s0, sprite.t1, sprite.s1, sprite.t0, color);
        draw_rect(r2, color);
    }

    if(bl || br)
    {
        rect r2 = r.take_y1(radius);
        if(bl) draw_sprite(r2.take_x0(radius), sprite.s1, sprite.t0, sprite.s0, sprite.t1, color);
        if(br) draw_sprite(r2.take_x1(radius), sprite.s0, sprite.t0, sprite.s1, sprite.t1, color);
        draw_rect(r2, color);
    }

    draw_rect(r, color);
}

void gui_context::draw_text(const font_face & font, const float4 & color, int x, int y, std::string_view text)
{
    for(auto ch : text)
    {
        auto it = font.glyphs.find(ch);
        if(it == font.glyphs.end()) continue;
        auto & b = it->second;
        auto & s = font.sheet.sprites[b.sprite_index];
        const int x0 = x + b.offset.x, y0 = y + b.offset.y, x1 = x0 + s.img.get_width(), y1 = y0 + s.img.get_height();
        draw_sprite({x0+s.border, y0+s.border, x1-s.border, y1-s.border}, s.s0, s.t0, s.s1, s.t1, color);
        x += b.advance;
    }
}

void gui_context::draw_shadowed_text(const font_face & font, const float4 & color, int x, int y, std::string_view text)
{
    draw_text(font,{0,0,0,color.w},x+1,y+1,text);
    draw_text(font,color,x,y,text);
}

void gui_context::end_frame(const scene_material & mtl, const sampler & samp)
{
    auto vertex_info = list.end_vertices();
    auto index_info = list.end_indices();
    auto desc = list.descriptor_set(mtl);
    desc.write_combined_image_sampler(0, 0, samp, *sprites.sheet.texture);
    list.draw(desc, {vertex_info}, index_info, num_quads*6, 1);
}

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

font_face::font_face(sprite_sheet & sheet, const char * filepath, float pixel_height) : sheet{sheet}
{
    auto data = load_binary_file(filepath);
    stbtt_fontinfo f {};
    if(!stbtt_InitFont(&f, data.data(), 0)) throw std::runtime_error("stbtt_InitFont(...) failed");
    const float scale = stbtt_ScaleForPixelHeight(&f, pixel_height);

    for(int ch=0; ch<128; ++ch)
    {
        if(!isprint(ch)) continue;
        const int g = stbtt_FindGlyphIndex(&f, ch);
        int advance, lsb, x0, y0, x1, y1;
        stbtt_GetGlyphHMetrics(&f, g, &advance, &lsb);
        stbtt_GetGlyphBitmapBox(&f, g, scale, scale, &x0, &y0, &x1, &y1);

        image img{{x1-x0, y1-y0}, VK_FORMAT_R8_UNORM};
        stbtt_MakeGlyphBitmap(&f, reinterpret_cast<uint8_t *>(img.get_pixels()), img.get_width(), img.get_height(), img.get_width(), scale, scale, g);
        glyphs[ch].sprite_index = sheet.add_sprite(std::move(img), 0);
        glyphs[ch].offset = {x0,y0};
        glyphs[ch].advance = static_cast<int>(std::round(scale * advance));
    }
}