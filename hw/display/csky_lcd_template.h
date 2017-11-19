/*
 * QEMU CSKY LCD Emulator templates
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#if DEPTH == 8
# define BPP 1
# define PIXEL_TYPE uint8_t
#elif DEPTH == 15 || DEPTH == 16
# define BPP 2
# define PIXEL_TYPE uint16_t
#elif DEPTH == 24  /* add to fix syntax bug */
# define BPP 4
# define PIXEL_TYPE uint32_t
#elif DEPTH == 32
# define BPP 4
# define PIXEL_TYPE uint32_t
#else
# error unsupport depth
#endif

/*
 * 4-bit colour
 */
/*
static void glue(draw_line4_, DEPTH)(void *opaque,
                uint8_t *d, const uint8_t *s, int width, int deststep)
{
    uint16_t *pal = opaque;
    uint8_t v, r, g, b;

    do {
        v = ldub_p((void *) s);
        r = (pal[v & 0xf] >> 4) & 0xf0;
        g = pal[v & 0xf] & 0xf0;
        b = (pal[v & 0xf] << 4) & 0xf0;
        ((PIXEL_TYPE *) d)[0] = glue(rgb_to_pixel, DEPTH)(r, g, b);
        d += BPP;
        v >>= 4;
        r = (pal[v & 0xf] >> 4) & 0xf0;
        g = pal[v & 0xf] & 0xf0;
        b = (pal[v & 0xf] << 4) & 0xf0;
        ((PIXEL_TYPE *) d)[0] = glue(rgb_to_pixel, DEPTH)(r, g, b);
        d += BPP;
        s ++;
        width -= 2;
    } while (width > 0);
}
*/

/*
 * 8-bit colour
 */
/*
static void glue(draw_line8_, DEPTH)(void *opaque,
                uint8_t *d, const uint8_t *s, int width, int deststep)
{
    uint16_t *pal = opaque;
    uint8_t v, r, g, b;

    do {
        v = ldub_p((void *) s);
        r = (pal[v] >> 4) & 0xf0;
        g = pal[v] & 0xf0;
        b = (pal[v] << 4) & 0xf0;
        ((PIXEL_TYPE *) d)[0] = glue(rgb_to_pixel, DEPTH)(r, g, b);
        s ++;
        d += BPP;
    } while (--width != 0);
}
*/

/*
 * 16-bit colour
 */
static void glue(draw_line16_, DEPTH)(void *opaque, uint8_t *d,
                                      const uint8_t *s, int width, int deststep)
{
    uint16_t v;
    uint8_t r, g, b;

    do {
        /* FIXME: need to distinguish Endian, lack of LCDC User Guide. */
        v = lduw_le_p((void *) s);
        r = (v >> 7) & 0xf8;
        g = (v >> 3) & 0x7c;
        b = (v << 3) & 0xf8;
        ((PIXEL_TYPE *) d)[0] = glue(rgb_to_pixel, DEPTH)(r, g, b);
        s += 2;
        d += BPP;
    } while (-- width != 0);
}

/*
 * 24-bit colour
 */
static void glue(draw_line24_, DEPTH)(void *opaque, uint8_t *d,
                                      const uint8_t *s, int width, int deststep)
{
    uint32_t v;
    uint8_t r, g, b;
    csky_lcdc_state *lcd_s = (csky_lcdc_state *) opaque;
    do {
        v = *(uint32_t *)s;
        if (lcd_s->endian_select == 1) {
            /* Big-endian */
            r = v >> 24;
            g = (v >> 16) & 0xff;
            b = (v >> 8) & 0xff;
        } else {
            /* Little-endian */
            r = (v >> 16) & 0xff;
            g = (v >> 8) & 0xff;
            b = (v >> 0) & 0xff;
        }

        ((PIXEL_TYPE *) d)[0] = glue(rgb_to_pixel, DEPTH)(r, g, b);
        s += 4;
        d += BPP;
    } while (--width != 0);
}

#undef DEPTH
#undef BPP
#undef PIXEL_TYPE
