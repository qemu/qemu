/*
 * Hardware simulation for PPC target.
 * For now, this is only a 'minimal' collection of hacks needed to boot Linux.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>
#include <time.h>

#include "cpu.h"
#include "vl.h"

//#define HARD_DEBUG_PPC_IO
#define DEBUG_PPC_IO

extern int loglevel;
extern FILE *logfile;

#if defined (HARD_DEBUG_PPC_IO) && !defined (DEBUG_PPC_IO)
#define DEBUG_PPC_IO
#endif

#if defined (HARD_DEBUG_PPC_IO)
#define PPC_IO_DPRINTF(fmt, args...)                     \
do {                                                     \
    if (loglevel > 0) {                                  \
        fprintf(logfile, "%s: " fmt, __func__ , ##args); \
    } else {                                             \
        printf("%s : " fmt, __func__ , ##args);          \
    }                                                    \
} while (0)
#elif defined (DEBUG_PPC_IO)
#define PPC_IO_DPRINTF(fmt, args...)                     \
do {                                                     \
    if (loglevel > 0) {                                  \
        fprintf(logfile, "%s: " fmt, __func__ , ##args); \
    }                                                    \
} while (0)
#else
#define PPC_IO_DPRINTF(fmt, args...) do { } while (0)
#endif

#if defined (USE_OPEN_FIRMWARE)
#include "of.h"
#else
#define NVRAM_SIZE 0x2000
#endif

/* IO ports emulation */
#define PPC_IO_BASE 0x80000000

static void PPC_io_writeb (uint32_t addr, uint32_t value)
{
    /* Don't polute serial port output */
    if ((addr < 0x800003F0 || addr > 0x80000400) &&
        (addr < 0x80000074 || addr > 0x80000077) &&
        (addr < 0x80000020 || addr > 0x80000021) &&
        (addr < 0x800000a0 || addr > 0x800000a1) &&
        (addr < 0x800001f0 || addr > 0x800001f7) &&
        (addr < 0x80000170 || addr > 0x80000177)) {
        PPC_IO_DPRINTF("0x%08x => 0x%02x\n", addr - PPC_IO_BASE, value);
    }
    cpu_outb(NULL, addr - PPC_IO_BASE, value);
}

static uint32_t PPC_io_readb (uint32_t addr)
{
    uint32_t ret = cpu_inb(NULL, addr - PPC_IO_BASE);

    if ((addr < 0x800003F0 || addr > 0x80000400) &&
        (addr < 0x80000074 || addr > 0x80000077) &&
        (addr < 0x80000020 || addr > 0x80000021) &&
        (addr < 0x800000a0 || addr > 0x800000a1) &&
        (addr < 0x800001f0 || addr > 0x800001f7) &&
        (addr < 0x80000170 || addr > 0x80000177) &&
        (addr < 0x8000060 || addr > 0x8000064)) {
//        PPC_IO_DPRINTF("0x%08x <= 0x%02x\n", addr - PPC_IO_BASE, ret);
    }

    return ret;
}

static void PPC_io_writew (uint32_t addr, uint32_t value)
{
    if ((addr < 0x800001f0 || addr > 0x800001f7) &&
        (addr < 0x80000170 || addr > 0x80000177)) {
        PPC_IO_DPRINTF("0x%08x => 0x%04x\n", addr - PPC_IO_BASE, value);
    }
    cpu_outw(NULL, addr - PPC_IO_BASE, value);
}

static uint32_t PPC_io_readw (uint32_t addr)
{
    uint32_t ret = cpu_inw(NULL, addr - PPC_IO_BASE);

    if ((addr < 0x800001f0 || addr > 0x800001f7) &&
        (addr < 0x80000170 || addr > 0x80000177)) {
        PPC_IO_DPRINTF("0x%08x <= 0x%04x\n", addr - PPC_IO_BASE, ret);
    }

    return ret;
}

