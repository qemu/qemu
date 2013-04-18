/*
 * Toshiba TC6393XB I/O Controller.
 * Found in Sharp Zaurus SL-6000 (tosa) or some
 * Toshiba e-Series PDAs.
 *
 * FB support code. Based on G364 fb emulator
 *
 * Copyright (c) 2007 Hervé Poussineau
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if BITS == 8
# define SET_PIXEL(addr, color)	*(uint8_t*)addr = color;
#elif BITS == 15 || BITS == 16
# define SET_PIXEL(addr, color)	*(uint16_t*)addr = color;
#elif BITS == 24
# define SET_PIXEL(addr, color)	\
    addr[0] = color; addr[1] = (color) >> 8; addr[2] = (color) >> 16;
#elif BITS == 32
# define SET_PIXEL(addr, color)	*(uint32_t*)addr = color;
#else
# error unknown bit depth
#endif


static void glue(tc6393xb_draw_graphic, BITS)(TC6393xbState *s)
{
    DisplaySurface *surface = qemu_console_surface(s->con);
    int i;
    uint16_t *data_buffer;
    uint8_t *data_display;

    data_buffer = s->vram_ptr;
    data_display = surface_data(surface);
    for(i = 0; i < s->scr_height; i++) {
#if (BITS == 16)
        memcpy(data_display, data_buffer, s->scr_width * 2);
        data_buffer += s->scr_width;
        data_display += surface_stride(surface);
#else
        int j;
        for (j = 0; j < s->scr_width; j++, data_display += BITS / 8, data_buffer++) {
            uint16_t color = *data_buffer;
            uint32_t dest_color = glue(rgb_to_pixel, BITS)(
                           ((color & 0xf800) * 0x108) >> 11,
                           ((color & 0x7e0) * 0x41) >> 9,
                           ((color & 0x1f) * 0x21) >> 2
                           );
            SET_PIXEL(data_display, dest_color);
        }
#endif
    }
}

#undef BITS
#undef SET_PIXEL
