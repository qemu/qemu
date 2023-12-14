/*
 * QEMU Eyetech AmigaOne/Mai Logic Teron emulation
 *
 * Copyright (c) 2023 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/datadir.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/ppc/ppc.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/pci-host/articia.h"
#include "hw/isa/vt82c686.h"
#include "hw/ide/pci.h"
#include "hw/i2c/smbus_eeprom.h"
#include "hw/ppc/ppc.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "kvm_ppc.h"

#define BUS_FREQ_HZ 100000000

/*
 * Firmware binary available at
 * https://www.hyperion-entertainment.com/index.php/downloads?view=files&parent=28
 * then "tail -c 524288 updater.image >u-boot-amigaone.bin"
 *
 * BIOS emulator in firmware cannot run QEMU vgabios and hangs on it, use
 * -device VGA,romfile=VGABIOS-lgpl-latest.bin
 * from http://www.nongnu.org/vgabios/ instead.
 */
#define PROM_ADDR 0xfff00000
#define PROM_SIZE (512 * KiB)

/* AmigaOS calls this routine from ROM, use this if no firmware loaded */
static const char dummy_fw[] = {
    0x38, 0x00, 0x00, 0x08, /* li      r0,8 */
    0x7c, 0x09, 0x03, 0xa6, /* mtctr   r0 */
    0x54, 0x63, 0xf8, 0x7e, /* srwi    r3,r3,1 */
    0x42, 0x00, 0xff, 0xfc, /* bdnz    0x8 */
    0x7c, 0x63, 0x18, 0xf8, /* not     r3,r3 */
    0x4e, 0x80, 0x00, 0x20, /* blr */
};

static void amigaone_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
    cpu_ppc_tb_reset(&cpu->env);
}

static void fix_spd_data(uint8_t *spd)
{
    uint32_t bank_size = 4 * MiB * spd[31];
    uint32_t rows = bank_size / spd[13] / spd[17];
    spd[3] = ctz32(rows) - spd[4];
}

static void amigaone_init(MachineState *machine)
{
    PowerPCCPU *cpu;
    CPUPPCState *env;
    MemoryRegion *rom, *pci_mem, *mr;
    ssize_t sz;
    PCIBus *pci_bus;
    Object *via;
    DeviceState *dev;
    I2CBus *i2c_bus;
    uint8_t *spd_data;
    int i;

    /* init CPU */
    cpu = POWERPC_CPU(cpu_create(machine->cpu_type));
    env = &cpu->env;
    if (PPC_INPUT(env) != PPC_FLAGS_INPUT_6xx) {
        error_report("Incompatible CPU, only 6xx bus supported");
        exit(1);
    }
    cpu_ppc_tb_init(env, BUS_FREQ_HZ / 4);
    qemu_register_reset(amigaone_cpu_reset, cpu);

    /* RAM */
    if (machine->ram_size > 2 * GiB) {
        error_report("RAM size more than 2 GiB is not supported");
        exit(1);
    }
    memory_region_add_subregion(get_system_memory(), 0, machine->ram);
    if (machine->ram_size < 1 * GiB + 32 * KiB) {
        /* Firmware uses this area for startup */
        mr = g_new(MemoryRegion, 1);
        memory_region_init_ram(mr, NULL, "init-cache", 32 * KiB, &error_fatal);
        memory_region_add_subregion(get_system_memory(), 0x40000000, mr);
    }

    /* allocate and load firmware */
    rom = g_new(MemoryRegion, 1);
    memory_region_init_rom(rom, NULL, "rom", PROM_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), PROM_ADDR, rom);
    if (!machine->firmware) {
        rom_add_blob_fixed("dummy-fw", dummy_fw, sizeof(dummy_fw),
                           PROM_ADDR + PROM_SIZE - 0x80);
    } else {
        g_autofree char *filename = qemu_find_file(QEMU_FILE_TYPE_BIOS,
                                                   machine->firmware);
        if (!filename) {
            error_report("Could not find firmware '%s'", machine->firmware);
            exit(1);
        }
        sz = load_image_targphys(filename, PROM_ADDR, PROM_SIZE);
        if (sz <= 0 || sz > PROM_SIZE) {
            error_report("Could not load firmware '%s'", filename);
            exit(1);
        }
    }

    /* Articia S */
    dev = sysbus_create_simple(TYPE_ARTICIA, 0xfe000000, NULL);

    i2c_bus = I2C_BUS(qdev_get_child_bus(dev, "smbus"));
    if (machine->ram_size > 512 * MiB) {
        spd_data = spd_data_generate(SDR, machine->ram_size / 2);
    } else {
        spd_data = spd_data_generate(SDR, machine->ram_size);
    }
    fix_spd_data(spd_data);
    smbus_eeprom_init_one(i2c_bus, 0x51, spd_data);
    if (machine->ram_size > 512 * MiB) {
        smbus_eeprom_init_one(i2c_bus, 0x52, spd_data);
    }

    pci_mem = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    mr = g_new(MemoryRegion, 1);
    memory_region_init_alias(mr, OBJECT(dev), "pci-mem-low", pci_mem,
                             0, 0x1000000);
    memory_region_add_subregion(get_system_memory(), 0xfd000000, mr);
    mr = g_new(MemoryRegion, 1);
    memory_region_init_alias(mr, OBJECT(dev), "pci-mem-high", pci_mem,
                             0x80000000, 0x7d000000);
    memory_region_add_subregion(get_system_memory(), 0x80000000, mr);
    pci_bus = PCI_BUS(qdev_get_child_bus(dev, "pci.0"));

    /* VIA VT82c686B South Bridge (multifunction PCI device) */
    via = OBJECT(pci_create_simple_multifunction(pci_bus, PCI_DEVFN(7, 0),
                                                 TYPE_VT82C686B_ISA));
    object_property_add_alias(OBJECT(machine), "rtc-time",
                              object_resolve_path_component(via, "rtc"),
                              "date");
    qdev_connect_gpio_out(DEVICE(via), 0,
                          qdev_get_gpio_in(DEVICE(cpu), PPC6xx_INPUT_INT));
    for (i = 0; i < PCI_NUM_PINS; i++) {
        qdev_connect_gpio_out(dev, i, qdev_get_gpio_in_named(DEVICE(via),
                                                             "pirq", i));
    }
    pci_ide_create_devs(PCI_DEVICE(object_resolve_path_component(via, "ide")));
    pci_vga_init(pci_bus);
}

static void amigaone_machine_init(MachineClass *mc)
{
    mc->desc = "Eyetech AmigaOne/Mai Logic Teron";
    mc->init = amigaone_init;
    mc->block_default_type = IF_IDE;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("7457_v1.2");
    mc->default_display = "std";
    mc->default_ram_id = "ram";
    mc->default_ram_size = 512 * MiB;
}

DEFINE_MACHINE("amigaone", amigaone_machine_init)
