/*
 * QEMU System Emulator header
 * 
 * Copyright (c) 2003 Fabrice Bellard
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef VL_H
#define VL_H

/* vl.c */
struct CPUX86State;
extern int reset_requested;

typedef void (IOPortWriteFunc)(struct CPUX86State *env, uint32_t address, uint32_t data);
typedef uint32_t (IOPortReadFunc)(struct CPUX86State *env, uint32_t address);

void *get_mmap_addr(unsigned long size);
int register_ioport_read(int start, int length, IOPortReadFunc *func, int size);
int register_ioport_write(int start, int length, IOPortWriteFunc *func, int size);

void kbd_put_keycode(int keycode);

#define MOUSE_EVENT_LBUTTON 0x01
#define MOUSE_EVENT_RBUTTON 0x02
#define MOUSE_EVENT_MBUTTON 0x04
void kbd_mouse_event(int dx, int dy, int dz, int buttons_state);

/* block.c */
typedef struct BlockDriverState BlockDriverState;

BlockDriverState *bdrv_open(const char *filename, int snapshot);
void bdrv_close(BlockDriverState *bs);
int bdrv_read(BlockDriverState *bs, int64_t sector_num, 
              uint8_t *buf, int nb_sectors);
int bdrv_write(BlockDriverState *bs, int64_t sector_num, 
               const uint8_t *buf, int nb_sectors);
void bdrv_get_geometry(BlockDriverState *bs, int64_t *nb_sectors_ptr);
int bdrv_commit(BlockDriverState *bs);

/* user mode linux compatible COW file */
#define COW_MAGIC 0x4f4f4f4d  /* MOOO */
#define COW_VERSION 2

struct cow_header_v2 {
    uint32_t magic;
    uint32_t version;
    char backing_file[1024];
    int32_t mtime;
    uint64_t size;
    uint32_t sectorsize;
};

/* vga.c */

#define VGA_RAM_SIZE (8192 * 1024)

typedef struct DisplayState {
    uint8_t *data;
    int linesize;
    int depth;
    void (*dpy_update)(struct DisplayState *s, int x, int y, int w, int h);
    void (*dpy_resize)(struct DisplayState *s, int w, int h);
    void (*dpy_refresh)(struct DisplayState *s);
} DisplayState;

static inline void dpy_update(DisplayState *s, int x, int y, int w, int h)
{
    s->dpy_update(s, x, y, w, h);
}

static inline void dpy_resize(DisplayState *s, int w, int h)
{
    s->dpy_resize(s, w, h);
}

int vga_init(DisplayState *ds, uint8_t *vga_ram_base, 
             unsigned long vga_ram_offset, int vga_ram_size);
void vga_update_display(void);

/* sdl.c */
void sdl_display_init(DisplayState *ds);

#endif /* VL_H */
