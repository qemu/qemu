/*
 * Framebuffer device helper routines
 *
 * Copyright (c) 2009 CodeSourcery
 * Written by Paul Brook <paul@codesourcery.com>
 *
 * This code is licensed under the GNU GPLv2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

/* TODO:
   - Do something similar for framebuffers with local ram
   - Handle rotation here instead of hacking dest_pitch
   - Use common pixel conversion routines instead of per-device drawfn
   - Remove all DisplayState knowledge from devices.
 */

#include "hw/hw.h"
#include "ui/console.h"
#include "framebuffer.h"

/* Render an image from a shared memory framebuffer.  */
   
void framebuffer_update_display(
    DisplaySurface *ds,
    MemoryRegion *address_space,
    hwaddr base,
    int cols, /* Width in pixels.  */
    int rows, /* Height in pixels.  */
    int src_width, /* Length of source line, in bytes.  */
    int dest_row_pitch, /* Bytes between adjacent horizontal output pixels.  */
    int dest_col_pitch, /* Bytes between adjacent vertical output pixels.  */
    int invalidate, /* nonzero to redraw the whole image.  */
    drawfn fn,
    void *opaque,
    int *first_row, /* Input and output.  */
    int *last_row /* Output only */)
{
    hwaddr src_len;
    uint8_t *dest;
    uint8_t *src;
    uint8_t *src_base;
    int first, last = 0;
    int dirty;
    int i;
    ram_addr_t addr;
    MemoryRegionSection mem_section;
    MemoryRegion *mem;

    i = *first_row;
    *first_row = -1;
    src_len = src_width * rows;

    mem_section = memory_region_find(address_space, base, src_len);
    if (mem_section.size != src_len || !memory_region_is_ram(mem_section.mr)) {
        return;
    }
    mem = mem_section.mr;
    assert(mem);
    assert(mem_section.offset_within_address_space == base);

    memory_region_sync_dirty_bitmap(mem);
    src_base = cpu_physical_memory_map(base, &src_len, 0);
    /* If we can't map the framebuffer then bail.  We could try harder,
       but it's not really worth it as dirty flag tracking will probably
       already have failed above.  */
    if (!src_base)
        return;
    if (src_len != src_width * rows) {
        cpu_physical_memory_unmap(src_base, src_len, 0, 0);
        return;
    }
    src = src_base;
    dest = surface_data(ds);
    if (dest_col_pitch < 0)
        dest -= dest_col_pitch * (cols - 1);
    if (dest_row_pitch < 0) {
        dest -= dest_row_pitch * (rows - 1);
    }
    first = -1;
    addr = mem_section.offset_within_region;

    addr += i * src_width;
    src += i * src_width;
    dest += i * dest_row_pitch;

    for (; i < rows; i++) {
        dirty = memory_region_get_dirty(mem, addr, src_width,
                                             DIRTY_MEMORY_VGA);
        if (dirty || invalidate) {
            fn(opaque, dest, src, cols, dest_col_pitch);
            if (first == -1)
                first = i;
            last = i;
        }
        addr += src_width;
        src += src_width;
        dest += dest_row_pitch;
    }
    cpu_physical_memory_unmap(src_base, src_len, 0, 0);
    if (first < 0) {
        return;
    }
    memory_region_reset_dirty(mem, mem_section.offset_within_region, src_len,
                              DIRTY_MEMORY_VGA);
    *first_row = first;
    *last_row = last;
}
