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

/* we put basic includes here to avoid repeating them in device drivers */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifdef _WIN32
#define lseek64 _lseeki64
#endif

#include "cpu.h"

#ifndef glue
#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)
#define stringify(s)	tostring(s)
#define tostring(s)	#s
#endif

#if defined(WORDS_BIGENDIAN)
static inline uint32_t be32_to_cpu(uint32_t v)
{
    return v;
}

static inline uint16_t be16_to_cpu(uint16_t v)
{
    return v;
}

static inline uint32_t cpu_to_be32(uint32_t v)
{
    return v;
}

static inline uint16_t cpu_to_be16(uint16_t v)
{
    return v;
}

static inline uint32_t le32_to_cpu(uint32_t v)
{
    return bswap32(v);
}

static inline uint16_t le16_to_cpu(uint16_t v)
{
    return bswap16(v);
}

static inline uint32_t cpu_to_le32(uint32_t v)
{
    return bswap32(v);
}

static inline uint16_t cpu_to_le16(uint16_t v)
{
    return bswap16(v);
}

#else

static inline uint32_t be32_to_cpu(uint32_t v)
{
    return bswap32(v);
}

static inline uint16_t be16_to_cpu(uint16_t v)
{
    return bswap16(v);
}

static inline uint32_t cpu_to_be32(uint32_t v)
{
    return bswap32(v);
}

static inline uint16_t cpu_to_be16(uint16_t v)
{
    return bswap16(v);
}

static inline uint32_t le32_to_cpu(uint32_t v)
{
    return v;
}

static inline uint16_t le16_to_cpu(uint16_t v)
{
    return v;
}

static inline uint32_t cpu_to_le32(uint32_t v)
{
    return v;
}

static inline uint16_t cpu_to_le16(uint16_t v)
{
    return v;
}
#endif


/* vl.c */
extern int reset_requested;

uint64_t muldiv64(uint64_t a, uint32_t b, uint32_t c);

void hw_error(const char *fmt, ...);

int load_image(const char *filename, uint8_t *addr);
extern const char *bios_dir;

void pstrcpy(char *buf, int buf_size, const char *str);
char *pstrcat(char *buf, int buf_size, const char *s);

int serial_open_device(void);

extern int vm_running;

typedef void VMStopHandler(void *opaque, int reason);

int qemu_add_vm_stop_handler(VMStopHandler *cb, void *opaque);
void qemu_del_vm_stop_handler(VMStopHandler *cb, void *opaque);

void vm_start(void);
void vm_stop(int reason);

extern int audio_enabled;
extern int ram_size;
extern int bios_size;
extern int rtc_utc;

/* XXX: make it dynamic */
#if defined (TARGET_PPC)
#define BIOS_SIZE (512 * 1024)
#else
#define BIOS_SIZE 0
#endif

/* keyboard/mouse support */

#define MOUSE_EVENT_LBUTTON 0x01
#define MOUSE_EVENT_RBUTTON 0x02
#define MOUSE_EVENT_MBUTTON 0x04

typedef void QEMUPutKBDEvent(void *opaque, int keycode);
typedef void QEMUPutMouseEvent(void *opaque, int dx, int dy, int dz, int buttons_state);

void qemu_add_kbd_event_handler(QEMUPutKBDEvent *func, void *opaque);
void qemu_add_mouse_event_handler(QEMUPutMouseEvent *func, void *opaque);

void kbd_put_keycode(int keycode);
void kbd_mouse_event(int dx, int dy, int dz, int buttons_state);

/* async I/O support */

typedef void IOReadHandler(void *opaque, const uint8_t *buf, int size);
typedef int IOCanRWHandler(void *opaque);

int qemu_add_fd_read_handler(int fd, IOCanRWHandler *fd_can_read, 
                             IOReadHandler *fd_read, void *opaque);
void qemu_del_fd_read_handler(int fd);

/* network redirectors support */

#define MAX_NICS 8

typedef struct NetDriverState {
    int index; /* index number in QEMU */
    uint8_t macaddr[6];
    char ifname[16];
    void (*send_packet)(struct NetDriverState *nd, 
                        const uint8_t *buf, int size);
    void (*add_read_packet)(struct NetDriverState *nd, 
                            IOCanRWHandler *fd_can_read, 
                            IOReadHandler *fd_read, void *opaque);
    /* tun specific data */
    int fd;
    /* slirp specific data */
} NetDriverState;

