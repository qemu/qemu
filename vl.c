/*
 * QEMU PC System Emulator
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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <malloc.h>
#include <termios.h>
#include <sys/poll.h>
#include <errno.h>
#include <sys/wait.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>

#include "cpu.h"
#include "disas.h"
#include "thunk.h"

#include "vl.h"

#define DEFAULT_NETWORK_SCRIPT "/etc/qemu-ifup"
#define BIOS_FILENAME "bios.bin"
#define VGABIOS_FILENAME "vgabios.bin"

//#define DEBUG_UNUSED_IOPORT

//#define DEBUG_IRQ_LATENCY

/* output Bochs bios info messages */
//#define DEBUG_BIOS

//#define DEBUG_CMOS

/* debug PIC */
//#define DEBUG_PIC

/* debug NE2000 card */
//#define DEBUG_NE2000

/* debug PC keyboard */
//#define DEBUG_KBD

/* debug PC keyboard : only mouse */
//#define DEBUG_MOUSE

//#define DEBUG_SERIAL

#define PHYS_RAM_BASE     0xac000000
#define PHYS_RAM_MAX_SIZE (256 * 1024 * 1024)

#if defined (TARGET_I386)
#define KERNEL_LOAD_ADDR   0x00100000
#elif defined (TARGET_PPC)
//#define USE_OPEN_FIRMWARE
#if defined (USE_OPEN_FIRMWARE)
#define KERNEL_LOAD_ADDR    0x01000000
#define KERNEL_STACK_ADDR   0x01200000
#else
#define KERNEL_LOAD_ADDR    0x00000000
#define KERNEL_STACK_ADDR   0x00400000
#endif
#endif
#define INITRD_LOAD_ADDR   0x00400000
#define KERNEL_PARAMS_ADDR 0x00090000

#define GUI_REFRESH_INTERVAL 30 

/* from plex86 (BSD license) */
struct  __attribute__ ((packed)) linux_params {
  // For 0x00..0x3f, see 'struct screen_info' in linux/include/linux/tty.h.
  // I just padded out the VESA parts, rather than define them.

  /* 0x000 */ uint8_t   orig_x;
  /* 0x001 */ uint8_t   orig_y;
  /* 0x002 */ uint16_t  ext_mem_k;
  /* 0x004 */ uint16_t  orig_video_page;
  /* 0x006 */ uint8_t   orig_video_mode;
  /* 0x007 */ uint8_t   orig_video_cols;
  /* 0x008 */ uint16_t  unused1;
  /* 0x00a */ uint16_t  orig_video_ega_bx;
  /* 0x00c */ uint16_t  unused2;
  /* 0x00e */ uint8_t   orig_video_lines;
  /* 0x00f */ uint8_t   orig_video_isVGA;
  /* 0x010 */ uint16_t  orig_video_points;
  /* 0x012 */ uint8_t   pad0[0x20 - 0x12]; // VESA info.
  /* 0x020 */ uint16_t  cl_magic;  // Commandline magic number (0xA33F)
  /* 0x022 */ uint16_t  cl_offset; // Commandline offset.  Address of commandline
                                 // is calculated as 0x90000 + cl_offset, bu
                                 // only if cl_magic == 0xA33F.
  /* 0x024 */ uint8_t   pad1[0x40 - 0x24]; // VESA info.

  /* 0x040 */ uint8_t   apm_bios_info[20]; // struct apm_bios_info
  /* 0x054 */ uint8_t   pad2[0x80 - 0x54];

  // Following 2 from 'struct drive_info_struct' in drivers/block/cciss.h.
  // Might be truncated?
  /* 0x080 */ uint8_t   hd0_info[16]; // hd0-disk-parameter from intvector 0x41
  /* 0x090 */ uint8_t   hd1_info[16]; // hd1-disk-parameter from intvector 0x46

  // System description table truncated to 16 bytes
  // From 'struct sys_desc_table_struct' in linux/arch/i386/kernel/setup.c.
  /* 0x0a0 */ uint16_t  sys_description_len;
  /* 0x0a2 */ uint8_t   sys_description_table[14];
                        // [0] machine id
                        // [1] machine submodel id
                        // [2] BIOS revision
                        // [3] bit1: MCA bus

  /* 0x0b0 */ uint8_t   pad3[0x1e0 - 0xb0];
  /* 0x1e0 */ uint32_t  alt_mem_k;
  /* 0x1e4 */ uint8_t   pad4[4];
  /* 0x1e8 */ uint8_t   e820map_entries;
  /* 0x1e9 */ uint8_t   eddbuf_entries; // EDD_NR
  /* 0x1ea */ uint8_t   pad5[0x1f1 - 0x1ea];
  /* 0x1f1 */ uint8_t   setup_sects; // size of setup.S, number of sectors
  /* 0x1f2 */ uint16_t  mount_root_rdonly; // MOUNT_ROOT_RDONLY (if !=0)
  /* 0x1f4 */ uint16_t  sys_size; // size of compressed kernel-part in the
                                // (b)zImage-file (in 16 byte units, rounded up)
  /* 0x1f6 */ uint16_t  swap_dev; // (unused AFAIK)
  /* 0x1f8 */ uint16_t  ramdisk_flags;
  /* 0x1fa */ uint16_t  vga_mode; // (old one)
  /* 0x1fc */ uint16_t  orig_root_dev; // (high=Major, low=minor)
  /* 0x1fe */ uint8_t   pad6[1];
  /* 0x1ff */ uint8_t   aux_device_info;
  /* 0x200 */ uint16_t  jump_setup; // Jump to start of setup code,
                                  // aka "reserved" field.
  /* 0x202 */ uint8_t   setup_signature[4]; // Signature for SETUP-header, ="HdrS"
  /* 0x206 */ uint16_t  header_format_version; // Version number of header format;
  /* 0x208 */ uint8_t   setup_S_temp0[8]; // Used by setup.S for communication with
                                        // boot loaders, look there.
  /* 0x210 */ uint8_t   loader_type;
                        // 0 for old one.
                        // else 0xTV:
                        //   T=0: LILO
                        //   T=1: Loadlin
                        //   T=2: bootsect-loader
                        //   T=3: SYSLINUX
                        //   T=4: ETHERBOOT
                        //   V=version
  /* 0x211 */ uint8_t   loadflags;
                        // bit0 = 1: kernel is loaded high (bzImage)
                        // bit7 = 1: Heap and pointer (see below) set by boot
                        //   loader.
  /* 0x212 */ uint16_t  setup_S_temp1;
  /* 0x214 */ uint32_t  kernel_start;
  /* 0x218 */ uint32_t  initrd_start;
  /* 0x21c */ uint32_t  initrd_size;
  /* 0x220 */ uint8_t   setup_S_temp2[4];
  /* 0x224 */ uint16_t  setup_S_heap_end_pointer;
  /* 0x226 */ uint8_t   pad7[0x2d0 - 0x226];

  /* 0x2d0 : Int 15, ax=e820 memory map. */
  // (linux/include/asm-i386/e820.h, 'struct e820entry')
#define E820MAX  32
#define E820_RAM  1
#define E820_RESERVED 2
#define E820_ACPI 3 /* usable as RAM once ACPI tables have been read */
#define E820_NVS  4
  struct {
    uint64_t addr;
    uint64_t size;
    uint32_t type;
    } e820map[E820MAX];

  /* 0x550 */ uint8_t   pad8[0x600 - 0x550];

  // BIOS Enhanced Disk Drive Services.
  // (From linux/include/asm-i386/edd.h, 'struct edd_info')
  // Each 'struct edd_info is 78 bytes, times a max of 6 structs in array.
  /* 0x600 */ uint8_t   eddbuf[0x7d4 - 0x600];

  /* 0x7d4 */ uint8_t   pad9[0x800 - 0x7d4];
  /* 0x800 */ uint8_t   commandline[0x800];

  /* 0x1000 */
  uint64_t gdt_table[256];
  uint64_t idt_table[48];
};

#define KERNEL_CS     0x10
#define KERNEL_DS     0x18

/* XXX: use a two level table to limit memory usage */
#define MAX_IOPORTS 65536

static const char *bios_dir = CONFIG_QEMU_SHAREDIR;
char phys_ram_file[1024];
CPUState *global_env;
CPUState *cpu_single_env;
IOPortReadFunc *ioport_read_table[3][MAX_IOPORTS];
IOPortWriteFunc *ioport_write_table[3][MAX_IOPORTS];
BlockDriverState *bs_table[MAX_DISKS], *fd_table[MAX_FD];
int vga_ram_size;
static DisplayState display_state;
int nographic;
int term_inited;
int64_t ticks_per_sec;
int boot_device = 'c';

/***********************************************************/
/* x86 io ports */

uint32_t default_ioport_readb(CPUState *env, uint32_t address)
{
#ifdef DEBUG_UNUSED_IOPORT
    fprintf(stderr, "inb: port=0x%04x\n", address);
#endif
    return 0xff;
}

void default_ioport_writeb(CPUState *env, uint32_t address, uint32_t data)
{
#ifdef DEBUG_UNUSED_IOPORT
    fprintf(stderr, "outb: port=0x%04x data=0x%02x\n", address, data);
#endif
}

/* default is to make two byte accesses */
uint32_t default_ioport_readw(CPUState *env, uint32_t address)
{
    uint32_t data;
    data = ioport_read_table[0][address & (MAX_IOPORTS - 1)](env, address);
    data |= ioport_read_table[0][(address + 1) & (MAX_IOPORTS - 1)](env, address + 1) << 8;
    return data;
}

void default_ioport_writew(CPUState *env, uint32_t address, uint32_t data)
{
    ioport_write_table[0][address & (MAX_IOPORTS - 1)](env, address, data & 0xff);
    ioport_write_table[0][(address + 1) & (MAX_IOPORTS - 1)](env, address + 1, (data >> 8) & 0xff);
}

uint32_t default_ioport_readl(CPUState *env, uint32_t address)
{
#ifdef DEBUG_UNUSED_IOPORT
    fprintf(stderr, "inl: port=0x%04x\n", address);
#endif
    return 0xffffffff;
}

void default_ioport_writel(CPUState *env, uint32_t address, uint32_t data)
{
#ifdef DEBUG_UNUSED_IOPORT
    fprintf(stderr, "outl: port=0x%04x data=0x%02x\n", address, data);
#endif
}

void init_ioports(void)
{
    int i;

    for(i = 0; i < MAX_IOPORTS; i++) {
        ioport_read_table[0][i] = default_ioport_readb;
        ioport_write_table[0][i] = default_ioport_writeb;
        ioport_read_table[1][i] = default_ioport_readw;
        ioport_write_table[1][i] = default_ioport_writew;
        ioport_read_table[2][i] = default_ioport_readl;
        ioport_write_table[2][i] = default_ioport_writel;
    }
}

/* size is the word size in byte */
int register_ioport_read(int start, int length, IOPortReadFunc *func, int size)
{
    int i, bsize;

    if (size == 1)
        bsize = 0;
    else if (size == 2)
        bsize = 1;
    else if (size == 4)
        bsize = 2;
    else
        return -1;
    for(i = start; i < start + length; i += size)
        ioport_read_table[bsize][i] = func;
    return 0;
}

/* size is the word size in byte */
int register_ioport_write(int start, int length, IOPortWriteFunc *func, int size)
{
    int i, bsize;

    if (size == 1)
        bsize = 0;
    else if (size == 2)
        bsize = 1;
    else if (size == 4)
        bsize = 2;
    else
        return -1;
    for(i = start; i < start + length; i += size)
        ioport_write_table[bsize][i] = func;
    return 0;
}

void pstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    if (buf_size <= 0)
        return;

    for(;;) {
        c = *str++;
        if (c == 0 || q >= buf + buf_size - 1)
            break;
        *q++ = c;
    }
    *q = '\0';
}

/* strcat and truncate. */
char *pstrcat(char *buf, int buf_size, const char *s)
{
    int len;
    len = strlen(buf);
    if (len < buf_size) 
        pstrcpy(buf + len, buf_size - len, s);
    return buf;
}

int load_kernel(const char *filename, uint8_t *addr)
{
    int fd, size;
#if defined (TARGET_I386)
    int setup_sects;
    uint8_t bootsect[512];
#endif

    printf("Load kernel at %p (0x%08x)\n", addr,
           (uint32_t)addr - (uint32_t)phys_ram_base);
    fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -1;
#if defined (TARGET_I386)
    if (read(fd, bootsect, 512) != 512)
        goto fail;
    setup_sects = bootsect[0x1F1];
    if (!setup_sects)
        setup_sects = 4;
    /* skip 16 bit setup code */
    lseek(fd, (setup_sects + 1) * 512, SEEK_SET);
#endif
    size = read(fd, addr, 16 * 1024 * 1024);
    if (size < 0)
        goto fail;
    close(fd);
    return size;
 fail:
    close(fd);
    return -1;
}

/* return the size or -1 if error */
int load_image(const char *filename, uint8_t *addr)
{
    int fd, size;
    fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -1;
    size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (read(fd, addr, size) != size) {
        close(fd);
        return -1;
    }
    close(fd);
    return size;
}

void cpu_outb(CPUState *env, int addr, int val)
{
    ioport_write_table[0][addr & (MAX_IOPORTS - 1)](env, addr, val);
}

void cpu_outw(CPUState *env, int addr, int val)
{
    ioport_write_table[1][addr & (MAX_IOPORTS - 1)](env, addr, val);
}

void cpu_outl(CPUState *env, int addr, int val)
{
    ioport_write_table[2][addr & (MAX_IOPORTS - 1)](env, addr, val);
}

int cpu_inb(CPUState *env, int addr)
{
    return ioport_read_table[0][addr & (MAX_IOPORTS - 1)](env, addr);
}

int cpu_inw(CPUState *env, int addr)
{
    return ioport_read_table[1][addr & (MAX_IOPORTS - 1)](env, addr);
}

int cpu_inl(CPUState *env, int addr)
{
    return ioport_read_table[2][addr & (MAX_IOPORTS - 1)](env, addr);
}

/***********************************************************/
void ioport80_write(CPUState *env, uint32_t addr, uint32_t data)
{
}

void hw_error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "qemu: hardware error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
#ifdef TARGET_I386
    cpu_x86_dump_state(global_env, stderr, X86_DUMP_FPU | X86_DUMP_CCOP);
#else
    cpu_dump_state(global_env, stderr, 0);
#endif
    va_end(ap);
    abort();
}

/***********************************************************/
/* cmos emulation */

#if defined (TARGET_I386)
#define RTC_SECONDS             0
#define RTC_SECONDS_ALARM       1
#define RTC_MINUTES             2
#define RTC_MINUTES_ALARM       3
#define RTC_HOURS               4
#define RTC_HOURS_ALARM         5
#define RTC_ALARM_DONT_CARE    0xC0

#define RTC_DAY_OF_WEEK         6
#define RTC_DAY_OF_MONTH        7
#define RTC_MONTH               8
#define RTC_YEAR                9

