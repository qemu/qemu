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

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "ui/console.h"
#include "framebuffer.h"

void framebuffer_update_memory_section(
    MemoryRegionSection *mem_section,
    MemoryRegion *root,
    hwaddr base,
    unsigned rows,
    unsigned src_width)
{
    hwaddr src_len = (hwaddr)rows * src_width;

    if (mem_section->mr) {
        memory_region_set_log(mem_section->mr, false, DIRTY_MEMORY_VGA);
        memory_region_unref(mem_section->mr);
        mem_section->mr = NULL;
    }

    *mem_section = memory_region_find(root, base, src_len);
    if (!mem_section->mr) {
        return;
    }

    if (int128_get64(mem_section->size) < src_len ||
            !memory_region_is_ram(mem_section->mr)) {
        memory_region_unref(mem_section->mr);
        mem_section->mr = NULL;
        return;
    }

    memory_region_set_log(mem_section->mr, true, DIRTY_MEMORY_VGA);
}

/* Render an image from a shared memory framebuffer.  */
void framebuffer_update_display(
    DisplaySurface *ds,
    MemoryRegionSection *mem_section,
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
    DirtyBitmapSnapshot *snap;
    uint8_t *dest;
    uint8_t *src;
    int first, last = 0;
    int dirty;
    int i;
    ram_addr_t addr;
    MemoryRegion *mem;

    i = *first_row;
    *first_row = -1;

    mem = mem_section->mr;
    if (!mem) {
        return;
    }

    addr = mem_section->offset_within_region;
    src = memory_region_get_ram_ptr(mem) + addr;

    dest = surface_data(ds);
    if (dest_col_pitch < 0) {
        dest -= dest_col_pitch * (cols - 1);
    }
    if (dest_row_pitch < 0) {
        dest -= dest_row_pitch * (rows - 1);
    }
    first = -1;

    addr += i * src_width;
    src += i * src_width;
    dest += i * dest_row_pitch;

    snap = memory_region_snapshot_and_clear_dirty(mem, addr, src_width * rows,
                                                  DIRTY_MEMORY_VGA);
    for (; i < rows; i++) {
        dirty = memory_region_snapshot_get_dirty(mem, snap, addr, src_width);
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
    g_free(snap);
    if (first < 0) {
        return;
    }
    *first_row = first;
    *last_row = last;
}
