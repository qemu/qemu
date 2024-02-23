/*
 * QEMU PowerPC PowerNV ADU unit
 *
 * The ADU unit actually implements XSCOM, which is the bridge between MMIO
 * and PIB. However it also includes control and status registers and other
 * functions that are exposed as PIB (xscom) registers.
 *
 * To keep things simple, pnv_xscom.c remains the XSCOM bridge
 * implementation, and pnv_adu.c implements the ADU registers and other
 * functions.
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"

#include "hw/qdev-properties.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_adu.h"
#include "hw/ppc/pnv_chip.h"
#include "hw/ppc/pnv_xscom.h"
#include "trace.h"

static uint64_t pnv_adu_xscom_read(void *opaque, hwaddr addr, unsigned width)
{
    uint32_t offset = addr >> 3;
    uint64_t val = 0;

    switch (offset) {
    case 0x18:     /* Receive status reg */
    case 0x12:     /* log register */
    case 0x13:     /* error register */
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "ADU Unimplemented read register: Ox%08x\n",
                                                                     offset);
    }

    trace_pnv_adu_xscom_read(addr, val);

    return val;
}

static void pnv_adu_xscom_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned width)
{
    uint32_t offset = addr >> 3;

    trace_pnv_adu_xscom_write(addr, val);

    switch (offset) {
    case 0x18:     /* Receive status reg */
    case 0x12:     /* log register */
    case 0x13:     /* error register */
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "ADU Unimplemented write register: Ox%08x\n",
                                                                     offset);
    }
}

const MemoryRegionOps pnv_adu_xscom_ops = {
    .read = pnv_adu_xscom_read,
    .write = pnv_adu_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_adu_realize(DeviceState *dev, Error **errp)
{
    PnvADU *adu = PNV_ADU(dev);

    /* XScom regions for ADU registers */
    pnv_xscom_region_init(&adu->xscom_regs, OBJECT(dev),
                          &pnv_adu_xscom_ops, adu, "xscom-adu",
                          PNV9_XSCOM_ADU_SIZE);
}

static void pnv_adu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_adu_realize;
    dc->desc = "PowerNV ADU";
    dc->user_creatable = false;
}

static const TypeInfo pnv_adu_type_info = {
    .name          = TYPE_PNV_ADU,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvADU),
    .class_init    = pnv_adu_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { } },
};

static void pnv_adu_register_types(void)
{
    type_register_static(&pnv_adu_type_info);
}

type_init(pnv_adu_register_types);