#define RTC_REG_A               10
#define RTC_REG_B               11
#define RTC_REG_C               12
#define RTC_REG_D               13

/* PC cmos mappings */
#define REG_EQUIPMENT_BYTE          0x14
#define REG_IBM_CENTURY_BYTE        0x32

uint8_t cmos_data[128];
uint8_t cmos_index;

void cmos_ioport_write(CPUState *env, uint32_t addr, uint32_t data)
{
    if (addr == 0x70) {
        cmos_index = data & 0x7f;
    } else {
#ifdef DEBUG_CMOS
        printf("cmos: write index=0x%02x val=0x%02x\n",
               cmos_index, data);
#endif        
        switch(addr) {
        case RTC_SECONDS_ALARM:
        case RTC_MINUTES_ALARM:
        case RTC_HOURS_ALARM:
            /* XXX: not supported */
            cmos_data[cmos_index] = data;
            break;
        case RTC_SECONDS:
        case RTC_MINUTES:
        case RTC_HOURS:
        case RTC_DAY_OF_WEEK:
        case RTC_DAY_OF_MONTH:
        case RTC_MONTH:
        case RTC_YEAR:
            cmos_data[cmos_index] = data;
            break;
        case RTC_REG_A:
        case RTC_REG_B:
            cmos_data[cmos_index] = data;
            break;
        case RTC_REG_C:
        case RTC_REG_D:
            /* cannot write to them */
            break;
        default:
            cmos_data[cmos_index] = data;
            break;
        }
    }
}

static inline int to_bcd(int a)
{
    return ((a / 10) << 4) | (a % 10);
}

static void cmos_update_time(void)
{
    struct tm *tm;
    time_t ti;

    ti = time(NULL);
    tm = gmtime(&ti);
    cmos_data[RTC_SECONDS] = to_bcd(tm->tm_sec);
    cmos_data[RTC_MINUTES] = to_bcd(tm->tm_min);
    cmos_data[RTC_HOURS] = to_bcd(tm->tm_hour);
    cmos_data[RTC_DAY_OF_WEEK] = to_bcd(tm->tm_wday);
    cmos_data[RTC_DAY_OF_MONTH] = to_bcd(tm->tm_mday);
    cmos_data[RTC_MONTH] = to_bcd(tm->tm_mon + 1);
    cmos_data[RTC_YEAR] = to_bcd(tm->tm_year % 100);
    cmos_data[REG_IBM_CENTURY_BYTE] = to_bcd((tm->tm_year / 100) + 19);
}

uint32_t cmos_ioport_read(CPUState *env, uint32_t addr)
{
    int ret;

    if (addr == 0x70) {
        return 0xff;
    } else {
        switch(cmos_index) {
        case RTC_SECONDS:
        case RTC_MINUTES:
        case RTC_HOURS:
        case RTC_DAY_OF_WEEK:
        case RTC_DAY_OF_MONTH:
        case RTC_MONTH:
        case RTC_YEAR:
        case REG_IBM_CENTURY_BYTE:
            cmos_update_time();
            ret = cmos_data[cmos_index];
            break;
        case RTC_REG_A:
            ret = cmos_data[cmos_index];
            /* toggle update-in-progress bit for Linux (same hack as
               plex86) */
            cmos_data[RTC_REG_A] ^= 0x80; 
            break;
        case RTC_REG_C:
            ret = cmos_data[cmos_index];
            pic_set_irq(8, 0);
            cmos_data[RTC_REG_C] = 0x00; 
            break;
        default:
            ret = cmos_data[cmos_index];
            break;
        }
#ifdef DEBUG_CMOS
        printf("cmos: read index=0x%02x val=0x%02x\n",
               cmos_index, ret);
#endif
        return ret;
    }
}

void cmos_init(void)
{
    int val;

    cmos_update_time();

    cmos_data[RTC_REG_A] = 0x26;
    cmos_data[RTC_REG_B] = 0x02;
    cmos_data[RTC_REG_C] = 0x00;
    cmos_data[RTC_REG_D] = 0x80;

    /* various important CMOS locations needed by PC/Bochs bios */

    cmos_data[REG_EQUIPMENT_BYTE] = 0x02; /* FPU is there */
    cmos_data[REG_EQUIPMENT_BYTE] |= 0x04; /* PS/2 mouse installed */

    /* memory size */
    val = (phys_ram_size / 1024) - 1024;
    if (val > 65535)
        val = 65535;
    cmos_data[0x17] = val;
    cmos_data[0x18] = val >> 8;
    cmos_data[0x30] = val;
    cmos_data[0x31] = val >> 8;

    val = (phys_ram_size / 65536) - ((16 * 1024 * 1024) / 65536);
    if (val > 65535)
        val = 65535;
    cmos_data[0x34] = val;
    cmos_data[0x35] = val >> 8;
    
    switch(boot_device) {
    case 'a':
    case 'b':
        cmos_data[0x3d] = 0x01; /* floppy boot */
        break;
    default:
    case 'c':
        cmos_data[0x3d] = 0x02; /* hard drive boot */
        break;
    case 'd':
        cmos_data[0x3d] = 0x03; /* CD-ROM boot */
        break;
    }

    register_ioport_write(0x70, 2, cmos_ioport_write, 1);
    register_ioport_read(0x70, 2, cmos_ioport_read, 1);
}

void cmos_register_fd (uint8_t fd0, uint8_t fd1)
{
    int nb = 0;

    cmos_data[0x10] = 0;
    switch (fd0) {
    case 0:
        /* 1.44 Mb 3"5 drive */
        cmos_data[0x10] |= 0x40;
        break;
    case 1:
        /* 2.88 Mb 3"5 drive */
        cmos_data[0x10] |= 0x60;
        break;
    case 2:
        /* 1.2 Mb 5"5 drive */
        cmos_data[0x10] |= 0x20;
        break;
    }
    switch (fd1) {
    case 0:
        /* 1.44 Mb 3"5 drive */
        cmos_data[0x10] |= 0x04;
        break;
    case 1:
        /* 2.88 Mb 3"5 drive */
        cmos_data[0x10] |= 0x06;
        break;
    case 2:
        /* 1.2 Mb 5"5 drive */
        cmos_data[0x10] |= 0x02;
        break;
    }
    if (fd0 < 3)
        nb++;
    if (fd1 < 3)
        nb++;
    switch (nb) {
    case 0:
        break;
    case 1:
        cmos_data[REG_EQUIPMENT_BYTE] |= 0x01; /* 1 drive, ready for boot */
        break;
    case 2:
        cmos_data[REG_EQUIPMENT_BYTE] |= 0x41; /* 2 drives, ready for boot */
        break;
    }
}
#endif /* TARGET_I386 */

/***********************************************************/
/* 8259 pic emulation */

typedef struct PicState {
    uint8_t last_irr; /* edge detection */
    uint8_t irr; /* interrupt request register */
    uint8_t imr; /* interrupt mask register */
    uint8_t isr; /* interrupt service register */
    uint8_t priority_add; /* highest irq priority */
    uint8_t irq_base;
    uint8_t read_reg_select;
    uint8_t poll;
    uint8_t special_mask;
    uint8_t init_state;
    uint8_t auto_eoi;
    uint8_t rotate_on_auto_eoi;
    uint8_t special_fully_nested_mode;
    uint8_t init4; /* true if 4 byte init */
} PicState;

/* 0 is master pic, 1 is slave pic */
PicState pics[2];
int pic_irq_requested;

/* set irq level. If an edge is detected, then the IRR is set to 1 */
static inline void pic_set_irq1(PicState *s, int irq, int level)
{
    int mask;
    mask = 1 << irq;
    if (level) {
        if ((s->last_irr & mask) == 0)
            s->irr |= mask;
        s->last_irr |= mask;
    } else {
        s->last_irr &= ~mask;
    }
}

/* return the highest priority found in mask (highest = smallest
   number). Return 8 if no irq */
static inline int get_priority(PicState *s, int mask)
{
    int priority;
    if (mask == 0)
        return 8;
    priority = 0;
    while ((mask & (1 << ((priority + s->priority_add) & 7))) == 0)
        priority++;
    return priority;
}

/* return the pic wanted interrupt. return -1 if none */
static int pic_get_irq(PicState *s)
{
    int mask, cur_priority, priority;

    mask = s->irr & ~s->imr;
    priority = get_priority(s, mask);
    if (priority == 8)
        return -1;
    /* compute current priority. If special fully nested mode on the
       master, the IRQ coming from the slave is not taken into account
       for the priority computation. */
    mask = s->isr;
    if (s->special_fully_nested_mode && s == &pics[0])
        mask &= ~(1 << 2);
    cur_priority = get_priority(s, mask);
    if (priority < cur_priority) {
        /* higher priority found: an irq should be generated */
        return (priority + s->priority_add) & 7;
    } else {
        return -1;
    }
}

/* raise irq to CPU if necessary. must be called every time the active
   irq may change */
void pic_update_irq(void)
{
    int irq2, irq;

    /* first look at slave pic */
    irq2 = pic_get_irq(&pics[1]);
    if (irq2 >= 0) {
        /* if irq request by slave pic, signal master PIC */
        pic_set_irq1(&pics[0], 2, 1);
        pic_set_irq1(&pics[0], 2, 0);
    }
    /* look at requested irq */
    irq = pic_get_irq(&pics[0]);
    if (irq >= 0) {
        if (irq == 2) {
            /* from slave pic */
            pic_irq_requested = 8 + irq2;
        } else {
            /* from master pic */
            pic_irq_requested = irq;
        }
#if defined(DEBUG_PIC)
        {
            int i;
            for(i = 0; i < 2; i++) {
                printf("pic%d: imr=%x irr=%x padd=%d\n", 
                       i, pics[i].imr, pics[i].irr, pics[i].priority_add);
                
            }
        }
        printf("pic: cpu_interrupt req=%d\n", pic_irq_requested);
#endif
        cpu_interrupt(global_env, CPU_INTERRUPT_HARD);
    }
}

#ifdef DEBUG_IRQ_LATENCY
int64_t irq_time[16];
int64_t cpu_get_ticks(void);
#endif
#if defined(DEBUG_PIC)
int irq_level[16];
#endif

void pic_set_irq(int irq, int level)
{
#if defined(DEBUG_PIC)
    if (level != irq_level[irq]) {
        printf("pic_set_irq: irq=%d level=%d\n", irq, level);
        irq_level[irq] = level;
    }
#endif
#ifdef DEBUG_IRQ_LATENCY
    if (level) {
        irq_time[irq] = cpu_get_ticks();
    }
#endif
    pic_set_irq1(&pics[irq >> 3], irq & 7, level);
    pic_update_irq();
}

/* acknowledge interrupt 'irq' */
static inline void pic_intack(PicState *s, int irq)
{
    if (s->auto_eoi) {
        if (s->rotate_on_auto_eoi)
            s->priority_add = (irq + 1) & 7;
    } else {
        s->isr |= (1 << irq);
    }
    s->irr &= ~(1 << irq);
}

int cpu_x86_get_pic_interrupt(CPUState *env)
{
    int irq, irq2, intno;

    /* signal the pic that the irq was acked by the CPU */
    irq = pic_irq_requested;
#ifdef DEBUG_IRQ_LATENCY
    printf("IRQ%d latency=%0.3fus\n", 
           irq, 
           (double)(cpu_get_ticks() - irq_time[irq]) * 1000000.0 / ticks_per_sec);
#endif
#if defined(DEBUG_PIC)
    printf("pic_interrupt: irq=%d\n", irq);
#endif

    if (irq >= 8) {
        irq2 = irq & 7;
        pic_intack(&pics[1], irq2);
        irq = 2;
        intno = pics[1].irq_base + irq2;
    } else {
        intno = pics[0].irq_base + irq;
    }
    pic_intack(&pics[0], irq);
    return intno;
}

void pic_ioport_write(CPUState *env, uint32_t addr, uint32_t val)
{
    PicState *s;
    int priority, cmd, irq;

#ifdef DEBUG_PIC
    printf("pic_write: addr=0x%02x val=0x%02x\n", addr, val);
#endif
    s = &pics[addr >> 7];
    addr &= 1;
    if (addr == 0) {
        if (val & 0x10) {
            /* init */
            memset(s, 0, sizeof(PicState));
            s->init_state = 1;
            s->init4 = val & 1;
            if (val & 0x02)
                hw_error("single mode not supported");
            if (val & 0x08)
                hw_error("level sensitive irq not supported");
        } else if (val & 0x08) {
            if (val & 0x04)
                s->poll = 1;
            if (val & 0x02)
                s->read_reg_select = val & 1;
            if (val & 0x40)
                s->special_mask = (val >> 5) & 1;
        } else {
            cmd = val >> 5;
            switch(cmd) {
            case 0:
            case 4:
                s->rotate_on_auto_eoi = cmd >> 2;
                break;
            case 1: /* end of interrupt */
            case 5:
                priority = get_priority(s, s->isr);
                if (priority != 8) {
                    irq = (priority + s->priority_add) & 7;
                    s->isr &= ~(1 << irq);
                    if (cmd == 5)
                        s->priority_add = (irq + 1) & 7;
                    pic_update_irq();
                }
                break;
            case 3:
                irq = val & 7;
                s->isr &= ~(1 << irq);
                pic_update_irq();
                break;
            case 6:
                s->priority_add = (val + 1) & 7;
                pic_update_irq();
                break;
            case 7:
                irq = val & 7;
                s->isr &= ~(1 << irq);
                s->priority_add = (irq + 1) & 7;
                pic_update_irq();
                break;
            default:
                /* no operation */
                break;
            }
        }
    } else {
        switch(s->init_state) {
        case 0:
            /* normal mode */
            s->imr = val;
            pic_update_irq();
            break;
        case 1:
            s->irq_base = val & 0xf8;
            s->init_state = 2;
            break;
        case 2:
            if (s->init4) {
                s->init_state = 3;
            } else {
                s->init_state = 0;
            }
            break;
        case 3:
            s->special_fully_nested_mode = (val >> 4) & 1;
            s->auto_eoi = (val >> 1) & 1;
            s->init_state = 0;
            break;
        }
    }
}

static uint32_t pic_poll_read (PicState *s, uint32_t addr1)
{
    int ret;

    ret = pic_get_irq(s);
    if (ret >= 0) {
        if (addr1 >> 7) {
            pics[0].isr &= ~(1 << 2);
            pics[0].irr &= ~(1 << 2);
        }
        s->irr &= ~(1 << ret);
        s->isr &= ~(1 << ret);
        if (addr1 >> 7 || ret != 2)
            pic_update_irq();
    } else {
        ret = 0x07;
        pic_update_irq();
    }

    return ret;
}

