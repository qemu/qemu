/*
 * QEMU Macintosh Nubus
 *
 * Copyright (c) 2013-2018 Laurent Vivier <laurent@vivier.eu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/nubus/nubus.h"
#include "qapi/error.h"


/* The Format Block Structure */

#define FBLOCK_DIRECTORY_OFFSET 0
#define FBLOCK_LENGTH           4
#define FBLOCK_CRC              8
#define FBLOCK_REVISION_LEVEL   12
#define FBLOCK_FORMAT           13
#define FBLOCK_TEST_PATTERN     14
#define FBLOCK_RESERVED         18
#define FBLOCK_BYTE_LANES       19

#define FBLOCK_SIZE             20
#define FBLOCK_PATTERN_VAL      0x5a932bc7

static uint64_t nubus_fblock_read(void *opaque, hwaddr addr, unsigned int size)
{
    NubusDevice *dev = opaque;
    uint64_t val;

#define BYTE(v, b) (((v) >> (24 - 8 * (b))) & 0xff)
    switch (addr) {
    case FBLOCK_BYTE_LANES:
        val = dev->byte_lanes;
        val |= (val ^ 0xf) << 4;
        break;
    case FBLOCK_RESERVED:
        val = 0x00;
        break;
    case FBLOCK_TEST_PATTERN...FBLOCK_TEST_PATTERN + 3:
        val = BYTE(FBLOCK_PATTERN_VAL, addr - FBLOCK_TEST_PATTERN);
        break;
    case FBLOCK_FORMAT:
        val = dev->rom_format;
        break;
    case FBLOCK_REVISION_LEVEL:
        val = dev->rom_rev;
        break;
    case FBLOCK_CRC...FBLOCK_CRC + 3:
        val = BYTE(dev->rom_crc, addr - FBLOCK_CRC);
        break;
    case FBLOCK_LENGTH...FBLOCK_LENGTH + 3:
        val = BYTE(dev->rom_length, addr - FBLOCK_LENGTH);
        break;
    case FBLOCK_DIRECTORY_OFFSET...FBLOCK_DIRECTORY_OFFSET + 3:
        val = BYTE(dev->directory_offset, addr - FBLOCK_DIRECTORY_OFFSET);
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void nubus_fblock_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned int size)
{
    /* read only */
}

static const MemoryRegionOps nubus_format_block_ops = {
    .read = nubus_fblock_read,
    .write = nubus_fblock_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    }
};

static void nubus_register_format_block(NubusDevice *dev)
{
    char *fblock_name;

    fblock_name = g_strdup_printf("nubus-slot-%d-format-block",
                                  dev->slot_nb);

    hwaddr fblock_offset = memory_region_size(&dev->slot_mem) - FBLOCK_SIZE;
    memory_region_init_io(&dev->fblock_io, NULL, &nubus_format_block_ops,
                          dev, fblock_name, FBLOCK_SIZE);
    memory_region_add_subregion(&dev->slot_mem, fblock_offset,
                                &dev->fblock_io);

    g_free(fblock_name);
}

static void mac_nubus_rom_write(void *opaque, hwaddr addr, uint64_t val,
                                       unsigned int size)
{
    /* read only */
}

static uint64_t mac_nubus_rom_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    NubusDevice *dev = opaque;

    return dev->rom[addr];
}

static const MemoryRegionOps mac_nubus_rom_ops = {
    .read  = mac_nubus_rom_read,
    .write = mac_nubus_rom_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};


void nubus_register_rom(NubusDevice *dev, const uint8_t *rom, uint32_t size,
                        int revision, int format, uint8_t byte_lanes)
{
    hwaddr rom_offset;
    char *rom_name;

    /* FIXME : really compute CRC */
    dev->rom_length = 0;
    dev->rom_crc = 0;

    dev->rom_rev = revision;
    dev->rom_format = format;

    dev->byte_lanes = byte_lanes;
    dev->directory_offset = -size;

    /* ROM */

    dev->rom = rom;
    rom_name = g_strdup_printf("nubus-slot-%d-rom", dev->slot_nb);
    memory_region_init_io(&dev->rom_io, NULL, &mac_nubus_rom_ops,
                          dev, rom_name, size);
    memory_region_set_readonly(&dev->rom_io, true);

    rom_offset = memory_region_size(&dev->slot_mem) - FBLOCK_SIZE +
                 dev->directory_offset;
    memory_region_add_subregion(&dev->slot_mem, rom_offset, &dev->rom_io);

    g_free(rom_name);
}

static void nubus_device_realize(DeviceState *dev, Error **errp)
{
    NubusBus *nubus = NUBUS_BUS(qdev_get_parent_bus(dev));
    NubusDevice *nd = NUBUS_DEVICE(dev);
    char *name;
    hwaddr slot_offset;

    if (nubus->current_slot < NUBUS_FIRST_SLOT ||
            nubus->current_slot > NUBUS_LAST_SLOT) {
        error_setg(errp, "Cannot register nubus card, not enough slots");
        return;
    }

    nd->slot_nb = nubus->current_slot++;
    name = g_strdup_printf("nubus-slot-%d", nd->slot_nb);

    if (nd->slot_nb < NUBUS_FIRST_SLOT) {
        /* Super */
        slot_offset = (nd->slot_nb - 6) * NUBUS_SUPER_SLOT_SIZE;

        memory_region_init(&nd->slot_mem, OBJECT(dev), name,
                           NUBUS_SUPER_SLOT_SIZE);
        memory_region_add_subregion(&nubus->super_slot_io, slot_offset,
                                    &nd->slot_mem);
    } else {
        /* Normal */
        slot_offset = nd->slot_nb * NUBUS_SLOT_SIZE;

        memory_region_init(&nd->slot_mem, OBJECT(dev), name, NUBUS_SLOT_SIZE);
        memory_region_add_subregion(&nubus->slot_io, slot_offset,
                                    &nd->slot_mem);
    }

    g_free(name);
    nubus_register_format_block(nd);
}

static void nubus_device_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = nubus_device_realize;
    dc->bus_type = TYPE_NUBUS_BUS;
}

static const TypeInfo nubus_device_type_info = {
    .name = TYPE_NUBUS_DEVICE,
    .parent = TYPE_DEVICE,
    .abstract = true,
    .instance_size = sizeof(NubusDevice),
    .class_init = nubus_device_class_init,
};

static void nubus_register_types(void)
{
    type_register_static(&nubus_device_type_info);
}

type_init(nubus_register_types)
