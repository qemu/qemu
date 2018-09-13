/*
 * QEMU fulong 2e mini pc support
 *
 * Copyright (c) 2008 yajin (yajin@vm-kernel.org)
 * Copyright (c) 2009 chenming (chenming@rdc.faw.com.cn)
 * Copyright (c) 2010 Huacai Chen (zltjiangshi@gmail.com)
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

/*
 * Fulong 2e mini pc is based on ICT/ST Loongson 2e CPU (MIPS III like, 800MHz)
 * http://www.linux-mips.org/wiki/Fulong
 *
 * Loongson 2e user manual:
 * http://www.loongsondeveloper.com/doc/Loongson2EUserGuide.pdf
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/dma/i8257.h"
#include "hw/isa/superio.h"
#include "net/net.h"
#include "hw/boards.h"
#include "hw/i2c/smbus.h"
#include "hw/block/flash.h"
#include "hw/mips/mips.h"
#include "hw/mips/cpudevs.h"
#include "hw/pci/pci.h"
#include "audio/audio.h"
#include "qemu/log.h"
#include "hw/loader.h"
#include "hw/mips/bios.h"
#include "hw/ide.h"
#include "elf.h"
#include "hw/isa/vt82c686.h"
#include "hw/timer/mc146818rtc.h"
#include "hw/timer/i8254.h"
#include "exec/address-spaces.h"
#include "sysemu/qtest.h"
#include "qemu/error-report.h"

#define DEBUG_FULONG2E_INIT

#define ENVP_ADDR       0x80002000l
#define ENVP_NB_ENTRIES	 	16
#define ENVP_ENTRY_SIZE	 	256

#define MAX_IDE_BUS 2

/*
 * PMON is not part of qemu and released with BSD license, anyone
 * who want to build a pmon binary please first git-clone the source
 * from the git repository at:
 * http://www.loongson.cn/support/git/pmon
 * Then follow the "Compile Guide" available at:
 * http://dev.lemote.com/code/pmon
 *
 * Notes:
 * 1, don't use the source at http://dev.lemote.com/http_git/pmon.git
 * 2, use "Bonito2edev" to replace "dir_corresponding_to_your_target_hardware"
 * in the "Compile Guide".
 */
#define FULONG_BIOSNAME "pmon_fulong2e.bin"

/* PCI SLOT in fulong 2e */
#define FULONG2E_VIA_SLOT        5
#define FULONG2E_ATI_SLOT        6
#define FULONG2E_RTL8139_SLOT    7

static struct _loaderparams {
    int ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
} loaderparams;

static void GCC_FMT_ATTR(3, 4) prom_set(uint32_t* prom_buf, int index,
                                        const char *string, ...)
{
    va_list ap;
    int32_t table_addr;

    if (index >= ENVP_NB_ENTRIES)
        return;

    if (string == NULL) {
        prom_buf[index] = 0;
        return;
    }

    table_addr = sizeof(int32_t) * ENVP_NB_ENTRIES + index * ENVP_ENTRY_SIZE;
    prom_buf[index] = tswap32(ENVP_ADDR + table_addr);

    va_start(ap, string);
    vsnprintf((char *)prom_buf + table_addr, ENVP_ENTRY_SIZE, string, ap);
    va_end(ap);
}

static int64_t load_kernel (CPUMIPSState *env)
{
    int64_t kernel_entry, kernel_low, kernel_high, initrd_size;
    int index = 0;
    long kernel_size;
    ram_addr_t initrd_offset;
    uint32_t *prom_buf;
    long prom_size;

    kernel_size = load_elf(loaderparams.kernel_filename, cpu_mips_kseg0_to_phys,
                           NULL, (uint64_t *)&kernel_entry,
                           (uint64_t *)&kernel_low, (uint64_t *)&kernel_high,
                           0, EM_MIPS, 1, 0);
    if (kernel_size < 0) {
        error_report("could not load kernel '%s': %s",
                     loaderparams.kernel_filename,
                     load_elf_strerror(kernel_size));
        exit(1);
    }

    /* load initrd */
    initrd_size = 0;
    initrd_offset = 0;
    if (loaderparams.initrd_filename) {
        initrd_size = get_image_size (loaderparams.initrd_filename);
        if (initrd_size > 0) {
            initrd_offset = (kernel_high + ~INITRD_PAGE_MASK) & INITRD_PAGE_MASK;
            if (initrd_offset + initrd_size > ram_size) {
                error_report("memory too small for initial ram disk '%s'",
                             loaderparams.initrd_filename);
                exit(1);
            }
            initrd_size = load_image_targphys(loaderparams.initrd_filename,
                                     initrd_offset, ram_size - initrd_offset);
        }
        if (initrd_size == (target_ulong) -1) {
            error_report("could not load initial ram disk '%s'",
                         loaderparams.initrd_filename);
            exit(1);
        }
    }

    /* Setup prom parameters. */
    prom_size = ENVP_NB_ENTRIES * (sizeof(int32_t) + ENVP_ENTRY_SIZE);
    prom_buf = g_malloc(prom_size);

    prom_set(prom_buf, index++, "%s", loaderparams.kernel_filename);
    if (initrd_size > 0) {
        prom_set(prom_buf, index++, "rd_start=0x%" PRIx64 " rd_size=%" PRId64 " %s",
                 cpu_mips_phys_to_kseg0(NULL, initrd_offset), initrd_size,
                 loaderparams.kernel_cmdline);
    } else {
        prom_set(prom_buf, index++, "%s", loaderparams.kernel_cmdline);
    }

    /* Setup minimum environment variables */
    prom_set(prom_buf, index++, "busclock=33000000");
    prom_set(prom_buf, index++, "cpuclock=100000000");
    prom_set(prom_buf, index++, "memsize=%"PRIi64, loaderparams.ram_size / MiB);
    prom_set(prom_buf, index++, "modetty0=38400n8r");
    prom_set(prom_buf, index++, NULL);

    rom_add_blob_fixed("prom", prom_buf, prom_size,
                       cpu_mips_kseg0_to_phys(NULL, ENVP_ADDR));

    g_free(prom_buf);
    return kernel_entry;
}

