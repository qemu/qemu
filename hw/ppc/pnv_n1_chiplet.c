/*
 * QEMU PowerPC N1 chiplet model
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv_n1_chiplet.h"
#include "hw/ppc/pnv_nest_pervasive.h"

/*
 * The n1 chiplet contains chiplet control unit,
 * PowerBus/RaceTrack/Bridge logic, nest Memory Management Unit(nMMU)
 * and more.
 *
 * In this model Nest1 chiplet control registers are modelled via common
 * nest pervasive model and few PowerBus racetrack registers are modelled.
 */

#define PB_SCOM_EQ0_HP_MODE2_CURR      0xe
#define PB_SCOM_ES3_MODE               0x8a

static uint64_t pnv_n1_chiplet_pb_scom_eq_read(void *opaque, hwaddr addr,
                                                  unsigned size)
{
    PnvN1Chiplet *n1_chiplet = PNV_N1_CHIPLET(opaque);
    uint32_t reg = addr >> 3;
    uint64_t val = ~0ull;

    switch (reg) {
    case PB_SCOM_EQ0_HP_MODE2_CURR:
        val = n1_chiplet->eq[0].hp_mode2_curr;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Invalid xscom read at 0x%" PRIx32 "\n",
                      __func__, reg);
    }
    return val;
}

static void pnv_n1_chiplet_pb_scom_eq_write(void *opaque, hwaddr addr,
                                               uint64_t val, unsigned size)
{
    PnvN1Chiplet *n1_chiplet = PNV_N1_CHIPLET(opaque);
    uint32_t reg = addr >> 3;

    switch (reg) {
    case PB_SCOM_EQ0_HP_MODE2_CURR:
        n1_chiplet->eq[0].hp_mode2_curr = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Invalid xscom write at 0x%" PRIx32 "\n",
                      __func__, reg);
    }
}

static const MemoryRegionOps pnv_n1_chiplet_pb_scom_eq_ops = {
    .read = pnv_n1_chiplet_pb_scom_eq_read,
    .write = pnv_n1_chiplet_pb_scom_eq_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static uint64_t pnv_n1_chiplet_pb_scom_es_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    PnvN1Chiplet *n1_chiplet = PNV_N1_CHIPLET(opaque);
    uint32_t reg = addr >> 3;
    uint64_t val = ~0ull;

    switch (reg) {
    case PB_SCOM_ES3_MODE:
        val = n1_chiplet->es[3].mode;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Invalid xscom read at 0x%" PRIx32 "\n",
                      __func__, reg);
    }
    return val;
}

static void pnv_n1_chiplet_pb_scom_es_write(void *opaque, hwaddr addr,
                                               uint64_t val, unsigned size)
{
    PnvN1Chiplet *n1_chiplet = PNV_N1_CHIPLET(opaque);
    uint32_t reg = addr >> 3;

    switch (reg) {
    case PB_SCOM_ES3_MODE:
        n1_chiplet->es[3].mode = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Invalid xscom write at 0x%" PRIx32 "\n",
                      __func__, reg);
    }
}

static const MemoryRegionOps pnv_n1_chiplet_pb_scom_es_ops = {
    .read = pnv_n1_chiplet_pb_scom_es_read,
    .write = pnv_n1_chiplet_pb_scom_es_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_n1_chiplet_realize(DeviceState *dev, Error **errp)
{
    PnvN1Chiplet *n1_chiplet = PNV_N1_CHIPLET(dev);

    /* Realize nest pervasive common chiplet model */
    if (!qdev_realize(DEVICE(&n1_chiplet->nest_pervasive), NULL, errp)) {
        return;
    }

    /* Nest1 chiplet power bus EQ xscom region */
    pnv_xscom_region_init(&n1_chiplet->xscom_pb_eq_mr, OBJECT(n1_chiplet),
                          &pnv_n1_chiplet_pb_scom_eq_ops, n1_chiplet,
                          "xscom-n1-chiplet-pb-scom-eq",
                          PNV10_XSCOM_N1_PB_SCOM_EQ_SIZE);

    /* Nest1 chiplet power bus ES xscom region */
    pnv_xscom_region_init(&n1_chiplet->xscom_pb_es_mr, OBJECT(n1_chiplet),
                          &pnv_n1_chiplet_pb_scom_es_ops, n1_chiplet,
                          "xscom-n1-chiplet-pb-scom-es",
                          PNV10_XSCOM_N1_PB_SCOM_ES_SIZE);
}

static void pnv_n1_chiplet_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PowerNV n1 chiplet";
    dc->realize = pnv_n1_chiplet_realize;
}

static void pnv_n1_chiplet_instance_init(Object *obj)
{
    PnvN1Chiplet *n1_chiplet = PNV_N1_CHIPLET(obj);

    object_initialize_child(OBJECT(n1_chiplet), "nest-pervasive-common",
                            &n1_chiplet->nest_pervasive,
                            TYPE_PNV_NEST_CHIPLET_PERVASIVE);
}

static const TypeInfo pnv_n1_chiplet_info = {
    .name          = TYPE_PNV_N1_CHIPLET,
    .parent        = TYPE_DEVICE,
    .instance_init = pnv_n1_chiplet_instance_init,
    .instance_size = sizeof(PnvN1Chiplet),
    .class_init    = pnv_n1_chiplet_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_n1_chiplet_register_types(void)
{
    type_register_static(&pnv_n1_chiplet_info);
}

type_init(pnv_n1_chiplet_register_types);
