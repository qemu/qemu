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

/* debug IDE devices */
//#define DEBUG_IDE

/* debug PIC */
//#define DEBUG_PIC

/* debug NE2000 card */
//#define DEBUG_NE2000

/* debug PC keyboard */
//#define DEBUG_KBD

/* debug PC keyboard : only mouse */
//#define DEBUG_MOUSE

#define PHYS_RAM_BASE     0xac000000
#define PHYS_RAM_MAX_SIZE (256 * 1024 * 1024)

#define KERNEL_LOAD_ADDR   0x00100000
#define INITRD_LOAD_ADDR   0x00400000
#define KERNEL_PARAMS_ADDR 0x00090000

#define GUI_REFRESH_INTERVAL 30 

#define MAX_DISKS 2

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

#define MAX_IOPORTS 4096

static const char *bios_dir = CONFIG_QEMU_SHAREDIR;
char phys_ram_file[1024];
CPUX86State *global_env;
CPUX86State *cpu_single_env;
IOPortReadFunc *ioport_read_table[3][MAX_IOPORTS];
IOPortWriteFunc *ioport_write_table[3][MAX_IOPORTS];
BlockDriverState *bs_table[MAX_DISKS];
int vga_ram_size;
static DisplayState display_state;
int nographic;
int term_inited;
int64_t ticks_per_sec;

/***********************************************************/
/* x86 io ports */

uint32_t default_ioport_readb(CPUX86State *env, uint32_t address)
{
#ifdef DEBUG_UNUSED_IOPORT
    fprintf(stderr, "inb: port=0x%04x\n", address);
#endif
    return 0xff;
}

void default_ioport_writeb(CPUX86State *env, uint32_t address, uint32_t data)
{
#ifdef DEBUG_UNUSED_IOPORT
    fprintf(stderr, "outb: port=0x%04x data=0x%02x\n", address, data);
#endif
}

/* default is to make two byte accesses */
uint32_t default_ioport_readw(CPUX86State *env, uint32_t address)
{
    uint32_t data;
    data = ioport_read_table[0][address & (MAX_IOPORTS - 1)](env, address);
    data |= ioport_read_table[0][(address + 1) & (MAX_IOPORTS - 1)](env, address + 1) << 8;
    return data;
}

void default_ioport_writew(CPUX86State *env, uint32_t address, uint32_t data)
{
    ioport_write_table[0][address & (MAX_IOPORTS - 1)](env, address, data & 0xff);
    ioport_write_table[0][(address + 1) & (MAX_IOPORTS - 1)](env, address + 1, (data >> 8) & 0xff);
}

uint32_t default_ioport_readl(CPUX86State *env, uint32_t address)
{
#ifdef DEBUG_UNUSED_IOPORT
    fprintf(stderr, "inl: port=0x%04x\n", address);
#endif
    return 0xffffffff;
}

void default_ioport_writel(CPUX86State *env, uint32_t address, uint32_t data)
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
    int fd, size, setup_sects;
    uint8_t bootsect[512];

    fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -1;
    if (read(fd, bootsect, 512) != 512)
        goto fail;
    setup_sects = bootsect[0x1F1];
    if (!setup_sects)
        setup_sects = 4;
    /* skip 16 bit setup code */
    lseek(fd, (setup_sects + 1) * 512, SEEK_SET);
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

void cpu_x86_outb(CPUX86State *env, int addr, int val)
{
    ioport_write_table[0][addr & (MAX_IOPORTS - 1)](env, addr, val);
}

void cpu_x86_outw(CPUX86State *env, int addr, int val)
{
    ioport_write_table[1][addr & (MAX_IOPORTS - 1)](env, addr, val);
}

void cpu_x86_outl(CPUX86State *env, int addr, int val)
{
    ioport_write_table[2][addr & (MAX_IOPORTS - 1)](env, addr, val);
}

int cpu_x86_inb(CPUX86State *env, int addr)
{
    return ioport_read_table[0][addr & (MAX_IOPORTS - 1)](env, addr);
}

int cpu_x86_inw(CPUX86State *env, int addr)
{
    return ioport_read_table[1][addr & (MAX_IOPORTS - 1)](env, addr);
}

int cpu_x86_inl(CPUX86State *env, int addr)
{
    return ioport_read_table[2][addr & (MAX_IOPORTS - 1)](env, addr);
}

/***********************************************************/
void ioport80_write(CPUX86State *env, uint32_t addr, uint32_t data)
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
#endif
    va_end(ap);
    abort();
}

/***********************************************************/
/* cmos emulation */

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

uint8_t cmos_data[128];
uint8_t cmos_index;

void cmos_ioport_write(CPUX86State *env, uint32_t addr, uint32_t data)
{
    if (addr == 0x70) {
        cmos_index = data & 0x7f;
    }
}

uint32_t cmos_ioport_read(CPUX86State *env, uint32_t addr)
{
    int ret;

    if (addr == 0x70) {
        return 0xff;
    } else {
        /* toggle update-in-progress bit for Linux (same hack as
           plex86) */
        ret = cmos_data[cmos_index];
        if (cmos_index == RTC_REG_A)
            cmos_data[RTC_REG_A] ^= 0x80; 
        else if (cmos_index == RTC_REG_C)
            cmos_data[RTC_REG_C] = 0x00; 
        return ret;
    }
}


static inline int to_bcd(int a)
{
    return ((a / 10) << 4) | (a % 10);
}

void cmos_init(void)
{
    struct tm *tm;
    time_t ti;
    int val;

    ti = time(NULL);
    tm = gmtime(&ti);
    cmos_data[RTC_SECONDS] = to_bcd(tm->tm_sec);
    cmos_data[RTC_MINUTES] = to_bcd(tm->tm_min);
    cmos_data[RTC_HOURS] = to_bcd(tm->tm_hour);
    cmos_data[RTC_DAY_OF_WEEK] = to_bcd(tm->tm_wday);
    cmos_data[RTC_DAY_OF_MONTH] = to_bcd(tm->tm_mday);
    cmos_data[RTC_MONTH] = to_bcd(tm->tm_mon + 1);
    cmos_data[RTC_YEAR] = to_bcd(tm->tm_year % 100);

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
    
    cmos_data[0x3d] = 0x02; /* hard drive boot */
    
    register_ioport_write(0x70, 2, cmos_ioport_write, 1);
    register_ioport_read(0x70, 2, cmos_ioport_read, 1);
}

