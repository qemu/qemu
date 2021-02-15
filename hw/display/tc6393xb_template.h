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

static void tc6393xb_draw_graphic32(TC6393xbState *s)
{
    DisplaySurface *surface = qemu_console_surface(s->con);
    int i;
    uint16_t *data_buffer;
    uint8_t *data_display;

    data_buffer = s->vram_ptr;
    data_display = surface_data(surface);
    for(i = 0; i < s->scr_height; i++) {
        int j;
        for (j = 0; j < s->scr_width; j++, data_display += 4, data_buffer++) {
            uint16_t color = *data_buffer;
            uint32_t dest_color = rgb_to_pixel32(
                           ((color & 0xf800) * 0x108) >> 11,
                           ((color & 0x7e0) * 0x41) >> 9,
                           ((color & 0x1f) * 0x21) >> 2
                           );
            *(uint32_t *)data_display = dest_color;
        }
    }
}