uint32_t pic_ioport_read(CPUState *env, uint32_t addr1)
{
    PicState *s;
    unsigned int addr;
    int ret;

    addr = addr1;
    s = &pics[addr >> 7];
    addr &= 1;
    if (s->poll) {
        ret = pic_poll_read(s, addr1);
        s->poll = 0;
    } else {
        if (addr == 0) {
            if (s->read_reg_select)
                ret = s->isr;
            else
                ret = s->irr;
        } else {
            ret = s->imr;
        }
    }
#ifdef DEBUG_PIC
    printf("pic_read: addr=0x%02x val=0x%02x\n", addr1, ret);
#endif
    return ret;
}

/* memory mapped interrupt status */
uint32_t pic_intack_read(CPUState *env)
{
    int ret;

    ret = pic_poll_read(&pics[0], 0x00);
    if (ret == 2)
        ret = pic_poll_read(&pics[1], 0x80) + 8;
    /* Prepare for ISR read */
    pics[0].read_reg_select = 1;
    
    return ret;
}

void pic_init(void)
{
#if defined (TARGET_I386) || defined (TARGET_PPC)
    register_ioport_write(0x20, 2, pic_ioport_write, 1);
    register_ioport_read(0x20, 2, pic_ioport_read, 1);
    register_ioport_write(0xa0, 2, pic_ioport_write, 1);
    register_ioport_read(0xa0, 2, pic_ioport_read, 1);
#endif
}

/***********************************************************/
/* 8253 PIT emulation */

#define PIT_FREQ 1193182

#define RW_STATE_LSB 0
#define RW_STATE_MSB 1
#define RW_STATE_WORD0 2
#define RW_STATE_WORD1 3
#define RW_STATE_LATCHED_WORD0 4
#define RW_STATE_LATCHED_WORD1 5

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

PITChannelState pit_channels[3];
int speaker_data_on;
int dummy_refresh_clock;
int pit_min_timer_count = 0;


#if defined(__powerpc__)

static inline uint32_t get_tbl(void) 
{
    uint32_t tbl;
    asm volatile("mftb %0" : "=r" (tbl));
    return tbl;
}

static inline uint32_t get_tbu(void) 
{
	uint32_t tbl;
	asm volatile("mftbu %0" : "=r" (tbl));
	return tbl;
}

int64_t cpu_get_real_ticks(void)
{
    uint32_t l, h, h1;
    /* NOTE: we test if wrapping has occurred */
    do {
        h = get_tbu();
        l = get_tbl();
        h1 = get_tbu();
    } while (h != h1);
    return ((int64_t)h << 32) | l;
}

#elif defined(__i386__)

int64_t cpu_get_real_ticks(void)
{
    int64_t val;
    asm("rdtsc" : "=A" (val));
    return val;
}

#else
#error unsupported CPU
#endif

static int64_t cpu_ticks_offset;
static int64_t cpu_ticks_last;

int64_t cpu_get_ticks(void)
{
    return cpu_get_real_ticks() + cpu_ticks_offset;
}

/* enable cpu_get_ticks() */
void cpu_enable_ticks(void)
{
    cpu_ticks_offset = cpu_ticks_last - cpu_get_real_ticks();
}

/* disable cpu_get_ticks() : the clock is stopped. You must not call
   cpu_get_ticks() after that.  */
void cpu_disable_ticks(void)
{
    cpu_ticks_last = cpu_get_ticks();
}

int64_t get_clock(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000LL + tv.tv_usec;
}

void cpu_calibrate_ticks(void)
{
    int64_t usec, ticks;

    usec = get_clock();
    ticks = cpu_get_ticks();
    usleep(50 * 1000);
    usec = get_clock() - usec;
    ticks = cpu_get_ticks() - ticks;
    ticks_per_sec = (ticks * 1000000LL + (usec >> 1)) / usec;
}

/* compute with 96 bit intermediate result: (a*b)/c */
static uint64_t muldiv64(uint64_t a, uint32_t b, uint32_t c)
{
    union {
        uint64_t ll;
        struct {
#ifdef WORDS_BIGENDIAN
            uint32_t high, low;
#else
            uint32_t low, high;
#endif            
        } l;
    } u, res;
    uint64_t rl, rh;

    u.ll = a;
    rl = (uint64_t)u.l.low * (uint64_t)b;
    rh = (uint64_t)u.l.high * (uint64_t)b;
    rh += (rl >> 32);
    res.l.high = rh / c;
    res.l.low = (((rh % c) << 32) + (rl & 0xffffffff)) / c;
    return res.ll;
}

static int pit_get_count(PITChannelState *s)
{
    uint64_t d;
    int counter;

    d = muldiv64(cpu_get_ticks() - s->count_load_time, PIT_FREQ, ticks_per_sec);
    switch(s->mode) {
    case 0:
    case 1:
    case 4:
    case 5:
        counter = (s->count - d) & 0xffff;
        break;
    case 3:
        /* XXX: may be incorrect for odd counts */
        counter = s->count - ((2 * d) % s->count);
        break;
    default:
        counter = s->count - (d % s->count);
        break;
    }
    return counter;
}

/* get pit output bit */
static int pit_get_out(PITChannelState *s)
{
    uint64_t d;
    int out;

    d = muldiv64(cpu_get_ticks() - s->count_load_time, PIT_FREQ, ticks_per_sec);
    switch(s->mode) {
    default:
    case 0:
        out = (d >= s->count);
        break;
    case 1:
        out = (d < s->count);
        break;
    case 2:
        if ((d % s->count) == 0 && d != 0)
            out = 1;
        else
            out = 0;
        break;
    case 3:
        out = (d % s->count) < ((s->count + 1) >> 1);
        break;
    case 4:
    case 5:
        out = (d == s->count);
        break;
    }
    return out;
}

/* get the number of 0 to 1 transitions we had since we call this
   function */
/* XXX: maybe better to use ticks precision to avoid getting edges
   twice if checks are done at very small intervals */
static int pit_get_out_edges(PITChannelState *s)
{
    uint64_t d1, d2;
    int64_t ticks;
    int ret, v;

    ticks = cpu_get_ticks();
    d1 = muldiv64(s->count_last_edge_check_time - s->count_load_time, 
                 PIT_FREQ, ticks_per_sec);
    d2 = muldiv64(ticks - s->count_load_time, 
                  PIT_FREQ, ticks_per_sec);
    s->count_last_edge_check_time = ticks;
    switch(s->mode) {
    default:
    case 0:
        if (d1 < s->count && d2 >= s->count)
            ret = 1;
        else
            ret = 0;
        break;
    case 1:
        ret = 0;
        break;
    case 2:
        d1 /= s->count;
        d2 /= s->count;
        ret = d2 - d1;
        break;
    case 3:
        v = s->count - ((s->count + 1) >> 1);
        d1 = (d1 + v) / s->count;
        d2 = (d2 + v) / s->count;
        ret = d2 - d1;
        break;
    case 4:
    case 5:
        if (d1 < s->count && d2 >= s->count)
            ret = 1;
        else
            ret = 0;
        break;
    }
    return ret;
}

/* val must be 0 or 1 */
static inline void pit_set_gate(PITChannelState *s, int val)
{
    switch(s->mode) {
    default:
    case 0:
    case 4:
        /* XXX: just disable/enable counting */
        break;
    case 1:
    case 5:
        if (s->gate < val) {
            /* restart counting on rising edge */
            s->count_load_time = cpu_get_ticks();
            s->count_last_edge_check_time = s->count_load_time;
        }
        break;
    case 2:
    case 3:
        if (s->gate < val) {
            /* restart counting on rising edge */
            s->count_load_time = cpu_get_ticks();
            s->count_last_edge_check_time = s->count_load_time;
        }
        /* XXX: disable/enable counting */
        break;
    }
    s->gate = val;
}

static inline void pit_load_count(PITChannelState *s, int val)
{
    if (val == 0)
        val = 0x10000;
    s->count_load_time = cpu_get_ticks();
    s->count_last_edge_check_time = s->count_load_time;
    s->count = val;
    if (s == &pit_channels[0] && val <= pit_min_timer_count) {
        fprintf(stderr, 
                "\nWARNING: qemu: on your system, accurate timer emulation is impossible if its frequency is more than %d Hz. If using a 2.5.xx Linux kernel, you must patch asm/param.h to change HZ from 1000 to 100.\n\n", 
                PIT_FREQ / pit_min_timer_count);
    }
}

void pit_ioport_write(CPUState *env, uint32_t addr, uint32_t val)
{
    int channel, access;
    PITChannelState *s;

    addr &= 3;
    if (addr == 3) {
        channel = val >> 6;
        if (channel == 3)
            return;
        s = &pit_channels[channel];
        access = (val >> 4) & 3;
        switch(access) {
        case 0:
            s->latched_count = pit_get_count(s);
            s->rw_state = RW_STATE_LATCHED_WORD0;
            break;
        default:
            s->mode = (val >> 1) & 7;
            s->bcd = val & 1;
            s->rw_state = access - 1 +  RW_STATE_LSB;
            break;
        }
    } else {
        s = &pit_channels[addr];
        switch(s->rw_state) {
        case RW_STATE_LSB:
            pit_load_count(s, val);
            break;
        case RW_STATE_MSB:
            pit_load_count(s, val << 8);
            break;
        case RW_STATE_WORD0:
        case RW_STATE_WORD1:
            if (s->rw_state & 1) {
                pit_load_count(s, (s->latched_count & 0xff) | (val << 8));
            } else {
                s->latched_count = val;
            }
            s->rw_state ^= 1;
            break;
        }
    }
}

uint32_t pit_ioport_read(CPUState *env, uint32_t addr)
{
    int ret, count;
    PITChannelState *s;
    
    addr &= 3;
    s = &pit_channels[addr];
    switch(s->rw_state) {
    case RW_STATE_LSB:
    case RW_STATE_MSB:
    case RW_STATE_WORD0:
    case RW_STATE_WORD1:
        count = pit_get_count(s);
        if (s->rw_state & 1)
            ret = (count >> 8) & 0xff;
        else
            ret = count & 0xff;
        if (s->rw_state & 2)
            s->rw_state ^= 1;
        break;
    default:
    case RW_STATE_LATCHED_WORD0:
    case RW_STATE_LATCHED_WORD1:
        if (s->rw_state & 1)
            ret = s->latched_count >> 8;
        else
            ret = s->latched_count & 0xff;
        s->rw_state ^= 1;
        break;
    }
    return ret;
}

#if defined (TARGET_I386)
void speaker_ioport_write(CPUState *env, uint32_t addr, uint32_t val)
{
    speaker_data_on = (val >> 1) & 1;
    pit_set_gate(&pit_channels[2], val & 1);
}

uint32_t speaker_ioport_read(CPUState *env, uint32_t addr)
{
    int out;
    out = pit_get_out(&pit_channels[2]);
    dummy_refresh_clock ^= 1;
    return (speaker_data_on << 1) | pit_channels[2].gate | (out << 5) |
      (dummy_refresh_clock << 4);
}
#endif

void pit_init(void)
{
    PITChannelState *s;
    int i;

    cpu_calibrate_ticks();

    for(i = 0;i < 3; i++) {
        s = &pit_channels[i];
        s->mode = 3;
        s->gate = (i != 2);
        pit_load_count(s, 0);
    }

    register_ioport_write(0x40, 4, pit_ioport_write, 1);
    register_ioport_read(0x40, 3, pit_ioport_read, 1);

#if defined (TARGET_I386)
    register_ioport_read(0x61, 1, speaker_ioport_read, 1);
    register_ioport_write(0x61, 1, speaker_ioport_write, 1);
#endif
}

/***********************************************************/
/* serial port emulation */

#define UART_IRQ        4

#define UART_LCR_DLAB	0x80	/* Divisor latch access bit */

#define UART_IER_MSI	0x08	/* Enable Modem status interrupt */
#define UART_IER_RLSI	0x04	/* Enable receiver line status interrupt */
#define UART_IER_THRI	0x02	/* Enable Transmitter holding register int. */
#define UART_IER_RDI	0x01	/* Enable receiver data interrupt */

#define UART_IIR_NO_INT	0x01	/* No interrupts pending */
#define UART_IIR_ID	0x06	/* Mask for the interrupt ID */

#define UART_IIR_MSI	0x00	/* Modem status interrupt */
#define UART_IIR_THRI	0x02	/* Transmitter holding register empty */
#define UART_IIR_RDI	0x04	/* Receiver data interrupt */
#define UART_IIR_RLSI	0x06	/* Receiver line status interrupt */

/*
 * These are the definitions for the Modem Control Register
 */
#define UART_MCR_LOOP	0x10	/* Enable loopback test mode */
#define UART_MCR_OUT2	0x08	/* Out2 complement */
#define UART_MCR_OUT1	0x04	/* Out1 complement */
#define UART_MCR_RTS	0x02	/* RTS complement */
#define UART_MCR_DTR	0x01	/* DTR complement */

/*
 * These are the definitions for the Modem Status Register
 */
#define UART_MSR_DCD	0x80	/* Data Carrier Detect */
#define UART_MSR_RI	0x40	/* Ring Indicator */
#define UART_MSR_DSR	0x20	/* Data Set Ready */
#define UART_MSR_CTS	0x10	/* Clear to Send */
#define UART_MSR_DDCD	0x08	/* Delta DCD */
#define UART_MSR_TERI	0x04	/* Trailing edge ring indicator */
#define UART_MSR_DDSR	0x02	/* Delta DSR */
#define UART_MSR_DCTS	0x01	/* Delta CTS */
#define UART_MSR_ANY_DELTA 0x0F	/* Any of the delta bits! */

#define UART_LSR_TEMT	0x40	/* Transmitter empty */
#define UART_LSR_THRE	0x20	/* Transmit-hold-register empty */
#define UART_LSR_BI	0x10	/* Break interrupt indicator */
#define UART_LSR_FE	0x08	/* Frame error indicator */
#define UART_LSR_PE	0x04	/* Parity error indicator */
#define UART_LSR_OE	0x02	/* Overrun error indicator */
#define UART_LSR_DR	0x01	/* Receiver data ready */

typedef struct SerialState {
    uint8_t divider;
    uint8_t rbr; /* receive register */
    uint8_t ier;
    uint8_t iir; /* read only */
    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr; /* read only */
    uint8_t msr;
    uint8_t scr;
    /* NOTE: this hidden state is necessary for tx irq generation as
       it can be reset while reading iir */
    int thr_ipending;
} SerialState;

SerialState serial_ports[1];

void serial_update_irq(void)
{
    SerialState *s = &serial_ports[0];

    if ((s->lsr & UART_LSR_DR) && (s->ier & UART_IER_RDI)) {
        s->iir = UART_IIR_RDI;
    } else if (s->thr_ipending && (s->ier & UART_IER_THRI)) {
        s->iir = UART_IIR_THRI;
    } else {
        s->iir = UART_IIR_NO_INT;
    }
    if (s->iir != UART_IIR_NO_INT) {
        pic_set_irq(UART_IRQ, 1);
    } else {
        pic_set_irq(UART_IRQ, 0);
    }
}

