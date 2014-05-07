/*
 * QEMU PPC PREP hardware System Emulator
 *
 * Copyright (c) 2003-2007 Jocelyn Mayer
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
#include "hw/hw.h"
#include "hw/timer/m48t59.h"
#include "hw/i386/pc.h"
#include "hw/char/serial.h"
#include "hw/block/fdc.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/isa/isa.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/ppc/ppc.h"
#include "hw/boards.h"
#include "qemu/log.h"
#include "hw/ide.h"
#include "hw/loader.h"
#include "hw/timer/mc146818rtc.h"
#include "hw/isa/pc87312.h"
#include "sysemu/blockdev.h"
#include "sysemu/arch_init.h"
#include "sysemu/qtest.h"
#include "exec/address-spaces.h"
#include "elf.h"

//#define HARD_DEBUG_PPC_IO
//#define DEBUG_PPC_IO

/* SMP is not enabled, for now */
#define MAX_CPUS 1

#define MAX_IDE_BUS 2

#define BIOS_SIZE (1024 * 1024)
#define BIOS_FILENAME "ppc_rom.bin"
#define KERNEL_LOAD_ADDR 0x01000000
#define INITRD_LOAD_ADDR 0x01800000

#if defined (HARD_DEBUG_PPC_IO) && !defined (DEBUG_PPC_IO)
#define DEBUG_PPC_IO
#endif