static void PPC_io_writel (uint32_t addr, uint32_t value)
{
    PPC_IO_DPRINTF("0x%08x => 0x%08x\n", addr - PPC_IO_BASE, value);
    cpu_outl(NULL, addr - PPC_IO_BASE, value);
}

static uint32_t PPC_io_readl (uint32_t addr)
{
    uint32_t ret = cpu_inl(NULL, addr - PPC_IO_BASE);

    PPC_IO_DPRINTF("0x%08x <= 0x%08x\n", addr - PPC_IO_BASE, ret);

    return ret;
}

static CPUWriteMemoryFunc *PPC_io_write[] = {
    &PPC_io_writeb,
    &PPC_io_writew,
    &PPC_io_writel,
};

static CPUReadMemoryFunc *PPC_io_read[] = {
    &PPC_io_readb,
    &PPC_io_readw,
    &PPC_io_readl,
};

uint32_t pic_intack_read(CPUState *env);

/* Read-only register (?) */
static void _PPC_ioB_write (uint32_t addr, uint32_t value)
{
    PPC_IO_DPRINTF("0x%08x => 0x%08x\n", addr, value);
}

static uint32_t _PPC_ioB_read (uint32_t addr)
{
    uint32_t retval = 0;

    if (addr == 0xBFFFFFF0)
        retval = pic_intack_read(NULL);
    PPC_IO_DPRINTF("0x%08x <= 0x%08x\n", addr, retval);

    return retval;
}

static CPUWriteMemoryFunc *PPC_ioB_write[] = {
    &_PPC_ioB_write,
    &_PPC_ioB_write,
    &_PPC_ioB_write,
};

static CPUReadMemoryFunc *PPC_ioB_read[] = {
    &_PPC_ioB_read,
    &_PPC_ioB_read,
    &_PPC_ioB_read,
};

#if 0
static CPUWriteMemoryFunc *PPC_io3_write[] = {
    &PPC_io3_writeb,
    &PPC_io3_writew,
    &PPC_io3_writel,
};

static CPUReadMemoryFunc *PPC_io3_read[] = {
    &PPC_io3_readb,
    &PPC_io3_readw,
    &PPC_io3_readl,
};
#endif

/* Fake super-io ports for PREP platform (Intel 82378ZB) */
static uint8_t PREP_fake_io[2];
static uint8_t NVRAM_lock;

static void PREP_io_write (CPUState *env, uint32_t addr, uint32_t val)
{
    PREP_fake_io[addr - 0x0398] = val;
}

static uint32_t PREP_io_read (CPUState *env, uint32_t addr)
{
    return PREP_fake_io[addr - 0x0398];
}

static uint8_t syscontrol;

static void PREP_io_800_writeb (CPUState *env, uint32_t addr, uint32_t val)
{
    switch (addr) {
    case 0x0092:
        /* Special port 92 */
        /* Check soft reset asked */
        if (val & 0x80) {
            printf("Soft reset asked... Stop emulation\n");
            abort();
        }
        /* Check LE mode */
        if (val & 0x40) {
            printf("Little Endian mode isn't supported (yet ?)\n");
            abort();
        }
        break;
    case 0x0808:
        /* Hardfile light register: don't care */
        break;
    case 0x0810:
        /* Password protect 1 register */
        NVRAM_lock ^= 0x01;
        break;
    case 0x0812:
        /* Password protect 2 register */
        NVRAM_lock ^= 0x02;
        break;
    case 0x0814:
        /* L2 invalidate register: don't care */
        break;
    case 0x081C:
        /* system control register */
        syscontrol = val;
        break;
    case 0x0850:
        /* I/O map type register */
        if (val & 0x80) {
            printf("No support for non-continuous I/O map mode\n");
            abort();
        }
        break;
    default:
        break;
    }
}

