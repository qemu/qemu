/*
 * QEMU PPC PREP hardware System Emulator
 * 
 * Copyright (c) 2003-2004 Jocelyn Mayer
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
#include "vl.h"
#include "m48t59.h"

//#define HARD_DEBUG_PPC_IO
//#define DEBUG_PPC_IO

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

#define BIOS_FILENAME "ppc_rom.bin"
#define LINUX_BOOT_FILENAME "linux_boot.bin"

#define KERNEL_LOAD_ADDR    0x00000000
#define KERNEL_STACK_ADDR   0x00400000
#define INITRD_LOAD_ADDR    0x00800000

int load_kernel(const char *filename, uint8_t *addr, 
                uint8_t *real_addr)
{
    int fd, size;
    int setup_sects;

    fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -1;

    /* load 16 bit code */
    if (read(fd, real_addr, 512) != 512)
        goto fail;
    setup_sects = real_addr[0x1F1];
    if (!setup_sects)
        setup_sects = 4;
    if (read(fd, real_addr + 512, setup_sects * 512) != 
        setup_sects * 512)
        goto fail;
    
    /* load 32 bit code */
    size = read(fd, addr, 16 * 1024 * 1024);
    if (size < 0)
        goto fail;
    close(fd);
    return size;
 fail:
    close(fd);
    return -1;
}

static const int ide_iobase[2] = { 0x1f0, 0x170 };
static const int ide_iobase2[2] = { 0x3f6, 0x376 };
static const int ide_irq[2] = { 13, 13 };

#define NE2000_NB_MAX 6

static uint32_t ne2000_io[NE2000_NB_MAX] = { 0x300, 0x320, 0x340, 0x360, 0x280, 0x380 };
static int ne2000_irq[NE2000_NB_MAX] = { 9, 10, 11, 3, 4, 5 };

/* IO ports emulation */
#define PPC_IO_BASE 0x80000000

static void PPC_io_writeb (uint32_t addr, uint32_t value, uint32_t vaddr)
{
    /* Don't polute serial port output */
#if 0
    if ((addr < 0x800003F0 || addr > 0x80000400) &&
        (addr < 0x80000074 || addr > 0x80000077) &&
        (addr < 0x80000020 || addr > 0x80000021) &&
        (addr < 0x800000a0 || addr > 0x800000a1) &&
        (addr < 0x800001f0 || addr > 0x800001f7) &&
        (addr < 0x80000170 || addr > 0x80000177)) 
#endif
    {
        PPC_IO_DPRINTF("0x%08x => 0x%02x\n", addr - PPC_IO_BASE, value);
    }
    cpu_outb(NULL, addr - PPC_IO_BASE, value);
}

static uint32_t PPC_io_readb (uint32_t addr)
{
    uint32_t ret = cpu_inb(NULL, addr - PPC_IO_BASE);

#if 0
    if ((addr < 0x800003F0 || addr > 0x80000400) &&
        (addr < 0x80000074 || addr > 0x80000077) &&
        (addr < 0x80000020 || addr > 0x80000021) &&
        (addr < 0x800000a0 || addr > 0x800000a1) &&
        (addr < 0x800001f0 || addr > 0x800001f7) &&
        (addr < 0x80000170 || addr > 0x80000177) &&
        (addr < 0x8000060 || addr > 0x8000064))
#endif
    {
        PPC_IO_DPRINTF("0x%08x <= 0x%02x\n", addr - PPC_IO_BASE, ret);
    }

    return ret;
}

static void PPC_io_writew (uint32_t addr, uint32_t value, uint32_t vaddr)
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

static void PPC_io_writel (uint32_t addr, uint32_t value, uint32_t vaddr)
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

/* Read-only register (?) */
static void _PPC_ioB_write (uint32_t addr, uint32_t value, uint32_t vaddr)
{
    //    printf("%s: 0x%08x => 0x%08x\n", __func__, addr, value);
}

