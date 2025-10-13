/*
 * ASPEED SoC family
 *
 * Andrew Jeffery <andrew@aj.id.au>
 * Jeremy Kerr <jk@ozlabs.org>
 *
 * Copyright 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/misc/unimp.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/char/serial-mm.h"
#include "system/blockdev.h"
#include "system/block-backend.h"
#include "hw/loader.h"
#include "qemu/datadir.h"


const char *aspeed_soc_cpu_type(const char * const *valid_cpu_types)
{
    assert(valid_cpu_types);
    assert(valid_cpu_types[0]);
    assert(!valid_cpu_types[1]);
    return valid_cpu_types[0];
}

bool aspeed_soc_uart_realize(MemoryRegion *memory, SerialMM *smm,
                             const hwaddr addr, Error **errp)
{
    /* Chardev property is set by the machine. */
    qdev_prop_set_uint8(DEVICE(smm), "regshift", 2);
    qdev_prop_set_uint32(DEVICE(smm), "baudbase", 38400);
    qdev_set_legacy_instance_id(DEVICE(smm), addr, 2);
    qdev_prop_set_uint8(DEVICE(smm), "endianness", DEVICE_LITTLE_ENDIAN);
    if (!sysbus_realize(SYS_BUS_DEVICE(smm), errp)) {
        return false;
    }

    aspeed_mmio_map(memory, SYS_BUS_DEVICE(smm), 0, addr);
    return true;
}

void aspeed_soc_uart_set_chr(SerialMM *uart, int dev, int uarts_base,
                             int uarts_num, Chardev *chr)
{
    int uart_first = aspeed_uart_first(uarts_base);
    int uart_index = aspeed_uart_index(dev);
    int i = uart_index - uart_first;

    g_assert(0 <= i && i < ASPEED_UARTS_NUM && i < uarts_num);
    qdev_prop_set_chr(DEVICE(&uart[i]), "chardev", chr);
}

/*
 * SDMC should be realized first to get correct RAM size and max size
 * values
 */
bool aspeed_soc_dram_init(AspeedSoCState *s, Error **errp)
{
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    ram_addr_t ram_size, max_ram_size;

    ram_size = object_property_get_uint(OBJECT(&s->sdmc), "ram-size",
                                        &error_abort);
    max_ram_size = object_property_get_uint(OBJECT(&s->sdmc), "max-ram-size",
                                            &error_abort);

    memory_region_init(&s->dram_container, OBJECT(s), "ram-container",
                       max_ram_size);
    memory_region_add_subregion(&s->dram_container, 0, s->dram_mr);

    /*
     * Add a memory region beyond the RAM region to let firmwares scan
     * the address space with load/store and guess how much RAM the
     * SoC has.
     */
    if (ram_size < max_ram_size) {
        DeviceState *dev = qdev_new(TYPE_UNIMPLEMENTED_DEVICE);

        qdev_prop_set_string(dev, "name", "ram-empty");
        qdev_prop_set_uint64(dev, "size", max_ram_size  - ram_size);
        if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), errp)) {
            return false;
        }

        memory_region_add_subregion_overlap(&s->dram_container, ram_size,
                      sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0), -1000);
    }

    memory_region_add_subregion(s->memory,
                      sc->memmap[ASPEED_DEV_SDRAM], &s->dram_container);
    return true;
}

void aspeed_mmio_map(MemoryRegion *memory, SysBusDevice *dev, int n,
                     hwaddr addr)
{
    memory_region_add_subregion(memory, addr, sysbus_mmio_get_region(dev, n));
}

void aspeed_mmio_map_unimplemented(MemoryRegion *memory, SysBusDevice *dev,
                                   const char *name, hwaddr addr, uint64_t size)
{
    qdev_prop_set_string(DEVICE(dev), "name", name);
    qdev_prop_set_uint64(DEVICE(dev), "size", size);
    sysbus_realize(dev, &error_abort);

    memory_region_add_subregion_overlap(memory, addr,
                                        sysbus_mmio_get_region(dev, 0), -1000);
}

