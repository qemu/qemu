/*
 * Framebuffer device helper routines
 *
 * Copyright (c) 2009 CodeSourcery
 * Written by Paul Brook <paul@codesourcery.com>
 *
 * This code is licensed under the GNU GPLv2.
 */

/* TODO:
   - Do something similar for framebuffers with local ram
   - Handle rotation here instead of hacking dest_pitch
   - Use common pixel conversion routines instead of per-device drawfn
   - Remove all DisplayState knowledge from devices.
 */

#include "hw.h"
#include "console.h"
#include "framebuffer.h"
#include "kvm.h"

/* Render an image from a shared memory framebuffer.  */
   
void framebuffer_update_display(
    DisplayState *ds,
    target_phys_addr_t base,
    int cols, /* Width in pixels.  */
    int rows, /* Leight in pixels.  */
    int src_width, /* Length of source line, in bytes.  */
    int dest_row_pitch, /* Bytes between adjacent horizontal output pixels.  */
    int dest_col_pitch, /* Bytes between adjacent vertical output pixels.  */
    int invalidate, /* nonzero to redraw the whole image.  */
    drawfn fn,
    void *opaque,
    int *first_row, /* Input and output.  */
    int *last_row /* Output only */)
{
    target_phys_addr_t src_len;
    uint8_t *dest;
    uint8_t *src;
    uint8_t *src_base;
    int first, last = 0;
    int dirty;
    int i;
    ram_addr_t addr;
    ram_addr_t pd;
    ram_addr_t pd2;

    i = *first_row;
    *first_row = -1;
    src_len = src_width * rows;

    if (kvm_enabled()) {
        kvm_physical_sync_dirty_bitmap(base, src_len);
    }
    pd = cpu_get_physical_page_desc(base);
    pd2 = cpu_get_physical_page_desc(base + src_len - 1);
    /* We should reall check that this is a continuous ram region.
       Instead we just check that the first and last pages are
       both ram, and the right distance apart.  */
    if ((pd & ~TARGET_PAGE_MASK) > IO_MEM_ROM
        || (pd2 & ~TARGET_PAGE_MASK) > IO_MEM_ROM) {
        return;
    }
    pd = (pd & TARGET_PAGE_MASK) + (base & ~TARGET_PAGE_MASK);
    if (((pd + src_len - 1) & TARGET_PAGE_MASK) != (pd2 & TARGET_PAGE_MASK)) {
        return;
    }

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
    dest = ds_get_data(ds);
    if (dest_col_pitch < 0)
        dest -= dest_col_pitch * (cols - 1);
    first = -1;
    addr = pd;

    addr += i * src_width;
    src += i * src_width;
    dest += i * dest_row_pitch;

    for (; i < rows; i++) {
        target_phys_addr_t dirty_offset;
        dirty = 0;
        dirty_offset = 0;
        while (addr + dirty_offset < TARGET_PAGE_ALIGN(addr + src_width)) {
            dirty |= cpu_physical_memory_get_dirty(addr + dirty_offset,
                                                   VGA_DIRTY_FLAG);
            dirty_offset += TARGET_PAGE_SIZE;
        }

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
    cpu_physical_memory_reset_dirty(pd, pd + src_len, VGA_DIRTY_FLAG);
    *first_row = first;
    *last_row = last;
    return;
}