static uint32_t _PPC_ioB_read (uint32_t addr)
{
    uint32_t retval = 0;

    if (addr == 0xBFFFFFF0)
        retval = pic_intack_read(NULL);
       //   printf("%s: 0x%08x <= %d\n", __func__, addr, retval);

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

static void PREP_io_write (void *opaque, uint32_t addr, uint32_t val)
{
    PPC_IO_DPRINTF("0x%08x => 0x%08x\n", addr - PPC_IO_BASE, val);
    PREP_fake_io[addr - 0x0398] = val;
}

static uint32_t PREP_io_read (void *opaque, uint32_t addr)
{
    PPC_IO_DPRINTF("0x%08x <= 0x%08x\n", addr - PPC_IO_BASE, PREP_fake_io[addr - 0x0398]);
    return PREP_fake_io[addr - 0x0398];
}

static uint8_t syscontrol;

static void PREP_io_800_writeb (void *opaque, uint32_t addr, uint32_t val)
{
    PPC_IO_DPRINTF("0x%08x => 0x%08x\n", addr - PPC_IO_BASE, val);
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

static uint32_t PREP_io_800_readb (void *opaque, uint32_t addr)
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
    PPC_IO_DPRINTF("0x%08x <= 0x%08x\n", addr - PPC_IO_BASE, retval);

    return retval;
}

#define NVRAM_SIZE        0x2000
#define NVRAM_END         0x1FF0
#define NVRAM_OSAREA_SIZE 512
#define NVRAM_CONFSIZE    1024

static inline void NVRAM_set_byte (m48t59_t *nvram, uint32_t addr, uint8_t value)
{
    m48t59_set_addr(nvram, addr);
    m48t59_write(nvram, value);
}

static inline uint8_t NVRAM_get_byte (m48t59_t *nvram, uint32_t addr)
{
    m48t59_set_addr(nvram, addr);
    return m48t59_read(nvram);
}

static inline void NVRAM_set_word (m48t59_t *nvram, uint32_t addr, uint16_t value)
{
    m48t59_set_addr(nvram, addr);
    m48t59_write(nvram, value >> 8);
    m48t59_set_addr(nvram, addr + 1);
    m48t59_write(nvram, value & 0xFF);
}

static inline uint16_t NVRAM_get_word (m48t59_t *nvram, uint32_t addr)
{
    uint16_t tmp;

    m48t59_set_addr(nvram, addr);
    tmp = m48t59_read(nvram) << 8;
    m48t59_set_addr(nvram, addr + 1);
    tmp |= m48t59_read(nvram);

    return tmp;
}

static inline void NVRAM_set_lword (m48t59_t *nvram, uint32_t addr,
				    uint32_t value)
{
    m48t59_set_addr(nvram, addr);
    m48t59_write(nvram, value >> 24);
    m48t59_set_addr(nvram, addr + 1);
    m48t59_write(nvram, (value >> 16) & 0xFF);
    m48t59_set_addr(nvram, addr + 2);
    m48t59_write(nvram, (value >> 8) & 0xFF);
    m48t59_set_addr(nvram, addr + 3);
    m48t59_write(nvram, value & 0xFF);
}

static inline uint32_t NVRAM_get_lword (m48t59_t *nvram, uint32_t addr)
{
    uint32_t tmp;

    m48t59_set_addr(nvram, addr);
    tmp = m48t59_read(nvram) << 24;
    m48t59_set_addr(nvram, addr + 1);
    tmp |= m48t59_read(nvram) << 16;
    m48t59_set_addr(nvram, addr + 2);
    tmp |= m48t59_read(nvram) << 8;
    m48t59_set_addr(nvram, addr + 3);
    tmp |= m48t59_read(nvram);

    return tmp;
}

static uint16_t NVRAM_crc_update (uint16_t prev, uint16_t value)
{
    uint16_t tmp;
    uint16_t pd, pd1, pd2;

    tmp = prev >> 8;
    pd = prev ^ value;
    pd1 = pd & 0x000F;
    pd2 = ((pd >> 4) & 0x000F) ^ pd1;
    tmp ^= (pd1 << 3) | (pd1 << 8);
    tmp ^= pd2 | (pd2 << 7) | (pd2 << 12);

    return tmp;
}

static void NVRAM_set_crc (m48t59_t *nvram, uint32_t addr,
			   uint32_t start, uint32_t count)
{
    uint32_t i;
    uint16_t crc = 0xFFFF;
    int odd = 0;

    if (count & 1)
	odd = 1;
    count &= ~1;
    for (i = 0; i != count; i++) {
	crc = NVRAM_crc_update(crc, NVRAM_get_word(nvram, start + i));
    }
    if (odd) {
	crc = NVRAM_crc_update(crc, NVRAM_get_byte(nvram, start + i) << 8);
    }
    NVRAM_set_word(nvram, addr, crc);
}

static void prep_NVRAM_init (void)
{
    m48t59_t *nvram;

    nvram = m48t59_init(8, 0x0074, NVRAM_SIZE);
    /* NVRAM header */
    /* 0x00: NVRAM size in kB */
    NVRAM_set_word(nvram, 0x00, NVRAM_SIZE >> 10);
    /* 0x02: NVRAM version */
    NVRAM_set_byte(nvram, 0x02, 0x01);
    /* 0x03: NVRAM revision */
    NVRAM_set_byte(nvram, 0x03, 0x01);
    /* 0x08: last OS */
    NVRAM_set_byte(nvram, 0x08, 0x00); /* Unknown */
    /* 0x09: endian */
    NVRAM_set_byte(nvram, 0x09, 'B');  /* Big-endian */
    /* 0x0A: OSArea usage */
    NVRAM_set_byte(nvram, 0x0A, 0x00); /* Empty */
    /* 0x0B: PM mode */
    NVRAM_set_byte(nvram, 0x0B, 0x00); /* Normal */
    /* Restart block description record */
    /* 0x0C: restart block version */
    NVRAM_set_word(nvram, 0x0C, 0x01);
    /* 0x0E: restart block revision */
    NVRAM_set_word(nvram, 0x0E, 0x01);
    /* 0x20: restart address */
    NVRAM_set_lword(nvram, 0x20, 0x00);
    /* 0x24: save area address */
    NVRAM_set_lword(nvram, 0x24, 0x00);
    /* 0x28: save area length */
    NVRAM_set_lword(nvram, 0x28, 0x00);
    /* 0x1C: checksum of restart block */
    NVRAM_set_crc(nvram, 0x1C, 0x0C, 32);

    /* Security section */
    /* Set all to zero */
    /* 0xC4: pointer to global environment area */
    NVRAM_set_lword(nvram, 0xC4, 0x0100);
    /* 0xC8: size of global environment area */
    NVRAM_set_lword(nvram, 0xC8,
		    NVRAM_END - NVRAM_OSAREA_SIZE - NVRAM_CONFSIZE - 0x0100);
    /* 0xD4: pointer to configuration area */
    NVRAM_set_lword(nvram, 0xD4, NVRAM_END - NVRAM_CONFSIZE);
    /* 0xD8: size of configuration area */
    NVRAM_set_lword(nvram, 0xD8, NVRAM_CONFSIZE);
    /* 0xE8: pointer to OS specific area */
    NVRAM_set_lword(nvram, 0xE8,
		    NVRAM_END - NVRAM_CONFSIZE - NVRAM_OSAREA_SIZE);
    /* 0xD8: size of OS specific area */
    NVRAM_set_lword(nvram, 0xEC, NVRAM_OSAREA_SIZE);

    /* Configuration area */
    /* RTC init */
    //    NVRAM_set_lword(nvram, 0x1FFC, 0x50);

    /* 0x04: checksum 0 => OS area   */
    NVRAM_set_crc(nvram, 0x04, 0x00,
		  NVRAM_END - NVRAM_CONFSIZE - NVRAM_OSAREA_SIZE);
    /* 0x06: checksum of config area */
    NVRAM_set_crc(nvram, 0x06, NVRAM_END - NVRAM_CONFSIZE, NVRAM_CONFSIZE);
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
 * data
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
    { "/dev/hda", 3, 1, },
//    { "/dev/ide/host0/bus0/target0/lun0/part1", 3, 1, },
//    { "/dev/hdc", 22, 0, },
    { "/dev/hdc", 22, 1, },
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

    arg_ptr = (uint16_t *)((void *)&s);
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
                        PPC_io_writeb(PPC_IO_BASE + 0x500, nibble + '0', 0);
                    else
                        PPC_io_writeb(PPC_IO_BASE + 0x500, nibble + 'A', 0);
                    digit--;
                }
                in_format = 0;
            }
            //else if (c == 'd') {
            //  in_format = 0;
            //  }
        } else {
            PPC_io_writeb(PPC_IO_BASE + 0x500, c, 0);
        }
        s++;
    }
}

