#ifndef QEMU_FRAMEBUFFER_H
#define QEMU_FRAMEBUFFER_H

#include "system/memory.h"

/* Framebuffer device helper routines.  */

typedef void (*drawfn)(void *, uint8_t *, const uint8_t *, int, int);

/* framebuffer_update_memory_section: Update framebuffer
 * #MemoryRegionSection, for example if the framebuffer is switched to
 * a different memory area.
 *
 * @mem_section: Output #MemoryRegionSection, to be passed to
 * framebuffer_update_display().
 * @root: #MemoryRegion within which the framebuffer lies
 * @base: Base address of the framebuffer within @root.
 * @rows: Height of the screen.
 * @src_width: Number of bytes in framebuffer memory between two rows.
 */
void framebuffer_update_memory_section(
    MemoryRegionSection *mem_section,
    MemoryRegion *root,
    hwaddr base,
    unsigned rows,
    unsigned src_width);

/* framebuffer_update_display: Draw the framebuffer on a surface.
 *
 * @ds: #DisplaySurface to draw to.
 * @mem_section: #MemoryRegionSection provided by
 * framebuffer_update_memory_section().
 * @cols: Width the screen.
 * @rows: Height of the screen.
 * @src_width: Number of bytes in framebuffer memory between two rows.
 * @dest_row_pitch: Number of bytes in the surface data between two rows.
 * Negative if the framebuffer is stored in the opposite order (e.g.
 * bottom-to-top) compared to the framebuffer.
 * @dest_col_pitch: Number of bytes in the surface data between two pixels.
 * Negative if the framebuffer is stored in the opposite order (e.g.
 * right-to-left) compared to the framebuffer.
 * @invalidate: True if the function should redraw the whole screen
 * without checking the DIRTY_MEMORY_VGA dirty bitmap.
 * @fn: Drawing function to be called for each row that has to be drawn.
 * @opaque: Opaque pointer passed to @fn.
 * @first_row: Pointer to an integer, receives the number of the first row
 * that was drawn (either the first dirty row, or 0 if @invalidate is true).
 * @last_row: Pointer to an integer, receives the number of the last row that
 * was drawn (either the last dirty row, or @rows-1 if @invalidate is true).
 */
void framebuffer_update_display(
    DisplaySurface *ds,
    MemoryRegionSection *mem_section,
    int cols,
    int rows,
    int src_width,
    int dest_row_pitch,
    int dest_col_pitch,
    int invalidate,
    drawfn fn,
    void *opaque,
    int *first_row,
    int *last_row);

#endif
