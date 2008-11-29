/*
 * QEMU G364 framebuffer Emulator.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

static void glue(g364fb_draw_graphic, BPP)(G364State *s, int full_update)
{
    int i, j;
    int w_display;
    uint8_t *data_buffer;
    uint8_t *data_display, *dd;

    data_buffer = s->vram_buffer;
    w_display = s->scr_width * PIXEL_WIDTH / 8;
    data_display = ds_get_data(s->ds);
    for(i = 0; i < s->scr_height; i++) {
        dd = data_display;
        for (j = 0; j < s->scr_width; j++, dd += PIXEL_WIDTH / 8, data_buffer++) {
            uint8_t index = *data_buffer;
            *((glue(glue(uint, PIXEL_WIDTH), _t) *)dd) = glue(rgb_to_pixel, BPP)(
                s->palette[index][0],
                s->palette[index][1],
                s->palette[index][2]);
        }
        data_display += ds_get_linesize(s->ds);
    }
}