static void write_bootloader (CPUMIPSState *env, uint8_t *base, int64_t kernel_addr)
{
    uint32_t *p;

    /* Small bootloader */
    p = (uint32_t *) base;

    stl_p(p++, 0x0bf00010);                                      /* j 0x1fc00040 */
    stl_p(p++, 0x00000000);                                      /* nop */

    /* Second part of the bootloader */
    p = (uint32_t *) (base + 0x040);

    stl_p(p++, 0x3c040000);                                      /* lui a0, 0 */
    stl_p(p++, 0x34840002);                                      /* ori a0, a0, 2 */
    stl_p(p++, 0x3c050000 | ((ENVP_ADDR >> 16) & 0xffff));       /* lui a1, high(ENVP_ADDR) */
    stl_p(p++, 0x34a50000 | (ENVP_ADDR & 0xffff));               /* ori a1, a0, low(ENVP_ADDR) */
    stl_p(p++, 0x3c060000 | (((ENVP_ADDR + 8) >> 16) & 0xffff)); /* lui a2, high(ENVP_ADDR + 8) */
    stl_p(p++, 0x34c60000 | ((ENVP_ADDR + 8) & 0xffff));         /* ori a2, a2, low(ENVP_ADDR + 8) */
    stl_p(p++, 0x3c070000 | (loaderparams.ram_size >> 16));      /* lui a3, high(env->ram_size) */
    stl_p(p++, 0x34e70000 | (loaderparams.ram_size & 0xffff));   /* ori a3, a3, low(env->ram_size) */
    stl_p(p++, 0x3c1f0000 | ((kernel_addr >> 16) & 0xffff));     /* lui ra, high(kernel_addr) */;
    stl_p(p++, 0x37ff0000 | (kernel_addr & 0xffff));             /* ori ra, ra, low(kernel_addr) */
    stl_p(p++, 0x03e00008);                                      /* jr ra */
    stl_p(p++, 0x00000000);                                      /* nop */
}


static void main_cpu_reset(void *opaque)
{
    MIPSCPU *cpu = opaque;
    CPUMIPSState *env = &cpu->env;

    cpu_reset(CPU(cpu));
    /* TODO: 2E reset stuff */
    if (loaderparams.kernel_filename) {
        env->CP0_Status &= ~((1 << CP0St_BEV) | (1 << CP0St_ERL));
    }
}

static const uint8_t eeprom_spd[0x80] = {
    0x80,0x08,0x07,0x0d,0x09,0x02,0x40,0x00,0x04,0x70,
    0x70,0x00,0x82,0x10,0x00,0x01,0x0e,0x04,0x0c,0x01,
    0x02,0x20,0x80,0x75,0x70,0x00,0x00,0x50,0x3c,0x50,
    0x2d,0x20,0xb0,0xb0,0x50,0x50,0x00,0x00,0x00,0x00,
    0x00,0x41,0x48,0x3c,0x32,0x75,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x9c,0x7b,0x07,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x48,0x42,0x35,0x34,0x41,0x32,
    0x35,0x36,0x38,0x4b,0x4e,0x2d,0x41,0x37,0x35,0x42,
    0x20,0x30,0x20
};