static uint32_t PREP_io_800_readb (CPUState *env, uint32_t addr)
{
    uint32_t retval = 0xFF;

    switch (addr) {
    case 0x0092:
        /* Special port 92 */
        retval = 0x40;
        break;
    case 0x080C:
        /* Equipment present register:
         *  no L2 cache
         *  no upgrade processor
         *  no cards in PCI slots
         *  SCSI fuse is bad
         */
        retval = 0xFC;
        break;
    case 0x0818:
        /* Keylock */
        retval = 0x00;
        break;
    case 0x081C:
        /* system control register
         * 7 - 6 / 1 - 0: L2 cache enable
         */
        retval = syscontrol;
        break;
    case 0x0823:
        /* */
        retval = 0x03; /* no L2 cache */
        break;
    case 0x0850:
        /* I/O map type register */
        retval = 0x00;
        break;
    default:
        break;
    }

    return retval;
}

/* M48T59 NVRAM/RTC emulation */
static uint8_t NVRAM[NVRAM_SIZE];

/* RTC */
static time_t time_offset;

time_t get_time (void)
{
    return time(NULL) + time_offset;
}

void set_time_offset (time_t new_time)
{
    time_t now = time(NULL);

    time_offset = new_time - now;
}

static void NVRAM_init (void)
{
    /* NVRAM header */
    /* 0x00: NVRAM size in kB */
    NVRAM[0x00] = (NVRAM_SIZE >> 12) & 0xFF;
    NVRAM[0x01] = (NVRAM_SIZE >> 10) & 0xFF;
    /* 0x02: NVRAM version */
    NVRAM[0x02] = 0x01;
    /* 0x03: NVRAM revision */
    NVRAM[0x03] = 0x00;
    /* 0x04: checksum 0 => OS area   */
    /* 0x06: checksum of config area */
    /* 0x08: last OS */
    NVRAM[0x08] = 0x00; /* Unknown */
    /* 0x09: endian */
    NVRAM[0x09] = 'B';
    /* 0x0B: PM mode */
    NVRAM[0x0B] = 0x00;
    /* Restart block description record */
    /* 0x0C: restart block version */
    NVRAM[0x0C] = 0x00;
    NVRAM[0x0D] = 0x01;
    /* 0x0E: restart block revision */
    NVRAM[0x0E] = 0x00;
    NVRAM[0x0F] = 0x00;
    /* 0x1C: checksum of restart block */
    /* 0x20: restart address */
    NVRAM[0x20] = 0x00;
    NVRAM[0x21] = 0x00;
    NVRAM[0x22] = 0x00;
    NVRAM[0x23] = 0x00;
    /* 0x24: save area address */
    NVRAM[0x24] = 0x00;
    NVRAM[0x25] = 0x00;
    NVRAM[0x26] = 0x00;
    NVRAM[0x27] = 0x00;
    /* 0x28: save area length */
    NVRAM[0x28] = 0x00;
    NVRAM[0x29] = 0x00;
    NVRAM[0x2A] = 0x00;
    NVRAM[0x2B] = 0x00;
    /* Security section */
    /* Set all to zero */
    /* 0xC4: pointer to global environment area */
    NVRAM[0xC4] = 0x00;
    NVRAM[0xC5] = 0x00;
    NVRAM[0xC6] = 0x01;
    NVRAM[0xC7] = 0x00;
    /* 0xC8: size of global environment area */
    NVRAM[0xC8] = 0x00;
    NVRAM[0xC9] = 0x00;
    NVRAM[0xCA] = 0x07;
    NVRAM[0xCB] = 0x00;
    /* 0xD4: pointer to configuration area */
    NVRAM[0xD4] = 0x00;
    NVRAM[0xD5] = 0x00;
    NVRAM[0xD6] = 0x08;
    NVRAM[0xD7] = 0x00;
    /* 0xD8: size of configuration area */
    NVRAM[0xD8] = 0x00;
    NVRAM[0xD9] = 0x00;
    NVRAM[0xDA] = 0x08;
    NVRAM[0xDB] = 0x00;
    /* 0xE8: pointer to OS specific area */
    NVRAM[0xE8] = 0x00;
    NVRAM[0xE9] = 0x00;
    NVRAM[0xEA] = 0x10;
    NVRAM[0xEB] = 0x00;
    /* 0xD8: size of OS specific area */
    NVRAM[0xEC] = 0x00;
    NVRAM[0xED] = 0x00;
    NVRAM[0xEE] = 0x0F;
    NVRAM[0xEF] = 0xF0;
    /* CRC */
    /* RTC init */
    NVRAM[0x1FFC] = 0x50;
}