#if defined (HARD_DEBUG_PPC_IO)
#define PPC_IO_DPRINTF(fmt, ...)                         \
do {                                                     \
    if (qemu_loglevel_mask(CPU_LOG_IOPORT)) {            \
        qemu_log("%s: " fmt, __func__ , ## __VA_ARGS__); \
    } else {                                             \
        printf("%s : " fmt, __func__ , ## __VA_ARGS__);  \
    }                                                    \
} while (0)
#elif defined (DEBUG_PPC_IO)
#define PPC_IO_DPRINTF(fmt, ...) \
qemu_log_mask(CPU_LOG_IOPORT, fmt, ## __VA_ARGS__)
#else
#define PPC_IO_DPRINTF(fmt, ...) do { } while (0)
#endif

/* Constants for devices init */
static const int ide_iobase[2] = { 0x1f0, 0x170 };
static const int ide_iobase2[2] = { 0x3f6, 0x376 };
static const int ide_irq[2] = { 13, 13 };

#define NE2000_NB_MAX 6

static uint32_t ne2000_io[NE2000_NB_MAX] = { 0x300, 0x320, 0x340, 0x360, 0x280, 0x380 };
static int ne2000_irq[NE2000_NB_MAX] = { 9, 10, 11, 3, 4, 5 };

/* ISA IO ports bridge */
#define PPC_IO_BASE 0x80000000

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

static void PPC_XCSR_writeb (void *opaque,
                             hwaddr addr, uint32_t value)
{
    printf("%s: 0x" TARGET_FMT_plx " => 0x%08" PRIx32 "\n", __func__, addr,
           value);
}

static void PPC_XCSR_writew (void *opaque,
                             hwaddr addr, uint32_t value)
{
    printf("%s: 0x" TARGET_FMT_plx " => 0x%08" PRIx32 "\n", __func__, addr,
           value);
}

static void PPC_XCSR_writel (void *opaque,
                             hwaddr addr, uint32_t value)
{
    printf("%s: 0x" TARGET_FMT_plx " => 0x%08" PRIx32 "\n", __func__, addr,
           value);
}

static uint32_t PPC_XCSR_readb (void *opaque, hwaddr addr)
{
    uint32_t retval = 0;

    printf("%s: 0x" TARGET_FMT_plx " <= %08" PRIx32 "\n", __func__, addr,
           retval);

    return retval;
}

static uint32_t PPC_XCSR_readw (void *opaque, hwaddr addr)
{
    uint32_t retval = 0;

    printf("%s: 0x" TARGET_FMT_plx " <= %08" PRIx32 "\n", __func__, addr,
           retval);

    return retval;
}

static uint32_t PPC_XCSR_readl (void *opaque, hwaddr addr)
{
    uint32_t retval = 0;

    printf("%s: 0x" TARGET_FMT_plx " <= %08" PRIx32 "\n", __func__, addr,
           retval);

    return retval;
}

static const MemoryRegionOps PPC_XCSR_ops = {
    .old_mmio = {
        .read = { PPC_XCSR_readb, PPC_XCSR_readw, PPC_XCSR_readl, },
        .write = { PPC_XCSR_writeb, PPC_XCSR_writew, PPC_XCSR_writel, },
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

#endif

/* Fake super-io ports for PREP platform (Intel 82378ZB) */
typedef struct sysctrl_t {
    qemu_irq reset_irq;
    M48t59State *nvram;
    uint8_t state;
    uint8_t syscontrol;
    int contiguous_map;
    qemu_irq contiguous_map_irq;
    int endian;
} sysctrl_t;

enum {
    STATE_HARDFILE = 0x01,
};

static sysctrl_t *sysctrl;

static void PREP_io_800_writeb (void *opaque, uint32_t addr, uint32_t val)
{
    sysctrl_t *sysctrl = opaque;

    PPC_IO_DPRINTF("0x%08" PRIx32 " => 0x%02" PRIx32 "\n",
                   addr - PPC_IO_BASE, val);
    switch (addr) {
    case 0x0092:
        /* Special port 92 */
        /* Check soft reset asked */
        if (val & 0x01) {
            qemu_irq_raise(sysctrl->reset_irq);
        } else {
            qemu_irq_lower(sysctrl->reset_irq);
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
        qemu_set_irq(sysctrl->contiguous_map_irq, sysctrl->contiguous_map);
        break;
    default:
        printf("ERROR: unaffected IO port write: %04" PRIx32
               " => %02" PRIx32"\n", addr, val);
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
        retval = sysctrl->endian << 1;
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
        printf("ERROR: unaffected IO port: %04" PRIx32 " read\n", addr);
        break;
    }
    PPC_IO_DPRINTF("0x%08" PRIx32 " <= 0x%02" PRIx32 "\n",
                   addr - PPC_IO_BASE, retval);

    return retval;
}


#define NVRAM_SIZE        0x2000

static void cpu_request_exit(void *opaque, int irq, int level)
{
    CPUState *cpu = current_cpu;

    if (cpu && level) {
        cpu_exit(cpu);
    }
}

static void ppc_prep_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));

    /* Reset address */
    cpu->env.nip = 0xfffffffc;
}

static const MemoryRegionPortio prep_portio_list[] = {
    /* System control ports */
    { 0x0092, 1, 1, .read = PREP_io_800_readb, .write = PREP_io_800_writeb, },
    { 0x0800, 0x52, 1,
      .read = PREP_io_800_readb, .write = PREP_io_800_writeb, },
    /* Special port to get debug messages from Open-Firmware */
    { 0x0F00, 4, 1, .write = PPC_debug_write, },
    PORTIO_END_OF_LIST(),
};

static PortioList prep_port_list;

/* PowerPC PREP hardware initialisation */
static void ppc_prep_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    const char *boot_device = machine->boot_order;
    MemoryRegion *sysmem = get_system_memory();
    PowerPCCPU *cpu = NULL;
    CPUPPCState *env = NULL;
    nvram_t nvram;
    M48t59State *m48t59;
#if 0
    MemoryRegion *xcsr = g_new(MemoryRegion, 1);
#endif
    int linux_boot, i, nb_nics1;
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *vga = g_new(MemoryRegion, 1);
    uint32_t kernel_base, initrd_base;
    long kernel_size, initrd_size;
    DeviceState *dev;
    PCIHostState *pcihost;
    PCIBus *pci_bus;
    PCIDevice *pci;
    ISABus *isa_bus;
    ISADevice *isa;
    qemu_irq *cpu_exit_irq;
    int ppc_boot_device;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];

    sysctrl = g_malloc0(sizeof(sysctrl_t));

    linux_boot = (kernel_filename != NULL);

    /* init CPUs */
    if (cpu_model == NULL)
        cpu_model = "602";
    for (i = 0; i < smp_cpus; i++) {
        cpu = cpu_ppc_init(cpu_model);
        if (cpu == NULL) {
            fprintf(stderr, "Unable to find PowerPC CPU definition\n");
            exit(1);
        }
        env = &cpu->env;

        if (env->flags & POWERPC_FLAG_RTC_CLK) {
            /* POWER / PowerPC 601 RTC clock frequency is 7.8125 MHz */
            cpu_ppc_tb_init(env, 7812500UL);
        } else {
            /* Set time-base frequency to 100 Mhz */
            cpu_ppc_tb_init(env, 100UL * 1000UL * 1000UL);
        }
        qemu_register_reset(ppc_prep_reset, cpu);
    }

    /* allocate RAM */
    memory_region_init_ram(ram, NULL, "ppc_prep.ram", ram_size);
    vmstate_register_ram_global(ram);
    memory_region_add_subregion(sysmem, 0, ram);

    if (linux_boot) {
        kernel_base = KERNEL_LOAD_ADDR;
        /* now we can load the kernel */
        kernel_size = load_image_targphys(kernel_filename, kernel_base,
                                          ram_size - kernel_base);
        if (kernel_size < 0) {
            hw_error("qemu: could not load kernel '%s'\n", kernel_filename);
            exit(1);
        }
        /* load initrd */
        if (initrd_filename) {
            initrd_base = INITRD_LOAD_ADDR;
            initrd_size = load_image_targphys(initrd_filename, initrd_base,
                                              ram_size - initrd_base);
            if (initrd_size < 0) {
                hw_error("qemu: could not load initial ram disk '%s'\n",
                          initrd_filename);
            }
        } else {
            initrd_base = 0;
            initrd_size = 0;
        }
        ppc_boot_device = 'm';
    } else {
        kernel_base = 0;
        kernel_size = 0;
        initrd_base = 0;
        initrd_size = 0;
        ppc_boot_device = '\0';
        /* For now, OHW cannot boot from the network. */
        for (i = 0; boot_device[i] != '\0'; i++) {
            if (boot_device[i] >= 'a' && boot_device[i] <= 'f') {
                ppc_boot_device = boot_device[i];
                break;
            }
        }
        if (ppc_boot_device == '\0') {
            fprintf(stderr, "No valid boot device for Mac99 machine\n");
            exit(1);
        }
    }

    if (PPC_INPUT(env) != PPC_FLAGS_INPUT_6xx) {
        hw_error("Only 6xx bus is supported on PREP machine\n");
    }

    dev = qdev_create(NULL, "raven-pcihost");
    if (bios_name == NULL) {
        bios_name = BIOS_FILENAME;
    }
    qdev_prop_set_string(dev, "bios-name", bios_name);
    qdev_prop_set_uint32(dev, "elf-machine", ELF_MACHINE);
    pcihost = PCI_HOST_BRIDGE(dev);
    object_property_add_child(qdev_get_machine(), "raven", OBJECT(dev), NULL);
    qdev_init_nofail(dev);
    pci_bus = (PCIBus *)qdev_get_child_bus(dev, "pci.0");
    if (pci_bus == NULL) {
        fprintf(stderr, "Couldn't create PCI host controller.\n");
        exit(1);
    }
    sysctrl->contiguous_map_irq = qdev_get_gpio_in(dev, 0);

    /* PCI -> ISA bridge */
    pci = pci_create_simple(pci_bus, PCI_DEVFN(1, 0), "i82378");
    cpu_exit_irq = qemu_allocate_irqs(cpu_request_exit, NULL, 1);
    cpu = POWERPC_CPU(first_cpu);
    qdev_connect_gpio_out(&pci->qdev, 0,
                          cpu->env.irq_inputs[PPC6xx_INPUT_INT]);
    qdev_connect_gpio_out(&pci->qdev, 1, *cpu_exit_irq);
    sysbus_connect_irq(&pcihost->busdev, 0, qdev_get_gpio_in(&pci->qdev, 9));
    sysbus_connect_irq(&pcihost->busdev, 1, qdev_get_gpio_in(&pci->qdev, 11));
    sysbus_connect_irq(&pcihost->busdev, 2, qdev_get_gpio_in(&pci->qdev, 9));
    sysbus_connect_irq(&pcihost->busdev, 3, qdev_get_gpio_in(&pci->qdev, 11));
    isa_bus = ISA_BUS(qdev_get_child_bus(DEVICE(pci), "isa.0"));

    /* Super I/O (parallel + serial ports) */
    isa = isa_create(isa_bus, TYPE_PC87312);
    dev = DEVICE(isa);
    qdev_prop_set_uint8(dev, "config", 13); /* fdc, ser0, ser1, par0 */
    qdev_init_nofail(dev);

    /* init basic PC hardware */
    pci_vga_init(pci_bus);
    /* Open Hack'Ware hack: PCI BAR#0 is programmed to 0xf0000000.
     * While bios will access framebuffer at 0xf0000000, real physical
     * address is 0xf0000000 + 0xc0000000 (PCI memory base).
     * Alias the wrong memory accesses to the right place.
     */
    memory_region_init_alias(vga, NULL, "vga-alias", pci_address_space(pci),
                             0xf0000000, 0x1000000);
    memory_region_add_subregion_overlap(sysmem, 0xf0000000, vga, 10);

    nb_nics1 = nb_nics;
    if (nb_nics1 > NE2000_NB_MAX)
        nb_nics1 = NE2000_NB_MAX;
    for(i = 0; i < nb_nics1; i++) {
        if (nd_table[i].model == NULL) {
	    nd_table[i].model = g_strdup("ne2k_isa");
        }
        if (strcmp(nd_table[i].model, "ne2k_isa") == 0) {
            isa_ne2000_init(isa_bus, ne2000_io[i], ne2000_irq[i],
                            &nd_table[i]);
        } else {
            pci_nic_init_nofail(&nd_table[i], pci_bus, "ne2k_pci", NULL);
        }
    }

    ide_drive_get(hd, MAX_IDE_BUS);
    for(i = 0; i < MAX_IDE_BUS; i++) {
        isa_ide_init(isa_bus, ide_iobase[i], ide_iobase2[i], ide_irq[i],
                     hd[2 * i],
		     hd[2 * i + 1]);
    }
    isa_create_simple(isa_bus, "i8042");

    cpu = POWERPC_CPU(first_cpu);
    sysctrl->reset_irq = cpu->env.irq_inputs[PPC6xx_INPUT_HRESET];

    portio_list_init(&prep_port_list, NULL, prep_portio_list, sysctrl, "prep");
    portio_list_add(&prep_port_list, isa_address_space_io(isa), 0x0);

    /* PowerPC control and status register group */
#if 0
    memory_region_init_io(xcsr, NULL, &PPC_XCSR_ops, NULL, "ppc-xcsr", 0x1000);
    memory_region_add_subregion(sysmem, 0xFEFF0000, xcsr);
#endif

    if (usb_enabled(false)) {
        pci_create_simple(pci_bus, -1, "pci-ohci");
    }

    m48t59 = m48t59_init_isa(isa_bus, 0x0074, NVRAM_SIZE, 59);
    if (m48t59 == NULL)
        return;
    sysctrl->nvram = m48t59;

    /* Initialise NVRAM */
    nvram.opaque = m48t59;
    nvram.read_fn = &m48t59_read;
    nvram.write_fn = &m48t59_write;
    PPC_NVRAM_set_params(&nvram, NVRAM_SIZE, "PREP", ram_size, ppc_boot_device,
                         kernel_base, kernel_size,
                         kernel_cmdline,
                         initrd_base, initrd_size,
                         /* XXX: need an option to load a NVRAM image */
                         0,
                         graphic_width, graphic_height, graphic_depth);
}

static QEMUMachine prep_machine = {
    .name = "prep",
    .desc = "PowerPC PREP platform",
    .init = ppc_prep_init,
    .max_cpus = MAX_CPUS,
    .default_boot_order = "cad",
};

static void prep_machine_init(void)
{
    qemu_register_machine(&prep_machine);
}

machine_init(prep_machine_init);