extern int nb_nics;
extern NetDriverState nd_table[MAX_NICS];

void qemu_send_packet(NetDriverState *nd, const uint8_t *buf, int size);
void qemu_add_read_packet(NetDriverState *nd, IOCanRWHandler *fd_can_read, 
                          IOReadHandler *fd_read, void *opaque);

/* timers */

typedef struct QEMUClock QEMUClock;
typedef struct QEMUTimer QEMUTimer;
typedef void QEMUTimerCB(void *opaque);

/* The real time clock should be used only for stuff which does not
   change the virtual machine state, as it is run even if the virtual
   machine is stopped. The real time clock has a frequency of 1000
   Hz. */
extern QEMUClock *rt_clock;

/* Rge virtual clock is only run during the emulation. It is stopped
   when the virtual machine is stopped. Virtual timers use a high
   precision clock, usually cpu cycles (use ticks_per_sec). */
extern QEMUClock *vm_clock;

int64_t qemu_get_clock(QEMUClock *clock);

QEMUTimer *qemu_new_timer(QEMUClock *clock, QEMUTimerCB *cb, void *opaque);
void qemu_free_timer(QEMUTimer *ts);
void qemu_del_timer(QEMUTimer *ts);
void qemu_mod_timer(QEMUTimer *ts, int64_t expire_time);
int qemu_timer_pending(QEMUTimer *ts);

extern int64_t ticks_per_sec;
extern int pit_min_timer_count;

void cpu_enable_ticks(void);
void cpu_disable_ticks(void);

/* VM Load/Save */

typedef FILE QEMUFile;

void qemu_put_buffer(QEMUFile *f, const uint8_t *buf, int size);
void qemu_put_byte(QEMUFile *f, int v);
void qemu_put_be16(QEMUFile *f, unsigned int v);
void qemu_put_be32(QEMUFile *f, unsigned int v);
void qemu_put_be64(QEMUFile *f, uint64_t v);
int qemu_get_buffer(QEMUFile *f, uint8_t *buf, int size);
int qemu_get_byte(QEMUFile *f);
unsigned int qemu_get_be16(QEMUFile *f);
unsigned int qemu_get_be32(QEMUFile *f);
uint64_t qemu_get_be64(QEMUFile *f);

static inline void qemu_put_be64s(QEMUFile *f, const uint64_t *pv)
{
    qemu_put_be64(f, *pv);
}

static inline void qemu_put_be32s(QEMUFile *f, const uint32_t *pv)
{
    qemu_put_be32(f, *pv);
}

static inline void qemu_put_be16s(QEMUFile *f, const uint16_t *pv)
{
    qemu_put_be16(f, *pv);
}

static inline void qemu_put_8s(QEMUFile *f, const uint8_t *pv)
{
    qemu_put_byte(f, *pv);
}

static inline void qemu_get_be64s(QEMUFile *f, uint64_t *pv)
{
    *pv = qemu_get_be64(f);
}

static inline void qemu_get_be32s(QEMUFile *f, uint32_t *pv)
{
    *pv = qemu_get_be32(f);
}

static inline void qemu_get_be16s(QEMUFile *f, uint16_t *pv)
{
    *pv = qemu_get_be16(f);
}

static inline void qemu_get_8s(QEMUFile *f, uint8_t *pv)
{
    *pv = qemu_get_byte(f);
}

int64_t qemu_ftell(QEMUFile *f);
int64_t qemu_fseek(QEMUFile *f, int64_t pos, int whence);

typedef void SaveStateHandler(QEMUFile *f, void *opaque);
typedef int LoadStateHandler(QEMUFile *f, void *opaque, int version_id);

int qemu_loadvm(const char *filename);
int qemu_savevm(const char *filename);
int register_savevm(const char *idstr, 
                    int instance_id, 
                    int version_id,
                    SaveStateHandler *save_state,
                    LoadStateHandler *load_state,
                    void *opaque);
void qemu_get_timer(QEMUFile *f, QEMUTimer *ts);
void qemu_put_timer(QEMUFile *f, QEMUTimer *ts);

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

/* ISA bus */

extern target_phys_addr_t isa_mem_base;

typedef void (IOPortWriteFunc)(void *opaque, uint32_t address, uint32_t data);
typedef uint32_t (IOPortReadFunc)(void *opaque, uint32_t address);

