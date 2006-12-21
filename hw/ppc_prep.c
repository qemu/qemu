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

//#define HARD_DEBUG_PPC_IO
//#define DEBUG_PPC_IO

#define BIOS_FILENAME "ppc_rom.bin"
#define KERNEL_LOAD_ADDR 0x01000000
#define INITRD_LOAD_ADDR 0x01800000

extern int loglevel;
extern FILE *logfile;

#if defined (HARD_DEBUG_PPC_IO) && !defined (DEBUG_PPC_IO)
#define DEBUG_PPC_IO
#endif

#if defined (HARD_DEBUG_PPC_IO)
#define PPC_IO_DPRINTF(fmt, args...)                     \
do {                                                     \
    if (loglevel & CPU_LOG_IOPORT) {                     \
        fprintf(logfile, "%s: " fmt, __func__ , ##args); \
    } else {                                             \
        printf("%s : " fmt, __func__ , ##args);          \
    }                                                    \
} while (0)
#elif defined (DEBUG_PPC_IO)
#define PPC_IO_DPRINTF(fmt, args...)                     \
do {                                                     \
    if (loglevel & CPU_LOG_IOPORT) {                     \
        fprintf(logfile, "%s: " fmt, __func__ , ##args); \
    }                                                    \
} while (0)
#else
#define PPC_IO_DPRINTF(fmt, args...) do { } while (0)
#endif

/* Constants for devices init */
static const int ide_iobase[2] = { 0x1f0, 0x170 };
static const int ide_iobase2[2] = { 0x3f6, 0x376 };
static const int ide_irq[2] = { 13, 13 };

#define NE2000_NB_MAX 6

static uint32_t ne2000_io[NE2000_NB_MAX] = { 0x300, 0x320, 0x340, 0x360, 0x280, 0x380 };
static int ne2000_irq[NE2000_NB_MAX] = { 9, 10, 11, 3, 4, 5 };

//static PITState *pit;

/* ISA IO ports bridge */
#define PPC_IO_BASE 0x80000000

/* Speaker port 0x61 */
int speaker_data_on;
int dummy_refresh_clock;

static void speaker_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
#if 0
    speaker_data_on = (val >> 1) & 1;
    pit_set_gate(pit, 2, val & 1);
#endif
}

static uint32_t speaker_ioport_read(void *opaque, uint32_t addr)
{
#if 0
    int out;
    out = pit_get_out(pit, 2, qemu_get_clock(vm_clock));
    dummy_refresh_clock ^= 1;
    return (speaker_data_on << 1) | pit_get_gate(pit, 2) | (out << 5) |
      (dummy_refresh_clock << 4);
#endif
    return 0;
}

static void pic_irq_request(void *opaque, int level)
{
    if (level)
        cpu_interrupt(first_cpu, CPU_INTERRUPT_HARD);
    else
        cpu_reset_interrupt(first_cpu, CPU_INTERRUPT_HARD);
}

/* PCI intack register */
/* Read-only register (?) */
static void _PPC_intack_write (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    //    printf("%s: 0x%08x => 0x%08x\n", __func__, addr, value);
}

static inline uint32_t _PPC_intack_read (target_phys_addr_t addr)
{
    uint32_t retval = 0;

    if (addr == 0xBFFFFFF0)
        retval = pic_intack_read(isa_pic);
       //   printf("%s: 0x%08x <= %d\n", __func__, addr, retval);

    return retval;
}

static uint32_t PPC_intack_readb (void *opaque, target_phys_addr_t addr)
{
    return _PPC_intack_read(addr);
}

static uint32_t PPC_intack_readw (void *opaque, target_phys_addr_t addr)
{
#ifdef TARGET_WORDS_BIGENDIAN
    return bswap16(_PPC_intack_read(addr));
#else
    return _PPC_intack_read(addr);
#endif
}

static uint32_t PPC_intack_readl (void *opaque, target_phys_addr_t addr)
{
#ifdef TARGET_WORDS_BIGENDIAN
    return bswap32(_PPC_intack_read(addr));
#else
    return _PPC_intack_read(addr);
#endif
}

static CPUWriteMemoryFunc *PPC_intack_write[] = {
    &_PPC_intack_write,
    &_PPC_intack_write,
    &_PPC_intack_write,
};

static CPUReadMemoryFunc *PPC_intack_read[] = {
    &PPC_intack_readb,
    &PPC_intack_readw,
    &PPC_intack_readl,
};

/* PowerPC control and status registers */
#if 0 // Not used
static struct {
    /* IDs */
    uint32_t veni_devi;
    uint32_t revi;
    /* Control and status */
    uint32_t gcsr;
    uint32_t xcfr;
    uint32_t ct32;
    uint32_t mcsr;
    /* General purpose registers */
    uint32_t gprg[6];
    /* Exceptions */
    uint32_t feen;
    uint32_t fest;
    uint32_t fema;
    uint32_t fecl;
    uint32_t eeen;
    uint32_t eest;
    uint32_t eecl;
    uint32_t eeint;
    uint32_t eemck0;
    uint32_t eemck1;
    /* Error diagnostic */
} XCSR;

static void PPC_XCSR_writeb (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    printf("%s: 0x%08lx => 0x%08x\n", __func__, (long)addr, value);
}

static void PPC_XCSR_writew (void *opaque, target_phys_addr_t addr, uint32_t value)
{
#ifdef TARGET_WORDS_BIGENDIAN
    value = bswap16(value);
#endif
    printf("%s: 0x%08lx => 0x%08x\n", __func__, (long)addr, value);
}

static void PPC_XCSR_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
#ifdef TARGET_WORDS_BIGENDIAN
    value = bswap32(value);
#endif
    printf("%s: 0x%08lx => 0x%08x\n", __func__, (long)addr, value);
}

static uint32_t PPC_XCSR_readb (void *opaque, target_phys_addr_t addr)
{
    uint32_t retval = 0;

    printf("%s: 0x%08lx <= %d\n", __func__, (long)addr, retval);

    return retval;
}

static uint32_t PPC_XCSR_readw (void *opaque, target_phys_addr_t addr)
{
    uint32_t retval = 0;

    printf("%s: 0x%08lx <= %d\n", __func__, (long)addr, retval);
#ifdef TARGET_WORDS_BIGENDIAN
    retval = bswap16(retval);
#endif

    return retval;
}

static uint32_t PPC_XCSR_readl (void *opaque, target_phys_addr_t addr)
{
    uint32_t retval = 0;

    printf("%s: 0x%08lx <= %d\n", __func__, (long)addr, retval);
#ifdef TARGET_WORDS_BIGENDIAN
    retval = bswap32(retval);
#endif

    return retval;
}

static CPUWriteMemoryFunc *PPC_XCSR_write[] = {
    &PPC_XCSR_writeb,
    &PPC_XCSR_writew,
    &PPC_XCSR_writel,
};

static CPUReadMemoryFunc *PPC_XCSR_read[] = {
    &PPC_XCSR_readb,
    &PPC_XCSR_readw,
    &PPC_XCSR_readl,
};
#endif

/* Fake super-io ports for PREP platform (Intel 82378ZB) */
typedef struct sysctrl_t {
    m48t59_t *nvram;
    uint8_t state;
    uint8_t syscontrol;
    uint8_t fake_io[2];
    int contiguous_map;
    int endian;
} sysctrl_t;

enum {
    STATE_HARDFILE = 0x01,
};

static sysctrl_t *sysctrl;

static void PREP_io_write (void *opaque, uint32_t addr, uint32_t val)
{
    sysctrl_t *sysctrl = opaque;

    PPC_IO_DPRINTF("0x%08lx => 0x%08x\n", (long)addr - PPC_IO_BASE, val);
    sysctrl->fake_io[addr - 0x0398] = val;
}

static uint32_t PREP_io_read (void *opaque, uint32_t addr)
{
    sysctrl_t *sysctrl = opaque;

    PPC_IO_DPRINTF("0x%08lx <= 0x%08x\n", (long)addr - PPC_IO_BASE,
                   sysctrl->fake_io[addr - 0x0398]);
    return sysctrl->fake_io[addr - 0x0398];
}

static void PREP_io_800_writeb (void *opaque, uint32_t addr, uint32_t val)
{
    sysctrl_t *sysctrl = opaque;

    PPC_IO_DPRINTF("0x%08lx => 0x%08x\n", (long)addr - PPC_IO_BASE, val);
    switch (addr) {
    case 0x0092:
        /* Special port 92 */
        /* Check soft reset asked */
        if (val & 0x01) {
            //            cpu_interrupt(first_cpu, CPU_INTERRUPT_RESET);
        }
        /* Check LE mode */
        if (val & 0x02) {
            sysctrl->endian = 1;
        } else {
            sysctrl->endian = 0;
        }
        break;
    case 0x0800:
        /* Motorola CPU configuration register : read-only */
        break;
    case 0x0802:
        /* Motorola base module feature register : read-only */
        break;
    case 0x0803:
        /* Motorola base module status register : read-only */
        break;
    case 0x0808:
        /* Hardfile light register */
        if (val & 1)
            sysctrl->state |= STATE_HARDFILE;
        else
            sysctrl->state &= ~STATE_HARDFILE;
        break;
    case 0x0810:
        /* Password protect 1 register */
        if (sysctrl->nvram != NULL)
            m48t59_toggle_lock(sysctrl->nvram, 1);
        break;
    case 0x0812:
        /* Password protect 2 register */
        if (sysctrl->nvram != NULL)
            m48t59_toggle_lock(sysctrl->nvram, 2);
        break;
    case 0x0814:
        /* L2 invalidate register */
        //        tlb_flush(first_cpu, 1);
        break;
    case 0x081C:
        /* system control register */
        sysctrl->syscontrol = val & 0x0F;
        break;
    case 0x0850:
        /* I/O map type register */
        sysctrl->contiguous_map = val & 0x01;
        break;
    default:
        printf("ERROR: unaffected IO port write: %04lx => %02x\n",
               (long)addr, val);
        break;
    }
}

static uint32_t PREP_io_800_readb (void *opaque, uint32_t addr)
{
    sysctrl_t *sysctrl = opaque;
    uint32_t retval = 0xFF;

    switch (addr) {
    case 0x0092:
        /* Special port 92 */
        retval = 0x00;
        break;
    case 0x0800:
        /* Motorola CPU configuration register */
        retval = 0xEF; /* MPC750 */
        break;
    case 0x0802:
        /* Motorola Base module feature register */
        retval = 0xAD; /* No ESCC, PMC slot neither ethernet */
        break;
    case 0x0803:
        /* Motorola base module status register */
        retval = 0xE0; /* Standard MPC750 */
        break;
    case 0x080C:
        /* Equipment present register:
         *  no L2 cache
         *  no upgrade processor
         *  no cards in PCI slots
         *  SCSI fuse is bad
         */
        retval = 0x3C;
        break;
    case 0x0810:
        /* Motorola base module extended feature register */
        retval = 0x39; /* No USB, CF and PCI bridge. NVRAM present */
        break;
    case 0x0814:
        /* L2 invalidate: don't care */
        break;
    case 0x0818:
        /* Keylock */
        retval = 0x00;
        break;
    case 0x081C:
        /* system control register
         * 7 - 6 / 1 - 0: L2 cache enable
         */
        retval = sysctrl->syscontrol;
        break;
    case 0x0823:
        /* */
        retval = 0x03; /* no L2 cache */
        break;
    case 0x0850:
        /* I/O map type register */
        retval = sysctrl->contiguous_map;
        break;
    default:
        printf("ERROR: unaffected IO port: %04lx read\n", (long)addr);
        break;
    }
    PPC_IO_DPRINTF("0x%08lx <= 0x%08x\n", (long)addr - PPC_IO_BASE, retval);

    return retval;
}

static inline target_phys_addr_t prep_IO_address (sysctrl_t *sysctrl,
                                                  target_phys_addr_t addr)
{
    if (sysctrl->contiguous_map == 0) {
        /* 64 KB contiguous space for IOs */
        addr &= 0xFFFF;
    } else {
        /* 8 MB non-contiguous space for IOs */
        addr = (addr & 0x1F) | ((addr & 0x007FFF000) >> 7);
    }

    return addr;
}

static void PPC_prep_io_writeb (void *opaque, target_phys_addr_t addr,
                                uint32_t value)
{
    sysctrl_t *sysctrl = opaque;

    addr = prep_IO_address(sysctrl, addr);
    cpu_outb(NULL, addr, value);
}

static uint32_t PPC_prep_io_readb (void *opaque, target_phys_addr_t addr)
{
    sysctrl_t *sysctrl = opaque;
    uint32_t ret;

    addr = prep_IO_address(sysctrl, addr);
    ret = cpu_inb(NULL, addr);

    return ret;
}

static void PPC_prep_io_writew (void *opaque, target_phys_addr_t addr,
                                uint32_t value)
{
    sysctrl_t *sysctrl = opaque;

    addr = prep_IO_address(sysctrl, addr);
#ifdef TARGET_WORDS_BIGENDIAN
    value = bswap16(value);
#endif
    PPC_IO_DPRINTF("0x%08lx => 0x%08x\n", (long)addr, value);
    cpu_outw(NULL, addr, value);
}

static uint32_t PPC_prep_io_readw (void *opaque, target_phys_addr_t addr)
{
    sysctrl_t *sysctrl = opaque;
    uint32_t ret;

    addr = prep_IO_address(sysctrl, addr);
    ret = cpu_inw(NULL, addr);
#ifdef TARGET_WORDS_BIGENDIAN
    ret = bswap16(ret);
#endif
    PPC_IO_DPRINTF("0x%08lx <= 0x%08x\n", (long)addr, ret);

    return ret;
}

static void PPC_prep_io_writel (void *opaque, target_phys_addr_t addr,
                                uint32_t value)
{
    sysctrl_t *sysctrl = opaque;

    addr = prep_IO_address(sysctrl, addr);
#ifdef TARGET_WORDS_BIGENDIAN
    value = bswap32(value);
#endif
    PPC_IO_DPRINTF("0x%08lx => 0x%08x\n", (long)addr, value);
    cpu_outl(NULL, addr, value);
}

static uint32_t PPC_prep_io_readl (void *opaque, target_phys_addr_t addr)
{
    sysctrl_t *sysctrl = opaque;
    uint32_t ret;

    addr = prep_IO_address(sysctrl, addr);
    ret = cpu_inl(NULL, addr);
#ifdef TARGET_WORDS_BIGENDIAN
    ret = bswap32(ret);
#endif
    PPC_IO_DPRINTF("0x%08lx <= 0x%08x\n", (long)addr, ret);

    return ret;
}

CPUWriteMemoryFunc *PPC_prep_io_write[] = {
    &PPC_prep_io_writeb,
    &PPC_prep_io_writew,
    &PPC_prep_io_writel,
};

CPUReadMemoryFunc *PPC_prep_io_read[] = {
    &PPC_prep_io_readb,
    &PPC_prep_io_readw,
    &PPC_prep_io_readl,
};

#define NVRAM_SIZE        0x2000

/* PowerPC PREP hardware initialisation */
static void ppc_prep_init(int ram_size, int vga_ram_size, int boot_device,
                          DisplayState *ds, const char **fd_filename, int snapshot,
                          const char *kernel_filename, const char *kernel_cmdline,
                          const char *initrd_filename)
{
    CPUState *env;
    char buf[1024];
    m48t59_t *nvram;
    int PPC_io_memory;
    int linux_boot, i, nb_nics1, bios_size;
    unsigned long bios_offset;
    uint32_t kernel_base, kernel_size, initrd_base, initrd_size;
    ppc_def_t *def;
    PCIBus *pci_bus;

    sysctrl = qemu_mallocz(sizeof(sysctrl_t));
    if (sysctrl == NULL)
	return;

    linux_boot = (kernel_filename != NULL);
    
    /* init CPUs */

    env = cpu_init();
    register_savevm("cpu", 0, 3, cpu_save, cpu_load, env);
    
    /* Register CPU as a 604 */
    /* XXX: CPU model (or PVR) should be provided on command line */
    //    ppc_find_by_name("604r", &def);
    //    ppc_find_by_name("604e", &def);
    ppc_find_by_name("604", &def);
    if (def == NULL) {
        cpu_abort(env, "Unable to find PowerPC CPU definition\n");
    }
    cpu_ppc_register(env, def);
    /* Set time-base frequency to 100 Mhz */
    cpu_ppc_tb_init(env, 100UL * 1000UL * 1000UL);

    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, IO_MEM_RAM);

    /* allocate and load BIOS */
    bios_offset = ram_size + vga_ram_size;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, BIOS_FILENAME);
    bios_size = load_image(buf, phys_ram_base + bios_offset);
    if (bios_size < 0 || bios_size > BIOS_SIZE) {
        fprintf(stderr, "qemu: could not load PPC PREP bios '%s'\n", buf);
        exit(1);
    }
    bios_size = (bios_size + 0xfff) & ~0xfff;
    cpu_register_physical_memory((uint32_t)(-bios_size), 
                                 bios_size, bios_offset | IO_MEM_ROM);

    if (linux_boot) {
        kernel_base = KERNEL_LOAD_ADDR;
        /* now we can load the kernel */
        kernel_size = load_image(kernel_filename, phys_ram_base + kernel_base);
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n", 
                    kernel_filename);
            exit(1);
        }
        /* load initrd */
        if (initrd_filename) {
            initrd_base = INITRD_LOAD_ADDR;
            initrd_size = load_image(initrd_filename,
                                     phys_ram_base + initrd_base);
            if (initrd_size < 0) {
                fprintf(stderr, "qemu: could not load initial ram disk '%s'\n", 
                        initrd_filename);
                exit(1);
            }
        } else {
            initrd_base = 0;
            initrd_size = 0;
        }
        boot_device = 'm';
    } else {
        kernel_base = 0;
        kernel_size = 0;
        initrd_base = 0;
        initrd_size = 0;
    }

    isa_mem_base = 0xc0000000;
    pci_bus = pci_prep_init();
    //    pci_bus = i440fx_init();
    /* Register 8 MB of ISA IO space (needed for non-contiguous map) */
    PPC_io_memory = cpu_register_io_memory(0, PPC_prep_io_read,
                                           PPC_prep_io_write, sysctrl);
    cpu_register_physical_memory(0x80000000, 0x00800000, PPC_io_memory);

    /* init basic PC hardware */
    pci_vga_init(pci_bus, ds, phys_ram_base + ram_size, ram_size, 
                 vga_ram_size, 0, 0);
    rtc_init(0x70, 8);
    //    openpic = openpic_init(0x00000000, 0xF0000000, 1);
    isa_pic = pic_init(pic_irq_request, first_cpu);
    //    pit = pit_init(0x40, 0);

    serial_init(&pic_set_irq_new, isa_pic, 0x3f8, 4, serial_hds[0]);
    nb_nics1 = nb_nics;
    if (nb_nics1 > NE2000_NB_MAX)
        nb_nics1 = NE2000_NB_MAX;
    for(i = 0; i < nb_nics1; i++) {
        if (nd_table[0].model == NULL
            || strcmp(nd_table[0].model, "ne2k_isa") == 0) {
            isa_ne2000_init(ne2000_io[i], ne2000_irq[i], &nd_table[i]);
        } else {
            fprintf(stderr, "qemu: Unsupported NIC: %s\n", nd_table[0].model);
            exit (1);
        }
    }

    for(i = 0; i < 2; i++) {
        isa_ide_init(ide_iobase[i], ide_iobase2[i], ide_irq[i],
                     bs_table[2 * i], bs_table[2 * i + 1]);
    }
    kbd_init();
    DMA_init(1);
    //    AUD_init();
    //    SB16_init();

    fdctrl_init(6, 2, 0, 0x3f0, fd_table);

    /* Register speaker port */
    register_ioport_read(0x61, 1, 1, speaker_ioport_read, NULL);
    register_ioport_write(0x61, 1, 1, speaker_ioport_write, NULL);
    /* Register fake IO ports for PREP */
    register_ioport_read(0x398, 2, 1, &PREP_io_read, sysctrl);
    register_ioport_write(0x398, 2, 1, &PREP_io_write, sysctrl);
    /* System control ports */
    register_ioport_read(0x0092, 0x01, 1, &PREP_io_800_readb, sysctrl);
    register_ioport_write(0x0092, 0x01, 1, &PREP_io_800_writeb, sysctrl);
    register_ioport_read(0x0800, 0x52, 1, &PREP_io_800_readb, sysctrl);
    register_ioport_write(0x0800, 0x52, 1, &PREP_io_800_writeb, sysctrl);
    /* PCI intack location */
    PPC_io_memory = cpu_register_io_memory(0, PPC_intack_read,
                                           PPC_intack_write, NULL);
    cpu_register_physical_memory(0xBFFFFFF0, 0x4, PPC_io_memory);
    /* PowerPC control and status register group */
#if 0
    PPC_io_memory = cpu_register_io_memory(0, PPC_XCSR_read, PPC_XCSR_write, NULL);
    cpu_register_physical_memory(0xFEFF0000, 0x1000, PPC_io_memory);
#endif

    if (usb_enabled) {
        usb_ohci_init(pci_bus, 3, -1);
    }

    nvram = m48t59_init(8, 0, 0x0074, NVRAM_SIZE, 59);
    if (nvram == NULL)
        return;
    sysctrl->nvram = nvram;

    /* Initialise NVRAM */
    PPC_NVRAM_set_params(nvram, NVRAM_SIZE, "PREP", ram_size, boot_device,
                         kernel_base, kernel_size,
                         kernel_cmdline,
                         initrd_base, initrd_size,
                         /* XXX: need an option to load a NVRAM image */
                         0,
                         graphic_width, graphic_height, graphic_depth);

    /* Special port to get debug messages from Open-Firmware */
    register_ioport_write(0x0F00, 4, 1, &PPC_debug_write, NULL);
}

QEMUMachine prep_machine = {
    "prep",
    "PowerPC PREP platform",
    ppc_prep_init,
};
