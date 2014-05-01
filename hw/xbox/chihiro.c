/*
 * QEMU Chihiro emulation
 *
 * Copyright (c) 2013 espes
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
#include "hw/boards.h"
#include "hw/ide.h"
#include "hw/loader.h"
#include "hw/isa/isa.h"
#include "exec/memory.h"
#include "qemu/config-file.h"
#include "sysemu/blockdev.h"
#include "block/blkmemory.h"
#include "hw/xbox/xbox.h"


#define SEGA_CHIP_REVISION                  0xF0
#   define SEGA_CHIP_REVISION_CHIP_ID            0xFF00
#       define SEGA_CHIP_REVISION_FPGA_CHIP_ID      0x0000
#       define SEGA_CHIP_REVISION_ASIC_CHIP_ID      0x0100
#   define SEGA_CHIP_REVISION_REVISION_ID_MASK   0x00FF
#define SEGA_DIMM_SIZE                      0xF4
#   define SEGA_DIMM_SIZE_128M                  0
#   define SEGA_DIMM_SIZE_256M                  1
#   define SEGA_DIMM_SIZE_512M                  2
#   define SEGA_DIMM_SIZE_1024M                 3


typedef struct ChihiroLPCState {
    ISADevice dev;
    MemoryRegion ioport;
} ChihiroLPCState;

#define CHIHIRO_LPC_DEVICE(obj) \
    OBJECT_CHECK(ChihiroLPCState, (obj), "chihiro-lpc")


static uint64_t chhiro_lpc_io_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    uint64_t r = 0;
    switch (addr) {
    case SEGA_CHIP_REVISION:
        r = SEGA_CHIP_REVISION_ASIC_CHIP_ID;
        break;
    case SEGA_DIMM_SIZE:
        r = SEGA_DIMM_SIZE_128M;
        break;
    }
    return r;
}

static void chhiro_lpc_io_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{

}

static const MemoryRegionOps chihiro_lpc_io_ops = {
    .read = chhiro_lpc_io_read,
    .write = chhiro_lpc_io_write,
    .impl = {
        .min_access_size = 2,
        .max_access_size = 2,
    },
};

static void chihiro_lpc_realize(DeviceState *dev, Error **errp)
{
    ChihiroLPCState *s = CHIHIRO_LPC_DEVICE(dev);
    ISADevice *isa = ISA_DEVICE(dev);
    
    memory_region_init_io(&s->ioport, OBJECT(dev), &chihiro_lpc_io_ops, s,
                          "chihiro-lpc-io", 0x100);
    isa_register_ioport(isa, &s->ioport, 0x4000);
}

static void chihiro_lpc_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = chihiro_lpc_realize;
    dc->desc = "Chihiro LPC";
}

static const TypeInfo chihiro_lpc_info = {
    .name          = "chihiro-lpc",
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ChihiroLPCState),
    .class_init    = chihiro_lpc_class_initfn,
};

static void chihiro_register_types(void)
{
    type_register_static(&chihiro_lpc_info);
}

type_init(chihiro_register_types)


/* The chihiro baseboard communicates with the xbox by acting as an IDE
 * device. The device maps the boot rom from the mediaboard, a communication
 * area for interfacing with the network board, and the ram on the baseboard.
 * The baseboard ram is populated at boot from the gd-rom drive on the
 * mediaboard containing something like a combined disc+hdd image.
 */

#define FILESYSTEM_START         0
#define ROM_START                0x8000000
#define ROM_SECTORS              0x2000
#define COMMUNICATION_START      0x9000000
#define COMMUNICATION_SECTORS    0x10000
#define SECTOR_SIZE              512

