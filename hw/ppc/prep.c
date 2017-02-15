/*
 * QEMU PPC PREP hardware System Emulator
 *
 * Copyright (c) 2003-2007 Jocelyn Mayer
 * Copyright (c) 2017 Hervé Poussineau
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
#include "qemu/osdep.h"
#include "cpu.h"
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
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "hw/ide.h"
#include "hw/loader.h"
#include "hw/timer/mc146818rtc.h"
#include "hw/isa/pc87312.h"
#include "sysemu/block-backend.h"
#include "sysemu/arch_init.h"
#include "sysemu/kvm.h"
#include "sysemu/qtest.h"
#include "exec/address-spaces.h"
#include "trace.h"
#include "elf.h"
#include "qemu/cutils.h"
#include "kvm_ppc.h"

/* SMP is not enabled, for now */
#define MAX_CPUS 1

#define MAX_IDE_BUS 2

#define CFG_ADDR 0xf0000510

#define BIOS_SIZE (1024 * 1024)
#define BIOS_FILENAME "ppc_rom.bin"
#define KERNEL_LOAD_ADDR 0x01000000
#define INITRD_LOAD_ADDR 0x01800000

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
    Nvram *nvram;
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

    trace_prep_io_800_writeb(addr - PPC_IO_BASE, val);
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
        if (sysctrl->nvram != NULL) {
            NvramClass *k = NVRAM_GET_CLASS(sysctrl->nvram);
            (k->toggle_lock)(sysctrl->nvram, 1);
        }
        break;
    case 0x0812:
        /* Password protect 2 register */
        if (sysctrl->nvram != NULL) {
            NvramClass *k = NVRAM_GET_CLASS(sysctrl->nvram);
            (k->toggle_lock)(sysctrl->nvram, 2);
        }
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
    trace_prep_io_800_readb(addr - PPC_IO_BASE, retval);

    return retval;
}


#define NVRAM_SIZE        0x2000

static void fw_cfg_boot_set(void *opaque, const char *boot_device,
                            Error **errp)
{
    fw_cfg_modify_i16(opaque, FW_CFG_BOOT_DEVICE, boot_device[0]);
}

static void ppc_prep_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
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

/*****************************************************************************/
/* NVRAM helpers */
static inline uint32_t nvram_read(Nvram *nvram, uint32_t addr)
{
    NvramClass *k = NVRAM_GET_CLASS(nvram);
    return (k->read)(nvram, addr);
}

static inline void nvram_write(Nvram *nvram, uint32_t addr, uint32_t val)
{
    NvramClass *k = NVRAM_GET_CLASS(nvram);
    (k->write)(nvram, addr, val);
}

static void NVRAM_set_byte(Nvram *nvram, uint32_t addr, uint8_t value)
{
    nvram_write(nvram, addr, value);
}

static uint8_t NVRAM_get_byte(Nvram *nvram, uint32_t addr)
{
    return nvram_read(nvram, addr);
}

static void NVRAM_set_word(Nvram *nvram, uint32_t addr, uint16_t value)
{
    nvram_write(nvram, addr, value >> 8);
    nvram_write(nvram, addr + 1, value & 0xFF);
}

static uint16_t NVRAM_get_word(Nvram *nvram, uint32_t addr)
{
    uint16_t tmp;

    tmp = nvram_read(nvram, addr) << 8;
    tmp |= nvram_read(nvram, addr + 1);

    return tmp;
}

static void NVRAM_set_lword(Nvram *nvram, uint32_t addr, uint32_t value)
{
    nvram_write(nvram, addr, value >> 24);
    nvram_write(nvram, addr + 1, (value >> 16) & 0xFF);
    nvram_write(nvram, addr + 2, (value >> 8) & 0xFF);
    nvram_write(nvram, addr + 3, value & 0xFF);
}