static void vt82c686b_southbridge_init(PCIBus *pci_bus, int slot, qemu_irq intc,
                                       I2CBus **i2c_bus, ISABus **p_isa_bus)
{
    qemu_irq *i8259;
    ISABus *isa_bus;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];

    isa_bus = vt82c686b_isa_init(pci_bus, PCI_DEVFN(slot, 0));
    if (!isa_bus) {
        fprintf(stderr, "vt82c686b_init error\n");
        exit(1);
    }
    *p_isa_bus = isa_bus;
    /* Interrupt controller */
    /* The 8259 -> IP5  */
    i8259 = i8259_init(isa_bus, intc);
    isa_bus_irqs(isa_bus, i8259);
    /* init other devices */
    i8254_pit_init(isa_bus, 0x40, 0, NULL);
    i8257_dma_init(isa_bus, 0);
    /* Super I/O */
    isa_create_simple(isa_bus, TYPE_VT82C686B_SUPERIO);

    ide_drive_get(hd, ARRAY_SIZE(hd));
    vt82c686b_ide_init(pci_bus, hd, PCI_DEVFN(slot, 1));

    pci_create_simple(pci_bus, PCI_DEVFN(slot, 2), "vt82c686b-usb-uhci");
    pci_create_simple(pci_bus, PCI_DEVFN(slot, 3), "vt82c686b-usb-uhci");

    *i2c_bus = vt82c686b_pm_init(pci_bus, PCI_DEVFN(slot, 4), 0xeee1, NULL);

    /* Audio support */
    vt82c686b_ac97_init(pci_bus, PCI_DEVFN(slot, 5));
    vt82c686b_mc97_init(pci_bus, PCI_DEVFN(slot, 6));
}

/* Network support */
static void network_init (PCIBus *pci_bus)
{
    int i;

    for(i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];
        const char *default_devaddr = NULL;

        if (i == 0 && (!nd->model || strcmp(nd->model, "rtl8139") == 0)) {
            /* The fulong board has a RTL8139 card using PCI SLOT 7 */
            default_devaddr = "07";
        }

        pci_nic_init_nofail(nd, pci_bus, "rtl8139", default_devaddr);
    }
}

static void mips_fulong2e_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    char *filename;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *bios = g_new(MemoryRegion, 1);
    long bios_size;
    int64_t kernel_entry;
    PCIBus *pci_bus;
    ISABus *isa_bus;
    I2CBus *smbus;
    MIPSCPU *cpu;
    CPUMIPSState *env;

    /* init CPUs */
    cpu = MIPS_CPU(cpu_create(machine->cpu_type));
    env = &cpu->env;

    qemu_register_reset(main_cpu_reset, cpu);

    /* fulong 2e has 256M ram. */
    ram_size = 256 * MiB;

    /* fulong 2e has a 1M flash.Winbond W39L040AP70Z */
    bios_size = 1 * MiB;

    /* allocate RAM */
    memory_region_allocate_system_memory(ram, NULL, "fulong2e.ram", ram_size);
    memory_region_init_ram(bios, NULL, "fulong2e.bios", bios_size,
                           &error_fatal);
    memory_region_set_readonly(bios, true);

    memory_region_add_subregion(address_space_mem, 0, ram);
    memory_region_add_subregion(address_space_mem, 0x1fc00000LL, bios);

    /* We do not support flash operation, just loading pmon.bin as raw BIOS.
     * Please use -L to set the BIOS path and -bios to set bios name. */

    if (kernel_filename) {
        loaderparams.ram_size = ram_size;
        loaderparams.kernel_filename = kernel_filename;
        loaderparams.kernel_cmdline = kernel_cmdline;
        loaderparams.initrd_filename = initrd_filename;
        kernel_entry = load_kernel (env);
        write_bootloader(env, memory_region_get_ram_ptr(bios), kernel_entry);
    } else {
        if (bios_name == NULL) {
                bios_name = FULONG_BIOSNAME;
        }
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
        if (filename) {
            bios_size = load_image_targphys(filename, 0x1fc00000LL,
                                            BIOS_SIZE);
            g_free(filename);
        } else {
            bios_size = -1;
        }

        if ((bios_size < 0 || bios_size > BIOS_SIZE) &&
            !kernel_filename && !qtest_enabled()) {
            error_report("Could not load MIPS bios '%s'", bios_name);
            exit(1);
        }
    }

    /* Init internal devices */
    cpu_mips_irq_init_cpu(cpu);
    cpu_mips_clock_init(cpu);

    /* North bridge, Bonito --> IP2 */
    pci_bus = bonito_init((qemu_irq *)&(env->irq[2]));

    /* South bridge -> IP5 */
    vt82c686b_southbridge_init(pci_bus, FULONG2E_VIA_SLOT, env->irq[5],
                               &smbus, &isa_bus);

    /* TODO: Populate SPD eeprom data.  */
    smbus_eeprom_init(smbus, 1, eeprom_spd, sizeof(eeprom_spd));

    mc146818_rtc_init(isa_bus, 2000, NULL);

    /* Network card: RTL8139D */
    network_init(pci_bus);
}

static void mips_fulong2e_machine_init(MachineClass *mc)
{
    mc->desc = "Fulong 2e mini pc";
    mc->init = mips_fulong2e_init;
    mc->block_default_type = IF_IDE;
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("Loongson-2E");
}

DEFINE_MACHINE("fulong2e", mips_fulong2e_machine_init)