void serial_ioport_write(CPUState *env, uint32_t addr, uint32_t val)
{
    SerialState *s = &serial_ports[0];
    unsigned char ch;
    int ret;
    
    addr &= 7;
#ifdef DEBUG_SERIAL
    printf("serial: write addr=0x%02x val=0x%02x\n", addr, val);
#endif
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = (s->divider & 0xff00) | val;
        } else {
            s->thr_ipending = 0;
            s->lsr &= ~UART_LSR_THRE;
            serial_update_irq();

            ch = val;
            do {
                ret = write(1, &ch, 1);
            } while (ret != 1);
            s->thr_ipending = 1;
            s->lsr |= UART_LSR_THRE;
            s->lsr |= UART_LSR_TEMT;
            serial_update_irq();
        }
        break;
    case 1:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = (s->divider & 0x00ff) | (val << 8);
        } else {
            s->ier = val;
            serial_update_irq();
        }
        break;
    case 2:
        break;
    case 3:
        s->lcr = val;
        break;
    case 4:
        s->mcr = val;
        break;
    case 5:
        break;
    case 6:
        s->msr = val;
        break;
    case 7:
        s->scr = val;
        break;
    }
}

uint32_t serial_ioport_read(CPUState *env, uint32_t addr)
{
    SerialState *s = &serial_ports[0];
    uint32_t ret;

    addr &= 7;
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            ret = s->divider & 0xff; 
        } else {
            ret = s->rbr;
            s->lsr &= ~(UART_LSR_DR | UART_LSR_BI);
            serial_update_irq();
        }
        break;
    case 1:
        if (s->lcr & UART_LCR_DLAB) {
            ret = (s->divider >> 8) & 0xff;
        } else {
            ret = s->ier;
        }
        break;
    case 2:
        ret = s->iir;
        /* reset THR pending bit */
        if ((ret & 0x7) == UART_IIR_THRI)
            s->thr_ipending = 0;
        serial_update_irq();
        break;
    case 3:
        ret = s->lcr;
        break;
    case 4:
        ret = s->mcr;
        break;
    case 5:
        ret = s->lsr;
        break;
    case 6:
        if (s->mcr & UART_MCR_LOOP) {
            /* in loopback, the modem output pins are connected to the
               inputs */
            ret = (s->mcr & 0x0c) << 4;
            ret |= (s->mcr & 0x02) << 3;
            ret |= (s->mcr & 0x01) << 5;
        } else {
            ret = s->msr;
        }
        break;
    case 7:
        ret = s->scr;
        break;
    }
#ifdef DEBUG_SERIAL
    printf("serial: read addr=0x%02x val=0x%02x\n", addr, ret);
#endif
    return ret;
}

#define TERM_ESCAPE 0x01 /* ctrl-a is used for escape */
static int term_got_escape, term_command;
static unsigned char term_cmd_buf[128];

typedef struct term_cmd_t {
    const unsigned char *name;
    void (*handler)(unsigned char *params);
} term_cmd_t;

static void do_change_cdrom (unsigned char *params);
static void do_change_fd0 (unsigned char *params);
static void do_change_fd1 (unsigned char *params);

static term_cmd_t term_cmds[] = {
    { "changecd", &do_change_cdrom, },
    { "changefd0", &do_change_fd0, },
    { "changefd1", &do_change_fd1, },
    { NULL, NULL, },
};

void term_print_help(void)
{
    printf("\n"
           "C-a h    print this help\n"
           "C-a x    exit emulatior\n"
           "C-a d    switch on/off debug log\n"
	   "C-a s    save disk data back to file (if -snapshot)\n"
           "C-a b    send break (magic sysrq)\n"
           "C-a c    send qemu internal command\n"
           "C-a C-a  send C-a\n"
           );
}

static void do_change_cdrom (unsigned char *params)
{
    /* Dunno how to do it... */
}

static void do_change_fd (int fd, unsigned char *params)
{
    unsigned char *name_start, *name_end, *ros;
    int ro;

    for (name_start = params;
         isspace(*name_start); name_start++)
        continue;
    if (*name_start == '\0')
        return;
    for (name_end = name_start;
         !isspace(*name_end) && *name_end != '\0'; name_end++)
        continue;
    for (ros = name_end + 1; isspace(*ros); ros++)
        continue;
    if (ros[0] == 'r' && ros[1] == 'o')
        ro = 1;
    else
        ro = 0;
    *name_end = '\0';
    printf("Change fd %d to %s (%s)\n", fd, name_start, params);
    fdctrl_disk_change(fd, name_start, ro);
}

static void do_change_fd0 (unsigned char *params)
{
    do_change_fd(0, params);
}

static void do_change_fd1 (unsigned char *params)
{
    do_change_fd(1, params);
}

static void serial_treat_command ()
{
    unsigned char *cmd_start, *cmd_end;
    int i;

    for (cmd_start = term_cmd_buf; isspace(*cmd_start); cmd_start++)
        continue;
    for (cmd_end = cmd_start;
         !isspace(*cmd_end) && *cmd_end != '\0'; cmd_end++)
        continue;
    for (i = 0; term_cmds[i].name != NULL; i++) {
        if (strlen(term_cmds[i].name) == (cmd_end - cmd_start) &&
            memcmp(term_cmds[i].name, cmd_start, cmd_end - cmd_start) == 0) {
            (*term_cmds[i].handler)(cmd_end + 1);
            return;
        }
    }
    *cmd_end = '\0';
    printf("Unknown term command: %s\n", cmd_start);
}

extern FILE *logfile;

/* called when a char is received */
void serial_received_byte(SerialState *s, int ch)
{
    if (term_command) {
        if (ch == '\n' || ch == '\r' || term_command == 127) {
            printf("\n");
            serial_treat_command();
            term_command = 0;
        } else {
            if (ch == 0x7F || ch == 0x08) {
                if (term_command > 1) {
                    term_cmd_buf[--term_command - 1] = '\0';
                    printf("\r                                               "
                           "                               ");
                    printf("\r> %s", term_cmd_buf);
                }
            } else if (ch > 0x1f) {
                term_cmd_buf[term_command++ - 1] = ch;
                term_cmd_buf[term_command - 1] = '\0';
                printf("\r> %s", term_cmd_buf);
            }
            fflush(stdout);
        }
    } else if (term_got_escape) {
        term_got_escape = 0;
        switch(ch) {
        case 'h':
            term_print_help();
            break;
        case 'x':
            exit(0);
            break;
	case 's': 
            {
                int i;
                for (i = 0; i < MAX_DISKS; i++) {
                    if (bs_table[i])
                        bdrv_commit(bs_table[i]);
                }
	    }
            break;
        case 'b':
            /* send break */
            s->rbr = 0;
            s->lsr |= UART_LSR_BI | UART_LSR_DR;
            serial_update_irq();
            break;
        case 'c':
            printf("> ");
            fflush(stdout);
            term_command = 1;
            break;
        case 'd':
            cpu_set_log(CPU_LOG_ALL);
            break;
        case TERM_ESCAPE:
            goto send_char;
        }
    } else if (ch == TERM_ESCAPE) {
        term_got_escape = 1;
    } else {
    send_char:
        s->rbr = ch;
        s->lsr |= UART_LSR_DR;
        serial_update_irq();
    }
}

void serial_init(void)
{
    SerialState *s = &serial_ports[0];

    s->lsr = UART_LSR_TEMT | UART_LSR_THRE;
    s->iir = UART_IIR_NO_INT;
    
#if defined(TARGET_I386) || defined (TARGET_PPC)
    register_ioport_write(0x3f8, 8, serial_ioport_write, 1);
    register_ioport_read(0x3f8, 8, serial_ioport_read, 1);
#endif
}

/***********************************************************/
/* ne2000 emulation */

#if defined (TARGET_I386)
#define NE2000_IOPORT   0x300
#define NE2000_IRQ      9

#define MAX_ETH_FRAME_SIZE 1514

#define E8390_CMD	0x00  /* The command register (for all pages) */
/* Page 0 register offsets. */
#define EN0_CLDALO	0x01	/* Low byte of current local dma addr  RD */
#define EN0_STARTPG	0x01	/* Starting page of ring bfr WR */
#define EN0_CLDAHI	0x02	/* High byte of current local dma addr  RD */
#define EN0_STOPPG	0x02	/* Ending page +1 of ring bfr WR */
#define EN0_BOUNDARY	0x03	/* Boundary page of ring bfr RD WR */
#define EN0_TSR		0x04	/* Transmit status reg RD */
#define EN0_TPSR	0x04	/* Transmit starting page WR */
#define EN0_NCR		0x05	/* Number of collision reg RD */
#define EN0_TCNTLO	0x05	/* Low  byte of tx byte count WR */
#define EN0_FIFO	0x06	/* FIFO RD */
#define EN0_TCNTHI	0x06	/* High byte of tx byte count WR */
#define EN0_ISR		0x07	/* Interrupt status reg RD WR */
#define EN0_CRDALO	0x08	/* low byte of current remote dma address RD */
#define EN0_RSARLO	0x08	/* Remote start address reg 0 */
#define EN0_CRDAHI	0x09	/* high byte, current remote dma address RD */
#define EN0_RSARHI	0x09	/* Remote start address reg 1 */
#define EN0_RCNTLO	0x0a	/* Remote byte count reg WR */
#define EN0_RCNTHI	0x0b	/* Remote byte count reg WR */
#define EN0_RSR		0x0c	/* rx status reg RD */
#define EN0_RXCR	0x0c	/* RX configuration reg WR */
#define EN0_TXCR	0x0d	/* TX configuration reg WR */
#define EN0_COUNTER0	0x0d	/* Rcv alignment error counter RD */
#define EN0_DCFG	0x0e	/* Data configuration reg WR */
#define EN0_COUNTER1	0x0e	/* Rcv CRC error counter RD */
#define EN0_IMR		0x0f	/* Interrupt mask reg WR */
#define EN0_COUNTER2	0x0f	/* Rcv missed frame error counter RD */

#define EN1_PHYS        0x11
#define EN1_CURPAG      0x17
#define EN1_MULT        0x18

/*  Register accessed at EN_CMD, the 8390 base addr.  */
#define E8390_STOP	0x01	/* Stop and reset the chip */
#define E8390_START	0x02	/* Start the chip, clear reset */
#define E8390_TRANS	0x04	/* Transmit a frame */
#define E8390_RREAD	0x08	/* Remote read */
#define E8390_RWRITE	0x10	/* Remote write  */
#define E8390_NODMA	0x20	/* Remote DMA */
#define E8390_PAGE0	0x00	/* Select page chip registers */
#define E8390_PAGE1	0x40	/* using the two high-order bits */
#define E8390_PAGE2	0x80	/* Page 3 is invalid. */

/* Bits in EN0_ISR - Interrupt status register */
#define ENISR_RX	0x01	/* Receiver, no error */
#define ENISR_TX	0x02	/* Transmitter, no error */
#define ENISR_RX_ERR	0x04	/* Receiver, with error */
#define ENISR_TX_ERR	0x08	/* Transmitter, with error */
#define ENISR_OVER	0x10	/* Receiver overwrote the ring */
#define ENISR_COUNTERS	0x20	/* Counters need emptying */
#define ENISR_RDC	0x40	/* remote dma complete */
#define ENISR_RESET	0x80	/* Reset completed */
#define ENISR_ALL	0x3f	/* Interrupts we will enable */

/* Bits in received packet status byte and EN0_RSR*/
#define ENRSR_RXOK	0x01	/* Received a good packet */
#define ENRSR_CRC	0x02	/* CRC error */
#define ENRSR_FAE	0x04	/* frame alignment error */
#define ENRSR_FO	0x08	/* FIFO overrun */
#define ENRSR_MPA	0x10	/* missed pkt */
#define ENRSR_PHY	0x20	/* physical/multicast address */
#define ENRSR_DIS	0x40	/* receiver disable. set in monitor mode */
#define ENRSR_DEF	0x80	/* deferring */

/* Transmitted packet status, EN0_TSR. */
#define ENTSR_PTX 0x01	/* Packet transmitted without error */
#define ENTSR_ND  0x02	/* The transmit wasn't deferred. */
#define ENTSR_COL 0x04	/* The transmit collided at least once. */
#define ENTSR_ABT 0x08  /* The transmit collided 16 times, and was deferred. */
#define ENTSR_CRS 0x10	/* The carrier sense was lost. */
#define ENTSR_FU  0x20  /* A "FIFO underrun" occurred during transmit. */
#define ENTSR_CDH 0x40	/* The collision detect "heartbeat" signal was lost. */
#define ENTSR_OWC 0x80  /* There was an out-of-window collision. */

#define NE2000_MEM_SIZE 32768

typedef struct NE2000State {
    uint8_t cmd;
    uint32_t start;
    uint32_t stop;
    uint8_t boundary;
    uint8_t tsr;
    uint8_t tpsr;
    uint16_t tcnt;
    uint16_t rcnt;
    uint32_t rsar;
    uint8_t isr;
    uint8_t dcfg;
    uint8_t imr;
    uint8_t phys[6]; /* mac address */
    uint8_t curpag;
    uint8_t mult[8]; /* multicast mask array */
    uint8_t mem[NE2000_MEM_SIZE];
} NE2000State;

NE2000State ne2000_state;
int net_fd = -1;
char network_script[1024];

void ne2000_reset(void)
{
    NE2000State *s = &ne2000_state;
    int i;

    s->isr = ENISR_RESET;
    s->mem[0] = 0x52;
    s->mem[1] = 0x54;
    s->mem[2] = 0x00;
    s->mem[3] = 0x12;
    s->mem[4] = 0x34;
    s->mem[5] = 0x56;
    s->mem[14] = 0x57;
    s->mem[15] = 0x57;

    /* duplicate prom data */
    for(i = 15;i >= 0; i--) {
        s->mem[2 * i] = s->mem[i];
        s->mem[2 * i + 1] = s->mem[i];
    }
}

void ne2000_update_irq(NE2000State *s)
{
    int isr;
    isr = s->isr & s->imr;
    if (isr)
        pic_set_irq(NE2000_IRQ, 1);
    else
        pic_set_irq(NE2000_IRQ, 0);
}

int net_init(void)
{
    struct ifreq ifr;
    int fd, ret, pid, status;
    
    fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "warning: could not open /dev/net/tun: no virtual network emulation\n");
        return -1;
    }
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    pstrcpy(ifr.ifr_name, IFNAMSIZ, "tun%d");
    ret = ioctl(fd, TUNSETIFF, (void *) &ifr);
    if (ret != 0) {
        fprintf(stderr, "warning: could not configure /dev/net/tun: no virtual network emulation\n");
        close(fd);
        return -1;
    }
    printf("Connected to host network interface: %s\n", ifr.ifr_name);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    net_fd = fd;

    /* try to launch network init script */
    pid = fork();
    if (pid >= 0) {
        if (pid == 0) {
            execl(network_script, network_script, ifr.ifr_name, NULL);
            exit(1);
        }
        while (waitpid(pid, &status, 0) != pid);
        if (!WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {
            fprintf(stderr, "%s: could not launch network script for '%s'\n",
                    network_script, ifr.ifr_name);
        }
    }
    return 0;
}