/***********************************************************/
/* 8259 pic emulation */

typedef struct PicState {
    uint8_t last_irr; /* edge detection */
    uint8_t irr; /* interrupt request register */
    uint8_t imr; /* interrupt mask register */
    uint8_t isr; /* interrupt service register */
    uint8_t priority_add; /* used to compute irq priority */
    uint8_t irq_base;
    uint8_t read_reg_select;
    uint8_t special_mask;
    uint8_t init_state;
    uint8_t auto_eoi;
    uint8_t rotate_on_autoeoi;
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

static inline int get_priority(PicState *s, int mask)
{
    int priority;
    if (mask == 0)
        return -1;
    priority = 7;
    while ((mask & (1 << ((priority + s->priority_add) & 7))) == 0)
        priority--;
    return priority;
}

/* return the pic wanted interrupt. return -1 if none */
static int pic_get_irq(PicState *s)
{
    int mask, cur_priority, priority;

    mask = s->irr & ~s->imr;
    priority = get_priority(s, mask);
    if (priority < 0)
        return -1;
    /* compute current priority */
    cur_priority = get_priority(s, s->isr);
    if (priority > cur_priority) {
        /* higher priority found: an irq should be generated */
        return priority;
    } else {
        return -1;
    }
}

/* raise irq to CPU if necessary. must be called every time the active
   irq may change */
static void pic_update_irq(void)
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
        cpu_x86_interrupt(global_env, CPU_INTERRUPT_HARD);
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

int cpu_x86_get_pic_interrupt(CPUX86State *env)
{
    int irq, irq2, intno;

    /* signal the pic that the irq was acked by the CPU */
    irq = pic_irq_requested;
#ifdef DEBUG_IRQ_LATENCY
    printf("IRQ%d latency=%0.3fus\n", 
           irq, 
           (double)(cpu_get_ticks() - irq_time[irq]) * 1000000.0 / ticks_per_sec);
#endif
#ifdef DEBUG_PIC
    printf("pic_interrupt: irq=%d\n", irq);
#endif

    if (irq >= 8) {
        irq2 = irq & 7;
        pics[1].isr |= (1 << irq2);
        pics[1].irr &= ~(1 << irq2);
        irq = 2;
        intno = pics[1].irq_base + irq2;
    } else {
        intno = pics[0].irq_base + irq;
    }
    pics[0].isr |= (1 << irq);
    pics[0].irr &= ~(1 << irq);
    return intno;
}

void pic_ioport_write(CPUX86State *env, uint32_t addr, uint32_t val)
{
    PicState *s;
    int priority;

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
            if (val & 0x02)
                s->read_reg_select = val & 1;
            if (val & 0x40)
                s->special_mask = (val >> 5) & 1;
        } else {
            switch(val) {
            case 0x00:
            case 0x80:
                s->rotate_on_autoeoi = val >> 7;
                break;
            case 0x20: /* end of interrupt */
            case 0xa0:
                priority = get_priority(s, s->isr);
                if (priority >= 0) {
                    s->isr &= ~(1 << ((priority + s->priority_add) & 7));
                }
                if (val == 0xa0)
                    s->priority_add = (s->priority_add + 1) & 7;
                pic_update_irq();
                break;
            case 0x60 ... 0x67:
                priority = val & 7;
                s->isr &= ~(1 << priority);
                pic_update_irq();
                break;
            case 0xc0 ... 0xc7:
                s->priority_add = (val + 1) & 7;
                pic_update_irq();
                break;
            case 0xe0 ... 0xe7:
                priority = val & 7;
                s->isr &= ~(1 << priority);
                s->priority_add = (priority + 1) & 7;
                pic_update_irq();
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
            s->auto_eoi = (val >> 1) & 1;
            s->init_state = 0;
            break;
        }
    }
}

uint32_t pic_ioport_read(CPUX86State *env, uint32_t addr1)
{
    PicState *s;
    unsigned int addr;
    int ret;

    addr = addr1;
    s = &pics[addr >> 7];
    addr &= 1;
    if (addr == 0) {
        if (s->read_reg_select)
            ret = s->isr;
        else
            ret = s->irr;
    } else {
        ret = s->imr;
    }
#ifdef DEBUG_PIC
    printf("pic_read: addr=0x%02x val=0x%02x\n", addr1, ret);
#endif
    return ret;
}

void pic_init(void)
{
    register_ioport_write(0x20, 2, pic_ioport_write, 1);
    register_ioport_read(0x20, 2, pic_ioport_read, 1);
    register_ioport_write(0xa0, 2, pic_ioport_write, 1);
    register_ioport_read(0xa0, 2, pic_ioport_read, 1);
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
        out = (d % s->count) < (s->count >> 1);
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
        v = s->count - (s->count >> 1);
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

static inline void pit_load_count(PITChannelState *s, int val)
{
    if (val == 0)
        val = 0x10000;
    s->count_load_time = cpu_get_ticks();
    s->count_last_edge_check_time = s->count_load_time;
    s->count = val;
    if (s == &pit_channels[0] && val <= pit_min_timer_count) {
        fprintf(stderr, 
                "\nWARNING: vl: on your system, accurate timer emulation is impossible if its frequency is more than %d Hz. If using a 2.5.xx Linux kernel, you must patch asm/param.h to change HZ from 1000 to 100.\n\n", 
                PIT_FREQ / pit_min_timer_count);
    }
}

void pit_ioport_write(CPUX86State *env, uint32_t addr, uint32_t val)
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

uint32_t pit_ioport_read(CPUX86State *env, uint32_t addr)
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

void speaker_ioport_write(CPUX86State *env, uint32_t addr, uint32_t val)
{
    speaker_data_on = (val >> 1) & 1;
    pit_channels[2].gate = val & 1;
}

uint32_t speaker_ioport_read(CPUX86State *env, uint32_t addr)
{
    int out;
    out = pit_get_out(&pit_channels[2]);
    dummy_refresh_clock ^= 1;
    return (speaker_data_on << 1) | pit_channels[2].gate | (out << 5) |
      (dummy_refresh_clock << 4);
}

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

    register_ioport_read(0x61, 1, speaker_ioport_read, 1);
    register_ioport_write(0x61, 1, speaker_ioport_write, 1);
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
} SerialState;

SerialState serial_ports[1];