int register_ioport_read(int start, int length, int size, 
                         IOPortReadFunc *func, void *opaque);
int register_ioport_write(int start, int length, int size, 
                          IOPortWriteFunc *func, void *opaque);
void isa_unassign_ioport(int start, int length);

/* PCI bus */

extern int pci_enabled;

extern target_phys_addr_t pci_mem_base;

typedef struct PCIDevice PCIDevice;

typedef void PCIConfigWriteFunc(PCIDevice *pci_dev, 
                                uint32_t address, uint32_t data, int len);
typedef uint32_t PCIConfigReadFunc(PCIDevice *pci_dev, 
                                   uint32_t address, int len);
typedef void PCIMapIORegionFunc(PCIDevice *pci_dev, int region_num, 
                                uint32_t addr, uint32_t size, int type);

#define PCI_ADDRESS_SPACE_MEM		0x00
#define PCI_ADDRESS_SPACE_IO		0x01
#define PCI_ADDRESS_SPACE_MEM_PREFETCH	0x08

typedef struct PCIIORegion {
    uint32_t addr; /* current PCI mapping address. -1 means not mapped */
    uint32_t size;
    uint8_t type;
    PCIMapIORegionFunc *map_func;
} PCIIORegion;

#define PCI_ROM_SLOT 6
#define PCI_NUM_REGIONS 7
struct PCIDevice {
    /* PCI config space */
    uint8_t config[256];

    /* the following fields are read only */
    int bus_num;
    int devfn;
    char name[64];
    PCIIORegion io_regions[PCI_NUM_REGIONS];
    
    /* do not access the following fields */
    PCIConfigReadFunc *config_read;
    PCIConfigWriteFunc *config_write;
    int irq_index;
};

PCIDevice *pci_register_device(const char *name, int instance_size,
                               int bus_num, int devfn,
                               PCIConfigReadFunc *config_read, 
                               PCIConfigWriteFunc *config_write);

void pci_register_io_region(PCIDevice *pci_dev, int region_num, 
                            uint32_t size, int type, 
                            PCIMapIORegionFunc *map_func);

void pci_set_irq(PCIDevice *pci_dev, int irq_num, int level);

uint32_t pci_default_read_config(PCIDevice *d, 
                                 uint32_t address, int len);
void pci_default_write_config(PCIDevice *d, 
                              uint32_t address, uint32_t val, int len);

extern struct PIIX3State *piix3_state;

void i440fx_init(void);
void piix3_init(void);
void pci_bios_init(void);
void pci_info(void);

/* temporary: will be moved in platform specific file */
void pci_prep_init(void);
void pci_pmac_init(void);
void pci_ppc_bios_init(void);

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
                   unsigned long vga_ram_offset, int vga_ram_size, 
                   int is_pci);
void vga_update_display(void);
void vga_screen_dump(const char *filename);

/* sdl.c */
void sdl_display_init(DisplayState *ds);

/* ide.c */
#define MAX_DISKS 4

extern BlockDriverState *bs_table[MAX_DISKS];

void isa_ide_init(int iobase, int iobase2, int irq,
                  BlockDriverState *hd0, BlockDriverState *hd1);
void pci_ide_init(BlockDriverState **hd_table);
void pci_piix3_ide_init(BlockDriverState **hd_table);

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

typedef struct fdctrl_t fdctrl_t;

fdctrl_t *fdctrl_init (int irq_lvl, int dma_chann, int mem_mapped, 
                       uint32_t io_base,
                       BlockDriverState **fds);
int fdctrl_get_drive_type(fdctrl_t *fdctrl, int drive_num);

/* ne2000.c */

void isa_ne2000_init(int base, int irq, NetDriverState *nd);
void pci_ne2000_init(NetDriverState *nd);

/* pckbd.c */

void kbd_init(void);

/* mc146818rtc.c */

typedef struct RTCState RTCState;

RTCState *rtc_init(int base, int irq);
void rtc_set_memory(RTCState *s, int addr, int val);
void rtc_set_date(RTCState *s, const struct tm *tm);

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
uint32_t pic_intack_read(CPUState *env);
void pic_info(void);
void irq_info(void);

/* i8254.c */

#define PIT_FREQ 1193182

typedef struct PITState PITState;