static uint16_t NVRAM_addr;

/* Direct access to NVRAM */
void NVRAM_write (CPUState *env, uint32_t addr, uint32_t val)
{
    switch (addr) {
    case 0x1FF0:
        /* flags register */
        break;
    case 0x1FF1:
        /* unused */
        break;
    case 0x1FF2:
        /* alarm seconds */
        break;
    case 0x1FF3:
        /* alarm minutes */
        break;
    case 0x1FF4:
        /* alarm hours */
        break;
    case 0x1FF5:
        /* alarm date */
        break;
    case 0x1FF6:
        /* interrupts */
        break;
    case 0x1FF7:
        /* watchdog */
        break;
    case 0x1FF8:
        /* control */
        break;
    case 0x1FF9:
        /* seconds (BCD) */
        break;
    case 0x1FFA:
        /* minutes (BCD) */
        break;
    case 0x1FFB:
        /* hours (BCD) */
        break;
    case 0x1FFC:
        /* day of the week / century */
        NVRAM[0x1FFC] = val & 0x50;
        break;
    case 0x1FFD:
        /* date */
        break;
    case 0x1FFE:
        /* month */
        break;
    case 0x1FFF:
        /* year */
        break;
    default:
        if (addr < NVRAM_SIZE)
            NVRAM[addr] = val & 0xFF;
        break;
    }
}

uint32_t NVRAM_read (CPUState *env, uint32_t addr)
{
    struct tm tm;
    time_t t;
    uint32_t retval = 0xFF;

    switch (addr) {
    case 0x1FF0:
        /* flags register */
        break;
    case 0x1FF1:
        /* unused */
        break;
    case 0x1FF2:
        /* alarm seconds */
        break;
    case 0x1FF3:
        /* alarm minutes */
        break;
    case 0x1FF4:
        /* alarm hours */
        break;
    case 0x1FF5:
        /* alarm date */
        break;
    case 0x1FF6:
        /* interrupts */
        break;
    case 0x1FF7:
        /* watchdog */
        break;
    case 0x1FF8:
        /* control */
        break;
    case 0x1FF9:
        /* seconds (BCD) */
        t = get_time();
        localtime_r(&t, &tm);
        retval = ((tm.tm_sec / 10) << 4) | (tm.tm_sec % 10);
//            printf("return seconds=%d\n", tm.tm_sec);
        break;
    case 0x1FFA:
        /* minutes (BCD) */
        t = get_time();
        localtime_r(&t, &tm);
        retval = ((tm.tm_min / 10) << 4) | (tm.tm_min % 10);
        break;
    case 0x1FFB:
        /* hours (BCD) */
        t = get_time();
        localtime_r(&t, &tm);
        retval = ((tm.tm_hour / 10) << 4) | (tm.tm_hour % 10);
        break;
    case 0x1FFC:
        /* day of the week / century */
        t = get_time();
        localtime_r(&t, &tm);
        retval = (NVRAM[0x1FFC] & 0x50) | tm.tm_wday;
        break;
    case 0x1FFD:
        /* date */
        t = get_time();
        localtime_r(&t, &tm);
        retval = ((tm.tm_mday / 10) << 4) | (tm.tm_mday % 10);
        break;
    case 0x1FFE:
        /* month */
        t = get_time();
        localtime_r(&t, &tm);
        retval = ((tm.tm_mon / 10) << 4) | (tm.tm_mon % 10);
        break;
    case 0x1FFF:
        /* year */
        t = get_time();
        localtime_r(&t, &tm);
        retval = ((tm.tm_year / 10) << 4) | (tm.tm_year % 10);
        break;
    default:
        if (NVRAM_addr < NVRAM_SIZE)
            retval = NVRAM[NVRAM_addr];
        break;
    }

    return retval;
}