void net_send_packet(NE2000State *s, const uint8_t *buf, int size)
{
#ifdef DEBUG_NE2000
    printf("NE2000: sending packet size=%d\n", size);
#endif
    write(net_fd, buf, size);
}

/* return true if the NE2000 can receive more data */
int ne2000_can_receive(NE2000State *s)
{
    int avail, index, boundary;
    
    if (s->cmd & E8390_STOP)
        return 0;
    index = s->curpag << 8;
    boundary = s->boundary << 8;
    if (index < boundary)
        avail = boundary - index;
    else
        avail = (s->stop - s->start) - (index - boundary);
    if (avail < (MAX_ETH_FRAME_SIZE + 4))
        return 0;
    return 1;
}

void ne2000_receive(NE2000State *s, uint8_t *buf, int size)
{
    uint8_t *p;
    int total_len, next, avail, len, index;

#if defined(DEBUG_NE2000)
    printf("NE2000: received len=%d\n", size);
#endif

    index = s->curpag << 8;
    /* 4 bytes for header */
    total_len = size + 4;
    /* address for next packet (4 bytes for CRC) */
    next = index + ((total_len + 4 + 255) & ~0xff);
    if (next >= s->stop)
        next -= (s->stop - s->start);
    /* prepare packet header */
    p = s->mem + index;
    p[0] = ENRSR_RXOK; /* receive status */
    p[1] = next >> 8;
    p[2] = total_len;
    p[3] = total_len >> 8;
    index += 4;

    /* write packet data */
    while (size > 0) {
        avail = s->stop - index;
        len = size;
        if (len > avail)
            len = avail;
        memcpy(s->mem + index, buf, len);
        buf += len;
        index += len;
        if (index == s->stop)
            index = s->start;
        size -= len;
    }
    s->curpag = next >> 8;
    
    /* now we can signal we have receive something */
    s->isr |= ENISR_RX;
    ne2000_update_irq(s);
}

void ne2000_ioport_write(CPUState *env, uint32_t addr, uint32_t val)
{
    NE2000State *s = &ne2000_state;
    int offset, page;

    addr &= 0xf;
#ifdef DEBUG_NE2000
    printf("NE2000: write addr=0x%x val=0x%02x\n", addr, val);
#endif
    if (addr == E8390_CMD) {
        /* control register */
        s->cmd = val;
        if (val & E8390_START) {
            /* test specific case: zero length transfert */
            if ((val & (E8390_RREAD | E8390_RWRITE)) &&
                s->rcnt == 0) {
                s->isr |= ENISR_RDC;
                ne2000_update_irq(s);
            }
            if (val & E8390_TRANS) {
                net_send_packet(s, s->mem + (s->tpsr << 8), s->tcnt);
                /* signal end of transfert */
                s->tsr = ENTSR_PTX;
                s->isr |= ENISR_TX;
                ne2000_update_irq(s);
            }
        }
    } else {
        page = s->cmd >> 6;
        offset = addr | (page << 4);
        switch(offset) {
        case EN0_STARTPG:
            s->start = val << 8;
            break;
        case EN0_STOPPG:
            s->stop = val << 8;
            break;
        case EN0_BOUNDARY:
            s->boundary = val;
            break;
        case EN0_IMR:
            s->imr = val;
            ne2000_update_irq(s);
            break;
        case EN0_TPSR:
            s->tpsr = val;
            break;
        case EN0_TCNTLO:
            s->tcnt = (s->tcnt & 0xff00) | val;
            break;
        case EN0_TCNTHI:
            s->tcnt = (s->tcnt & 0x00ff) | (val << 8);
            break;
        case EN0_RSARLO:
            s->rsar = (s->rsar & 0xff00) | val;
            break;
        case EN0_RSARHI:
            s->rsar = (s->rsar & 0x00ff) | (val << 8);
            break;
        case EN0_RCNTLO:
            s->rcnt = (s->rcnt & 0xff00) | val;
            break;
        case EN0_RCNTHI:
            s->rcnt = (s->rcnt & 0x00ff) | (val << 8);
            break;
        case EN0_DCFG:
            s->dcfg = val;
            break;
        case EN0_ISR:
            s->isr &= ~val;
            ne2000_update_irq(s);
            break;
        case EN1_PHYS ... EN1_PHYS + 5:
            s->phys[offset - EN1_PHYS] = val;
            break;
        case EN1_CURPAG:
            s->curpag = val;
            break;
        case EN1_MULT ... EN1_MULT + 7:
            s->mult[offset - EN1_MULT] = val;
            break;
        }
    }
}

uint32_t ne2000_ioport_read(CPUState *env, uint32_t addr)
{
    NE2000State *s = &ne2000_state;
    int offset, page, ret;

    addr &= 0xf;
    if (addr == E8390_CMD) {
        ret = s->cmd;
    } else {
        page = s->cmd >> 6;
        offset = addr | (page << 4);
        switch(offset) {
        case EN0_TSR:
            ret = s->tsr;
            break;
        case EN0_BOUNDARY:
            ret = s->boundary;
            break;
        case EN0_ISR:
            ret = s->isr;
            break;
        case EN1_PHYS ... EN1_PHYS + 5:
            ret = s->phys[offset - EN1_PHYS];
            break;
        case EN1_CURPAG:
            ret = s->curpag;
            break;
        case EN1_MULT ... EN1_MULT + 7:
            ret = s->mult[offset - EN1_MULT];
            break;
        default:
            ret = 0x00;
            break;
        }
    }
#ifdef DEBUG_NE2000
    printf("NE2000: read addr=0x%x val=%02x\n", addr, ret);
#endif
    return ret;
}

void ne2000_asic_ioport_write(CPUState *env, uint32_t addr, uint32_t val)
{
    NE2000State *s = &ne2000_state;
    uint8_t *p;

#ifdef DEBUG_NE2000
    printf("NE2000: asic write val=0x%04x\n", val);
#endif
    p = s->mem + s->rsar;
    if (s->dcfg & 0x01) {
        /* 16 bit access */
        p[0] = val;
        p[1] = val >> 8;
        s->rsar += 2;
        s->rcnt -= 2;
    } else {
        /* 8 bit access */
        p[0] = val;
        s->rsar++;
        s->rcnt--;
    }
    /* wrap */
    if (s->rsar == s->stop)
        s->rsar = s->start;
    if (s->rcnt == 0) {
        /* signal end of transfert */
        s->isr |= ENISR_RDC;
        ne2000_update_irq(s);
    }
}

uint32_t ne2000_asic_ioport_read(CPUState *env, uint32_t addr)
{
    NE2000State *s = &ne2000_state;
    uint8_t *p;
    int ret;

    p = s->mem + s->rsar;
    if (s->dcfg & 0x01) {
        /* 16 bit access */
        ret = p[0] | (p[1] << 8);
        s->rsar += 2;
        s->rcnt -= 2;
    } else {
        /* 8 bit access */
        ret = p[0];
        s->rsar++;
        s->rcnt--;
    }
    /* wrap */
    if (s->rsar == s->stop)
        s->rsar = s->start;
    if (s->rcnt == 0) {
        /* signal end of transfert */
        s->isr |= ENISR_RDC;
        ne2000_update_irq(s);
    }
#ifdef DEBUG_NE2000
    printf("NE2000: asic read val=0x%04x\n", ret);
#endif
    return ret;
}

void ne2000_reset_ioport_write(CPUState *env, uint32_t addr, uint32_t val)
{
    /* nothing to do (end of reset pulse) */
}

uint32_t ne2000_reset_ioport_read(CPUState *env, uint32_t addr)
{
    ne2000_reset();
    return 0;
}

void ne2000_init(void)
{
    register_ioport_write(NE2000_IOPORT, 16, ne2000_ioport_write, 1);
    register_ioport_read(NE2000_IOPORT, 16, ne2000_ioport_read, 1);

    register_ioport_write(NE2000_IOPORT + 0x10, 1, ne2000_asic_ioport_write, 1);
    register_ioport_read(NE2000_IOPORT + 0x10, 1, ne2000_asic_ioport_read, 1);
    register_ioport_write(NE2000_IOPORT + 0x10, 2, ne2000_asic_ioport_write, 2);
    register_ioport_read(NE2000_IOPORT + 0x10, 2, ne2000_asic_ioport_read, 2);

    register_ioport_write(NE2000_IOPORT + 0x1f, 1, ne2000_reset_ioport_write, 1);
    register_ioport_read(NE2000_IOPORT + 0x1f, 1, ne2000_reset_ioport_read, 1);
    ne2000_reset();
}
#endif

/***********************************************************/
/* PC floppy disk controler emulation glue */
#define PC_FDC_DMA  0x2
#define PC_FDC_IRQ  0x6
#define PC_FDC_BASE 0x3F0

static void fdctrl_register (unsigned char **disknames, int ro,
                             char boot_device)
{
    int i;

    fdctrl_init(PC_FDC_IRQ, PC_FDC_DMA, 0, PC_FDC_BASE, boot_device);
    for (i = 0; i < MAX_FD; i++) {
        if (disknames[i] != NULL)
            fdctrl_disk_change(i, disknames[i], ro);
    }
}

/***********************************************************/
/* keyboard emulation */

/*	Keyboard Controller Commands */
#define KBD_CCMD_READ_MODE	0x20	/* Read mode bits */
#define KBD_CCMD_WRITE_MODE	0x60	/* Write mode bits */
#define KBD_CCMD_GET_VERSION	0xA1	/* Get controller version */
#define KBD_CCMD_MOUSE_DISABLE	0xA7	/* Disable mouse interface */
#define KBD_CCMD_MOUSE_ENABLE	0xA8	/* Enable mouse interface */
#define KBD_CCMD_TEST_MOUSE	0xA9	/* Mouse interface test */
#define KBD_CCMD_SELF_TEST	0xAA	/* Controller self test */
#define KBD_CCMD_KBD_TEST	0xAB	/* Keyboard interface test */
#define KBD_CCMD_KBD_DISABLE	0xAD	/* Keyboard interface disable */
#define KBD_CCMD_KBD_ENABLE	0xAE	/* Keyboard interface enable */
#define KBD_CCMD_READ_INPORT    0xC0    /* read input port */
#define KBD_CCMD_READ_OUTPORT	0xD0    /* read output port */
#define KBD_CCMD_WRITE_OUTPORT	0xD1    /* write output port */
#define KBD_CCMD_WRITE_OBUF	0xD2
#define KBD_CCMD_WRITE_AUX_OBUF	0xD3    /* Write to output buffer as if
					   initiated by the auxiliary device */
#define KBD_CCMD_WRITE_MOUSE	0xD4	/* Write the following byte to the mouse */
#define KBD_CCMD_DISABLE_A20    0xDD    /* HP vectra only ? */
#define KBD_CCMD_ENABLE_A20     0xDF    /* HP vectra only ? */
#define KBD_CCMD_RESET	        0xFE

/* Keyboard Commands */
#define KBD_CMD_SET_LEDS	0xED	/* Set keyboard leds */
#define KBD_CMD_ECHO     	0xEE
#define KBD_CMD_GET_ID 	        0xF2	/* get keyboard ID */
#define KBD_CMD_SET_RATE	0xF3	/* Set typematic rate */
#define KBD_CMD_ENABLE		0xF4	/* Enable scanning */
#define KBD_CMD_RESET_DISABLE	0xF5	/* reset and disable scanning */
#define KBD_CMD_RESET_ENABLE   	0xF6    /* reset and enable scanning */
#define KBD_CMD_RESET		0xFF	/* Reset */

/* Keyboard Replies */
#define KBD_REPLY_POR		0xAA	/* Power on reset */
#define KBD_REPLY_ACK		0xFA	/* Command ACK */
#define KBD_REPLY_RESEND	0xFE	/* Command NACK, send the cmd again */

/* Status Register Bits */
#define KBD_STAT_OBF 		0x01	/* Keyboard output buffer full */
#define KBD_STAT_IBF 		0x02	/* Keyboard input buffer full */
#define KBD_STAT_SELFTEST	0x04	/* Self test successful */
#define KBD_STAT_CMD		0x08	/* Last write was a command write (0=data) */
#define KBD_STAT_UNLOCKED	0x10	/* Zero if keyboard locked */
#define KBD_STAT_MOUSE_OBF	0x20	/* Mouse output buffer full */
#define KBD_STAT_GTO 		0x40	/* General receive/xmit timeout */
#define KBD_STAT_PERR 		0x80	/* Parity error */

/* Controller Mode Register Bits */
#define KBD_MODE_KBD_INT	0x01	/* Keyboard data generate IRQ1 */
#define KBD_MODE_MOUSE_INT	0x02	/* Mouse data generate IRQ12 */
#define KBD_MODE_SYS 		0x04	/* The system flag (?) */
#define KBD_MODE_NO_KEYLOCK	0x08	/* The keylock doesn't affect the keyboard if set */
#define KBD_MODE_DISABLE_KBD	0x10	/* Disable keyboard interface */
#define KBD_MODE_DISABLE_MOUSE	0x20	/* Disable mouse interface */
#define KBD_MODE_KCC 		0x40	/* Scan code conversion to PC format */
#define KBD_MODE_RFU		0x80

/* Mouse Commands */
#define AUX_SET_SCALE11		0xE6	/* Set 1:1 scaling */
#define AUX_SET_SCALE21		0xE7	/* Set 2:1 scaling */
#define AUX_SET_RES		0xE8	/* Set resolution */
#define AUX_GET_SCALE		0xE9	/* Get scaling factor */
#define AUX_SET_STREAM		0xEA	/* Set stream mode */
#define AUX_POLL		0xEB	/* Poll */
#define AUX_RESET_WRAP		0xEC	/* Reset wrap mode */
#define AUX_SET_WRAP		0xEE	/* Set wrap mode */
#define AUX_SET_REMOTE		0xF0	/* Set remote mode */
#define AUX_GET_TYPE		0xF2	/* Get type */
#define AUX_SET_SAMPLE		0xF3	/* Set sample rate */
#define AUX_ENABLE_DEV		0xF4	/* Enable aux device */
#define AUX_DISABLE_DEV		0xF5	/* Disable aux device */
#define AUX_SET_DEFAULT		0xF6
#define AUX_RESET		0xFF	/* Reset aux device */
#define AUX_ACK			0xFA	/* Command byte ACK. */

#define MOUSE_STATUS_REMOTE     0x40
#define MOUSE_STATUS_ENABLED    0x20
#define MOUSE_STATUS_SCALE21    0x10

#define KBD_QUEUE_SIZE 256

typedef struct {
    uint8_t data[KBD_QUEUE_SIZE];
    int rptr, wptr, count;
} KBDQueue;

