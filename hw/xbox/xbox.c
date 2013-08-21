/*
 * QEMU Xbox System Emulator
 *
 * Copyright (c) 2012 espes
 *
 * Based on pc.c
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/hw.h"
#include "sysemu/arch_init.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "hw/boards.h"
#include "hw/ide.h"
#include "hw/timer/mc146818rtc.h"
#include "hw/timer/i8254.h"
#include "hw/audio/pcspk.h"
#include "sysemu/sysemu.h"
#include "hw/cpu/icc_bus.h"
#include "hw/sysbus.h"
#include "hw/i2c/smbus.h"
#include "sysemu/blockdev.h"
#include "hw/loader.h"
#include "exec/address-spaces.h"

#include "hw/xbox/xbox_pci.h"
#include "hw/xbox/nvnet.h"
#include "hw/xbox/nv2a.h"
#include "hw/xbox/mcpx_apu.h"

#include "hw/xbox/xbox.h"

/* mostly from pc_memory_init */
static void xbox_memory_init(MemoryRegion *system_memory,
                             ram_addr_t mem_size,
                             MemoryRegion *rom_memory,
                             MemoryRegion **ram_memory)
{
    MemoryRegion *ram;

    int ret;
    char *filename;
    int bios_size;
    MemoryRegion *bios;

    MemoryRegion *map_bios;
    uint32_t map_loc;

    /* Allocate RAM.  We allocate it as a single memory region and use
     * aliases to address portions of it, mostly for backwards compatibility
     * with older qemus that used qemu_ram_alloc().
     */
    ram = g_malloc(sizeof(*ram));
    memory_region_init_ram(ram, NULL, "xbox.ram", mem_size);
    vmstate_register_ram_global(ram);
    *ram_memory = ram;
    memory_region_add_subregion(system_memory, 0, ram);


    /* Load the bios. (mostly from pc_sysfw)
     * Can't use it verbatim, since we need the bios repeated
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
    if (bios_size <= 0 ||
        (bios_size % 65536) != 0) {
        goto bios_error;
    }
    bios = g_malloc(sizeof(*bios));
    memory_region_init_ram(bios, NULL, "xbox.bios", bios_size);
    vmstate_register_ram_global(bios);
    memory_region_set_readonly(bios, true);
    ret = rom_add_file_fixed(bios_name, (uint32_t)(-bios_size), -1);
    if (ret != 0) {
bios_error:
        fprintf(stderr, "qemu: could not load xbox BIOS '%s'\n", bios_name);
        exit(1);
    }
    if (filename) {
        g_free(filename);
    }


    /* map the bios repeated at the top of memory */
    for (map_loc=(uint32_t)(-bios_size); map_loc >= 0xff000000; map_loc-=bios_size) {
        map_bios = g_malloc(sizeof(*map_bios));
        memory_region_init_alias(map_bios, NULL, NULL, bios, 0, bios_size);

        memory_region_add_subregion(rom_memory, map_loc, map_bios);
        memory_region_set_readonly(map_bios, true);
    }

    /*memory_region_add_subregion(rom_memory,
                                (uint32_t)(-bios_size),
                                bios);
    */

}

/* mostly from pc_init1 */
void xbox_init_common(QEMUMachineInitArgs *args,
                      uint8_t *default_eeprom,
                      ISABus **out_isa_bus)
{
    int i;
    ram_addr_t ram_size = args->ram_size;
    const char *cpu_model = args->cpu_model;
    const char *boot_device = args->boot_device;

    PCIBus *host_bus;
    ISABus *isa_bus;

    MemoryRegion *ram_memory;
    MemoryRegion *pci_memory;

    qemu_irq *cpu_irq;
    qemu_irq *gsi;
    qemu_irq *i8259;
    GSIState *gsi_state;

    PCIDevice *ide_dev;
    BusState *idebus[MAX_IDE_BUS];

    ISADevice *rtc_state;
    ISADevice *pit;
    i2c_bus *smbus;
    PCIBus *agp_bus;


    DeviceState *icc_bridge;
    icc_bridge = qdev_create(NULL, TYPE_ICC_BRIDGE);
    object_property_add_child(qdev_get_machine(), "icc-bridge",
                              OBJECT(icc_bridge), NULL);

    pc_cpus_init(cpu_model, icc_bridge);

    pci_memory = g_new(MemoryRegion, 1);
    memory_region_init(pci_memory, NULL, "pci", INT64_MAX);

    /* allocate ram and load rom/bios */
    xbox_memory_init(get_system_memory(), ram_size,
                     pci_memory, &ram_memory);


    gsi_state = g_malloc0(sizeof(*gsi_state));
    gsi = qemu_allocate_irqs(gsi_handler, gsi_state, GSI_NUM_PINS);


    /* init buses */
    host_bus = xbox_pci_init(gsi,
                             get_system_memory(), get_system_io(),
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


    /* basic device init */
    rtc_state = rtc_init(isa_bus, 2000, NULL);
    pit = pit_init(isa_bus, 0x40, 0, NULL);

    /* does apparently have a pc speaker, though not used? */
    pcspk_init(isa_bus, pit);

    /* IDE shit
     * piix3's ide be right for now, maybe
     */
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    ide_drive_get(hd, MAX_IDE_BUS);
    ide_dev = pci_piix3_ide_init(host_bus, hd, PCI_DEVFN(9, 0));

    idebus[0] = qdev_get_child_bus(&ide_dev->qdev, "ide.0");
    idebus[1] = qdev_get_child_bus(&ide_dev->qdev, "ide.1");
    

    pc_cmos_init(ram_size, 0, boot_device,
                 NULL, idebus[0], idebus[1], rtc_state);

    uint8_t *eeprom_buf = g_malloc0(256);
    memcpy(eeprom_buf, default_eeprom, 256);
    smbus_eeprom_init_single(smbus, 0x54, eeprom_buf);
    
    smbus_xbox_smc_init(smbus, 0x10);
    smbus_cx25871_init(smbus, 0x45);
    smbus_adm1032_init(smbus, 0x4c);


    /* USB */
    pci_create_simple(host_bus, PCI_DEVFN(2, 0), "pci-ohci");
    pci_create_simple(host_bus, PCI_DEVFN(3, 0), "pci-ohci");

    /* Ethernet! */
    nvnet_init(host_bus, PCI_DEVFN(4, 0), gsi[4]);

    /* APU! */
    mcpx_apu_init(host_bus, PCI_DEVFN(5, 0), gsi[5]);

    /* ACI! */
    mcpx_aci_init(host_bus, PCI_DEVFN(6, 0), gsi[6]);

    /* GPU! */
    nv2a_init(agp_bus, PCI_DEVFN(0, 0), gsi[3], ram_memory);

    *out_isa_bus = isa_bus;
}

static void xbox_init(QEMUMachineInitArgs *args)
{
    /* Placeholder blank eeprom for xbox 1.0:
     *   Serial number 000000000000
     *   Mac address 00:00:00:00:00:00
     *   ...etc.
     */
    const uint8_t eeprom[] = {
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

    ISABus *isa_bus;
    xbox_init_common(args, (uint8_t*)eeprom, &isa_bus);
}

static QEMUMachine xbox_machine = {
    .name = "xbox",
    .desc = "Microsoft Xbox",
    .init = xbox_init,
    .max_cpus = 1,
    .no_floppy = 1,
    .no_cdrom = 1,
    .no_sdcard = 1,
    DEFAULT_MACHINE_OPTIONS
};

static void xbox_machine_init(void) {
    qemu_register_machine(&xbox_machine);
}

machine_init(xbox_machine_init);