static inline unsigned int rgb_to_pixel8(unsigned int r, unsigned int g,
                                         unsigned int b)
{
    return ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6);
}

static inline unsigned int rgb_to_pixel15(unsigned int r, unsigned int g,
                                          unsigned int b)
{
    return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
}

static inline unsigned int rgb_to_pixel15bgr(unsigned int r, unsigned int g,
                                             unsigned int b)
{
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);
}

static inline unsigned int rgb_to_pixel16(unsigned int r, unsigned int g,
                                          unsigned int b)
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static inline unsigned int rgb_to_pixel16bgr(unsigned int r, unsigned int g,
                                             unsigned int b)
{
    return ((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3);
}

static inline unsigned int rgb_to_pixel24(unsigned int r, unsigned int g,
                                          unsigned int b)
{
    return (r << 16) | (g << 8) | b;
}

static inline unsigned int rgb_to_pixel24bgr(unsigned int r, unsigned int g,
                                             unsigned int b)
{
    return (b << 16) | (g << 8) | r;
}

static inline unsigned int rgb_to_pixel32(unsigned int r, unsigned int g,
                                          unsigned int b)
{
    return (r << 16) | (g << 8) | b;
}

static inline unsigned int rgb_to_pixel32bgr(unsigned int r, unsigned int g,
                                             unsigned int b)
{
    return (b << 16) | (g << 8) | r;
}