/* IO access to NVRAM */
static void NVRAM_writeb (CPUState *env, uint32_t addr, uint32_t val)
{
    switch (addr) {
    case 0x74:
        NVRAM_addr &= ~0x00FF;
        NVRAM_addr |= val;
        break;
    case 0x75:
        NVRAM_addr &= ~0xFF00;
        NVRAM_addr |= val << 8;
        break;
    case 0x77:
        NVRAM_write(env, NVRAM_addr, val);
        NVRAM_addr = 0x0000;
        break;
    default:
        break;
    }
}

static uint32_t NVRAM_readb (CPUState *env, uint32_t addr)
{
    if (addr == 0x77)
        return NVRAM_read(env, NVRAM_addr);

    return 0xFF;
}

int load_initrd (const char *filename, uint8_t *addr)
{
    int fd, size;

    printf("Load initrd\n");
    fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -1;
    size = read(fd, addr, 16 * 1024 * 1024);
    if (size < 0)
        goto fail;
    close(fd);
    printf("Load initrd: %d\n", size);
    return size;
 fail:
    close(fd);
    printf("Load initrd failed\n");
    return -1;
}

/* Quick hack for PPC memory infos... */
static void put_long (void *addr, uint32_t l)
{
    char *pos = addr;
    pos[0] = (l >> 24) & 0xFF;
    pos[1] = (l >> 16) & 0xFF;
    pos[2] = (l >> 8) & 0xFF;
    pos[3] = l & 0xFF;
}

/* bootloader infos are in the form:
 * uint32_t TAG
 * uint32_t TAG_size (from TAG to next TAG).
 * datas
 * ....
 */
#if !defined (USE_OPEN_FIRMWARE)
static void *set_bootinfo_tag (void *addr, uint32_t tag, uint32_t size,
                               void *data)
{
    char *pos = addr;

    put_long(pos, tag);
    pos += 4;
    put_long(pos, size + 8);
    pos += 4;
    memcpy(pos, data, size);
    pos += size;

    return pos;
}
#endif

typedef struct boot_dev_t {
    const unsigned char *name;
    int major;
    int minor;
} boot_dev_t;

static boot_dev_t boot_devs[] = 
{
    { "/dev/fd0", 2, 0, },
    { "/dev/fd1", 2, 1, },
    { "/dev/hda1", 3, 1, },
//    { "/dev/ide/host0/bus0/target0/lun0/part1", 3, 1, },
    { "/dev/hdc", 22, 0, },
    { "/dev/ram0 init=/linuxrc", 1, 0, },
};

/* BATU:
 * BEPI  : bloc virtual address
 * BL    : area size bits (128 kB is 0, 256 1, 512 3, ...
 * Vs/Vp
 * BATL:
 * BPRN  : bloc real address align on 4MB boundary
 * WIMG  : cache access mode : not used
 * PP    : protection bits
 */
static void setup_BAT (CPUPPCState *env, int BAT,
                       uint32_t virtual, uint32_t physical,
                       uint32_t size, int Vs, int Vp, int PP)
{
    uint32_t sz_bits, tmp_sz, align, tmp;
    
    sz_bits = 0;
    align = 131072;
    for (tmp_sz = size / 131072; tmp_sz != 1; tmp_sz = tmp_sz >> 1) {
        sz_bits = (sz_bits << 1) + 1;
        align = align << 1;
    }
    tmp = virtual & ~(align - 1);  /* Align virtual area start */
    tmp |= sz_bits << 2;           /* Fix BAT size             */
    tmp |= Vs << 1;                /* Supervisor access        */
    tmp |= Vp;                     /* User access              */
    env->DBAT[0][BAT] = tmp;
    env->IBAT[0][BAT] = tmp;
    tmp = physical & ~(align - 1); /* Align physical area start */
    tmp |= 0;                      /* Don't care about WIMG     */
    tmp |= PP;                     /* Protection                */
    env->DBAT[1][BAT] = tmp;
    env->IBAT[1][BAT] = tmp;
    printf("Set BATU0 to 0x%08x BATL0 to 0x%08x\n",
           env->DBAT[0][BAT], env->DBAT[1][BAT]);
}

