/*
 * Arm M-profile RAS (Reliability, Availability and Serviceability) block
 *
 * Copyright (c) 2021 Linaro Limited
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/misc/armv7m_ras.h"
#include "qemu/log.h"

static MemTxResult ras_read(void *opaque, hwaddr addr,
                            uint64_t *data, unsigned size,
                            MemTxAttrs attrs)
{
    if (attrs.user) {
        return MEMTX_ERROR;
    }

    switch (addr) {
    case 0xe10: /* ERRIIDR */
        /* architect field = Arm; product/variant/revision 0 */
        *data = 0x43b;
        break;
    case 0xfc8: /* ERRDEVID */
        /* Minimal RAS: we implement 0 error record indexes */
        *data = 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "Read RAS register offset 0x%x\n",
                      (uint32_t)addr);
        *data = 0;
        break;
    }
    return MEMTX_OK;
}

static MemTxResult ras_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size,
                             MemTxAttrs attrs)
{
    if (attrs.user) {
        return MEMTX_ERROR;
    }

    switch (addr) {
    default:
        qemu_log_mask(LOG_UNIMP, "Write to RAS register offset 0x%x\n",
                      (uint32_t)addr);
        break;
    }
    return MEMTX_OK;
}

static const MemoryRegionOps ras_ops = {
    .read_with_attrs = ras_read,
    .write_with_attrs = ras_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static void armv7m_ras_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    ARMv7MRAS *s = ARMV7M_RAS(obj);

    memory_region_init_io(&s->iomem, obj, &ras_ops,
                          s, "armv7m-ras", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void armv7m_ras_class_init(ObjectClass *klass, void *data)
{
    /* This device has no state: no need for vmstate or reset */
}

static const TypeInfo armv7m_ras_info = {
    .name = TYPE_ARMV7M_RAS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMv7MRAS),
    .instance_init = armv7m_ras_init,
    .class_init = armv7m_ras_class_init,
};

static void armv7m_ras_register_types(void)
{
    type_register_static(&armv7m_ras_info);
}

type_init(armv7m_ras_register_types);