PITState *pit_init(int base, int irq);
void pit_set_gate(PITState *pit, int channel, int val);
int pit_get_gate(PITState *pit, int channel);
int pit_get_out(PITState *pit, int channel, int64_t current_time);

/* pc.c */
void pc_init(int ram_size, int vga_ram_size, int boot_device,
             DisplayState *ds, const char **fd_filename, int snapshot,
             const char *kernel_filename, const char *kernel_cmdline,
             const char *initrd_filename);

/* ppc.c */
void ppc_init (int ram_size, int vga_ram_size, int boot_device,
	       DisplayState *ds, const char **fd_filename, int snapshot,
	       const char *kernel_filename, const char *kernel_cmdline,
	       const char *initrd_filename);
void ppc_prep_init (int ram_size, int vga_ram_size, int boot_device,
		    DisplayState *ds, const char **fd_filename, int snapshot,
		    const char *kernel_filename, const char *kernel_cmdline,
		    const char *initrd_filename);
void ppc_chrp_init(int ram_size, int vga_ram_size, int boot_device,
		   DisplayState *ds, const char **fd_filename, int snapshot,
		   const char *kernel_filename, const char *kernel_cmdline,
		   const char *initrd_filename);
#ifdef TARGET_PPC
ppc_tb_t *cpu_ppc_tb_init (CPUState *env, uint32_t freq);
#endif
void PREP_debug_write (void *opaque, uint32_t addr, uint32_t val);

extern CPUWriteMemoryFunc *PPC_io_write[];
extern CPUReadMemoryFunc *PPC_io_read[];
extern int prep_enabled;

/* NVRAM helpers */
#include "hw/m48t59.h"

void NVRAM_set_byte (m48t59_t *nvram, uint32_t addr, uint8_t value);
uint8_t NVRAM_get_byte (m48t59_t *nvram, uint32_t addr);
void NVRAM_set_word (m48t59_t *nvram, uint32_t addr, uint16_t value);
uint16_t NVRAM_get_word (m48t59_t *nvram, uint32_t addr);
void NVRAM_set_lword (m48t59_t *nvram, uint32_t addr, uint32_t value);
uint32_t NVRAM_get_lword (m48t59_t *nvram, uint32_t addr);
void NVRAM_set_string (m48t59_t *nvram, uint32_t addr,
                       const unsigned char *str, uint32_t max);
int NVRAM_get_string (m48t59_t *nvram, uint8_t *dst, uint16_t addr, int max);
void NVRAM_set_crc (m48t59_t *nvram, uint32_t addr,
                    uint32_t start, uint32_t count);
int PPC_NVRAM_set_params (m48t59_t *nvram, uint16_t NVRAM_size,
                          const unsigned char *arch,
                          uint32_t RAM_size, int boot_device,
                          uint32_t kernel_image, uint32_t kernel_size,
                          uint32_t cmdline, uint32_t cmdline_size,
                          uint32_t initrd_image, uint32_t initrd_size,
                          uint32_t NVRAM_image);

/* adb.c */

#define MAX_ADB_DEVICES 16

typedef struct ADBDevice ADBDevice;

typedef void ADBDeviceReceivePacket(ADBDevice *d, const uint8_t *buf, int len);

struct ADBDevice {
    struct ADBBusState *bus;
    int devaddr;
    int handler;
    ADBDeviceReceivePacket *receive_packet;
    void *opaque;
};

typedef struct ADBBusState {
    ADBDevice devices[MAX_ADB_DEVICES];
    int nb_devices;
} ADBBusState;

void adb_receive_packet(ADBBusState *s, const uint8_t *buf, int len);
void adb_send_packet(ADBBusState *s, const uint8_t *buf, int len);

ADBDevice *adb_register_device(ADBBusState *s, int devaddr, 
                               ADBDeviceReceivePacket *receive_packet, 
                               void *opaque);
void adb_kbd_init(ADBBusState *bus);
void adb_mouse_init(ADBBusState *bus);

/* cuda.c */

extern ADBBusState adb_bus;
int cuda_init(void);

/* monitor.c */
void monitor_init(void);
void term_printf(const char *fmt, ...) __attribute__ ((__format__ (__printf__, 1, 2)));
void term_flush(void);
void term_print_help(void);

/* gdbstub.c */

#define DEFAULT_GDBSTUB_PORT 1234

int gdbserver_start(int port);

#endif /* VL_H */
