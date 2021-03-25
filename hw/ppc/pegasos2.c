/*
 * QEMU PowerPC CHRP (Genesi/bPlan Pegasos II) hardware System Emulator
 *
 * Copyright (c) 2018-2020 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/ppc/ppc.h"
#include "hw/sysbus.h"
#include "hw/pci/pci_host.h"
#include "hw/irq.h"
#include "hw/pci-host/mv64361.h"
#include "hw/isa/vt82c686.h"
#include "hw/ide/pci.h"
#include "hw/i2c/smbus_eeprom.h"
#include "hw/qdev-properties.h"
#include "sysemu/reset.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/fw-path-provider.h"
#include "elf.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "exec/address-spaces.h"
#include "trace.h"
#include "qemu/datadir.h"
#include "sysemu/device_tree.h"

#define PROM_FILENAME "pegasos2.rom"
#define PROM_ADDR     0xfff00000
#define PROM_SIZE     0x80000

#define BUS_FREQ_HZ 133333333

static void pegasos2_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
    cpu->env.spr[SPR_HID1] = 7ULL << 28;
}

static void pegasos2_init(MachineState *machine)
{
    PowerPCCPU *cpu = NULL;
    MemoryRegion *rom = g_new(MemoryRegion, 1);
    DeviceState *mv;
    PCIBus *pci_bus;
    PCIDevice *dev;
    I2CBus *i2c_bus;
    const char *fwname = machine->firmware ?: PROM_FILENAME;
    char *filename;
    int sz;
    uint8_t *spd_data;

    /* init CPU */
    cpu = POWERPC_CPU(cpu_create(machine->cpu_type));
    if (PPC_INPUT(&cpu->env) != PPC_FLAGS_INPUT_6xx) {
        error_report("Incompatible CPU, only 6xx bus supported");
        exit(1);
    }

    /* Set time-base frequency */
    cpu_ppc_tb_init(&cpu->env, BUS_FREQ_HZ / 4);
    qemu_register_reset(pegasos2_cpu_reset, cpu);

    /* RAM */
    memory_region_add_subregion(get_system_memory(), 0, machine->ram);

    /* allocate and load firmware */
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, fwname);
    if (!filename) {
        error_report("Could not find firmware '%s'", fwname);
        exit(1);
    }
    memory_region_init_rom(rom, NULL, "pegasos2.rom", PROM_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), PROM_ADDR, rom);
    sz = load_elf(filename, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 1,
                  PPC_ELF_MACHINE, 0, 0);
    if (sz <= 0) {
        sz = load_image_targphys(filename, PROM_ADDR, PROM_SIZE);
    }
    if (sz <= 0 || sz > PROM_SIZE) {
        error_report("Could not load firmware '%s'", filename);
        exit(1);
    }
    g_free(filename);

    /* Marvell Discovery II system controller */
    mv = DEVICE(sysbus_create_simple(TYPE_MV64361, -1,
                        ((qemu_irq *)cpu->env.irq_inputs)[PPC6xx_INPUT_INT]));
    pci_bus = mv64361_get_pci_bus(mv, 1);

    /* VIA VT8231 South Bridge (multifunction PCI device) */
    /* VT8231 function 0: PCI-to-ISA Bridge */
    dev = pci_create_simple_multifunction(pci_bus, PCI_DEVFN(12, 0), true,
                                          TYPE_VT8231_ISA);
    qdev_connect_gpio_out(DEVICE(dev), 0,
                          qdev_get_gpio_in_named(mv, "gpp", 31));

    /* VT8231 function 1: IDE Controller */
    dev = pci_create_simple(pci_bus, PCI_DEVFN(12, 1), "via-ide");
    pci_ide_create_devs(dev);

    /* VT8231 function 2-3: USB Ports */
    pci_create_simple(pci_bus, PCI_DEVFN(12, 2), "vt82c686b-usb-uhci");
    pci_create_simple(pci_bus, PCI_DEVFN(12, 3), "vt82c686b-usb-uhci");

    /* VT8231 function 4: Power Management Controller */
    dev = pci_create_simple(pci_bus, PCI_DEVFN(12, 4), TYPE_VT8231_PM);
    i2c_bus = I2C_BUS(qdev_get_child_bus(DEVICE(dev), "i2c"));
    spd_data = spd_data_generate(DDR, machine->ram_size);
    smbus_eeprom_init_one(i2c_bus, 0x57, spd_data);

    /* VT8231 function 5-6: AC97 Audio & Modem */
    pci_create_simple(pci_bus, PCI_DEVFN(12, 5), TYPE_VIA_AC97);
    pci_create_simple(pci_bus, PCI_DEVFN(12, 6), TYPE_VIA_MC97);

    /* other PC hardware */
    pci_vga_init(pci_bus);
}

static void pegasos2_machine(MachineClass *mc)
{
    mc->desc = "Genesi/bPlan Pegasos II";
    mc->init = pegasos2_init;
    mc->block_default_type = IF_IDE;
    mc->default_boot_order = "cd";
    mc->default_display = "std";
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("7400_v2.9");
    mc->default_ram_id = "pegasos2.ram";
    mc->default_ram_size = 512 * MiB;
}

DEFINE_MACHINE("pegasos2", pegasos2_machine)