static void VGA_printf (uint8_t *s)
{
    uint16_t *arg_ptr;
    unsigned int format_width, i;
    int in_format;
    uint16_t arg, digit, nibble;
    uint8_t c;

    arg_ptr = (uint16_t *)(&s);
    in_format = 0;
    format_width = 0;
    while ((c = *s) != '\0') {
        if (c == '%') {
            in_format = 1;
            format_width = 0;
        } else if (in_format) {
            if ((c >= '0') && (c <= '9')) {
                format_width = (format_width * 10) + (c - '0');
            } else if (c == 'x') {
                arg_ptr++; // increment to next arg
                arg = *arg_ptr;
                if (format_width == 0)
                    format_width = 4;
                digit = format_width - 1;
                for (i = 0; i < format_width; i++) {
                    nibble = (arg >> (4 * digit)) & 0x000f;
                    if (nibble <= 9)
                        PPC_io_writeb(PPC_IO_BASE + 0x500, nibble + '0');
                    else
                        PPC_io_writeb(PPC_IO_BASE + 0x500, nibble + 'A');
                    digit--;
                }
                in_format = 0;
            }
            //else if (c == 'd') {
            //  in_format = 0;
            //  }
        } else {
            PPC_io_writeb(PPC_IO_BASE + 0x500, c);
        }
        s++;
    }
}

static void VGA_init (void)
{
    /* Basic VGA init, inspired by plex86 VGAbios */
    printf("Init VGA...\n");
    /* switch to color mode and enable CPU access 480 lines */
    PPC_io_writeb(PPC_IO_BASE + 0x3C2, 0xC3);
    /* more than 64k 3C4/04 */
    PPC_io_writeb(PPC_IO_BASE + 0x3C4, 0x04);
    PPC_io_writeb(PPC_IO_BASE + 0x3C5, 0x02);
    VGA_printf("PPC VGA BIOS...\n");
}

