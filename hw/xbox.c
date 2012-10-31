/*
 * QEMU Xbox System Emulator
 *
 * Copyright (c) 2012 espes
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

#include "hw.h"
#include "arch_init.h"
#include "pc.h"
#include "pci.h"
#include "net.h"
#include "boards.h"
#include "ide.h"
#include "mc146818rtc.h"
#include "i8254.h"
#include "pcspk.h"
#include "kvm.h"
#include "sysemu.h"
#include "sysbus.h"
#include "smbus.h"
#include "blockdev.h"
#include "loader.h"
#include "exec-memory.h"

#include "xbox_pci.h"
#include "nv2a.h"
#include "mcpx.h"

/* mostly from pc_memory_init */
static void xbox_memory_init(MemoryRegion *system_memory,
                             ram_addr_t mem_size,
                             MemoryRegion *rom_memory,
                             MemoryRegion **ram_memory)
{
    MemoryRegion *ram;
    MemoryRegion *ram_below_4g;

    int ret;
    char *filename;
    int bios_size, isa_bios_size;
    MemoryRegion *bios, *isa_bios;

    MemoryRegion *map_bios;
    uint32_t map_loc;

    /* Allocate RAM.  We allocate it as a single memory region and use
     * aliases to address portions of it, mostly for backwards compatibility
     * with older qemus that used qemu_ram_alloc().
     */
    ram = g_malloc(sizeof(*ram));
    memory_region_init_ram(ram, "pc.ram", mem_size);
    vmstate_register_ram_global(ram);
    *ram_memory = ram;
    ram_below_4g = g_malloc(sizeof(*ram_below_4g));
    memory_region_init_alias(ram_below_4g, "ram-below-4g", ram,
                             0, mem_size);
    memory_region_add_subregion(system_memory, 0, ram_below_4g);


    /* Load the bios. (mostly from pc_sysfw)
     * Can't use it verbatim, since we need the bios repeated\
     * over top 1MB of memory.
     */
    if (bios_name == NULL) {
        bios_name = "bios.bin";
    }
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (filename) {
        bios_size = get_image_size(filename);
    } else {
        bios_size = -1;
    }
    bios = g_malloc(sizeof(*bios));
    memory_region_init_ram(bios, "pc.bios", bios_size);
    vmstate_register_ram_global(bios);
    memory_region_set_readonly(bios, true);
    ret = rom_add_file_fixed(bios_name, (uint32_t)(-bios_size), -1);
    if (ret != 0) {
        fprintf(stderr, "qemu: could not load PC BIOS '%s'\n", bios_name);
        exit(1);
    }
    if (filename) {
        g_free(filename);
    }



    /* map the last 128KB of the BIOS in ISA space */
    isa_bios_size = bios_size;
    if (isa_bios_size > (128 * 1024)) {
        isa_bios_size = 128 * 1024;
    }
    isa_bios = g_malloc(sizeof(*isa_bios));
    memory_region_init_alias(isa_bios, "isa-bios", bios,
                             bios_size - isa_bios_size, isa_bios_size);
    memory_region_add_subregion_overlap(rom_memory,
                                        0x100000 - isa_bios_size,
                                        isa_bios,
                                        1);
    memory_region_set_readonly(isa_bios, true);


    /* map the bios repeated at the top of memory */
    for (map_loc=(uint32_t)(-bios_size); map_loc >= 0xff000000; map_loc-=bios_size) {
        map_bios = g_malloc(sizeof(*map_bios));
        memory_region_init_alias(map_bios, NULL, bios, 0, bios_size);

        memory_region_add_subregion(rom_memory, map_loc, map_bios);
        memory_region_set_readonly(map_bios, true);
    }

    /*memory_region_add_subregion(rom_memory,
                                (uint32_t)(-bios_size),
                                bios);
    */

}



static void ioapic_init(GSIState *gsi_state)
{
    DeviceState *dev;
    SysBusDevice *d;
    unsigned int i;

    dev = qdev_create(NULL, "ioapic");

    qdev_init_nofail(dev);
    d = sysbus_from_qdev(dev);
    sysbus_mmio_map(d, 0, 0xfec00000);

    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        gsi_state->ioapic_irq[i] = qdev_get_gpio_in(dev, i);
    }
}


#define MAX_IDE_BUS 2