typedef struct KBDState {
    KBDQueue queues[2];
    uint8_t write_cmd; /* if non zero, write data to port 60 is expected */
    uint8_t status;
    uint8_t mode;
    /* keyboard state */
    int kbd_write_cmd;
    int scan_enabled;
    /* mouse state */
    int mouse_write_cmd;
    uint8_t mouse_status;
    uint8_t mouse_resolution;
    uint8_t mouse_sample_rate;
    uint8_t mouse_wrap;
    uint8_t mouse_type; /* 0 = PS2, 3 = IMPS/2, 4 = IMEX */
    uint8_t mouse_detect_state;
    int mouse_dx; /* current values, needed for 'poll' mode */
    int mouse_dy;
    int mouse_dz;
    uint8_t mouse_buttons;
} KBDState;

KBDState kbd_state;
int reset_requested;

/* update irq and KBD_STAT_[MOUSE_]OBF */
/* XXX: not generating the irqs if KBD_MODE_DISABLE_KBD is set may be
   incorrect, but it avoids having to simulate exact delays */
static void kbd_update_irq(KBDState *s)
{
    int irq12_level, irq1_level;

    irq1_level = 0;    
    irq12_level = 0;    
    s->status &= ~(KBD_STAT_OBF | KBD_STAT_MOUSE_OBF);
    if (s->queues[0].count != 0 ||
        s->queues[1].count != 0) {
        s->status |= KBD_STAT_OBF;
        if (s->queues[1].count != 0) {
            s->status |= KBD_STAT_MOUSE_OBF;
            if (s->mode & KBD_MODE_MOUSE_INT)
                irq12_level = 1;
        } else {
            if ((s->mode & KBD_MODE_KBD_INT) && 
                !(s->mode & KBD_MODE_DISABLE_KBD))
                irq1_level = 1;
        }
    }
    pic_set_irq(1, irq1_level);
    pic_set_irq(12, irq12_level);
}

static void kbd_queue(KBDState *s, int b, int aux)
{
    KBDQueue *q = &kbd_state.queues[aux];

#if defined(DEBUG_MOUSE) || defined(DEBUG_KBD)
    if (aux)
        printf("mouse event: 0x%02x\n", b);
#ifdef DEBUG_KBD
    else
        printf("kbd event: 0x%02x\n", b);
#endif
#endif
    if (q->count >= KBD_QUEUE_SIZE)
        return;
    q->data[q->wptr] = b;
    if (++q->wptr == KBD_QUEUE_SIZE)
        q->wptr = 0;
    q->count++;
    kbd_update_irq(s);
}

void kbd_put_keycode(int keycode)
{
    KBDState *s = &kbd_state;
    kbd_queue(s, keycode, 0);
}

uint32_t kbd_read_status(CPUState *env, uint32_t addr)
{
    KBDState *s = &kbd_state;
    int val;
    val = s->status;
#if defined(DEBUG_KBD)
    printf("kbd: read status=0x%02x\n", val);
#endif
    return val;
}

void kbd_write_command(CPUState *env, uint32_t addr, uint32_t val)
{
    KBDState *s = &kbd_state;

#ifdef DEBUG_KBD
    printf("kbd: write cmd=0x%02x\n", val);
#endif
    switch(val) {
    case KBD_CCMD_READ_MODE:
        kbd_queue(s, s->mode, 0);
        break;
    case KBD_CCMD_WRITE_MODE:
    case KBD_CCMD_WRITE_OBUF:
    case KBD_CCMD_WRITE_AUX_OBUF:
    case KBD_CCMD_WRITE_MOUSE:
    case KBD_CCMD_WRITE_OUTPORT:
        s->write_cmd = val;
        break;
    case KBD_CCMD_MOUSE_DISABLE:
        s->mode |= KBD_MODE_DISABLE_MOUSE;
        break;
    case KBD_CCMD_MOUSE_ENABLE:
        s->mode &= ~KBD_MODE_DISABLE_MOUSE;
        break;
    case KBD_CCMD_TEST_MOUSE:
        kbd_queue(s, 0x00, 0);
        break;
    case KBD_CCMD_SELF_TEST:
        s->status |= KBD_STAT_SELFTEST;
        kbd_queue(s, 0x55, 0);
        break;
    case KBD_CCMD_KBD_TEST:
        kbd_queue(s, 0x00, 0);
        break;
    case KBD_CCMD_KBD_DISABLE:
        s->mode |= KBD_MODE_DISABLE_KBD;
        kbd_update_irq(s);
        break;
    case KBD_CCMD_KBD_ENABLE:
        s->mode &= ~KBD_MODE_DISABLE_KBD;
        kbd_update_irq(s);
        break;
    case KBD_CCMD_READ_INPORT:
        kbd_queue(s, 0x00, 0);
        break;
    case KBD_CCMD_READ_OUTPORT:
        /* XXX: check that */
#ifdef TARGET_I386
        val = 0x01 | (a20_enabled << 1);
#else
        val = 0x01;
#endif
        if (s->status & KBD_STAT_OBF)
            val |= 0x10;
        if (s->status & KBD_STAT_MOUSE_OBF)
            val |= 0x20;
        kbd_queue(s, val, 0);
        break;
#ifdef TARGET_I386
    case KBD_CCMD_ENABLE_A20:
        cpu_x86_set_a20(env, 1);
        break;
    case KBD_CCMD_DISABLE_A20:
        cpu_x86_set_a20(env, 0);
        break;
#endif
    case KBD_CCMD_RESET:
        reset_requested = 1;
        cpu_interrupt(global_env, CPU_INTERRUPT_EXIT);
        break;
    case 0xff:
        /* ignore that - I don't know what is its use */
        break;
    default:
        fprintf(stderr, "qemu: unsupported keyboard cmd=0x%02x\n", val);
        break;
    }
}

uint32_t kbd_read_data(CPUState *env, uint32_t addr)
{
    KBDState *s = &kbd_state;
    KBDQueue *q;
    int val, index;
    
    q = &s->queues[0]; /* first check KBD data */
    if (q->count == 0)
        q = &s->queues[1]; /* then check AUX data */
    if (q->count == 0) {
        /* NOTE: if no data left, we return the last keyboard one
           (needed for EMM386) */
        /* XXX: need a timer to do things correctly */
        q = &s->queues[0];
        index = q->rptr - 1;
        if (index < 0)
            index = KBD_QUEUE_SIZE - 1;
        val = q->data[index];
    } else {
        val = q->data[q->rptr];
        if (++q->rptr == KBD_QUEUE_SIZE)
            q->rptr = 0;
        q->count--;
        /* reading deasserts IRQ */
        if (q == &s->queues[0])
            pic_set_irq(1, 0);
        else
            pic_set_irq(12, 0);
    }
    /* reassert IRQs if data left */
    kbd_update_irq(s);
#ifdef DEBUG_KBD
    printf("kbd: read data=0x%02x\n", val);
#endif
    return val;
}

static void kbd_reset_keyboard(KBDState *s)
{
    s->scan_enabled = 1;
}

static void kbd_write_keyboard(KBDState *s, int val)
{
    switch(s->kbd_write_cmd) {
    default:
    case -1:
        switch(val) {
        case 0x00:
            kbd_queue(s, KBD_REPLY_ACK, 0);
            break;
        case 0x05:
            kbd_queue(s, KBD_REPLY_RESEND, 0);
            break;
        case KBD_CMD_GET_ID:
            kbd_queue(s, KBD_REPLY_ACK, 0);
            kbd_queue(s, 0xab, 0);
            kbd_queue(s, 0x83, 0);
            break;
        case KBD_CMD_ECHO:
            kbd_queue(s, KBD_CMD_ECHO, 0);
            break;
        case KBD_CMD_ENABLE:
            s->scan_enabled = 1;
            kbd_queue(s, KBD_REPLY_ACK, 0);
            break;
        case KBD_CMD_SET_LEDS:
        case KBD_CMD_SET_RATE:
            s->kbd_write_cmd = val;
            kbd_queue(s, KBD_REPLY_ACK, 0);
            break;
        case KBD_CMD_RESET_DISABLE:
            kbd_reset_keyboard(s);
            s->scan_enabled = 0;
            kbd_queue(s, KBD_REPLY_ACK, 0);
            break;
        case KBD_CMD_RESET_ENABLE:
            kbd_reset_keyboard(s);
            s->scan_enabled = 1;
            kbd_queue(s, KBD_REPLY_ACK, 0);
            break;
        case KBD_CMD_RESET:
            kbd_reset_keyboard(s);
            kbd_queue(s, KBD_REPLY_ACK, 0);
            kbd_queue(s, KBD_REPLY_POR, 0);
            break;
        default:
            kbd_queue(s, KBD_REPLY_ACK, 0);
            break;
        }
        break;
    case KBD_CMD_SET_LEDS:
        kbd_queue(s, KBD_REPLY_ACK, 0);
        s->kbd_write_cmd = -1;
        break;
    case KBD_CMD_SET_RATE:
        kbd_queue(s, KBD_REPLY_ACK, 0);
        s->kbd_write_cmd = -1;
        break;
    }
}

static void kbd_mouse_send_packet(KBDState *s)
{
    unsigned int b;
    int dx1, dy1, dz1;

    dx1 = s->mouse_dx;
    dy1 = s->mouse_dy;
    dz1 = s->mouse_dz;
    /* XXX: increase range to 8 bits ? */
    if (dx1 > 127)
        dx1 = 127;
    else if (dx1 < -127)
        dx1 = -127;
    if (dy1 > 127)
        dy1 = 127;
    else if (dy1 < -127)
        dy1 = -127;
    b = 0x08 | ((dx1 < 0) << 4) | ((dy1 < 0) << 5) | (s->mouse_buttons & 0x07);
    kbd_queue(s, b, 1);
    kbd_queue(s, dx1 & 0xff, 1);
    kbd_queue(s, dy1 & 0xff, 1);
    /* extra byte for IMPS/2 or IMEX */
    switch(s->mouse_type) {
    default:
        break;
    case 3:
        if (dz1 > 127)
            dz1 = 127;
        else if (dz1 < -127)
                dz1 = -127;
        kbd_queue(s, dz1 & 0xff, 1);
        break;
    case 4:
        if (dz1 > 7)
            dz1 = 7;
        else if (dz1 < -7)
            dz1 = -7;
        b = (dz1 & 0x0f) | ((s->mouse_buttons & 0x18) << 1);
        kbd_queue(s, b, 1);
        break;
    }

    /* update deltas */
    s->mouse_dx -= dx1;
    s->mouse_dy -= dy1;
    s->mouse_dz -= dz1;
}

void kbd_mouse_event(int dx, int dy, int dz, int buttons_state)
{
    KBDState *s = &kbd_state;

    /* check if deltas are recorded when disabled */
    if (!(s->mouse_status & MOUSE_STATUS_ENABLED))
        return;

    s->mouse_dx += dx;
    s->mouse_dy -= dy;
    s->mouse_dz += dz;
    s->mouse_buttons = buttons_state;
    
    if (!(s->mouse_status & MOUSE_STATUS_REMOTE) &&
        (s->queues[1].count < (KBD_QUEUE_SIZE - 16))) {
        for(;;) {
            /* if not remote, send event. Multiple events are sent if
               too big deltas */
            kbd_mouse_send_packet(s);
            if (s->mouse_dx == 0 && s->mouse_dy == 0 && s->mouse_dz == 0)
                break;
        }
    }
}

static void kbd_write_mouse(KBDState *s, int val)
{
#ifdef DEBUG_MOUSE
    printf("kbd: write mouse 0x%02x\n", val);
#endif
    switch(s->mouse_write_cmd) {
    default:
    case -1:
        /* mouse command */
        if (s->mouse_wrap) {
            if (val == AUX_RESET_WRAP) {
                s->mouse_wrap = 0;
                kbd_queue(s, AUX_ACK, 1);
                return;
            } else if (val != AUX_RESET) {
                kbd_queue(s, val, 1);
                return;
            }
        }
        switch(val) {
        case AUX_SET_SCALE11:
            s->mouse_status &= ~MOUSE_STATUS_SCALE21;
            kbd_queue(s, AUX_ACK, 1);
            break;
        case AUX_SET_SCALE21:
            s->mouse_status |= MOUSE_STATUS_SCALE21;
            kbd_queue(s, AUX_ACK, 1);
            break;
        case AUX_SET_STREAM:
            s->mouse_status &= ~MOUSE_STATUS_REMOTE;
            kbd_queue(s, AUX_ACK, 1);
            break;
        case AUX_SET_WRAP:
            s->mouse_wrap = 1;
            kbd_queue(s, AUX_ACK, 1);
            break;
        case AUX_SET_REMOTE:
            s->mouse_status |= MOUSE_STATUS_REMOTE;
            kbd_queue(s, AUX_ACK, 1);
            break;
        case AUX_GET_TYPE:
            kbd_queue(s, AUX_ACK, 1);
            kbd_queue(s, s->mouse_type, 1);
            break;
        case AUX_SET_RES:
        case AUX_SET_SAMPLE:
            s->mouse_write_cmd = val;
            kbd_queue(s, AUX_ACK, 1);
            break;
        case AUX_GET_SCALE:
            kbd_queue(s, AUX_ACK, 1);
            kbd_queue(s, s->mouse_status, 1);
            kbd_queue(s, s->mouse_resolution, 1);
            kbd_queue(s, s->mouse_sample_rate, 1);
            break;
        case AUX_POLL:
            kbd_queue(s, AUX_ACK, 1);
            kbd_mouse_send_packet(s);
            break;
        case AUX_ENABLE_DEV:
            s->mouse_status |= MOUSE_STATUS_ENABLED;
            kbd_queue(s, AUX_ACK, 1);
            break;
        case AUX_DISABLE_DEV:
            s->mouse_status &= ~MOUSE_STATUS_ENABLED;
            kbd_queue(s, AUX_ACK, 1);
            break;
        case AUX_SET_DEFAULT:
            s->mouse_sample_rate = 100;
            s->mouse_resolution = 2;
            s->mouse_status = 0;
            kbd_queue(s, AUX_ACK, 1);
            break;
        case AUX_RESET:
            s->mouse_sample_rate = 100;
            s->mouse_resolution = 2;
            s->mouse_status = 0;
            kbd_queue(s, AUX_ACK, 1);
            kbd_queue(s, 0xaa, 1);
            kbd_queue(s, s->mouse_type, 1);
            break;
        default:
            break;
        }
        break;
    case AUX_SET_SAMPLE:
        s->mouse_sample_rate = val;
#if 0
        /* detect IMPS/2 or IMEX */
        switch(s->mouse_detect_state) {
        default:
        case 0:
            if (val == 200)
                s->mouse_detect_state = 1;
            break;
        case 1:
            if (val == 100)
                s->mouse_detect_state = 2;
            else if (val == 200)
                s->mouse_detect_state = 3;
            else
                s->mouse_detect_state = 0;
            break;
        case 2:
            if (val == 80) 
                s->mouse_type = 3; /* IMPS/2 */
            s->mouse_detect_state = 0;
            break;
        case 3:
            if (val == 80) 
                s->mouse_type = 4; /* IMEX */
            s->mouse_detect_state = 0;
            break;
        }
#endif
        kbd_queue(s, AUX_ACK, 1);
        s->mouse_write_cmd = -1;
        break;
    case AUX_SET_RES:
        s->mouse_resolution = val;
        kbd_queue(s, AUX_ACK, 1);
        s->mouse_write_cmd = -1;
        break;
    }
}