static void NVRAM_set_string(Nvram *nvram, uint32_t addr, const char *str,
                             uint32_t max)
{
    int i;

    for (i = 0; i < max && str[i] != '\0'; i++) {
        nvram_write(nvram, addr + i, str[i]);
    }
    nvram_write(nvram, addr + i, str[i]);
    nvram_write(nvram, addr + max - 1, '\0');
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

static uint16_t NVRAM_compute_crc (Nvram *nvram, uint32_t start, uint32_t count)
{
    uint32_t i;
    uint16_t crc = 0xFFFF;
    int odd;

    odd = count & 1;
    count &= ~1;
    for (i = 0; i != count; i++) {
        crc = NVRAM_crc_update(crc, NVRAM_get_word(nvram, start + i));
    }
    if (odd) {
        crc = NVRAM_crc_update(crc, NVRAM_get_byte(nvram, start + i) << 8);
    }

    return crc;
}

#define CMDLINE_ADDR 0x017ff000

static int PPC_NVRAM_set_params (Nvram *nvram, uint16_t NVRAM_size,
                          const char *arch,
                          uint32_t RAM_size, int boot_device,
                          uint32_t kernel_image, uint32_t kernel_size,
                          const char *cmdline,
                          uint32_t initrd_image, uint32_t initrd_size,
                          uint32_t NVRAM_image,
                          int width, int height, int depth)
{
    uint16_t crc;

    /* Set parameters for Open Hack'Ware BIOS */
    NVRAM_set_string(nvram, 0x00, "QEMU_BIOS", 16);
    NVRAM_set_lword(nvram,  0x10, 0x00000002); /* structure v2 */
    NVRAM_set_word(nvram,   0x14, NVRAM_size);
    NVRAM_set_string(nvram, 0x20, arch, 16);
    NVRAM_set_lword(nvram,  0x30, RAM_size);
    NVRAM_set_byte(nvram,   0x34, boot_device);
    NVRAM_set_lword(nvram,  0x38, kernel_image);
    NVRAM_set_lword(nvram,  0x3C, kernel_size);
    if (cmdline) {
        /* XXX: put the cmdline in NVRAM too ? */
        pstrcpy_targphys("cmdline", CMDLINE_ADDR, RAM_size - CMDLINE_ADDR,
                         cmdline);
        NVRAM_set_lword(nvram,  0x40, CMDLINE_ADDR);
        NVRAM_set_lword(nvram,  0x44, strlen(cmdline));
    } else {
        NVRAM_set_lword(nvram,  0x40, 0);
        NVRAM_set_lword(nvram,  0x44, 0);
    }
    NVRAM_set_lword(nvram,  0x48, initrd_image);
    NVRAM_set_lword(nvram,  0x4C, initrd_size);
    NVRAM_set_lword(nvram,  0x50, NVRAM_image);

    NVRAM_set_word(nvram,   0x54, width);
    NVRAM_set_word(nvram,   0x56, height);
    NVRAM_set_word(nvram,   0x58, depth);
    crc = NVRAM_compute_crc(nvram, 0x00, 0xF8);
    NVRAM_set_word(nvram,   0xFC, crc);

    return 0;
}

/* PowerPC PREP hardware initialisation */
static void ppc_prep_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    const char *boot_device = machine->boot_order;
    MemoryRegion *sysmem = get_system_memory();
    PowerPCCPU *cpu = NULL;
    CPUPPCState *env = NULL;
    Nvram *m48t59;
#if 0
    MemoryRegion *xcsr = g_new(MemoryRegion, 1);
#endif
    int linux_boot, i, nb_nics1;
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    uint32_t kernel_base, initrd_base;
    long kernel_size, initrd_size;
    DeviceState *dev;
    PCIHostState *pcihost;
    PCIBus *pci_bus;
    PCIDevice *pci;
    ISABus *isa_bus;
    ISADevice *isa;
    int ppc_boot_device;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];

    sysctrl = g_malloc0(sizeof(sysctrl_t));

    linux_boot = (kernel_filename != NULL);

    /* init CPUs */
    if (machine->cpu_model == NULL)
        machine->cpu_model = "602";
    for (i = 0; i < smp_cpus; i++) {
        cpu = cpu_ppc_init(machine->cpu_model);
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
    memory_region_allocate_system_memory(ram, NULL, "ppc_prep.ram", ram_size);
    memory_region_add_subregion(sysmem, 0, ram);

    if (linux_boot) {
        kernel_base = KERNEL_LOAD_ADDR;
        /* now we can load the kernel */
        kernel_size = load_image_targphys(kernel_filename, kernel_base,
                                          ram_size - kernel_base);
        if (kernel_size < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
            exit(1);
        }
        /* load initrd */
        if (initrd_filename) {
            initrd_base = INITRD_LOAD_ADDR;
            initrd_size = load_image_targphys(initrd_filename, initrd_base,
                                              ram_size - initrd_base);
            if (initrd_size < 0) {
                error_report("could not load initial ram disk '%s'",
                             initrd_filename);
                exit(1);
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
        error_report("Only 6xx bus is supported on PREP machine");
        exit(1);
    }

    dev = qdev_create(NULL, "raven-pcihost");
    if (bios_name == NULL) {
        bios_name = BIOS_FILENAME;
    }
    qdev_prop_set_string(dev, "bios-name", bios_name);
    qdev_prop_set_uint32(dev, "elf-machine", PPC_ELF_MACHINE);
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
    cpu = POWERPC_CPU(first_cpu);
    qdev_connect_gpio_out(&pci->qdev, 0,
                          cpu->env.irq_inputs[PPC6xx_INPUT_INT]);
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

    ide_drive_get(hd, ARRAY_SIZE(hd));
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

    if (machine_usb(machine)) {
        pci_create_simple(pci_bus, -1, "pci-ohci");
    }

    m48t59 = m48t59_init_isa(isa_bus, 0x0074, NVRAM_SIZE, 2000, 59);
    if (m48t59 == NULL)
        return;
    sysctrl->nvram = m48t59;

    /* Initialise NVRAM */
    PPC_NVRAM_set_params(m48t59, NVRAM_SIZE, "PREP", ram_size,
                         ppc_boot_device,
                         kernel_base, kernel_size,
                         kernel_cmdline,
                         initrd_base, initrd_size,
                         /* XXX: need an option to load a NVRAM image */
                         0,
                         graphic_width, graphic_height, graphic_depth);
}

static void prep_machine_init(MachineClass *mc)
{
    mc->desc = "PowerPC PREP platform";
    mc->init = ppc_prep_init;
    mc->block_default_type = IF_IDE;
    mc->max_cpus = MAX_CPUS;
    mc->default_boot_order = "cad";
}

static int prep_set_cmos_checksum(DeviceState *dev, void *opaque)
{
    uint16_t checksum = *(uint16_t *)opaque;
    ISADevice *rtc;

    if (object_dynamic_cast(OBJECT(dev), "mc146818rtc")) {
        rtc = ISA_DEVICE(dev);
        rtc_set_memory(rtc, 0x2e, checksum & 0xff);
        rtc_set_memory(rtc, 0x3e, checksum & 0xff);
        rtc_set_memory(rtc, 0x2f, checksum >> 8);
        rtc_set_memory(rtc, 0x3f, checksum >> 8);
    }
    return 0;
}

static void ibm_40p_init(MachineState *machine)
{
    CPUPPCState *env = NULL;
    uint16_t cmos_checksum;
    PowerPCCPU *cpu;
    DeviceState *dev;
    SysBusDevice *pcihost;
    Nvram *m48t59 = NULL;
    PCIBus *pci_bus;
    ISABus *isa_bus;
    void *fw_cfg;
    int i;
    uint32_t kernel_base = 0, initrd_base = 0;
    long kernel_size = 0, initrd_size = 0;
    char boot_device;

    /* init CPU */
    if (!machine->cpu_model) {
        machine->cpu_model = "604";
    }
    cpu = cpu_ppc_init(machine->cpu_model);
    if (!cpu) {
        error_report("could not initialize CPU '%s'",
                     machine->cpu_model);
        exit(1);
    }
    env = &cpu->env;
    if (PPC_INPUT(env) != PPC_FLAGS_INPUT_6xx) {
        error_report("only 6xx bus is supported on this machine");
        exit(1);
    }

    if (env->flags & POWERPC_FLAG_RTC_CLK) {
        /* POWER / PowerPC 601 RTC clock frequency is 7.8125 MHz */
        cpu_ppc_tb_init(env, 7812500UL);
    } else {
        /* Set time-base frequency to 100 Mhz */
        cpu_ppc_tb_init(env, 100UL * 1000UL * 1000UL);
    }
    qemu_register_reset(ppc_prep_reset, cpu);

    /* PCI host */
    dev = qdev_create(NULL, "raven-pcihost");
    if (!bios_name) {
        bios_name = BIOS_FILENAME;
    }
    qdev_prop_set_string(dev, "bios-name", bios_name);
    qdev_prop_set_uint32(dev, "elf-machine", PPC_ELF_MACHINE);
    pcihost = SYS_BUS_DEVICE(dev);
    object_property_add_child(qdev_get_machine(), "raven", OBJECT(dev), NULL);
    qdev_init_nofail(dev);
    pci_bus = PCI_BUS(qdev_get_child_bus(dev, "pci.0"));
    if (!pci_bus) {
        error_report("could not create PCI host controller");
        exit(1);
    }

    /* PCI -> ISA bridge */
    dev = DEVICE(pci_create_simple(pci_bus, PCI_DEVFN(11, 0), "i82378"));
    qdev_connect_gpio_out(dev, 0,
                          cpu->env.irq_inputs[PPC6xx_INPUT_INT]);
    sysbus_connect_irq(pcihost, 0, qdev_get_gpio_in(dev, 15));
    sysbus_connect_irq(pcihost, 1, qdev_get_gpio_in(dev, 13));
    sysbus_connect_irq(pcihost, 2, qdev_get_gpio_in(dev, 15));
    sysbus_connect_irq(pcihost, 3, qdev_get_gpio_in(dev, 13));
    isa_bus = ISA_BUS(qdev_get_child_bus(dev, "isa.0"));

    /* Memory controller */
    dev = DEVICE(isa_create(isa_bus, "rs6000-mc"));
    qdev_prop_set_uint32(dev, "ram-size", machine->ram_size);
    qdev_init_nofail(dev);

    /* initialize CMOS checksums */
    cmos_checksum = 0x6aa9;
    qbus_walk_children(BUS(isa_bus), prep_set_cmos_checksum, NULL, NULL, NULL,
                       &cmos_checksum);

    /* initialize audio subsystem */
    audio_init();

    /* add some more devices */
    if (defaults_enabled()) {
        isa_create_simple(isa_bus, "i8042");
        m48t59 = NVRAM(isa_create_simple(isa_bus, "isa-m48t59"));

        dev = DEVICE(isa_create(isa_bus, "cs4231a"));
        qdev_prop_set_uint32(dev, "iobase", 0x830);
        qdev_prop_set_uint32(dev, "irq", 10);
        qdev_init_nofail(dev);

        dev = DEVICE(isa_create(isa_bus, "pc87312"));
        qdev_prop_set_uint32(dev, "config", 12);
        qdev_init_nofail(dev);

        dev = DEVICE(isa_create(isa_bus, "prep-systemio"));
        qdev_prop_set_uint32(dev, "ibm-planar-id", 0xfc);
        qdev_prop_set_uint32(dev, "equipment", 0xc0);
        qdev_init_nofail(dev);

        pci_create_simple(pci_bus, PCI_DEVFN(1, 0), "lsi53c810");

        /* XXX: s3-trio at PCI_DEVFN(2, 0) */
        pci_vga_init(pci_bus);

        for (i = 0; i < nb_nics; i++) {
            pci_nic_init_nofail(&nd_table[i], pci_bus, "pcnet",
                                i == 0 ? "3" : NULL);
        }
    }

    /* Prepare firmware configuration for OpenBIOS */
    fw_cfg = fw_cfg_init_mem(CFG_ADDR, CFG_ADDR + 2);

    if (machine->kernel_filename) {
        /* load kernel */
        kernel_base = KERNEL_LOAD_ADDR;
        kernel_size = load_image_targphys(machine->kernel_filename,
                                          kernel_base,
                                          machine->ram_size - kernel_base);
        if (kernel_size < 0) {
            error_report("could not load kernel '%s'",
                         machine->kernel_filename);
            exit(1);
        }
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ADDR, kernel_base);
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_SIZE, kernel_size);
        /* load initrd */
        if (machine->initrd_filename) {
            initrd_base = INITRD_LOAD_ADDR;
            initrd_size = load_image_targphys(machine->initrd_filename,
                                              initrd_base,
                                              machine->ram_size - initrd_base);
            if (initrd_size < 0) {
                error_report("could not load initial ram disk '%s'",
                             machine->initrd_filename);
                exit(1);
            }
            fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_ADDR, initrd_base);
            fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_SIZE, initrd_size);
        }
        if (machine->kernel_cmdline && *machine->kernel_cmdline) {
            fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, CMDLINE_ADDR);
            pstrcpy_targphys("cmdline", CMDLINE_ADDR, TARGET_PAGE_SIZE,
                             machine->kernel_cmdline);
            fw_cfg_add_string(fw_cfg, FW_CFG_CMDLINE_DATA,
                              machine->kernel_cmdline);
            fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE,
                           strlen(machine->kernel_cmdline) + 1);
        }
        boot_device = 'm';
    } else {
        boot_device = machine->boot_order[0];
    }

    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, (uint16_t)max_cpus);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)machine->ram_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MACHINE_ID, ARCH_PREP);

    fw_cfg_add_i16(fw_cfg, FW_CFG_PPC_WIDTH, graphic_width);
    fw_cfg_add_i16(fw_cfg, FW_CFG_PPC_HEIGHT, graphic_height);
    fw_cfg_add_i16(fw_cfg, FW_CFG_PPC_DEPTH, graphic_depth);

    fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_IS_KVM, kvm_enabled());
    if (kvm_enabled()) {
#ifdef CONFIG_KVM
        uint8_t *hypercall;

        fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_TBFREQ, kvmppc_get_tbfreq());
        hypercall = g_malloc(16);
        kvmppc_get_hypercall(env, hypercall, 16);
        fw_cfg_add_bytes(fw_cfg, FW_CFG_PPC_KVM_HC, hypercall, 16);
        fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_KVM_PID, getpid());
#endif
    } else {
        fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_TBFREQ, NANOSECONDS_PER_SECOND);
    }
    fw_cfg_add_i16(fw_cfg, FW_CFG_BOOT_DEVICE, boot_device);
    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);

    /* Prepare firmware configuration for Open Hack'Ware */
    if (m48t59) {
        PPC_NVRAM_set_params(m48t59, NVRAM_SIZE, "PREP", ram_size,
                             boot_device,
                             kernel_base, kernel_size,
                             machine->kernel_cmdline,
                             initrd_base, initrd_size,
                             /* XXX: need an option to load a NVRAM image */
                             0,
                             graphic_width, graphic_height, graphic_depth);
    }
}

static void ibm_40p_machine_init(MachineClass *mc)
{
    mc->desc = "IBM RS/6000 7020 (40p)",
    mc->init = ibm_40p_init;
    mc->max_cpus = 1;
    mc->pci_allow_0_address = true;
    mc->default_ram_size = 128 * M_BYTE;
    mc->block_default_type = IF_SCSI;
    mc->default_boot_order = "c";
}

DEFINE_MACHINE("40p", ibm_40p_machine_init)
DEFINE_MACHINE("prep", prep_machine_init)