static void chihiro_ide_interface_init(const char *rom_file,
                                       const char *filesystem_file)
{
    if (drive_get(IF_IDE, 0, 1)) {
        fprintf(stderr, "chihiro ide interface needs to be attached "
                        "to IDE device 1 but it's already in use.");
        exit(1);
    }

    MemoryRegion *interface, *rom, *filesystem;
    interface = g_malloc(sizeof(*interface));
    memory_region_init(interface, NULL, "chihiro.interface",
                       (uint64_t)0x10000000 * SECTOR_SIZE);

    rom = g_malloc(sizeof(*rom));
    memory_region_init_ram(rom, NULL, "chihiro.interface.rom",
                           ROM_SECTORS * SECTOR_SIZE);
    memory_region_add_subregion(interface,
                                (uint64_t)ROM_START * SECTOR_SIZE, rom);


    /* limited by the size of the board ram, which we emulate as 128M for now */
    filesystem = g_malloc(sizeof(*filesystem));
    memory_region_init_ram(filesystem, NULL, "chihiro.interface.filesystem",
                           128 * 1024 * 1024);
    memory_region_add_subregion(interface,
                                (uint64_t)FILESYSTEM_START * SECTOR_SIZE,
                                filesystem);


    AddressSpace *interface_space;
    interface_space = g_malloc(sizeof(*interface_space));
    address_space_init(interface_space, interface, "chihiro-interface");

    /* read files */
    int rc, fd = -1;

    if (!rom_file) rom_file = "fpr21042_m29w160et.bin";
    char *rom_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, rom_file);
    if (rom_filename) {
        int rom_size = get_image_size(rom_filename);
        assert(rom_size < memory_region_size(rom));

        fd = open(rom_filename, O_RDONLY | O_BINARY);
        assert(fd != -1);
        rc = read(fd, memory_region_get_ram_ptr(rom), rom_size);
        assert(rc == rom_size);
        close(fd);
    }


    if (filesystem_file) {
        assert(access(filesystem_file, R_OK) == 0);

        int filesystem_size = get_image_size(filesystem_file);
        assert(filesystem_size < memory_region_size(filesystem));

        fd = open(filesystem_file, O_RDONLY | O_BINARY);
        assert(fd != -1);
        rc = read(fd, memory_region_get_ram_ptr(rom), filesystem_size);
        assert(rc == filesystem_size);
        close(fd);
    }

    /* create the device */
    DriveInfo *dinfo;
    dinfo = g_malloc0(sizeof(*dinfo));
    dinfo->id = g_strdup("chihiro-interface");
    dinfo->bdrv = bdrv_new(dinfo->id);
    dinfo->type = IF_IDE;
    dinfo->bus = 0;
    dinfo->unit = 1;
    dinfo->refcount = 1;

    assert(!bdrv_memory_open(dinfo->bdrv, interface_space,
                             memory_region_size(interface)));

    drive_append(dinfo);
}

static void chihiro_init(QEMUMachineInitArgs *args)
{
    /* Placeholder blank eeprom for chihiro:
     *   Serial number 000000000000
     *   Mac address 00:00:00:00:00:00
     *   ...etc.
     */
    const uint8_t eeprom[] = {
        0xA7, 0x65, 0x60, 0x76, 0xB7, 0x2F, 0xFE, 0xD8,
        0x20, 0xBC, 0x8B, 0x15, 0x13, 0xBF, 0x73, 0x9C,
        0x8C, 0x3F, 0xD8, 0x07, 0x75, 0x55, 0x5F, 0x8B,
        0x09, 0xD1, 0x25, 0xD1, 0x1A, 0xA2, 0xD5, 0xB7,
        0x01, 0x7D, 0x9A, 0x31, 0xCD, 0x9C, 0x83, 0x6B,
        0x2C, 0xAB, 0xAD, 0x6F, 0xAC, 0x36, 0xDE, 0xEF,
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
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    QemuOpts *machine_opts = qemu_opts_find(qemu_find_opts("machine"), 0);
    if (machine_opts) {
        const char *mediaboard_rom_file =
            qemu_opt_get(machine_opts, "mediaboard_rom");
        const char *mediaboard_filesystem_file =
            qemu_opt_get(machine_opts, "mediaboard_filesystem");
        
        if (mediaboard_rom_file || mediaboard_filesystem_file) {
            chihiro_ide_interface_init(mediaboard_rom_file,
                                       mediaboard_filesystem_file);
        }
    }

    ISABus *isa_bus;
    xbox_init_common(args, (uint8_t*)eeprom, &isa_bus);

    isa_create_simple(isa_bus, "chihiro-lpc");
}


static QEMUMachine chihiro_machine = {
    .name = "chihiro",
    .desc = "Sega Chihiro",
    .init = chihiro_init,
    .max_cpus = 1,
    .no_floppy = 1,
    .no_cdrom = 1,
    .no_sdcard = 1,
    DEFAULT_MACHINE_OPTIONS
};

static void chihiro_machine_init(void) {
    qemu_register_machine(&chihiro_machine);
}
machine_init(chihiro_machine_init);