static void VGA_init (void)
{
    /* Basic VGA init, inspired by plex86 VGAbios */
    printf("Init VGA...\n");
#if 1
    /* switch to color mode and enable CPU access 480 lines */
    PPC_io_writeb(PPC_IO_BASE + 0x3C2, 0xC3, 0);
    /* more than 64k 3C4/04 */
    PPC_io_writeb(PPC_IO_BASE + 0x3C4, 0x04, 0);
    PPC_io_writeb(PPC_IO_BASE + 0x3C5, 0x02, 0);
#endif
    VGA_printf("PPC VGA BIOS...\n");
}

extern CPUPPCState *global_env;

void PPC_init_hw (/*CPUPPCState *env,*/ uint32_t mem_size,
                  uint32_t kernel_addr, uint32_t kernel_size,
                  uint32_t stack_addr, int boot_device,
		  const unsigned char *initrd_file)
{
    CPUPPCState *env = global_env;
    char *p;
#if !defined (USE_OPEN_FIRMWARE)
    char *tmp;
    uint32_t tmpi[2];
#endif

    printf("RAM size: %u 0x%08x (%u)\n", mem_size, mem_size, mem_size >> 20);
#if defined (USE_OPEN_FIRMWARE)
    setup_memory(env, mem_size);
#endif

    /* Fake bootloader */
    {
#if 1
        uint32_t offset = 
            *((uint32_t *)((uint32_t)phys_ram_base + kernel_addr));
#else
        uint32_t offset = 12;
#endif
        env->nip = kernel_addr + offset;
        printf("Start address: 0x%08x\n", env->nip);
    }
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
    if (initrd_file != NULL) {
        int size;
        env->gpr[4] = (kernel_addr + kernel_size + 4095) & ~4095;
        size = load_initrd(initrd_file,
                           (void *)((uint32_t)phys_ram_base + env->gpr[4]));
        if (size < 0) {
            /* No initrd */
            env->gpr[4] = env->gpr[5] = 0;
        } else {
            env->gpr[5] = size;
            boot_device = 'e';
        }
        printf("Initrd loaded at 0x%08x (%d) (0x%08x 0x%08x)\n",
	       env->gpr[4], env->gpr[5], kernel_addr, kernel_size);
    } else {
	env->gpr[4] = env->gpr[5] = 0;
    }
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
            mem_size >> 20);
