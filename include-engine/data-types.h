#ifndef DATA_TYPES_H
#define DATA_TYPES_H

class image
{
    int width, height;
    void * pixels;
public:
    image(const char * filename);
    image(const image &) = delete;
    image(image &&) = delete;
    image & operator = (const image &) = delete;
    image & operator = (image &&) = delete;
    ~image();

    int get_width() const { return width; }
    int get_height() const { return height; }
    int get_channels() const { return 4; }
    const void * get_pixels() const { return pixels; }
};

#endif