void aspeed_board_init_flashes(AspeedSMCState *s, const char *flashtype,
                               unsigned int count, int unit0)
{
    int i;

    if (!flashtype) {
        return;
    }

    for (i = 0; i < count; ++i) {
        DriveInfo *dinfo = drive_get(IF_MTD, 0, unit0 + i);
        DeviceState *dev;

        dev = qdev_new(flashtype);
        if (dinfo) {
            qdev_prop_set_drive(dev, "drive", blk_by_legacy_dinfo(dinfo));
        }
        qdev_prop_set_uint8(dev, "cs", i);
        qdev_realize_and_unref(dev, BUS(s->spi), &error_fatal);
    }
}

void aspeed_write_boot_rom(BlockBackend *blk, hwaddr addr, size_t rom_size,
                           Error **errp)
{
    g_autofree void *storage = NULL;
    int64_t size;

    /*
     * The block backend size should have already been 'validated' by
     * the creation of the m25p80 object.
     */
    size = blk_getlength(blk);
    if (size <= 0) {
        error_setg(errp, "failed to get flash size");
        return;
    }

    if (rom_size > size) {
        rom_size = size;
    }

    storage = g_malloc0(rom_size);
    if (blk_pread(blk, 0, rom_size, storage, 0) < 0) {
        error_setg(errp, "failed to read the initial flash content");
        return;
    }

    rom_add_blob_fixed("aspeed.boot_rom", storage, rom_size, addr);
}

/*
 * Create a ROM and copy the flash contents at the expected address
 * (0x0). Boots faster than execute-in-place.
 */
void aspeed_install_boot_rom(AspeedSoCState *soc, BlockBackend *blk,
                             MemoryRegion *boot_rom, uint64_t rom_size)
{
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(soc);

    memory_region_init_rom(boot_rom, NULL, "aspeed.boot_rom", rom_size,
                           &error_abort);
    memory_region_add_subregion_overlap(&soc->spi_boot_container, 0,
                                        boot_rom, 1);
    aspeed_write_boot_rom(blk, sc->memmap[ASPEED_DEV_SPI_BOOT], rom_size,
                          &error_abort);
}

/*
 * This function locates the vbootrom image file specified via the command line
 * using the -bios option. It loads the specified image into the vbootrom
 * memory region and handles errors if the file cannot be found or loaded.
 */
void aspeed_load_vbootrom(AspeedSoCState *soc, const char *bios_name,
                          Error **errp)
{
    g_autofree char *filename = NULL;
    int ret;

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (!filename) {
        error_setg(errp, "Could not find vbootrom image '%s'", bios_name);
        return;
    }

    ret = load_image_mr(filename, &soc->vbootrom);
    if (ret < 0) {
        error_setg(errp, "Failed to load vbootrom image '%s'", bios_name);
        return;
    }
}

static void aspeed_soc_realize(DeviceState *dev, Error **errp)
{
    AspeedSoCState *s = ASPEED_SOC(dev);

    if (!s->memory) {
        error_setg(errp, "'memory' link is not set");
        return;
    }
}

static bool aspeed_soc_boot_from_emmc(AspeedSoCState *s)
{
    return false;
}

static const Property aspeed_soc_properties[] = {
    DEFINE_PROP_LINK("dram", AspeedSoCState, dram_mr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_LINK("memory", AspeedSoCState, memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
};

static void aspeed_soc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    AspeedSoCClass *sc = ASPEED_SOC_CLASS(oc);

    dc->realize = aspeed_soc_realize;
    device_class_set_props(dc, aspeed_soc_properties);
    sc->boot_from_emmc = aspeed_soc_boot_from_emmc;
}

static const TypeInfo aspeed_soc_types[] = {
    {
        .name           = TYPE_ASPEED_SOC,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(AspeedSoCState),
        .class_size     = sizeof(AspeedSoCClass),
        .class_init     = aspeed_soc_class_init,
        .abstract       = true,
    },
};

DEFINE_TYPES(aspeed_soc_types)
