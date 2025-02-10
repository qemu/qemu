/*
 * QEMU fuloong 2e mini pc support
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
 * Fuloong 2e mini pc is based on ICT/ST Loongson 2e CPU (MIPS III like, 800MHz)
 * https://www.linux-mips.org/wiki/Fuloong_2E
 *
 * Loongson 2e manuals:
 * https://github.com/loongson-community/docs/tree/master/2E
 */

#include "qemu/osdep.h"
#include "qemu/datadir.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/clock.h"
#include "net/net.h"
#include "hw/boards.h"
#include "hw/i2c/smbus_eeprom.h"
#include "hw/block/flash.h"
#include "hw/mips/mips.h"
#include "hw/mips/bootloader.h"
#include "hw/pci/pci.h"
#include "hw/loader.h"
#include "hw/ide/pci.h"
#include "hw/qdev-properties.h"
#include "elf.h"
#include "hw/isa/vt82c686.h"
#include "system/qtest.h"
#include "system/reset.h"
#include "system/system.h"
#include "qemu/error-report.h"
#include "exec/tswap.h"

#define ENVP_PADDR              0x2000
#define ENVP_VADDR              cpu_mips_phys_to_kseg0(NULL, ENVP_PADDR)
#define ENVP_NB_ENTRIES         16
#define ENVP_ENTRY_SIZE         256

/* Fuloong 2e has a 512k flash: Winbond W39L040AP70Z */
#define BIOS_SIZE               (512 * KiB)

/*
 * PMON is not part of qemu and released with BSD license, anyone
 * who want to build a pmon binary please first git-clone the source
 * from the git repository at:
 * https://github.com/loongson-community/pmon
 */
#define FULOONG_BIOSNAME "pmon_2e.bin"

/* PCI SLOT in Fuloong 2e */
#define FULOONG2E_VIA_SLOT        5
#define FULOONG2E_ATI_SLOT        6
#define FULOONG2E_RTL8139_SLOT    7

static struct _loaderparams {
    int ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
} loaderparams;

static void G_GNUC_PRINTF(3, 4) prom_set(uint32_t *prom_buf, int index,
                                        const char *string, ...)
{
    va_list ap;
    int32_t table_addr;

    if (index >= ENVP_NB_ENTRIES) {
        return;
    }

    if (string == NULL) {
        prom_buf[index] = 0;
        return;
    }

    table_addr = sizeof(int32_t) * ENVP_NB_ENTRIES + index * ENVP_ENTRY_SIZE;
    prom_buf[index] = tswap32(ENVP_VADDR + table_addr);

    va_start(ap, string);
    vsnprintf((char *)prom_buf + table_addr, ENVP_ENTRY_SIZE, string, ap);
    va_end(ap);
}

static uint64_t load_kernel(MIPSCPU *cpu)
{
    uint64_t kernel_entry, kernel_high, initrd_size;
    int index = 0;
    long kernel_size;
    ram_addr_t initrd_offset;
    uint32_t *prom_buf;
    long prom_size;

    kernel_size = load_elf(loaderparams.kernel_filename, NULL,
                           cpu_mips_kseg0_to_phys, NULL,
                           &kernel_entry, NULL,
                           &kernel_high, NULL,
                           ELFDATA2LSB, EM_MIPS, 1, 0);
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
        initrd_size = get_image_size(loaderparams.initrd_filename);
        if (initrd_size > 0) {
            initrd_offset = ROUND_UP(kernel_high, INITRD_PAGE_SIZE);
            if (initrd_offset + initrd_size > loaderparams.ram_size) {
                error_report("memory too small for initial ram disk '%s'",
                             loaderparams.initrd_filename);
                exit(1);
            }
            initrd_size = load_image_targphys(loaderparams.initrd_filename,
                                              initrd_offset,
                                              loaderparams.ram_size - initrd_offset);
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
        prom_set(prom_buf, index++,
                 "rd_start=0x%" PRIx64 " rd_size=%" PRId64 " %s",
                 cpu_mips_phys_to_kseg0(NULL, initrd_offset),
                 initrd_size, loaderparams.kernel_cmdline);
    } else {
        prom_set(prom_buf, index++, "%s", loaderparams.kernel_cmdline);
    }

    /* Setup minimum environment variables */
    prom_set(prom_buf, index++, "busclock=33000000");
    prom_set(prom_buf, index++, "cpuclock=%u", clock_get_hz(cpu->clock));
    prom_set(prom_buf, index++, "memsize=%"PRIi64, loaderparams.ram_size / MiB);
    prom_set(prom_buf, index++, NULL);

    rom_add_blob_fixed("prom", prom_buf, prom_size, ENVP_PADDR);

    g_free(prom_buf);
    return kernel_entry;
}