void PPC_init_hw (CPUPPCState *env, uint32_t mem_size,
                  uint32_t kernel_addr, uint32_t kernel_size,
                  uint32_t stack_addr, int boot_device)
{
    char *p;
#if !defined (USE_OPEN_FIRMWARE)
    char *tmp;
    uint32_t tmpi[2];
#endif
    int PPC_io_memory;
    
#if defined (USE_OPEN_FIRMWARE)
    setup_memory(env, mem_size);
#endif
    /* Register 64 kB of IO space */
    PPC_io_memory = cpu_register_io_memory(0, PPC_io_read, PPC_io_write);
    cpu_register_physical_memory(0x80000000, 0x10000, PPC_io_memory);
    /* Register fake IO ports for PREP */
    register_ioport_read(0x398, 2, PREP_io_read, 1);
    register_ioport_write(0x398, 2, PREP_io_write, 1);
    /* System control ports */
    register_ioport_write(0x0092, 0x1, PREP_io_800_writeb, 1);
    register_ioport_read(0x0800, 0x52, PREP_io_800_readb, 1);
    register_ioport_write(0x0800, 0x52, PREP_io_800_writeb, 1);
    /* PCI intack location */
    PPC_io_memory = cpu_register_io_memory(0, PPC_ioB_read, PPC_ioB_write);
    cpu_register_physical_memory(0xBFFFFFF0, 0x4, PPC_io_memory);
    /* NVRAM ports */
    NVRAM_init();
    register_ioport_read(0x0074, 0x04, NVRAM_readb, 1);
    register_ioport_write(0x0074, 0x04, NVRAM_writeb, 1);

    /* Fake bootloader */
    env->nip = kernel_addr + (3 * sizeof(uint32_t));
    /* Set up msr according to PREP specification */
    msr_ee = 0;
    msr_fp = 1;
    msr_pr = 0; /* Start in supervisor mode */
    msr_me = 1;
    msr_fe0 = msr_fe1 = 0;
    msr_ip = 0;
    msr_ir = msr_dr = 1;
//    msr_sf = 0;
    msr_le = msr_ile = 0;
    env->gpr[1] = stack_addr; /* Let's have a stack */
    env->gpr[2] = 0;
    env->gpr[8] = kernel_addr;
    /* There is a bug in  2.4 kernels:
     * if a decrementer exception is pending when it enables msr_ee,
     * it's not ready to handle it...
     */
    env->decr = 0xFFFFFFFF;
    p = (void *)(phys_ram_base + kernel_addr);
#if !defined (USE_OPEN_FIRMWARE)
    /* Let's register the whole memory available only in supervisor mode */
    setup_BAT(env, 0, 0x00000000, 0x00000000, mem_size, 1, 0, 2);
    /* Avoid open firmware init call (to get a console)
     * This will make the kernel think we are a PREP machine...
     */
    put_long(p, 0xdeadc0de);
    /* Build a real stack room */
    p = (void *)(phys_ram_base + stack_addr);
    put_long(p, stack_addr);
    p -= 32;
    env->gpr[1] -= 32;
    /* Pretend there are no residual data */
    env->gpr[3] = 0;
#if 1
    {
        int size;
        env->gpr[4] = 0x00800000;
        size = load_initrd("initrd",
                           (void *)((uint32_t)phys_ram_base + env->gpr[4]));
        if (size < 0) {
            /* No initrd */
            env->gpr[4] = env->gpr[5] = 0;
        } else {
            env->gpr[5] = size;
            boot_device = 'e';
        }
        printf("Initrd loaded at 0x%08x (%d)\n", env->gpr[4], env->gpr[5]);
    }
#else
    env->gpr[4] = env->gpr[5] = 0;
#endif
    /* We have to put bootinfos after the BSS
     * The BSS starts after the kernel end.
     */
#if 0
    p = (void *)(((uint32_t)phys_ram_base + kernel_addr +
                  kernel_size + (1 << 20) - 1) & ~((1 << 20) - 1));
#else
    p = (void *)((uint32_t)phys_ram_base + kernel_addr + 0x400000);
#endif
    if (loglevel > 0) {
        fprintf(logfile, "bootinfos: %p 0x%08x\n",
                p, (uint32_t)p - (uint32_t)phys_ram_base);
    } else {
        printf("bootinfos: %p 0x%08x\n",
               p, (uint32_t)p - (uint32_t)phys_ram_base);
    }
    /* Command line: let's put it after bootinfos */
#if 0
    sprintf(p + 0x1000, "console=ttyS0,9600 root=%02x%02x mem=%dM",
            boot_devs[boot_device - 'a'].major,
            boot_devs[boot_device - 'a'].minor,
            phys_ram_size >> 20);
#else
    sprintf(p + 0x1000, "console=ttyS0,9600 console=tty0 root=%s mem=%dM load_ramdisk=1",
            boot_devs[boot_device - 'a'].name,
            phys_ram_size >> 20);
#endif
    env->gpr[6] = (uint32_t)p + 0x1000 - (uint32_t)phys_ram_base;
    env->gpr[7] = env->gpr[6] + strlen(p + 0x1000);
    if (loglevel > 0) {
        fprintf(logfile, "cmdline: %p 0x%08x [%s]\n",
                p + 0x1000, env->gpr[6], p + 0x1000);
    } else {
        printf("cmdline: %p 0x%08x [%s]\n",
               p + 0x1000, env->gpr[6], p + 0x1000);
    }
    /* BI_FIRST */
    p = set_bootinfo_tag(p, 0x1010, 0, 0);
    /* BI_CMD_LINE */
    p = set_bootinfo_tag(p, 0x1012, env->gpr[7] - env->gpr[6],
                         (void *)(env->gpr[6] + (uint32_t)phys_ram_base));
    /* BI_MEM_SIZE */
    tmp = (void *)tmpi;
    tmp[0] = (phys_ram_size >> 24) & 0xFF;
    tmp[1] = (phys_ram_size >> 16) & 0xFF;
    tmp[2] = (phys_ram_size >> 8) & 0xFF;
    tmp[3] = phys_ram_size & 0xFF;
    p = set_bootinfo_tag(p, 0x1017, 4, tmpi);
    /* BI_INITRD */
    tmp[0] = (env->gpr[4] >> 24) & 0xFF;
    tmp[1] = (env->gpr[4] >> 16) & 0xFF;
    tmp[2] = (env->gpr[4] >> 8) & 0xFF;
    tmp[3] = env->gpr[4] & 0xFF;
    tmp[4] = (env->gpr[5] >> 24) & 0xFF;
    tmp[5] = (env->gpr[5] >> 16) & 0xFF;
    tmp[6] = (env->gpr[5] >> 8) & 0xFF;
    tmp[7] = env->gpr[5] & 0xFF;
    p = set_bootinfo_tag(p, 0x1014, 8, tmpi);
    /* BI_LAST */
    p = set_bootinfo_tag(p, 0x1011, 0, 0);
#else
    /* Set up MMU:
     * kernel is loaded at kernel_addr and wants to be seen at 0x01000000
     */
    setup_BAT(env, 0, 0x01000000, kernel_addr, 0x00400000, 1, 0, 2);
    {
#if 0
        uint32_t offset = 
            *((uint32_t *)((uint32_t)phys_ram_base + kernel_addr));
#else
        uint32_t offset = 12;
#endif
        env->nip = 0x01000000 | (kernel_addr + offset);
        printf("Start address: 0x%08x\n", env->nip);
    }
    env->gpr[1] = env->nip + (1 << 22);
    p = (void *)(phys_ram_base + stack_addr);
    put_long(p - 32, stack_addr);
    env->gpr[1] -= 32;
    printf("Kernel starts at 0x%08x stack 0x%08x\n", env->nip, env->gpr[1]);
    /* We want all lower address not to be translated */
    setup_BAT(env, 1, 0x00000000, 0x00000000, 0x010000000, 1, 1, 2);
    /* We also need a BAT to access OF */
    setup_BAT(env, 2, 0xFFFE0000, mem_size - 131072, 131072, 1, 0, 1);
    /* Setup OF entry point */
    {
        char *p;
        p = (char *)phys_ram_base + mem_size - 131072;
        /* Special opcode to call OF */
        *p++ = 0x18; *p++ = 0x00; *p++ = 0x00; *p++ = 0x02;
        /* blr */
        *p++ = 0x4E; *p++ = 0x80; *p++ = 0x00; *p++ = 0x20;
    }
    env->gpr[5] = 0xFFFE0000;
    /* Register translations */
    {
        OF_transl_t translations[3] = {
            { 0x01000000, 0x00400000, kernel_addr, 0x00000002, },
            { 0x00000000, 0x01000000, 0x00000000, 0x00000002, },
            { 0xFFFE0000, 0x00020000, mem_size - (128 * 1024),
              0x00000001, },
        };
        OF_register_translations(3, translations);
    }
    /* Quite artificial, for now */
    OF_register_bus("isa", "isa");
    OF_register_serial("isa", "serial", 4, 0x3f8);
    OF_register_stdio("serial", "serial");
    /* Set up RTAS service */
    RTAS_init();
    /* Command line: let's put it just over the stack */
#if 1
    sprintf(p, "console=ttyS0,9600 root=%02x%02x mem=%dM",
            boot_devs[boot_device - 'a'].major,
            boot_devs[boot_device - 'a'].minor,
            phys_ram_size >> 20);
#else
    sprintf(p, "console=ttyS0,9600 root=%s mem=%dM ne2000=0x300,9",
            boot_devs[boot_device - 'a'].name,
            phys_ram_size >> 20);
#endif
    OF_register_bootargs(p);
#endif
}

void PPC_end_init (void)
{
    VGA_init();
}