#else
    sprintf(p + 0x1000, "console=ttyS0,9600 console=tty0 root=%s mem=%dM",
            boot_devs[boot_device - 'a'].name,
            mem_size >> 20);
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
    tmp[0] = (mem_size >> 24) & 0xFF;
    tmp[1] = (mem_size >> 16) & 0xFF;
    tmp[2] = (mem_size >> 8) & 0xFF;
    tmp[3] = mem_size & 0xFF;
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
    env->gpr[4] = env->gpr[5] = 0;
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
#if 0
#if 0
    p = (void *)(((uint32_t)phys_ram_base + kernel_addr +
                  kernel_size + (1 << 20) - 1) & ~((1 << 20) - 1));
#else
    p = (void *)((uint32_t)phys_ram_base + kernel_addr + 0x400000);
#endif
#if 1
    sprintf(p, "console=ttyS0,9600 root=%02x%02x mem=%dM",
            boot_devs[boot_device - 'a'].major,
            boot_devs[boot_device - 'a'].minor,
            mem_size >> 20);
#else
    sprintf(p, "console=ttyS0,9600 root=%s mem=%dM ne2000=0x300,9",
            boot_devs[boot_device - 'a'].name,
            mem_size >> 20);
#endif
    OF_register_bootargs(p);
#endif
#endif
}