void kbd_write_data(CPUState *env, uint32_t addr, uint32_t val)
{
    KBDState *s = &kbd_state;

#ifdef DEBUG_KBD
    printf("kbd: write data=0x%02x\n", val);
#endif

    switch(s->write_cmd) {
    case 0:
        kbd_write_keyboard(s, val);
        break;
    case KBD_CCMD_WRITE_MODE:
        s->mode = val;
        kbd_update_irq(s);
        break;
    case KBD_CCMD_WRITE_OBUF:
        kbd_queue(s, val, 0);
        break;
    case KBD_CCMD_WRITE_AUX_OBUF:
        kbd_queue(s, val, 1);
        break;
    case KBD_CCMD_WRITE_OUTPORT:
#ifdef TARGET_I386
        cpu_x86_set_a20(env, (val >> 1) & 1);
#endif
        if (!(val & 1)) {
            reset_requested = 1;
            cpu_interrupt(global_env, CPU_INTERRUPT_EXIT);
        }
        break;
    case KBD_CCMD_WRITE_MOUSE:
        kbd_write_mouse(s, val);
        break;
    default:
        break;
    }
    s->write_cmd = 0;
}

void kbd_reset(KBDState *s)
{
    KBDQueue *q;
    int i;

    s->kbd_write_cmd = -1;
    s->mouse_write_cmd = -1;
    s->mode = KBD_MODE_KBD_INT | KBD_MODE_MOUSE_INT;
    s->status = KBD_STAT_CMD | KBD_STAT_UNLOCKED;
    for(i = 0; i < 2; i++) {
        q = &s->queues[i];
        q->rptr = 0;
        q->wptr = 0;
        q->count = 0;
    }
}

void kbd_init(void)
{
    kbd_reset(&kbd_state);
#if defined (TARGET_I386) || defined (TARGET_PPC)
    register_ioport_read(0x60, 1, kbd_read_data, 1);
    register_ioport_write(0x60, 1, kbd_write_data, 1);
    register_ioport_read(0x64, 1, kbd_read_status, 1);
    register_ioport_write(0x64, 1, kbd_write_command, 1);
#endif
}

/***********************************************************/
/* Bochs BIOS debug ports */
#ifdef TARGET_I386
void bochs_bios_write(CPUX86State *env, uint32_t addr, uint32_t val)
{
    switch(addr) {
        /* Bochs BIOS messages */
    case 0x400:
    case 0x401:
        fprintf(stderr, "BIOS panic at rombios.c, line %d\n", val);
        exit(1);
    case 0x402:
    case 0x403:
#ifdef DEBUG_BIOS
        fprintf(stderr, "%c", val);
#endif
        break;

        /* LGPL'ed VGA BIOS messages */
    case 0x501:
    case 0x502:
        fprintf(stderr, "VGA BIOS panic, line %d\n", val);
        exit(1);
    case 0x500:
    case 0x503:
#ifdef DEBUG_BIOS
        fprintf(stderr, "%c", val);
#endif
        break;
    }
}

void bochs_bios_init(void)
{
    register_ioport_write(0x400, 1, bochs_bios_write, 2);
    register_ioport_write(0x401, 1, bochs_bios_write, 2);
    register_ioport_write(0x402, 1, bochs_bios_write, 1);
    register_ioport_write(0x403, 1, bochs_bios_write, 1);

    register_ioport_write(0x501, 1, bochs_bios_write, 2);
    register_ioport_write(0x502, 1, bochs_bios_write, 2);
    register_ioport_write(0x500, 1, bochs_bios_write, 1);
    register_ioport_write(0x503, 1, bochs_bios_write, 1);
}
#endif

/***********************************************************/
/* dumb display */

/* init terminal so that we can grab keys */
static struct termios oldtty;

static void term_exit(void)
{
    tcsetattr (0, TCSANOW, &oldtty);
}

static void term_init(void)
{
    struct termios tty;

    tcgetattr (0, &tty);
    oldtty = tty;

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                          |INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);
    /* if graphical mode, we allow Ctrl-C handling */
    if (nographic)
        tty.c_lflag &= ~ISIG;
    tty.c_cflag &= ~(CSIZE|PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    
    tcsetattr (0, TCSANOW, &tty);

    atexit(term_exit);

    fcntl(0, F_SETFL, O_NONBLOCK);
}

static void dumb_update(DisplayState *ds, int x, int y, int w, int h)
{
}

static void dumb_resize(DisplayState *ds, int w, int h)
{
}

static void dumb_refresh(DisplayState *ds)
{
    vga_update_display();
}

void dumb_display_init(DisplayState *ds)
{
    ds->data = NULL;
    ds->linesize = 0;
    ds->depth = 0;
    ds->dpy_update = dumb_update;
    ds->dpy_resize = dumb_resize;
    ds->dpy_refresh = dumb_refresh;
}

#if !defined(CONFIG_SOFTMMU)
/***********************************************************/
/* cpu signal handler */
static void host_segv_handler(int host_signum, siginfo_t *info, 
                              void *puc)
{
    if (cpu_signal_handler(host_signum, info, puc))
        return;
    term_exit();
    abort();
}
#endif

static int timer_irq_pending;
static int timer_irq_count;

static int timer_ms;
static int gui_refresh_pending, gui_refresh_count;

static void host_alarm_handler(int host_signum, siginfo_t *info, 
                               void *puc)
{
    /* NOTE: since usually the OS asks a 100 Hz clock, there can be
       some drift between cpu_get_ticks() and the interrupt time. So
       we queue some interrupts to avoid missing some */
    timer_irq_count += pit_get_out_edges(&pit_channels[0]);
    if (timer_irq_count) {
        if (timer_irq_count > 2)
            timer_irq_count = 2;
        timer_irq_count--;
        timer_irq_pending = 1;
    }
    gui_refresh_count += timer_ms;
    if (gui_refresh_count >= GUI_REFRESH_INTERVAL) {
        gui_refresh_count = 0;
        gui_refresh_pending = 1;
    }

    /* XXX: seems dangerous to run that here. */
    DMA_run();
    SB16_run();

    if (gui_refresh_pending || timer_irq_pending) {
        /* just exit from the cpu to have a chance to handle timers */
        cpu_interrupt(global_env, CPU_INTERRUPT_EXIT);
    }
}

#ifdef CONFIG_SOFTMMU
void *get_mmap_addr(unsigned long size)
{
    return NULL;
}
#else
unsigned long mmap_addr = PHYS_RAM_BASE;

void *get_mmap_addr(unsigned long size)
{
    unsigned long addr;
    addr = mmap_addr;
    mmap_addr += ((size + 4095) & ~4095) + 4096;
    return (void *)addr;
}
#endif

/* main execution loop */

CPUState *cpu_gdbstub_get_env(void *opaque)
{
    return global_env;
}

int main_loop(void *opaque)
{
    struct pollfd ufds[3], *pf, *serial_ufd, *gdb_ufd;
#if defined (TARGET_I386)
    struct pollfd *net_ufd;
#endif
    int ret, n, timeout, serial_ok;
    uint8_t ch;
    CPUState *env = global_env;

    if (!term_inited) {
        /* initialize terminal only there so that the user has a
           chance to stop QEMU with Ctrl-C before the gdb connection
           is launched */
        term_inited = 1;
        term_init();
    }

    serial_ok = 1;
    cpu_enable_ticks();
    for(;;) {
#if defined (DO_TB_FLUSH)
        tb_flush();
#endif
        ret = cpu_exec(env);
        if (reset_requested) {
            ret = EXCP_INTERRUPT; 
            break;
        }
        if (ret == EXCP_DEBUG) {
            ret = EXCP_DEBUG;
            break;
        }
        /* if hlt instruction, we wait until the next IRQ */
        if (ret == EXCP_HLT) 
            timeout = 10;
        else
            timeout = 0;
        /* poll any events */
        serial_ufd = NULL;
        pf = ufds;
        if (serial_ok && !(serial_ports[0].lsr & UART_LSR_DR)) {
            serial_ufd = pf;
            pf->fd = 0;
            pf->events = POLLIN;
            pf++;
        }
#if defined (TARGET_I386)
        net_ufd = NULL;
        if (net_fd > 0 && ne2000_can_receive(&ne2000_state)) {
            net_ufd = pf;
            pf->fd = net_fd;
            pf->events = POLLIN;
            pf++;
        }
#endif
        gdb_ufd = NULL;
        if (gdbstub_fd > 0) {
            gdb_ufd = pf;
            pf->fd = gdbstub_fd;
            pf->events = POLLIN;
            pf++;
        }

        ret = poll(ufds, pf - ufds, timeout);
        if (ret > 0) {
            if (serial_ufd && (serial_ufd->revents & POLLIN)) {
                n = read(0, &ch, 1);
                if (n == 1) {
                    serial_received_byte(&serial_ports[0], ch);
                } else {
		    /* Closed, stop polling. */
                    serial_ok = 0;
                }
            }
#if defined (TARGET_I386)
            if (net_ufd && (net_ufd->revents & POLLIN)) {
                uint8_t buf[MAX_ETH_FRAME_SIZE];

                n = read(net_fd, buf, MAX_ETH_FRAME_SIZE);
                if (n > 0) {
                    if (n < 60) {
                        memset(buf + n, 0, 60 - n);
                        n = 60;
                    }
                    ne2000_receive(&ne2000_state, buf, n);
                }
            }
#endif
            if (gdb_ufd && (gdb_ufd->revents & POLLIN)) {
                uint8_t buf[1];
                /* stop emulation if requested by gdb */
                n = read(gdbstub_fd, buf, 1);
                if (n == 1) {
                    ret = EXCP_INTERRUPT; 
                    break;
                }
            }
        }

        /* timer IRQ */
        if (timer_irq_pending) {
#if defined (TARGET_I386)
            pic_set_irq(0, 1);
            pic_set_irq(0, 0);
            timer_irq_pending = 0;
            /* XXX: RTC test */
            if (cmos_data[RTC_REG_B] & 0x50) {
                pic_set_irq(8, 1);
            }
#endif
        }

        /* VGA */
        if (gui_refresh_pending) {
            display_state.dpy_refresh(&display_state);
            gui_refresh_pending = 0;
        }
    }
    cpu_disable_ticks();
    return ret;
}

void help(void)
{
    printf("QEMU PC emulator version " QEMU_VERSION ", Copyright (c) 2003 Fabrice Bellard\n"
           "usage: %s [options] [disk_image]\n"
           "\n"
           "'disk_image' is a raw hard image image for IDE hard disk 0\n"
           "\n"
           "Standard options:\n"
           "-fda/-fdb file  use 'file' as floppy disk 0/1 image\n"
           "-hda/-hdb file  use 'file' as IDE hard disk 0/1 image\n"
           "-hdc/-hdd file  use 'file' as IDE hard disk 2/3 image\n"
           "-cdrom file     use 'file' as IDE cdrom 2 image\n"
           "-boot [a|b|c|d] boot on floppy (a, b), hard disk (c) or CD-ROM (d)\n"
	   "-snapshot       write to temporary files instead of disk image files\n"
           "-m megs         set virtual RAM size to megs MB\n"
           "-n script       set network init script [default=%s]\n"
           "-tun-fd fd      this fd talks to tap/tun, use it.\n"
           "-nographic      disable graphical output\n"
           "\n"
           "Linux boot specific (does not require PC BIOS):\n"
           "-kernel bzImage use 'bzImage' as kernel image\n"
           "-append cmdline use 'cmdline' as kernel command line\n"
           "-initrd file    use 'file' as initial ram disk\n"
           "\n"
           "Debug/Expert options:\n"
           "-s              wait gdb connection to port %d\n"
           "-p port         change gdb connection port\n"
           "-d              output log to %s\n"
           "-hdachs c,h,s   force hard disk 0 geometry (usually qemu can guess it)\n"
           "-L path         set the directory for the BIOS and VGA BIOS\n"
           "\n"
           "During emulation, use C-a h to get terminal commands:\n",
#ifdef CONFIG_SOFTMMU
           "qemu",
#else
           "qemu-fast",
#endif
           DEFAULT_NETWORK_SCRIPT, 
           DEFAULT_GDBSTUB_PORT,
           "/tmp/qemu.log");
    term_print_help();
#ifndef CONFIG_SOFTMMU
    printf("\n"
           "NOTE: this version of QEMU is faster but it needs slightly patched OSes to\n"
           "work. Please use the 'qemu' executable to have a more accurate (but slower)\n"
           "PC emulation.\n");
#endif
    exit(1);
}

struct option long_options[] = {
    { "initrd", 1, NULL, 0, },
    { "hda", 1, NULL, 0, },
    { "hdb", 1, NULL, 0, },
    { "snapshot", 0, NULL, 0, },
    { "hdachs", 1, NULL, 0, },
    { "nographic", 0, NULL, 0, },
    { "kernel", 1, NULL, 0, },
    { "append", 1, NULL, 0, },
    { "tun-fd", 1, NULL, 0, },
    { "hdc", 1, NULL, 0, },
    { "hdd", 1, NULL, 0, },
    { "cdrom", 1, NULL, 0, },
    { "boot", 1, NULL, 0, },
    { "fda", 1, NULL, 0, },
    { "fdb", 1, NULL, 0, },
    { NULL, 0, NULL, 0 },
};

#ifdef CONFIG_SDL
/* SDL use the pthreads and they modify sigaction. We don't
   want that. */
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 2)
extern void __libc_sigaction();
#define sigaction(sig, act, oact) __libc_sigaction(sig, act, oact)
#else
extern void __sigaction();
#define sigaction(sig, act, oact) __sigaction(sig, act, oact)
#endif
#endif /* CONFIG_SDL */

