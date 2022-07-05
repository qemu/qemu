/*
 * QEMU PowerNV PNOR simple model
 *
 * Copyright (c) 2015-2019, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "hw/loader.h"
#include "hw/ppc/pnv_pnor.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"

static uint64_t pnv_pnor_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvPnor *s = PNV_PNOR(opaque);
    uint64_t ret = 0;
    int i;

    for (i = 0; i < size; i++) {
        ret |= (uint64_t) s->storage[addr + i] << (8 * (size - i - 1));
    }

    return ret;
}

static void pnv_pnor_update(PnvPnor *s, int offset, int size)
{
    int offset_end;
    int ret;

    if (!s->blk || !blk_is_writable(s->blk)) {
        return;
    }

    offset_end = offset + size;
    offset = QEMU_ALIGN_DOWN(offset, BDRV_SECTOR_SIZE);
    offset_end = QEMU_ALIGN_UP(offset_end, BDRV_SECTOR_SIZE);

    ret = blk_pwrite(s->blk, offset, offset_end - offset, s->storage + offset,
                     0);
    if (ret < 0) {
        error_report("Could not update PNOR offset=0x%" PRIx32" : %s", offset,
                     strerror(-ret));
    }
}

static void pnv_pnor_write(void *opaque, hwaddr addr, uint64_t data,
                           unsigned size)
{
    PnvPnor *s = PNV_PNOR(opaque);
    int i;

    for (i = 0; i < size; i++) {
        s->storage[addr + i] = (data >> (8 * (size - i - 1))) & 0xFF;
    }
    pnv_pnor_update(s, addr, size);
}

/*
 * TODO: Check endianness: skiboot is BIG, Aspeed AHB is LITTLE, flash
 * is BIG.
 */
static const MemoryRegionOps pnv_pnor_ops = {
    .read = pnv_pnor_read,
    .write = pnv_pnor_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void pnv_pnor_realize(DeviceState *dev, Error **errp)
{
    PnvPnor *s = PNV_PNOR(dev);
    int ret;

    if (s->blk) {
        uint64_t perm = BLK_PERM_CONSISTENT_READ |
                        (blk_supports_write_perm(s->blk) ? BLK_PERM_WRITE : 0);
        ret = blk_set_perm(s->blk, perm, BLK_PERM_ALL, errp);
        if (ret < 0) {
            return;
        }

        s->size = blk_getlength(s->blk);
        if (s->size <= 0) {
            error_setg(errp, "failed to get flash size");
            return;
        }

        s->storage = blk_blockalign(s->blk, s->size);

        if (blk_pread(s->blk, 0, s->size, s->storage, 0) < 0) {
            error_setg(errp, "failed to read the initial flash content");
            return;
        }
    } else {
        s->storage = blk_blockalign(NULL, s->size);
        memset(s->storage, 0xFF, s->size);
    }

    memory_region_init_io(&s->mmio, OBJECT(s), &pnv_pnor_ops, s,
                          TYPE_PNV_PNOR, s->size);
}

static Property pnv_pnor_properties[] = {
    DEFINE_PROP_INT64("size", PnvPnor, size, 128 * MiB),
    DEFINE_PROP_DRIVE("drive", PnvPnor, blk),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_pnor_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_pnor_realize;
    device_class_set_props(dc, pnv_pnor_properties);
}

static const TypeInfo pnv_pnor_info = {
    .name          = TYPE_PNV_PNOR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PnvPnor),
    .class_init    = pnv_pnor_class_init,
};

static void pnv_pnor_register_types(void)
{
    type_register_static(&pnv_pnor_info);
}

type_init(pnv_pnor_register_types)