void PPC_end_init (void)
{
    VGA_init();
}

/* PC hardware initialisation */
void ppc_prep_init(int ram_size, int vga_ram_size, int boot_device,
		   DisplayState *ds, const char **fd_filename, int snapshot,
		   const char *kernel_filename, const char *kernel_cmdline,
		   const char *initrd_filename)
{
    char buf[1024];
    int PPC_io_memory;
    int ret, linux_boot, initrd_size, i, nb_nics1, fd;

    linux_boot = (kernel_filename != NULL);

    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, 0);

    if (linux_boot) {
        /* now we can load the kernel */
        ret = load_image(kernel_filename, phys_ram_base + KERNEL_LOAD_ADDR);
        if (ret < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n", 
                    kernel_filename);
            exit(1);
        }
        /* load initrd */
        initrd_size = 0;
#if 0
        if (initrd_filename) {
            initrd_size = load_image(initrd_filename, phys_ram_base + INITRD_LOAD_ADDR);
            if (initrd_size < 0) {
                fprintf(stderr, "qemu: could not load initial ram disk '%s'\n", 
                        initrd_filename);
                exit(1);
            }
        }
#endif
        PPC_init_hw(/*env,*/ ram_size, KERNEL_LOAD_ADDR, ret,
                    KERNEL_STACK_ADDR, boot_device, initrd_filename);
    } else {
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
    }

    /* init basic PC hardware */
    vga_initialize(ds, phys_ram_base + ram_size, ram_size, 
                   vga_ram_size);
    rtc_init(0x70, 8);
    pic_init();
    //    pit_init(0x40, 0);

    fd = serial_open_device();
    serial_init(0x3f8, 4, fd);
#if 1
    nb_nics1 = nb_nics;
    if (nb_nics1 > NE2000_NB_MAX)
        nb_nics1 = NE2000_NB_MAX;
    for(i = 0; i < nb_nics1; i++) {
        ne2000_init(ne2000_io[i], ne2000_irq[i], &nd_table[i]);
    }
#endif

    for(i = 0; i < 2; i++) {
        ide_init(ide_iobase[i], ide_iobase2[i], ide_irq[i],
                 bs_table[2 * i], bs_table[2 * i + 1]);
    }
    kbd_init();
    AUD_init();
    DMA_init();
    //    SB16_init();

    fdctrl_init(6, 2, 0, 0x3f0, fd_table);

    /* Register 64 kB of IO space */
    PPC_io_memory = cpu_register_io_memory(0, PPC_io_read, PPC_io_write);
    cpu_register_physical_memory(0x80000000, 0x10000, PPC_io_memory);
    /* Register fake IO ports for PREP */
    register_ioport_read(0x398, 2, 1, &PREP_io_read, NULL);
    register_ioport_write(0x398, 2, 1, &PREP_io_write, NULL);
    /* System control ports */
    register_ioport_write(0x0092, 0x1, 1, &PREP_io_800_writeb, NULL);
    register_ioport_read(0x0800, 0x52, 1, &PREP_io_800_readb, NULL);
    register_ioport_write(0x0800, 0x52, 1, &PREP_io_800_writeb, NULL);
    /* PCI intack location (0xfef00000 / 0xbffffff0) */
    PPC_io_memory = cpu_register_io_memory(0, PPC_ioB_read, PPC_ioB_write);
    cpu_register_physical_memory(0xBFFFFFF0, 0x4, PPC_io_memory);
    //    cpu_register_physical_memory(0xFEF00000, 0x4, PPC_io_memory);
    prep_NVRAM_init();

    PPC_end_init();
}