void serial_update_irq(void)
{
    SerialState *s = &serial_ports[0];

    if ((s->lsr & UART_LSR_DR) && (s->ier & UART_IER_RDI)) {
        s->iir = UART_IIR_RDI;
    } else if ((s->lsr & UART_LSR_THRE) && (s->ier & UART_IER_THRI)) {
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

void serial_ioport_write(CPUX86State *env, uint32_t addr, uint32_t val)
{
    SerialState *s = &serial_ports[0];
    unsigned char ch;
    int ret;
    
    addr &= 7;
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = (s->divider & 0xff00) | val;
        } else {
            s->lsr &= ~UART_LSR_THRE;
            serial_update_irq();

            ch = val;
            do {
                ret = write(1, &ch, 1);
            } while (ret != 1);
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

uint32_t serial_ioport_read(CPUX86State *env, uint32_t addr)
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
        ret = s->msr;
        break;
    case 7:
        ret = s->scr;
        break;
    }
    return ret;
}

#define TERM_ESCAPE 0x01 /* ctrl-a is used for escape */
static int term_got_escape;

void term_print_help(void)
{
    printf("\n"
           "C-a h    print this help\n"
           "C-a x    exit emulatior\n"
	   "C-a s    save disk data back to file (if -snapshot)\n"
           "C-a b    send break (magic sysrq)\n"
           "C-a C-a  send C-a\n"
           );
}

/* called when a char is received */
void serial_received_byte(SerialState *s, int ch)
{
    if (term_got_escape) {
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

    register_ioport_write(0x3f8, 8, serial_ioport_write, 1);
    register_ioport_read(0x3f8, 8, serial_ioport_read, 1);
}

/***********************************************************/
/* ne2000 emulation */

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

void ne2000_ioport_write(CPUX86State *env, uint32_t addr, uint32_t val)
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

uint32_t ne2000_ioport_read(CPUX86State *env, uint32_t addr)
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

void ne2000_asic_ioport_write(CPUX86State *env, uint32_t addr, uint32_t val)
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

uint32_t ne2000_asic_ioport_read(CPUX86State *env, uint32_t addr)
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

void ne2000_reset_ioport_write(CPUX86State *env, uint32_t addr, uint32_t val)
{
    /* nothing to do (end of reset pulse) */
}

uint32_t ne2000_reset_ioport_read(CPUX86State *env, uint32_t addr)
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

/***********************************************************/
/* ide emulation */

/* Bits of HD_STATUS */
#define ERR_STAT		0x01
#define INDEX_STAT		0x02
#define ECC_STAT		0x04	/* Corrected error */
#define DRQ_STAT		0x08
#define SEEK_STAT		0x10
#define SRV_STAT		0x10
#define WRERR_STAT		0x20
#define READY_STAT		0x40
#define BUSY_STAT		0x80

/* Bits for HD_ERROR */
#define MARK_ERR		0x01	/* Bad address mark */
#define TRK0_ERR		0x02	/* couldn't find track 0 */
#define ABRT_ERR		0x04	/* Command aborted */
#define MCR_ERR			0x08	/* media change request */
#define ID_ERR			0x10	/* ID field not found */
#define MC_ERR			0x20	/* media changed */
#define ECC_ERR			0x40	/* Uncorrectable ECC error */
#define BBD_ERR			0x80	/* pre-EIDE meaning:  block marked bad */
#define ICRC_ERR		0x80	/* new meaning:  CRC error during transfer */

/* Bits of HD_NSECTOR */
#define CD			0x01
#define IO			0x02
#define REL			0x04
#define TAG_MASK		0xf8

#define IDE_CMD_RESET           0x04
#define IDE_CMD_DISABLE_IRQ     0x02

/* ATA/ATAPI Commands pre T13 Spec */
#define WIN_NOP				0x00
/*
 *	0x01->0x02 Reserved
 */
#define CFA_REQ_EXT_ERROR_CODE		0x03 /* CFA Request Extended Error Code */
/*
 *	0x04->0x07 Reserved
 */
#define WIN_SRST			0x08 /* ATAPI soft reset command */
#define WIN_DEVICE_RESET		0x08
/*
 *	0x09->0x0F Reserved
 */
#define WIN_RECAL			0x10
#define WIN_RESTORE			WIN_RECAL
/*
 *	0x10->0x1F Reserved
 */
#define WIN_READ			0x20 /* 28-Bit */
#define WIN_READ_ONCE			0x21 /* 28-Bit without retries */
#define WIN_READ_LONG			0x22 /* 28-Bit */
#define WIN_READ_LONG_ONCE		0x23 /* 28-Bit without retries */
#define WIN_READ_EXT			0x24 /* 48-Bit */
#define WIN_READDMA_EXT			0x25 /* 48-Bit */
#define WIN_READDMA_QUEUED_EXT		0x26 /* 48-Bit */
#define WIN_READ_NATIVE_MAX_EXT		0x27 /* 48-Bit */
/*
 *	0x28
 */
#define WIN_MULTREAD_EXT		0x29 /* 48-Bit */
/*
 *	0x2A->0x2F Reserved
 */
#define WIN_WRITE			0x30 /* 28-Bit */
#define WIN_WRITE_ONCE			0x31 /* 28-Bit without retries */
#define WIN_WRITE_LONG			0x32 /* 28-Bit */
#define WIN_WRITE_LONG_ONCE		0x33 /* 28-Bit without retries */
#define WIN_WRITE_EXT			0x34 /* 48-Bit */
#define WIN_WRITEDMA_EXT		0x35 /* 48-Bit */
#define WIN_WRITEDMA_QUEUED_EXT		0x36 /* 48-Bit */
#define WIN_SET_MAX_EXT			0x37 /* 48-Bit */
#define CFA_WRITE_SECT_WO_ERASE		0x38 /* CFA Write Sectors without erase */
#define WIN_MULTWRITE_EXT		0x39 /* 48-Bit */
/*
 *	0x3A->0x3B Reserved
 */
#define WIN_WRITE_VERIFY		0x3C /* 28-Bit */
/*
 *	0x3D->0x3F Reserved
 */
#define WIN_VERIFY			0x40 /* 28-Bit - Read Verify Sectors */
#define WIN_VERIFY_ONCE			0x41 /* 28-Bit - without retries */
#define WIN_VERIFY_EXT			0x42 /* 48-Bit */
/*
 *	0x43->0x4F Reserved
 */
#define WIN_FORMAT			0x50
/*
 *	0x51->0x5F Reserved
 */
#define WIN_INIT			0x60
/*
 *	0x61->0x5F Reserved
 */
#define WIN_SEEK			0x70 /* 0x70-0x7F Reserved */
#define CFA_TRANSLATE_SECTOR		0x87 /* CFA Translate Sector */
#define WIN_DIAGNOSE			0x90
#define WIN_SPECIFY			0x91 /* set drive geometry translation */
#define WIN_DOWNLOAD_MICROCODE		0x92
#define WIN_STANDBYNOW2			0x94
#define WIN_STANDBY2			0x96
#define WIN_SETIDLE2			0x97
#define WIN_CHECKPOWERMODE2		0x98
#define WIN_SLEEPNOW2			0x99
/*
 *	0x9A VENDOR
 */
#define WIN_PACKETCMD			0xA0 /* Send a packet command. */
#define WIN_PIDENTIFY			0xA1 /* identify ATAPI device	*/
#define WIN_QUEUED_SERVICE		0xA2
#define WIN_SMART			0xB0 /* self-monitoring and reporting */
#define CFA_ERASE_SECTORS       	0xC0
#define WIN_MULTREAD			0xC4 /* read sectors using multiple mode*/
#define WIN_MULTWRITE			0xC5 /* write sectors using multiple mode */
#define WIN_SETMULT			0xC6 /* enable/disable multiple mode */
#define WIN_READDMA_QUEUED		0xC7 /* read sectors using Queued DMA transfers */
#define WIN_READDMA			0xC8 /* read sectors using DMA transfers */
#define WIN_READDMA_ONCE		0xC9 /* 28-Bit - without retries */
#define WIN_WRITEDMA			0xCA /* write sectors using DMA transfers */
#define WIN_WRITEDMA_ONCE		0xCB /* 28-Bit - without retries */
#define WIN_WRITEDMA_QUEUED		0xCC /* write sectors using Queued DMA transfers */
#define CFA_WRITE_MULTI_WO_ERASE	0xCD /* CFA Write multiple without erase */
#define WIN_GETMEDIASTATUS		0xDA	
#define WIN_ACKMEDIACHANGE		0xDB /* ATA-1, ATA-2 vendor */
#define WIN_POSTBOOT			0xDC
#define WIN_PREBOOT			0xDD
#define WIN_DOORLOCK			0xDE /* lock door on removable drives */
#define WIN_DOORUNLOCK			0xDF /* unlock door on removable drives */
#define WIN_STANDBYNOW1			0xE0
#define WIN_IDLEIMMEDIATE		0xE1 /* force drive to become "ready" */
#define WIN_STANDBY             	0xE2 /* Set device in Standby Mode */
#define WIN_SETIDLE1			0xE3
#define WIN_READ_BUFFER			0xE4 /* force read only 1 sector */
#define WIN_CHECKPOWERMODE1		0xE5
#define WIN_SLEEPNOW1			0xE6
#define WIN_FLUSH_CACHE			0xE7
#define WIN_WRITE_BUFFER		0xE8 /* force write only 1 sector */
#define WIN_WRITE_SAME			0xE9 /* read ata-2 to use */
	/* SET_FEATURES 0x22 or 0xDD */
#define WIN_FLUSH_CACHE_EXT		0xEA /* 48-Bit */
#define WIN_IDENTIFY			0xEC /* ask drive to identify itself	*/
#define WIN_MEDIAEJECT			0xED
#define WIN_IDENTIFY_DMA		0xEE /* same as WIN_IDENTIFY, but DMA */
#define WIN_SETFEATURES			0xEF /* set special drive features */
#define EXABYTE_ENABLE_NEST		0xF0
#define WIN_SECURITY_SET_PASS		0xF1
#define WIN_SECURITY_UNLOCK		0xF2
#define WIN_SECURITY_ERASE_PREPARE	0xF3
#define WIN_SECURITY_ERASE_UNIT		0xF4
#define WIN_SECURITY_FREEZE_LOCK	0xF5
#define WIN_SECURITY_DISABLE		0xF6
#define WIN_READ_NATIVE_MAX		0xF8 /* return the native maximum address */
#define WIN_SET_MAX			0xF9
#define DISABLE_SEAGATE			0xFB

/* set to 1 set disable mult support */
#define MAX_MULT_SECTORS 8

struct IDEState;

typedef void EndTransferFunc(struct IDEState *);

typedef struct IDEState {
    /* ide config */
    int cylinders, heads, sectors;
    int64_t nb_sectors;
    int mult_sectors;
    int irq;
    /* ide regs */
    uint8_t feature;
    uint8_t error;
    uint16_t nsector; /* 0 is 256 to ease computations */
    uint8_t sector;
    uint8_t lcyl;
    uint8_t hcyl;
    uint8_t select;
    uint8_t status;
    /* 0x3f6 command, only meaningful for drive 0 */
    uint8_t cmd;
    /* depends on bit 4 in select, only meaningful for drive 0 */
    struct IDEState *cur_drive; 
    BlockDriverState *bs;
    int req_nb_sectors; /* number of sectors per interrupt */
    EndTransferFunc *end_transfer_func;
    uint8_t *data_ptr;
    uint8_t *data_end;
    uint8_t io_buffer[MAX_MULT_SECTORS*512 + 4];
} IDEState;

IDEState ide_state[MAX_DISKS];

static void padstr(char *str, const char *src, int len)
{
    int i, v;
    for(i = 0; i < len; i++) {
        if (*src)
            v = *src++;
        else
            v = ' ';
        *(char *)((long)str ^ 1) = v;
        str++;
    }
}

static void ide_identify(IDEState *s)
{
    uint16_t *p;
    unsigned int oldsize;

    memset(s->io_buffer, 0, 512);
    p = (uint16_t *)s->io_buffer;
    stw(p + 0, 0x0040);
    stw(p + 1, s->cylinders); 
    stw(p + 3, s->heads);
    stw(p + 4, 512 * s->sectors); /* sectors */
    stw(p + 5, 512); /* sector size */
    stw(p + 6, s->sectors); 
    stw(p + 20, 3); /* buffer type */
    stw(p + 21, 512); /* cache size in sectors */
    stw(p + 22, 4); /* ecc bytes */
    padstr((uint8_t *)(p + 27), "QEMU HARDDISK", 40);
#if MAX_MULT_SECTORS > 1    
    stw(p + 47, MAX_MULT_SECTORS);
#endif
    stw(p + 48, 1); /* dword I/O */
    stw(p + 49, 1 << 9); /* LBA supported, no DMA */
    stw(p + 51, 0x200); /* PIO transfer cycle */
    stw(p + 52, 0x200); /* DMA transfer cycle */
    stw(p + 54, s->cylinders);
    stw(p + 55, s->heads);
    stw(p + 56, s->sectors);
    oldsize = s->cylinders * s->heads * s->sectors;
    stw(p + 57, oldsize);
    stw(p + 58, oldsize >> 16);
    if (s->mult_sectors)
        stw(p + 59, 0x100 | s->mult_sectors);
    stw(p + 60, s->nb_sectors);
    stw(p + 61, s->nb_sectors >> 16);
    stw(p + 80, (1 << 1) | (1 << 2));
    stw(p + 82, (1 << 14));
    stw(p + 83, (1 << 14));
    stw(p + 84, (1 << 14));
    stw(p + 85, (1 << 14));
    stw(p + 86, 0);
    stw(p + 87, (1 << 14));
}

static inline void ide_abort_command(IDEState *s)
{
    s->status = READY_STAT | ERR_STAT;
    s->error = ABRT_ERR;
}

static inline void ide_set_irq(IDEState *s)
{
    if (!(ide_state[0].cmd & IDE_CMD_DISABLE_IRQ)) {
        pic_set_irq(s->irq, 1);
    }
}

/* prepare data transfer and tell what to do after */
static void ide_transfer_start(IDEState *s, int size, 
                               EndTransferFunc *end_transfer_func)
{
    s->end_transfer_func = end_transfer_func;
    s->data_ptr = s->io_buffer;
    s->data_end = s->io_buffer + size;
    s->status |= DRQ_STAT;
}

static void ide_transfer_stop(IDEState *s)
{
    s->end_transfer_func = ide_transfer_stop;
    s->data_ptr = s->io_buffer;
    s->data_end = s->io_buffer;
    s->status &= ~DRQ_STAT;
}

static int64_t ide_get_sector(IDEState *s)
{
    int64_t sector_num;
    if (s->select & 0x40) {
        /* lba */
        sector_num = ((s->select & 0x0f) << 24) | (s->hcyl << 16) | 
            (s->lcyl << 8) | s->sector;
    } else {
        sector_num = ((s->hcyl << 8) | s->lcyl) * s->heads * s->sectors +
            (s->select & 0x0f) * s->sectors + 
            (s->sector - 1);
    }
    return sector_num;
}

static void ide_set_sector(IDEState *s, int64_t sector_num)
{
    unsigned int cyl, r;
    if (s->select & 0x40) {
        s->select = (s->select & 0xf0) | (sector_num >> 24);
        s->hcyl = (sector_num >> 16);
        s->lcyl = (sector_num >> 8);
        s->sector = (sector_num);
    } else {
        cyl = sector_num / (s->heads * s->sectors);
        r = sector_num % (s->heads * s->sectors);
        s->hcyl = cyl >> 8;
        s->lcyl = cyl;
        s->select = (s->select & 0xf0) | (r / s->sectors);
        s->sector = (r % s->sectors) + 1;
    }
}

static void ide_sector_read(IDEState *s)
{
    int64_t sector_num;
    int ret, n;

    s->status = READY_STAT | SEEK_STAT;
    sector_num = ide_get_sector(s);
    n = s->nsector;
    if (n == 0) {
        /* no more sector to read from disk */
        ide_transfer_stop(s);
    } else {
#if defined(DEBUG_IDE)
        printf("read sector=%Ld\n", sector_num);
#endif
        if (n > s->req_nb_sectors)
            n = s->req_nb_sectors;
        ret = bdrv_read(s->bs, sector_num, s->io_buffer, n);
        ide_transfer_start(s, 512 * n, ide_sector_read);
        ide_set_irq(s);
        ide_set_sector(s, sector_num + n);
        s->nsector -= n;
    }
}

static void ide_sector_write(IDEState *s)
{
    int64_t sector_num;
    int ret, n, n1;

    s->status = READY_STAT | SEEK_STAT;
    sector_num = ide_get_sector(s);
#if defined(DEBUG_IDE)
    printf("write sector=%Ld\n", sector_num);
#endif
    n = s->nsector;
    if (n > s->req_nb_sectors)
        n = s->req_nb_sectors;
    ret = bdrv_write(s->bs, sector_num, s->io_buffer, n);
    s->nsector -= n;
    if (s->nsector == 0) {
        /* no more sector to write */
        ide_transfer_stop(s);
    } else {
        n1 = s->nsector;
        if (n1 > s->req_nb_sectors)
            n1 = s->req_nb_sectors;
        ide_transfer_start(s, 512 * n1, ide_sector_write);
    }
    ide_set_sector(s, sector_num + n);
    ide_set_irq(s);
}

void ide_ioport_write(CPUX86State *env, uint32_t addr, uint32_t val)
{
    IDEState *s = ide_state[0].cur_drive;
    int unit, n;

    addr &= 7;
#ifdef DEBUG_IDE
    printf("IDE: write addr=0x%x val=0x%02x\n", addr, val);
#endif
    switch(addr) {
    case 0:
        break;
    case 1:
        s->feature = val;
        break;
    case 2:
        if (val == 0)
            val = 256;
        s->nsector = val;
        break;
    case 3:
        s->sector = val;
        break;
    case 4:
        s->lcyl = val;
        break;
    case 5:
        s->hcyl = val;
        break;
    case 6:
        /* select drive */
        unit = (val >> 4) & 1;
        s = &ide_state[unit];
        ide_state[0].cur_drive = s;
        s->select = val;
        break;
    default:
    case 7:
        /* command */
#if defined(DEBUG_IDE)
        printf("ide: CMD=%02x\n", val);
#endif
        switch(val) {
        case WIN_PIDENTIFY:
        case WIN_IDENTIFY:
            if (s->bs) {
                ide_identify(s);
                s->status = READY_STAT;
                ide_transfer_start(s, 512, ide_transfer_stop);
            } else {
                ide_abort_command(s);
            }
            ide_set_irq(s);
            break;
        case WIN_SPECIFY:
        case WIN_RECAL:
            s->status = READY_STAT;
            ide_set_irq(s);
            break;
        case WIN_SETMULT:
            if (s->nsector > MAX_MULT_SECTORS || 
                s->nsector == 0 ||
                (s->nsector & (s->nsector - 1)) != 0) {
                ide_abort_command(s);
            } else {
                s->mult_sectors = s->nsector;
                s->status = READY_STAT;
            }
            ide_set_irq(s);
            break;
        case WIN_READ:
        case WIN_READ_ONCE:
            s->req_nb_sectors = 1;
            ide_sector_read(s);
            break;
        case WIN_WRITE:
        case WIN_WRITE_ONCE:
            s->status = SEEK_STAT;
            s->req_nb_sectors = 1;
            ide_transfer_start(s, 512, ide_sector_write);
            break;
        case WIN_MULTREAD:
            if (!s->mult_sectors)
                goto abort_cmd;
            s->req_nb_sectors = s->mult_sectors;
            ide_sector_read(s);
            break;
        case WIN_MULTWRITE:
            if (!s->mult_sectors)
                goto abort_cmd;
            s->status = SEEK_STAT;
            s->req_nb_sectors = s->mult_sectors;
            n = s->nsector;
            if (n > s->req_nb_sectors)
                n = s->req_nb_sectors;
            ide_transfer_start(s, 512 * n, ide_sector_write);
            break;
        case WIN_READ_NATIVE_MAX:
            ide_set_sector(s, s->nb_sectors - 1);
            s->status = READY_STAT;
            ide_set_irq(s);
            break;
        default:
        abort_cmd:
            ide_abort_command(s);
            ide_set_irq(s);
            break;
        }
    }
}

uint32_t ide_ioport_read(CPUX86State *env, uint32_t addr)
{
    IDEState *s = ide_state[0].cur_drive;
    int ret;

    addr &= 7;
    switch(addr) {
    case 0:
        ret = 0xff;
        break;
    case 1:
        ret = s->error;
        break;
    case 2:
        ret = s->nsector & 0xff;
        break;
    case 3:
        ret = s->sector;
        break;
    case 4:
        ret = s->lcyl;
        break;
    case 5:
        ret = s->hcyl;
        break;
    case 6:
        ret = s->select;
        break;
    default:
    case 7:
        ret = s->status;
        pic_set_irq(s->irq, 0);
        break;
    }
#ifdef DEBUG_IDE
    printf("ide: read addr=0x%x val=%02x\n", addr, ret);
#endif
    return ret;
}

uint32_t ide_status_read(CPUX86State *env, uint32_t addr)
{
    IDEState *s = ide_state[0].cur_drive;
    int ret;
    ret = s->status;
#ifdef DEBUG_IDE
    printf("ide: read status val=%02x\n", ret);
#endif
    return ret;
}

void ide_cmd_write(CPUX86State *env, uint32_t addr, uint32_t val)
{
    IDEState *s;
    int i;

#ifdef DEBUG_IDE
    printf("ide: write control val=%02x\n", val);
#endif
    /* common for both drives */
    if (!(ide_state[0].cmd & IDE_CMD_RESET) &&
        (val & IDE_CMD_RESET)) {
        /* reset low to high */
        for(i = 0;i < 2; i++) {
            s = &ide_state[i];
            s->status = BUSY_STAT | SEEK_STAT;
            s->error = 0x01;
        }
    } else if ((ide_state[0].cmd & IDE_CMD_RESET) &&
               !(val & IDE_CMD_RESET)) {
        /* high to low */
        for(i = 0;i < 2; i++) {
            s = &ide_state[i];
            s->status = READY_STAT;
            /* set hard disk drive ID */
            s->select &= 0xf0; /* clear head */
            s->nsector = 1;
            s->sector = 1;
            if (s->nb_sectors == 0) {
                /* no disk present */
                s->lcyl = 0x12;
                s->hcyl = 0x34;
            } else {
                s->lcyl = 0;
                s->hcyl = 0;
            }
        }
    }

    ide_state[0].cmd = val;
}

void ide_data_writew(CPUX86State *env, uint32_t addr, uint32_t val)
{
    IDEState *s = ide_state[0].cur_drive;
    uint8_t *p;

    p = s->data_ptr;
    *(uint16_t *)p = tswap16(val);
    p += 2;
    s->data_ptr = p;
    if (p >= s->data_end)
        s->end_transfer_func(s);
}

uint32_t ide_data_readw(CPUX86State *env, uint32_t addr)
{
    IDEState *s = ide_state[0].cur_drive;
    uint8_t *p;
    int ret;
    
    p = s->data_ptr;
    ret = tswap16(*(uint16_t *)p);
    p += 2;
    s->data_ptr = p;
    if (p >= s->data_end)
        s->end_transfer_func(s);
    return ret;
}

void ide_data_writel(CPUX86State *env, uint32_t addr, uint32_t val)
{
    IDEState *s = ide_state[0].cur_drive;
    uint8_t *p;

    p = s->data_ptr;
    *(uint32_t *)p = tswap32(val);
    p += 4;
    s->data_ptr = p;
    if (p >= s->data_end)
        s->end_transfer_func(s);
}

uint32_t ide_data_readl(CPUX86State *env, uint32_t addr)
{
    IDEState *s = ide_state[0].cur_drive;
    uint8_t *p;
    int ret;
    
    p = s->data_ptr;
    ret = tswap32(*(uint32_t *)p);
    p += 4;
    s->data_ptr = p;
    if (p >= s->data_end)
        s->end_transfer_func(s);
    return ret;
}

void ide_reset(IDEState *s)
{
    s->mult_sectors = MAX_MULT_SECTORS;
    s->status = READY_STAT;
    s->cur_drive = s;
    s->select = 0xa0;
}

struct partition {
	uint8_t boot_ind;		/* 0x80 - active */
	uint8_t head;		/* starting head */
	uint8_t sector;		/* starting sector */
	uint8_t cyl;		/* starting cylinder */
	uint8_t sys_ind;		/* What partition type */
	uint8_t end_head;		/* end head */
	uint8_t end_sector;	/* end sector */
	uint8_t end_cyl;		/* end cylinder */
	uint32_t start_sect;	/* starting sector counting from 0 */
	uint32_t nr_sects;		/* nr of sectors in partition */
} __attribute__((packed));

/* try to guess the IDE geometry from the MSDOS partition table */
void ide_guess_geometry(IDEState *s)
{
    uint8_t buf[512];
    int ret, i;
    struct partition *p;
    uint32_t nr_sects;

    if (s->cylinders != 0)
        return;
    ret = bdrv_read(s->bs, 0, buf, 1);
    if (ret < 0)
        return;
    /* test msdos magic */
    if (buf[510] != 0x55 || buf[511] != 0xaa)
        return;
    for(i = 0; i < 4; i++) {
        p = ((struct partition *)(buf + 0x1be)) + i;
        nr_sects = tswap32(p->nr_sects);
        if (nr_sects && p->end_head) {
            /* We make the assumption that the partition terminates on
               a cylinder boundary */
            s->heads = p->end_head + 1;
            s->sectors = p->end_sector & 63;
            s->cylinders = s->nb_sectors / (s->heads * s->sectors);
#if 0
            printf("guessed partition: CHS=%d %d %d\n", 
                   s->cylinders, s->heads, s->sectors);
#endif
        }
    }
}

void ide_init(void)
{
    IDEState *s;
    int i, cylinders;
    int64_t nb_sectors;

    for(i = 0; i < MAX_DISKS; i++) {
        s = &ide_state[i];
        s->bs = bs_table[i];
        if (s->bs) {
            bdrv_get_geometry(s->bs, &nb_sectors);
            s->nb_sectors = nb_sectors;
            ide_guess_geometry(s);
            if (s->cylinders == 0) {
                /* if no geometry, use a LBA compatible one */
                cylinders = nb_sectors / (16 * 63);
                if (cylinders > 16383)
                    cylinders = 16383;
                else if (cylinders < 2)
                    cylinders = 2;
                s->cylinders = cylinders;
                s->heads = 16;
                s->sectors = 63;
            }
        }
        s->irq = 14;
        ide_reset(s);
    }
    register_ioport_write(0x1f0, 8, ide_ioport_write, 1);
    register_ioport_read(0x1f0, 8, ide_ioport_read, 1);
    register_ioport_read(0x3f6, 1, ide_status_read, 1);
    register_ioport_write(0x3f6, 1, ide_cmd_write, 1);

    /* data ports */
    register_ioport_write(0x1f0, 2, ide_data_writew, 2);
    register_ioport_read(0x1f0, 2, ide_data_readw, 2);
    register_ioport_write(0x1f0, 4, ide_data_writel, 4);
    register_ioport_read(0x1f0, 4, ide_data_readl, 4);
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
#define KBD_CCMD_ENABLE_A20     0xDD
#define KBD_CCMD_DISABLE_A20    0xDF
#define KBD_CCMD_RESET	        0xFE

/* Keyboard Commands */
#define KBD_CMD_SET_LEDS	0xED	/* Set keyboard leds */
#define KBD_CMD_ECHO     	0xEE
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
int a20_enabled;

/* update irq and KBD_STAT_[MOUSE_]OBF */
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
            if (s->mode & KBD_MODE_KBD_INT)
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

uint32_t kbd_read_status(CPUX86State *env, uint32_t addr)
{
    KBDState *s = &kbd_state;
    int val;
    val = s->status;
#if defined(DEBUG_KBD) && 0
    printf("kbd: read status=0x%02x\n", val);
#endif
    return val;
}

void kbd_write_command(CPUX86State *env, uint32_t addr, uint32_t val)
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
        break;
    case KBD_CCMD_KBD_ENABLE:
        s->mode &= ~KBD_MODE_DISABLE_KBD;
        break;
    case KBD_CCMD_READ_INPORT:
        kbd_queue(s, 0x00, 0);
        break;
    case KBD_CCMD_READ_OUTPORT:
        /* XXX: check that */
        val = 0x01 | (a20_enabled << 1);
        if (s->status & KBD_STAT_OBF)
            val |= 0x10;
        if (s->status & KBD_STAT_MOUSE_OBF)
            val |= 0x20;
        kbd_queue(s, val, 0);
        break;
    case KBD_CCMD_ENABLE_A20:
        a20_enabled = 1;
        break;
    case KBD_CCMD_DISABLE_A20:
        a20_enabled = 0;
        break;
    case KBD_CCMD_RESET:
        reset_requested = 1;
        cpu_x86_interrupt(global_env, CPU_INTERRUPT_EXIT);
        break;
    default:
        fprintf(stderr, "vl: unsupported keyboard cmd=0x%02x\n", val);
        break;
    }
}

uint32_t kbd_read_data(CPUX86State *env, uint32_t addr)
{
    KBDState *s = &kbd_state;
    KBDQueue *q;
    int val;
    
    q = &s->queues[0]; /* first check KBD data */
    if (q->count == 0)
        q = &s->queues[1]; /* then check AUX data */
    if (q->count == 0) {
        /* XXX: return something else ? */
        val = 0;
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

void kbd_write_data(CPUX86State *env, uint32_t addr, uint32_t val)
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
        a20_enabled = (val >> 1) & 1;
        if (!(val & 1)) {
            reset_requested = 1;
            cpu_x86_interrupt(global_env, CPU_INTERRUPT_EXIT);
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
    register_ioport_read(0x60, 1, kbd_read_data, 1);
    register_ioport_write(0x60, 1, kbd_write_data, 1);
    register_ioport_read(0x64, 1, kbd_read_status, 1);
    register_ioport_write(0x64, 1, kbd_write_command, 1);
}

/***********************************************************/
/* Bochs BIOS debug ports */

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

    if (gui_refresh_pending || timer_irq_pending) {
        /* just exit from the cpu to have a chance to handle timers */
        cpu_x86_interrupt(global_env, CPU_INTERRUPT_EXIT);
    }
}

unsigned long mmap_addr = PHYS_RAM_BASE;

void *get_mmap_addr(unsigned long size)
{
    unsigned long addr;
    addr = mmap_addr;
    mmap_addr += ((size + 4095) & ~4095) + 4096;
    return (void *)addr;
}

/* main execution loop */

CPUState *cpu_gdbstub_get_env(void *opaque)
{
    return global_env;
}

int main_loop(void *opaque)
{
    struct pollfd ufds[3], *pf, *serial_ufd, *net_ufd, *gdb_ufd;
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
        ret = cpu_x86_exec(env);
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
        net_ufd = NULL;
        if (net_fd > 0 && ne2000_can_receive(&ne2000_state)) {
            net_ufd = pf;
            pf->fd = net_fd;
            pf->events = POLLIN;
            pf++;
        }
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
            pic_set_irq(0, 1);
            pic_set_irq(0, 0);
            timer_irq_pending = 0;
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
           "usage: qemu [options] [disk_image]\n"
           "\n"
           "'disk_image' is a raw hard image image for IDE hard disk 0\n"
           "\n"
           "Standard options:\n"
           "-hda file       use 'file' as IDE hard disk 0 image\n"
           "-hdb file       use 'file' as IDE hard disk 1 image\n"
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
           "-d              output log in /tmp/vl.log\n"
           "-hdachs c,h,s   force hard disk 0 geometry (usually qemu can guess it)\n"
           "-L path         set the directory for the BIOS and VGA BIOS\n"
           "\n"
           "During emulation, use C-a h to get terminal commands:\n",
           DEFAULT_NETWORK_SCRIPT, DEFAULT_GDBSTUB_PORT);
    term_print_help();
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
    { NULL, 0, NULL, 0 },
};

#ifdef CONFIG_SDL
/* SDL use the pthreads and they modify sigaction. We don't
   want that. */
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 3)
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
    struct linux_params *params;
    struct sigaction act;
    struct itimerval itv;
    CPUX86State *env;
    const char *tmpdir, *initrd_filename;
    const char *hd_filename[MAX_DISKS];
    const char *kernel_filename, *kernel_cmdline;
    DisplayState *ds = &display_state;

    /* we never want that malloc() uses mmap() */
    mallopt(M_MMAP_THRESHOLD, 4096 * 1024);
    initrd_filename = NULL;
    for(i = 0; i < MAX_DISKS; i++)
        hd_filename[i] = NULL;
    phys_ram_size = 32 * 1024 * 1024;
    vga_ram_size = VGA_RAM_SIZE;
    pstrcpy(network_script, sizeof(network_script), DEFAULT_NETWORK_SCRIPT);
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
                    ide_state[0].cylinders = cyls;
                    ide_state[0].heads = heads;
                    ide_state[0].sectors = secs;
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
	    case 8:
		net_fd = atoi(optarg);
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
                fprintf(stderr, "vl: at most %d MB RAM can be simulated\n",
                        PHYS_RAM_MAX_SIZE / (1024 * 1024));
                exit(1);
            }
            break;
        case 'd':
            cpu_set_log(CPU_LOG_ALL);
            break;
        case 'n':
            pstrcpy(network_script, sizeof(network_script), optarg);
            break;
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
        
    if (!linux_boot && hd_filename[0] == '\0')
        help();

    /* init debug */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* init network tun interface */
    if (net_fd < 0)
	net_init();

    /* init the memory */
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
    total_ram_size = phys_ram_size + vga_ram_size;
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

    /* open the virtual block devices */
    for(i = 0; i < MAX_DISKS; i++) {
        if (hd_filename[i]) {
            bs_table[i] = bdrv_open(hd_filename[i], snapshot);
            if (!bs_table[i]) {
                fprintf(stderr, "vl: could not open hard disk image '%s\n",
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
            fprintf(stderr, "vl: could not load kernel '%s'\n", 
                    kernel_filename);
            exit(1);
        }
        
        /* load initrd */
        initrd_size = 0;
        if (initrd_filename) {
            initrd_size = load_image(initrd_filename, phys_ram_base + INITRD_LOAD_ADDR);
            if (initrd_size < 0) {
                fprintf(stderr, "vl: could not load initial ram disk '%s'\n", 
                        initrd_filename);
                exit(1);
            }
        }
        
        /* init kernel params */
        params = (void *)(phys_ram_base + KERNEL_PARAMS_ADDR);
        memset(params, 0, sizeof(struct linux_params));
        params->mount_root_rdonly = 0;
        params->cl_magic = 0xA33F;
        params->cl_offset = params->commandline - (uint8_t *)params;
        params->alt_mem_k = (phys_ram_size / 1024) - 1024;
        pstrcat(params->commandline, sizeof(params->commandline), kernel_cmdline);
        params->loader_type = 0x01;
        if (initrd_size > 0) {
            params->initrd_start = INITRD_LOAD_ADDR;
            params->initrd_size = initrd_size;
        }
        params->orig_video_lines = 25;
        params->orig_video_cols = 80;

        /* setup basic memory access */
        env->cr[0] = 0x00000033;
        cpu_x86_init_mmu(env);
        
        memset(params->idt_table, 0, sizeof(params->idt_table));
        
        params->gdt_table[2] = 0x00cf9a000000ffffLL; /* KERNEL_CS */
        params->gdt_table[3] = 0x00cf92000000ffffLL; /* KERNEL_DS */
        
        env->idt.base = (void *)params->idt_table;
        env->idt.limit = sizeof(params->idt_table) - 1;
        env->gdt.base = (void *)params->gdt_table;
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

    } else {
        char buf[1024];

        /* RAW PC boot */

        /* BIOS load */
        snprintf(buf, sizeof(buf), "%s/%s", bios_dir, BIOS_FILENAME);
        ret = load_image(buf, phys_ram_base + 0x000f0000);
        if (ret != 0x10000) {
            fprintf(stderr, "vl: could not load PC bios '%s'\n", buf);
            exit(1);
        }

        /* VGA BIOS load */
        snprintf(buf, sizeof(buf), "%s/%s", bios_dir, VGABIOS_FILENAME);
        ret = load_image(buf, phys_ram_base + 0x000c0000);

        /* setup basic memory access */
        env->cr[0] = 0x60000010;
        cpu_x86_init_mmu(env);
        
        env->idt.limit = 0xffff;
        env->gdt.limit = 0xffff;
        env->ldt.limit = 0xffff;

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

    vga_init(ds, phys_ram_base + phys_ram_size, phys_ram_size, 
             vga_ram_size);
    cmos_init();
    pic_init();
    pit_init();
    serial_init();
    ne2000_init();
    ide_init();
    kbd_init();
    
    /* setup cpu signal handlers for MMU / self modifying code handling */
    sigfillset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = host_segv_handler;
    sigaction(SIGSEGV, &act, NULL);
    sigaction(SIGBUS, &act, NULL);

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
