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

#include "cpu.h"

/* vl.c */
extern int reset_requested;
extern int64_t ticks_per_sec;
extern int pit_min_timer_count;

typedef void (IOPortWriteFunc)(void *opaque, uint32_t address, uint32_t data);
typedef uint32_t (IOPortReadFunc)(void *opaque, uint32_t address);

int register_ioport_read(int start, int length, int size, 
                         IOPortReadFunc *func, void *opaque);
int register_ioport_write(int start, int length, int size, 
                          IOPortWriteFunc *func, void *opaque);
int64_t cpu_get_ticks(void);
uint64_t muldiv64(uint64_t a, uint32_t b, uint32_t c);

void hw_error(const char *fmt, ...);

int load_image(const char *filename, uint8_t *addr);
extern const char *bios_dir;

void pstrcpy(char *buf, int buf_size, const char *str);
char *pstrcat(char *buf, int buf_size, const char *s);

int serial_open_device(void);

/* network redirectors support */

#define MAX_NICS 8

typedef struct NetDriverState {
    int fd;
    uint8_t macaddr[6];
    char ifname[16];
} NetDriverState;

extern int nb_nics;
extern NetDriverState nd_table[MAX_NICS];

void net_send_packet(NetDriverState *nd, const uint8_t *buf, int size);

/* async I/O support */

typedef void IOReadHandler(void *opaque, const uint8_t *buf, int size);
typedef int IOCanRWHandler(void *opaque);

int add_fd_read_handler(int fd, IOCanRWHandler *fd_can_read, 
                        IOReadHandler *fd_read, void *opaque);

/* block.c */
typedef struct BlockDriverState BlockDriverState;

BlockDriverState *bdrv_new(const char *device_name);
void bdrv_delete(BlockDriverState *bs);
int bdrv_open(BlockDriverState *bs, const char *filename, int snapshot);
void bdrv_close(BlockDriverState *bs);
int bdrv_read(BlockDriverState *bs, int64_t sector_num, 
              uint8_t *buf, int nb_sectors);
int bdrv_write(BlockDriverState *bs, int64_t sector_num, 
               const uint8_t *buf, int nb_sectors);
void bdrv_get_geometry(BlockDriverState *bs, int64_t *nb_sectors_ptr);
int bdrv_commit(BlockDriverState *bs);
void bdrv_set_boot_sector(BlockDriverState *bs, const uint8_t *data, int size);

#define BDRV_TYPE_HD     0
#define BDRV_TYPE_CDROM  1
#define BDRV_TYPE_FLOPPY 2

void bdrv_set_geometry_hint(BlockDriverState *bs, 
                            int cyls, int heads, int secs);
void bdrv_set_type_hint(BlockDriverState *bs, int type);
void bdrv_get_geometry_hint(BlockDriverState *bs, 
                            int *pcyls, int *pheads, int *psecs);
int bdrv_get_type_hint(BlockDriverState *bs);
int bdrv_is_removable(BlockDriverState *bs);
int bdrv_is_read_only(BlockDriverState *bs);
int bdrv_is_inserted(BlockDriverState *bs);
int bdrv_is_locked(BlockDriverState *bs);
void bdrv_set_locked(BlockDriverState *bs, int locked);
void bdrv_set_change_cb(BlockDriverState *bs, 
                        void (*change_cb)(void *opaque), void *opaque);

void bdrv_info(void);
BlockDriverState *bdrv_find(const char *name);

/* vga.c */

#define VGA_RAM_SIZE (4096 * 1024)

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

int vga_initialize(DisplayState *ds, uint8_t *vga_ram_base, 
                   unsigned long vga_ram_offset, int vga_ram_size);
void vga_update_display(void);
void vga_screen_dump(const char *filename);

/* sdl.c */
void sdl_display_init(DisplayState *ds);

/* ide.c */
#define MAX_DISKS 4

extern BlockDriverState *bs_table[MAX_DISKS];

void ide_init(int iobase, int iobase2, int irq,
              BlockDriverState *hd0, BlockDriverState *hd1);

/* oss.c */
typedef enum {
  AUD_FMT_U8,
  AUD_FMT_S8,
  AUD_FMT_U16,
  AUD_FMT_S16
} audfmt_e;

void AUD_open (int rfreq, int rnchannels, audfmt_e rfmt);
void AUD_reset (int rfreq, int rnchannels, audfmt_e rfmt);
int AUD_write (void *in_buf, int size);
void AUD_run (void);
void AUD_adjust_estimate (int _leftover);
int AUD_get_free (void);
int AUD_get_live (void);
int AUD_get_buffer_size (void);
void AUD_init (void);

/* dma.c */
typedef int (*DMA_transfer_handler) (void *opaque, target_ulong addr, int size);
int DMA_get_channel_mode (int nchan);
void DMA_hold_DREQ (int nchan);
void DMA_release_DREQ (int nchan);
void DMA_schedule(int nchan);
void DMA_run (void);
void DMA_init (void);
void DMA_register_channel (int nchan,
                           DMA_transfer_handler transfer_handler, void *opaque);

/* sb16.c */
void SB16_run (void);
void SB16_init (void);
 
/* fdc.c */
#define MAX_FD 2
extern BlockDriverState *fd_table[MAX_FD];

void fdctrl_init (int irq_lvl, int dma_chann, int mem_mapped, uint32_t base,
                  BlockDriverState **fds);
int fdctrl_get_drive_type(int drive_num);

/* ne2000.c */

void ne2000_init(int base, int irq, NetDriverState *nd);

/* pckbd.c */

void kbd_put_keycode(int keycode);

#define MOUSE_EVENT_LBUTTON 0x01
#define MOUSE_EVENT_RBUTTON 0x02
#define MOUSE_EVENT_MBUTTON 0x04
void kbd_mouse_event(int dx, int dy, int dz, int buttons_state);

void kbd_init(void);

/* mc146818rtc.c */

typedef struct RTCState {
    uint8_t cmos_data[128];
    uint8_t cmos_index;
    int irq;
} RTCState;

extern RTCState rtc_state;

void rtc_init(int base, int irq);
void rtc_timer(void);

/* serial.c */

typedef struct SerialState SerialState;

extern SerialState *serial_console;

SerialState *serial_init(int base, int irq, int fd);
int serial_can_receive(SerialState *s);
void serial_receive_byte(SerialState *s, int ch);
void serial_receive_break(SerialState *s);

/* i8259.c */

void pic_set_irq(int irq, int level);
void pic_init(void);

/* i8254.c */

#define PIT_FREQ 1193182

typedef struct PITChannelState {
    int count; /* can be 65536 */
    uint16_t latched_count;
    uint8_t rw_state;
    uint8_t mode;
    uint8_t bcd; /* not supported */
    uint8_t gate; /* timer start */
    int64_t count_load_time;
    int64_t count_last_edge_check_time;
} PITChannelState;

extern PITChannelState pit_channels[3];

void pit_init(int base);
void pit_set_gate(PITChannelState *s, int val);
int pit_get_out(PITChannelState *s);
int pit_get_out_edges(PITChannelState *s);

/* pc.c */
void pc_init(int ram_size, int vga_ram_size, int boot_device,
             DisplayState *ds, const char **fd_filename, int snapshot,
             const char *kernel_filename, const char *kernel_cmdline,
             const char *initrd_filename);

/* monitor.c */
void monitor_init(void);
void term_printf(const char *fmt, ...);
void term_flush(void);
void term_print_help(void);

#endif /* VL_H */
