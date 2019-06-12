#include "qemu/osdep.h"
#include "ui/console.h"

#include "cursor_hidden.xpm"
#include "cursor_left_ptr.xpm"

/* for creating built-in cursors */
static QEMUCursor *cursor_parse_xpm(const char *xpm[])
{
    QEMUCursor *c;
    uint32_t ctab[128];
    unsigned int width, height, colors, chars;
    unsigned int line = 0, i, r, g, b, x, y, pixel;
    char name[16];
    uint8_t idx;

    /* parse header line: width, height, #colors, #chars */
    if (sscanf(xpm[line], "%u %u %u %u",
               &width, &height, &colors, &chars) != 4) {
        fprintf(stderr, "%s: header parse error: \"%s\"\n",
                __func__, xpm[line]);
        return NULL;
    }
    if (chars != 1) {
        fprintf(stderr, "%s: chars != 1 not supported\n", __func__);
        return NULL;
    }
    line++;

    /* parse color table */
    for (i = 0; i < colors; i++, line++) {
        if (sscanf(xpm[line], "%c c %15s", &idx, name) == 2) {
            if (sscanf(name, "#%02x%02x%02x", &r, &g, &b) == 3) {
                ctab[idx] = (0xff << 24) | (b << 16) | (g << 8) | r;
                continue;
            }
            if (strcmp(name, "None") == 0) {
                ctab[idx] = 0x00000000;
                continue;
            }
        }
        fprintf(stderr, "%s: color parse error: \"%s\"\n",
                __func__, xpm[line]);
        return NULL;
    }

    /* parse pixel data */
    c = cursor_alloc(width, height);
    for (pixel = 0, y = 0; y < height; y++, line++) {
        for (x = 0; x < height; x++, pixel++) {
            idx = xpm[line][x];
            c->data[pixel] = ctab[idx];
        }
    }
    return c;
}

/* nice for debugging */
void cursor_print_ascii_art(QEMUCursor *c, const char *prefix)
{
    uint32_t *data = c->data;
    int x,y;

    for (y = 0; y < c->height; y++) {
        fprintf(stderr, "%s: %2d: |", prefix, y);
        for (x = 0; x < c->width; x++, data++) {
            if ((*data & 0xff000000) != 0xff000000) {
                fprintf(stderr, " "); /* transparent */
            } else if ((*data & 0x00ffffff) == 0x00ffffff) {
                fprintf(stderr, "."); /* white */
            } else if ((*data & 0x00ffffff) == 0x00000000) {
                fprintf(stderr, "X"); /* black */
            } else {
                fprintf(stderr, "o"); /* other */
            }
        }
        fprintf(stderr, "|\n");
    }
}

QEMUCursor *cursor_builtin_hidden(void)
{
    return cursor_parse_xpm(cursor_hidden_xpm);
}

QEMUCursor *cursor_builtin_left_ptr(void)
{
    return cursor_parse_xpm(cursor_left_ptr_xpm);
}

QEMUCursor *cursor_alloc(int width, int height)
{
    QEMUCursor *c;
    int datasize = width * height * sizeof(uint32_t);

    c = g_malloc0(sizeof(QEMUCursor) + datasize);
    c->width  = width;
    c->height = height;
    c->refcount = 1;
    return c;
}

void cursor_get(QEMUCursor *c)
{
    c->refcount++;
}

void cursor_put(QEMUCursor *c)
{
    if (c == NULL)
        return;
    c->refcount--;
    if (c->refcount)
        return;
    g_free(c);
}

int cursor_get_mono_bpl(QEMUCursor *c)
{
    return DIV_ROUND_UP(c->width, 8);
}

void cursor_set_mono(QEMUCursor *c,
                     uint32_t foreground, uint32_t background, uint8_t *image,
                     int transparent, uint8_t *mask)
{
    uint32_t *data = c->data;
    uint8_t bit;
    int x,y,bpl;
    bool expand_bitmap_only = image == mask;
    bool has_inverted_colors = false;
    const uint32_t inverted = 0x80000000;

    /*
     * Converts a monochrome bitmap with XOR mask 'image' and AND mask 'mask':
     * https://docs.microsoft.com/en-us/windows-hardware/drivers/display/drawing-monochrome-pointers
     */
    bpl = cursor_get_mono_bpl(c);
    for (y = 0; y < c->height; y++) {
        bit = 0x80;
        for (x = 0; x < c->width; x++, data++) {
            if (transparent && mask[x/8] & bit) {
                if (!expand_bitmap_only && image[x / 8] & bit) {
                    *data = inverted;
                    has_inverted_colors = true;
                } else {
                    *data = 0x00000000;
                }
            } else if (!transparent && !(mask[x/8] & bit)) {
                *data = 0x00000000;
            } else if (image[x/8] & bit) {
                *data = 0xff000000 | foreground;
            } else {
                *data = 0xff000000 | background;
            }
            bit >>= 1;
            if (bit == 0) {
                bit = 0x80;
            }
        }
        mask  += bpl;
        image += bpl;
    }

    /*
     * If there are any pixels with inverted colors, create an outline (fill
     * transparent neighbors with the background color) and use the foreground
     * color as "inverted" color.
     */
    if (has_inverted_colors) {
        data = c->data;
        for (y = 0; y < c->height; y++) {
            for (x = 0; x < c->width; x++, data++) {
                if (*data == 0 /* transparent */ &&
                        ((x > 0 && data[-1] == inverted) ||
                         (x + 1 < c->width && data[1] == inverted) ||
                         (y > 0 && data[-c->width] == inverted) ||
                         (y + 1 < c->height && data[c->width] == inverted))) {
                    *data = 0xff000000 | background;
                }
            }
        }
        data = c->data;
        for (x = 0; x < c->width * c->height; x++, data++) {
            if (*data == inverted) {
                *data = 0xff000000 | foreground;
            }
        }
    }
}

void cursor_get_mono_image(QEMUCursor *c, int foreground, uint8_t *image)
{
    uint32_t *data = c->data;
    uint8_t bit;
    int x,y,bpl;

    bpl = cursor_get_mono_bpl(c);
    memset(image, 0, bpl * c->height);
    for (y = 0; y < c->height; y++) {
        bit = 0x80;
        for (x = 0; x < c->width; x++, data++) {
            if (((*data & 0xff000000) == 0xff000000) &&
                ((*data & 0x00ffffff) == foreground)) {
                image[x/8] |= bit;
            }
            bit >>= 1;
            if (bit == 0) {
                bit = 0x80;
            }
        }
        image += bpl;
    }
}

void cursor_get_mono_mask(QEMUCursor *c, int transparent, uint8_t *mask)
{
    uint32_t *data = c->data;
    uint8_t bit;
    int x,y,bpl;

    bpl = cursor_get_mono_bpl(c);
    memset(mask, 0, bpl * c->height);
    for (y = 0; y < c->height; y++) {
        bit = 0x80;
        for (x = 0; x < c->width; x++, data++) {
            if ((*data & 0xff000000) != 0xff000000) {
                if (transparent != 0) {
                    mask[x/8] |= bit;
                }
            } else {
                if (transparent == 0) {
                    mask[x/8] |= bit;
                }
            }
            bit >>= 1;
            if (bit == 0) {
                bit = 0x80;
            }
        }
        mask += bpl;
    }
}