int main(int argc, char **argv)
{
    int c, ret, initrd_size, i, use_gdbstub, gdbstub_port, long_index;
    int snapshot, linux_boot, total_ram_size;
#if defined (TARGET_I386)
    struct linux_params *params;
#endif
    struct sigaction act;
    struct itimerval itv;
    CPUState *env;
    const char *initrd_filename;
    const char *hd_filename[MAX_DISKS], *fd_filename[MAX_FD];
    const char *kernel_filename, *kernel_cmdline;
    DisplayState *ds = &display_state;

    /* we never want that malloc() uses mmap() */
    mallopt(M_MMAP_THRESHOLD, 4096 * 1024);
    initrd_filename = NULL;
    for(i = 0; i < MAX_FD; i++)
        fd_filename[i] = NULL;
    for(i = 0; i < MAX_DISKS; i++)
        hd_filename[i] = NULL;
    phys_ram_size = 32 * 1024 * 1024;
    vga_ram_size = VGA_RAM_SIZE;
#if defined (TARGET_I386)
    pstrcpy(network_script, sizeof(network_script), DEFAULT_NETWORK_SCRIPT);
#endif
    use_gdbstub = 0;
    gdbstub_port = DEFAULT_GDBSTUB_PORT;
    snapshot = 0;
    nographic = 0;
    kernel_filename = NULL;
    kernel_cmdline = "";
    for(;;) {
        c = getopt_long_only(argc, argv, "hm:dn:sp:L:", long_options, &long_index);
        if (c == -1)
            break;
        switch(c) {
        case 0:
            switch(long_index) {
            case 0:
                initrd_filename = optarg;
                break;
            case 1:
                hd_filename[0] = optarg;
                break;
            case 2:
                hd_filename[1] = optarg;
                break;
            case 3:
                snapshot = 1;
                break;
            case 4:
                {
                    int cyls, heads, secs;
                    const char *p;
                    p = optarg;
                    cyls = strtol(p, (char **)&p, 0);
                    if (*p != ',')
                        goto chs_fail;
                    p++;
                    heads = strtol(p, (char **)&p, 0);
                    if (*p != ',')
                        goto chs_fail;
                    p++;
                    secs = strtol(p, (char **)&p, 0);
                    if (*p != '\0')
                        goto chs_fail;
                    ide_set_geometry(0, cyls, heads, secs);
                chs_fail: ;
                }
                break;
            case 5:
                nographic = 1;
                break;
            case 6:
                kernel_filename = optarg;
                break;
            case 7:
                kernel_cmdline = optarg;
                break;
#if defined (TARGET_I386)
	    case 8:
		net_fd = atoi(optarg);
		break;
#endif
            case 9:
                hd_filename[2] = optarg;
                break;
            case 10:
                hd_filename[3] = optarg;
                break;
            case 11:
                hd_filename[2] = optarg;
                ide_set_cdrom(2, 1);
                break;
            case 12:
                boot_device = optarg[0];
                if (boot_device != 'a' && boot_device != 'b' &&
                    boot_device != 'c' && boot_device != 'd') {
                    fprintf(stderr, "qemu: invalid boot device '%c'\n", boot_device);
                    exit(1);
                }
                break;
            case 13:
                fd_filename[0] = optarg;
                break;
            case 14:
                fd_filename[1] = optarg;
                break;
            }
            break;
        case 'h':
            help();
            break;
        case 'm':
            phys_ram_size = atoi(optarg) * 1024 * 1024;
            if (phys_ram_size <= 0)
                help();
            if (phys_ram_size > PHYS_RAM_MAX_SIZE) {
                fprintf(stderr, "qemu: at most %d MB RAM can be simulated\n",
                        PHYS_RAM_MAX_SIZE / (1024 * 1024));
                exit(1);
            }
            break;
        case 'd':
            cpu_set_log(CPU_LOG_ALL);
            break;
#if defined (TARGET_I386)
        case 'n':
            pstrcpy(network_script, sizeof(network_script), optarg);
            break;
#endif
        case 's':
            use_gdbstub = 1;
            break;
        case 'p':
            gdbstub_port = atoi(optarg);
            break;
        case 'L':
            bios_dir = optarg;
            break;
        }
    }

    if (optind < argc) {
        hd_filename[0] = argv[optind++];
    }

    linux_boot = (kernel_filename != NULL);
        
    if (!linux_boot && hd_filename[0] == '\0' && hd_filename[2] == '\0' &&
        fd_filename[0] == '\0')
        help();
    
    /* boot to cd by default if no hard disk */
    if (hd_filename[0] == '\0' && boot_device == 'c') {
        if (fd_filename[0] != '\0')
            boot_device = 'a';
        else
            boot_device = 'd';
    }

#if !defined(CONFIG_SOFTMMU)
    /* must avoid mmap() usage of glibc by setting a buffer "by hand" */
    {
        static uint8_t stdout_buf[4096];
        setvbuf(stdout, stdout_buf, _IOLBF, sizeof(stdout_buf));
    }
#else
    setvbuf(stdout, NULL, _IOLBF, 0);
#endif

    /* init network tun interface */
#if defined (TARGET_I386)
    if (net_fd < 0)
	net_init();
#endif

    /* init the memory */
    total_ram_size = phys_ram_size + vga_ram_size;

#ifdef CONFIG_SOFTMMU
    phys_ram_base = malloc(total_ram_size);
    if (!phys_ram_base) {
        fprintf(stderr, "Could not allocate physical memory\n");
        exit(1);
    }
#else
    /* as we must map the same page at several addresses, we must use
       a fd */
    {
        const char *tmpdir;

        tmpdir = getenv("QEMU_TMPDIR");
        if (!tmpdir)
            tmpdir = "/tmp";
        snprintf(phys_ram_file, sizeof(phys_ram_file), "%s/vlXXXXXX", tmpdir);
        if (mkstemp(phys_ram_file) < 0) {
            fprintf(stderr, "Could not create temporary memory file '%s'\n", 
                    phys_ram_file);
            exit(1);
        }
        phys_ram_fd = open(phys_ram_file, O_CREAT | O_TRUNC | O_RDWR, 0600);
        if (phys_ram_fd < 0) {
            fprintf(stderr, "Could not open temporary memory file '%s'\n", 
                    phys_ram_file);
            exit(1);
        }
        ftruncate(phys_ram_fd, total_ram_size);
        unlink(phys_ram_file);
        phys_ram_base = mmap(get_mmap_addr(total_ram_size), 
                             total_ram_size, 
                             PROT_WRITE | PROT_READ, MAP_SHARED | MAP_FIXED, 
                             phys_ram_fd, 0);
        if (phys_ram_base == MAP_FAILED) {
            fprintf(stderr, "Could not map physical memory\n");
            exit(1);
        }
    }
#endif

    /* open the virtual block devices */
    for(i = 0; i < MAX_DISKS; i++) {
        if (hd_filename[i]) {
            bs_table[i] = bdrv_open(hd_filename[i], snapshot);
            if (!bs_table[i]) {
                fprintf(stderr, "qemu: could not open hard disk image '%s\n",
                        hd_filename[i]);
                exit(1);
            }
        }
    }

    /* init CPU state */
    env = cpu_init();
    global_env = env;
    cpu_single_env = env;

    init_ioports();

    /* allocate RAM */
    cpu_register_physical_memory(0, phys_ram_size, 0);

    if (linux_boot) {
        /* now we can load the kernel */
        ret = load_kernel(kernel_filename, phys_ram_base + KERNEL_LOAD_ADDR);
        if (ret < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n", 
                    kernel_filename);
            exit(1);
        }
        
        /* load initrd */
        initrd_size = 0;
        if (initrd_filename) {
            initrd_size = load_image(initrd_filename, phys_ram_base + INITRD_LOAD_ADDR);
            if (initrd_size < 0) {
                fprintf(stderr, "qemu: could not load initial ram disk '%s'\n", 
                        initrd_filename);
                exit(1);
            }
        }
        
        /* init kernel params */
#ifdef TARGET_I386
        params = (void *)(phys_ram_base + KERNEL_PARAMS_ADDR);
        memset(params, 0, sizeof(struct linux_params));
        params->mount_root_rdonly = 0;
        stw_raw(&params->cl_magic, 0xA33F);
        stw_raw(&params->cl_offset, params->commandline - (uint8_t *)params);
        stl_raw(&params->alt_mem_k, (phys_ram_size / 1024) - 1024);
        pstrcat(params->commandline, sizeof(params->commandline), kernel_cmdline);
        params->loader_type = 0x01;
        if (initrd_size > 0) {
            stl_raw(&params->initrd_start, INITRD_LOAD_ADDR);
            stl_raw(&params->initrd_size, initrd_size);
        }
        params->orig_video_lines = 25;
        params->orig_video_cols = 80;

        /* setup basic memory access */
        env->cr[0] = 0x00000033;
        env->hflags |= HF_PE_MASK;
        cpu_x86_init_mmu(env);
        
        memset(params->idt_table, 0, sizeof(params->idt_table));
        
        stq_raw(&params->gdt_table[2], 0x00cf9a000000ffffLL); /* KERNEL_CS */
        stq_raw(&params->gdt_table[3], 0x00cf92000000ffffLL); /* KERNEL_DS */
        /* for newer kernels (2.6.0) CS/DS are at different addresses */
        stq_raw(&params->gdt_table[12], 0x00cf9a000000ffffLL); /* KERNEL_CS */
        stq_raw(&params->gdt_table[13], 0x00cf92000000ffffLL); /* KERNEL_DS */
        
        env->idt.base = (void *)((uint8_t *)params->idt_table - phys_ram_base);
        env->idt.limit = sizeof(params->idt_table) - 1;
        env->gdt.base = (void *)((uint8_t *)params->gdt_table - phys_ram_base);
        env->gdt.limit = sizeof(params->gdt_table) - 1;
        
        cpu_x86_load_seg_cache(env, R_CS, KERNEL_CS, NULL, 0xffffffff, 0x00cf9a00);
        cpu_x86_load_seg_cache(env, R_DS, KERNEL_DS, NULL, 0xffffffff, 0x00cf9200);
        cpu_x86_load_seg_cache(env, R_ES, KERNEL_DS, NULL, 0xffffffff, 0x00cf9200);
        cpu_x86_load_seg_cache(env, R_SS, KERNEL_DS, NULL, 0xffffffff, 0x00cf9200);
        cpu_x86_load_seg_cache(env, R_FS, KERNEL_DS, NULL, 0xffffffff, 0x00cf9200);
        cpu_x86_load_seg_cache(env, R_GS, KERNEL_DS, NULL, 0xffffffff, 0x00cf9200);
        
        env->eip = KERNEL_LOAD_ADDR;
        env->regs[R_ESI] = KERNEL_PARAMS_ADDR;
        env->eflags = 0x2;
#elif defined (TARGET_PPC)
        cpu_x86_init_mmu(env);
        PPC_init_hw(env, phys_ram_size, KERNEL_LOAD_ADDR, ret,
                    KERNEL_STACK_ADDR, boot_device);
#endif
    } else {
        char buf[1024];

        /* RAW PC boot */
#if defined(TARGET_I386)
        /* BIOS load */
        snprintf(buf, sizeof(buf), "%s/%s", bios_dir, BIOS_FILENAME);
        ret = load_image(buf, phys_ram_base + 0x000f0000);
        if (ret != 0x10000) {
            fprintf(stderr, "qemu: could not load PC bios '%s'\n", buf);
            exit(1);
        }

        /* VGA BIOS load */
        snprintf(buf, sizeof(buf), "%s/%s", bios_dir, VGABIOS_FILENAME);
        ret = load_image(buf, phys_ram_base + 0x000c0000);

        /* setup basic memory access */
        env->cr[0] = 0x60000010;
        cpu_x86_init_mmu(env);
        
        cpu_register_physical_memory(0xc0000, 0x10000, 0xc0000 | IO_MEM_ROM);
        cpu_register_physical_memory(0xf0000, 0x10000, 0xf0000 | IO_MEM_ROM);

        env->idt.limit = 0xffff;
        env->gdt.limit = 0xffff;
        env->ldt.limit = 0xffff;
        env->ldt.flags = DESC_P_MASK;
        env->tr.limit = 0xffff;
        env->tr.flags = DESC_P_MASK;

        /* not correct (CS base=0xffff0000) */
        cpu_x86_load_seg_cache(env, R_CS, 0xf000, (uint8_t *)0x000f0000, 0xffff, 0); 
        cpu_x86_load_seg_cache(env, R_DS, 0, NULL, 0xffff, 0);
        cpu_x86_load_seg_cache(env, R_ES, 0, NULL, 0xffff, 0);
        cpu_x86_load_seg_cache(env, R_SS, 0, NULL, 0xffff, 0);
        cpu_x86_load_seg_cache(env, R_FS, 0, NULL, 0xffff, 0);
        cpu_x86_load_seg_cache(env, R_GS, 0, NULL, 0xffff, 0);

        env->eip = 0xfff0;
        env->regs[R_EDX] = 0x600; /* indicate P6 processor */

        env->eflags = 0x2;

        bochs_bios_init();
#elif defined(TARGET_PPC)
        cpu_x86_init_mmu(env);
        /* allocate ROM */
        //        snprintf(buf, sizeof(buf), "%s/%s", bios_dir, BIOS_FILENAME);
        snprintf(buf, sizeof(buf), "%s", BIOS_FILENAME);
        printf("load BIOS at %p\n", phys_ram_base + 0x000f0000);
        ret = load_image(buf, phys_ram_base + 0x000f0000);
        if (ret != 0x10000) {
            fprintf(stderr, "qemu: could not load PPC bios '%s' (%d)\n%m\n",
                    buf, ret);
            exit(1);
        }
#endif
    }

    /* terminal init */
    if (nographic) {
        dumb_display_init(ds);
    } else {
#ifdef CONFIG_SDL
        sdl_display_init(ds);
#else
        dumb_display_init(ds);
#endif
    }
    /* init basic PC hardware */
    register_ioport_write(0x80, 1, ioport80_write, 1);

    vga_initialize(ds, phys_ram_base + phys_ram_size, phys_ram_size, 
             vga_ram_size);
#if defined (TARGET_I386)
    cmos_init();
#endif
    pic_init();
    pit_init();
    serial_init();
#if defined (TARGET_I386)
    ne2000_init();
#endif
    ide_init();
    kbd_init();
    AUD_init();
    DMA_init();
#if defined (TARGET_I386)
    SB16_init();
#endif
#if defined (TARGET_PPC)
    PPC_end_init();
#endif
    fdctrl_register((unsigned char **)fd_filename, snapshot, boot_device);
    /* setup cpu signal handlers for MMU / self modifying code handling */
    sigfillset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
#if !defined(CONFIG_SOFTMMU)
    act.sa_sigaction = host_segv_handler;
    sigaction(SIGSEGV, &act, NULL);
    sigaction(SIGBUS, &act, NULL);
#endif

    act.sa_sigaction = host_alarm_handler;
    sigaction(SIGALRM, &act, NULL);

    itv.it_interval.tv_sec = 0;
    itv.it_interval.tv_usec = 1000;
    itv.it_value.tv_sec = 0;
    itv.it_value.tv_usec = 10 * 1000;
    setitimer(ITIMER_REAL, &itv, NULL);
    /* we probe the tick duration of the kernel to inform the user if
       the emulated kernel requested a too high timer frequency */
    getitimer(ITIMER_REAL, &itv);
    timer_ms = itv.it_interval.tv_usec / 1000;
    pit_min_timer_count = ((uint64_t)itv.it_interval.tv_usec * PIT_FREQ) / 
        1000000;

    if (use_gdbstub) {
        cpu_gdbstub(NULL, main_loop, gdbstub_port);
    } else {
        main_loop(NULL);
    }
    return 0;
}