/* mostly from pc_init1 */
static void xbox_init(QEMUMachineInitArgs *args)
{
    int i;
    ram_addr_t ram_size;
    const char *cpu_model;
    const char *boot_device;

    PCIBus *host_bus;
    ISABus *isa_bus;

    MemoryRegion *system_memory;
    MemoryRegion *system_io;
    MemoryRegion *ram_memory;
    MemoryRegion *pci_memory;

    qemu_irq *cpu_irq;
    qemu_irq *gsi;
    qemu_irq *i8259;
    GSIState *gsi_state;

    PCIDevice *ide_dev;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    BusState *idebus[MAX_IDE_BUS];

    ISADevice *rtc_state;
    ISADevice *pit;
    i2c_bus *smbus;
    PCIBus *agp_bus;


    ram_size = args->ram_size;
    cpu_model = args->cpu_model;
    boot_device = args->boot_device;
    system_memory = get_system_memory();
    system_io = get_system_io();


    pc_cpus_init(cpu_model);

    pci_memory = g_new(MemoryRegion, 1);
    memory_region_init(pci_memory, "pci", INT64_MAX);

    /* allocate ram and load rom/bios */
    xbox_memory_init(system_memory, ram_size,
                     pci_memory, &ram_memory);


    gsi_state = g_malloc0(sizeof(*gsi_state));
    gsi = qemu_allocate_irqs(gsi_handler, gsi_state, GSI_NUM_PINS);


    /* init buses */
    host_bus = xbox_pci_init(gsi,
                             system_memory, system_io,
                             pci_memory, ram_memory);


    /* bridges */
    agp_bus = xbox_agp_init(host_bus);
    isa_bus = xbox_lpc_init(host_bus, gsi);
    smbus = xbox_smbus_init(host_bus, gsi);


    /* irq shit */
    isa_bus_irqs(isa_bus, gsi);
    cpu_irq = pc_allocate_cpu_irq();
    i8259 = i8259_init(isa_bus, cpu_irq[0]);

    for (i = 0; i < ISA_NUM_IRQS; i++) {
        gsi_state->i8259_irq[i] = i8259[i];
    }
    ioapic_init(gsi_state);


    /* basic device init */
    rtc_state = rtc_init(isa_bus, 2000, NULL);
    pit = pit_init(isa_bus, 0x40, 0, NULL);

    /* does apparently have a pc speaker, though not used? */
    pcspk_init(isa_bus, pit);


    /* TODO: ethernet */

    /* USB */
    pci_create_simple(host_bus, PCI_DEVFN(2, 0), "pci-ohci");
    pci_create_simple(host_bus, PCI_DEVFN(3, 0), "pci-ohci");

    /* hdd shit
     * piix3's ide be right for now, maybe
     */
    ide_drive_get(hd, MAX_IDE_BUS);
    ide_dev = pci_piix3_ide_init(host_bus, hd, PCI_DEVFN(9, 0));

    idebus[0] = qdev_get_child_bus(&ide_dev->qdev, "ide.0");
    idebus[1] = qdev_get_child_bus(&ide_dev->qdev, "ide.1");
    

    pc_cmos_init(ram_size, 0, boot_device,
                 NULL, idebus[0], idebus[1], rtc_state);


    /* Temporary blank eeprom for xbox 1.0:
     *   Serial number 000000000000
     *   Mac address 00:00:00:00:00:00
     *   ...etc.
     * TODO: persist it...
     */
    const uint8_t tmp_eeprom[] = {
        0x25, 0x42, 0x88, 0x24, 0xA3, 0x1A, 0x7D, 0xF4,
        0xEE, 0x53, 0x3F, 0x39, 0x5D, 0x27, 0x98, 0x0E,
        0x58, 0xB3, 0x26, 0xC3, 0x70, 0x82, 0xE5, 0xC6,
        0xF7, 0xC5, 0x54, 0x38, 0xA0, 0x58, 0xB9, 0x5D,
        0xB7, 0x27, 0xC7, 0xB1, 0x67, 0xCF, 0x99, 0x3E,
        0xC8, 0x6E, 0xC8, 0x53, 0xEF, 0x7C, 0x01, 0x37,
        0x6F, 0x6E, 0x2F, 0x6F, 0x30, 0x30, 0x30, 0x30,
        0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    uint8_t *eeprom_buf = g_malloc0(256);
    memcpy(eeprom_buf, tmp_eeprom, 256);

    smbus_eeprom_init_single(smbus, 0x54, eeprom_buf);
    
    smbus_xbox_smc_init(smbus, 0x10);
    smbus_cx25871_init(smbus, 0x45);
    smbus_adm1032_init(smbus, 0x4c);

    /* APU! */
    mcpx_init(host_bus, PCI_DEVFN(5, 0), gsi[5]);

    /* GPU! */
    nv2a_init(agp_bus, PCI_DEVFN(0, 0), gsi[3]);
}

static QEMUMachine xbox_machine = {
    .name = "xbox",
    .desc = "Microsoft Xbox",
    .init = xbox_init,
};

static void xbox_machine_init(void) {
    qemu_register_machine(&xbox_machine);
}

machine_init(xbox_machine_init);