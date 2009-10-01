#ifndef QEMU_FRAMEBUFFER_H
#define QEMU_FRAMEBUFFER_H

/* Framebuffer device helper routines.  */

typedef void (*drawfn)(void *, uint8_t *, const uint8_t *, int, int);

void framebuffer_update_display(
    DisplayState *ds,
    target_phys_addr_t base,
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