static void write_bootloader(CPUMIPSState *env, uint8_t *base,
                             uint64_t kernel_addr)
{
    uint32_t *p;

    /* Small bootloader */
    p = (uint32_t *)base;

    /* j 0x1fc00040 */
    stl_p(p++, 0x0bf00010);
    /* nop */
    stl_p(p++, 0x00000000);

    /* Second part of the bootloader */
    p = (uint32_t *)(base + 0x040);

    bl_gen_jump_kernel((void **)&p,
                       true, ENVP_VADDR - 64,
                       true, 2, true, ENVP_VADDR,
                       true, ENVP_VADDR + 8,
                       true, loaderparams.ram_size,
                       kernel_addr);
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

/* Network support */
static void network_init(PCIBus *pci_bus)
{
    /* The Fuloong board has a RTL8139 card using PCI SLOT 7 */
    pci_init_nic_in_slot(pci_bus, "rtl8139", NULL, "07");
    pci_init_nic_devices(pci_bus, "rtl8139");
}

static void mips_fuloong2e_init(MachineState *machine)
{
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    char *filename;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *bios = g_new(MemoryRegion, 1);
    long bios_size;
    uint8_t *spd_data;
    uint64_t kernel_entry;
    PCIDevice *pci_dev;
    PCIBus *pci_bus;
    I2CBus *smbus;
    Clock *cpuclk;
    MIPSCPU *cpu;
    CPUMIPSState *env;
    DeviceState *dev;

    cpuclk = clock_new(OBJECT(machine), "cpu-refclk");
    clock_set_hz(cpuclk, 533080000); /* ~533 MHz */

    /* init CPUs */
    cpu = mips_cpu_create_with_clock(machine->cpu_type, cpuclk, false);
    env = &cpu->env;

    qemu_register_reset(main_cpu_reset, cpu);

    /* TODO: support more than 256M RAM as highmem */
    if (machine->ram_size != 256 * MiB) {
        error_report("Invalid RAM size, should be 256MB");
        exit(EXIT_FAILURE);
    }
    memory_region_add_subregion(address_space_mem, 0, machine->ram);

    /* Boot ROM */
    memory_region_init_rom(bios, NULL, "fuloong2e.bios", BIOS_SIZE,
                           &error_fatal);
    memory_region_add_subregion(address_space_mem, 0x1fc00000LL, bios);

    /*
     * We do not support flash operation, just loading pmon.bin as raw BIOS.
     * Please use -L to set the BIOS path and -bios to set bios name.
     */

    if (kernel_filename) {
        loaderparams.ram_size = machine->ram_size;
        loaderparams.kernel_filename = kernel_filename;
        loaderparams.kernel_cmdline = kernel_cmdline;
        loaderparams.initrd_filename = initrd_filename;
        kernel_entry = load_kernel(cpu);
        write_bootloader(env, memory_region_get_ram_ptr(bios), kernel_entry);
    } else {
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS,
                                  machine->firmware ?: FULOONG_BIOSNAME);
        if (filename) {
            bios_size = load_image_targphys(filename, 0x1fc00000LL,
                                            BIOS_SIZE);
            g_free(filename);
        } else {
            bios_size = -1;
        }

        if ((bios_size < 0 || bios_size > BIOS_SIZE) &&
            machine->firmware && !qtest_enabled()) {
            error_report("Could not load MIPS bios '%s'", machine->firmware);
            exit(1);
        }
    }

    /* Init internal devices */
    cpu_mips_irq_init_cpu(cpu);
    cpu_mips_clock_init(cpu);

    /* North bridge, Bonito --> IP2 */
    pci_bus = bonito_init((qemu_irq *)&(env->irq[2]));

    /* South bridge -> IP5 */
    pci_dev = pci_new_multifunction(PCI_DEVFN(FULOONG2E_VIA_SLOT, 0),
                                    TYPE_VT82C686B_ISA);

    /* Set properties on individual devices before realizing the south bridge */
    if (machine->audiodev) {
        dev = DEVICE(object_resolve_path_component(OBJECT(pci_dev), "ac97"));
        qdev_prop_set_string(dev, "audiodev", machine->audiodev);
    }

    pci_realize_and_unref(pci_dev, pci_bus, &error_abort);

    object_property_add_alias(OBJECT(machine), "rtc-time",
                              object_resolve_path_component(OBJECT(pci_dev),
                                                            "rtc"),
                              "date");
    qdev_connect_gpio_out_named(DEVICE(pci_dev), "intr", 0, env->irq[5]);

    dev = DEVICE(object_resolve_path_component(OBJECT(pci_dev), "ide"));
    pci_ide_create_devs(PCI_DEVICE(dev));

    dev = DEVICE(object_resolve_path_component(OBJECT(pci_dev), "pm"));
    smbus = I2C_BUS(qdev_get_child_bus(dev, "i2c"));

    /* GPU */
    if (vga_interface_type != VGA_NONE) {
        vga_interface_created = true;
        pci_dev = pci_new(-1, "ati-vga");
        dev = DEVICE(pci_dev);
        qdev_prop_set_uint32(dev, "vgamem_mb", 16);
        qdev_prop_set_uint16(dev, "x-device-id", 0x5159);
        pci_realize_and_unref(pci_dev, pci_bus, &error_fatal);
    }

    /* Populate SPD eeprom data */
    spd_data = spd_data_generate(DDR, machine->ram_size);
    smbus_eeprom_init_one(smbus, 0x50, spd_data);

    /* Network card: RTL8139D */
    network_init(pci_bus);
}

static void mips_fuloong2e_machine_init(MachineClass *mc)
{
    mc->desc = "Fuloong 2e mini pc";
    mc->init = mips_fuloong2e_init;
    mc->block_default_type = IF_IDE;
    mc->default_cpu_type = MIPS_CPU_TYPE_NAME("Loongson-2E");
    mc->default_ram_size = 256 * MiB;
    mc->default_ram_id = "fuloong2e.ram";
    mc->minimum_page_bits = 14;
    machine_add_audiodev_property(mc);
}

DEFINE_MACHINE("fuloong2e", mips_fuloong2e_machine_init)
