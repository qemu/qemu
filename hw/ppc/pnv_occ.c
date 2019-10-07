/*
 * QEMU PowerPC PowerNV Emulation of a few OCC related registers
 *
 * Copyright (c) 2015-2017, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "target/ppc/cpu.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"

#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv_occ.h"

#define OCB_OCI_OCCMISC         0x4020
#define OCB_OCI_OCCMISC_AND     0x4021
#define OCB_OCI_OCCMISC_OR      0x4022

/* OCC sensors */
#define OCC_SENSOR_DATA_BLOCK_OFFSET          0x580000
#define OCC_SENSOR_DATA_VALID                 0x580001
#define OCC_SENSOR_DATA_VERSION               0x580002
#define OCC_SENSOR_DATA_READING_VERSION       0x580004
#define OCC_SENSOR_DATA_NR_SENSORS            0x580008
#define OCC_SENSOR_DATA_NAMES_OFFSET          0x580010
#define OCC_SENSOR_DATA_READING_PING_OFFSET   0x580014
#define OCC_SENSOR_DATA_READING_PONG_OFFSET   0x58000c
#define OCC_SENSOR_DATA_NAME_LENGTH           0x58000d
#define OCC_SENSOR_NAME_STRUCTURE_TYPE        0x580023
#define OCC_SENSOR_LOC_CORE                   0x580022
#define OCC_SENSOR_LOC_GPU                    0x580020
#define OCC_SENSOR_TYPE_POWER                 0x580003
#define OCC_SENSOR_NAME                       0x580005
#define HWMON_SENSORS_MASK                    0x58001e
#define SLW_IMAGE_BASE                        0x0

static void pnv_occ_set_misc(PnvOCC *occ, uint64_t val)
{
    bool irq_state;
    PnvOCCClass *poc = PNV_OCC_GET_CLASS(occ);

    val &= 0xffff000000000000ull;

    occ->occmisc = val;
    irq_state = !!(val >> 63);
    pnv_psi_irq_set(occ->psi, poc->psi_irq, irq_state);
}

static uint64_t pnv_occ_power8_xscom_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    PnvOCC *occ = PNV_OCC(opaque);
    uint32_t offset = addr >> 3;
    uint64_t val = 0;

    switch (offset) {
    case OCB_OCI_OCCMISC:
        val = occ->occmisc;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "OCC Unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }
    return val;
}

static void pnv_occ_power8_xscom_write(void *opaque, hwaddr addr,
                                       uint64_t val, unsigned size)
{
    PnvOCC *occ = PNV_OCC(opaque);
    uint32_t offset = addr >> 3;

    switch (offset) {
    case OCB_OCI_OCCMISC_AND:
        pnv_occ_set_misc(occ, occ->occmisc & val);
        break;
    case OCB_OCI_OCCMISC_OR:
        pnv_occ_set_misc(occ, occ->occmisc | val);
        break;
    case OCB_OCI_OCCMISC:
        pnv_occ_set_misc(occ, val);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "OCC Unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }
}

static uint64_t pnv_occ_common_area_read(void *opaque, hwaddr addr,
                                         unsigned width)
{
    switch (addr) {
    /*
     * occ-sensor sanity check that asserts the sensor
     * header block
     */
    case OCC_SENSOR_DATA_BLOCK_OFFSET:
    case OCC_SENSOR_DATA_VALID:
    case OCC_SENSOR_DATA_VERSION:
    case OCC_SENSOR_DATA_READING_VERSION:
    case OCC_SENSOR_DATA_NR_SENSORS:
    case OCC_SENSOR_DATA_NAMES_OFFSET:
    case OCC_SENSOR_DATA_READING_PING_OFFSET:
    case OCC_SENSOR_DATA_READING_PONG_OFFSET:
    case OCC_SENSOR_NAME_STRUCTURE_TYPE:
        return 1;
    case OCC_SENSOR_DATA_NAME_LENGTH:
        return 0x30;
    case OCC_SENSOR_LOC_CORE:
        return 0x0040;
    case OCC_SENSOR_TYPE_POWER:
        return 0x0080;
    case OCC_SENSOR_NAME:
        return 0x1000;
    case HWMON_SENSORS_MASK:
    case OCC_SENSOR_LOC_GPU:
        return 0x8e00;
    case SLW_IMAGE_BASE:
        return 0x1000000000000000;
    }
    return 0;
}

static void pnv_occ_common_area_write(void *opaque, hwaddr addr,
                                             uint64_t val, unsigned width)
{
    /* callback function defined to occ common area write */
    return;
}

static const MemoryRegionOps pnv_occ_power8_xscom_ops = {
    .read = pnv_occ_power8_xscom_read,
    .write = pnv_occ_power8_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

const MemoryRegionOps pnv_occ_sram_ops = {
    .read = pnv_occ_common_area_read,
    .write = pnv_occ_common_area_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_occ_power8_class_init(ObjectClass *klass, void *data)
{
    PnvOCCClass *poc = PNV_OCC_CLASS(klass);

    poc->xscom_size = PNV_XSCOM_OCC_SIZE;
    poc->sram_size = PNV_OCC_COMMON_AREA_SIZE;
    poc->xscom_ops = &pnv_occ_power8_xscom_ops;
    poc->sram_ops = &pnv_occ_sram_ops;
    poc->psi_irq = PSIHB_IRQ_OCC;
}

static const TypeInfo pnv_occ_power8_type_info = {
    .name          = TYPE_PNV8_OCC,
    .parent        = TYPE_PNV_OCC,
    .instance_size = sizeof(PnvOCC),
    .class_init    = pnv_occ_power8_class_init,
};

#define P9_OCB_OCI_OCCMISC              0x6080
#define P9_OCB_OCI_OCCMISC_CLEAR        0x6081
#define P9_OCB_OCI_OCCMISC_OR           0x6082


static uint64_t pnv_occ_power9_xscom_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    PnvOCC *occ = PNV_OCC(opaque);
    uint32_t offset = addr >> 3;
    uint64_t val = 0;

    switch (offset) {
    case P9_OCB_OCI_OCCMISC:
        val = occ->occmisc;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "OCC Unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }
    return val;
}

static void pnv_occ_power9_xscom_write(void *opaque, hwaddr addr,
                                       uint64_t val, unsigned size)
{
    PnvOCC *occ = PNV_OCC(opaque);
    uint32_t offset = addr >> 3;

    switch (offset) {
    case P9_OCB_OCI_OCCMISC_CLEAR:
        pnv_occ_set_misc(occ, 0);
        break;
    case P9_OCB_OCI_OCCMISC_OR:
        pnv_occ_set_misc(occ, occ->occmisc | val);
        break;
    case P9_OCB_OCI_OCCMISC:
        pnv_occ_set_misc(occ, val);
       break;
    default:
        qemu_log_mask(LOG_UNIMP, "OCC Unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr >> 3);
    }
}

static const MemoryRegionOps pnv_occ_power9_xscom_ops = {
    .read = pnv_occ_power9_xscom_read,
    .write = pnv_occ_power9_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_occ_power9_class_init(ObjectClass *klass, void *data)
{
    PnvOCCClass *poc = PNV_OCC_CLASS(klass);

    poc->xscom_size = PNV9_XSCOM_OCC_SIZE;
    poc->sram_size = PNV9_OCC_COMMON_AREA_SIZE;
    poc->xscom_ops = &pnv_occ_power9_xscom_ops;
    poc->sram_ops = &pnv_occ_sram_ops;
    poc->psi_irq = PSIHB9_IRQ_OCC;
}

static const TypeInfo pnv_occ_power9_type_info = {
    .name          = TYPE_PNV9_OCC,
    .parent        = TYPE_PNV_OCC,
    .instance_size = sizeof(PnvOCC),
    .class_init    = pnv_occ_power9_class_init,
};

static void pnv_occ_realize(DeviceState *dev, Error **errp)
{
    PnvOCC *occ = PNV_OCC(dev);
    PnvOCCClass *poc = PNV_OCC_GET_CLASS(occ);
    Object *obj;
    Error *local_err = NULL;

    occ->occmisc = 0;

    obj = object_property_get_link(OBJECT(dev), "psi", &local_err);
    if (!obj) {
        error_propagate(errp, local_err);
        error_prepend(errp, "required link 'psi' not found: ");
        return;
    }
    occ->psi = PNV_PSI(obj);

    /* XScom region for OCC registers */
    pnv_xscom_region_init(&occ->xscom_regs, OBJECT(dev), poc->xscom_ops,
                          occ, "xscom-occ", poc->xscom_size);

    /* XScom region for OCC SRAM registers */
    pnv_xscom_region_init(&occ->sram_regs, OBJECT(dev), poc->sram_ops,
                          occ, "occ-common-area", poc->sram_size);
}

static void pnv_occ_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_occ_realize;
    dc->desc = "PowerNV OCC Controller";
}

static const TypeInfo pnv_occ_type_info = {
    .name          = TYPE_PNV_OCC,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvOCC),
    .class_init    = pnv_occ_class_init,
    .class_size    = sizeof(PnvOCCClass),
    .abstract      = true,
};

static void pnv_occ_register_types(void)
{
    type_register_static(&pnv_occ_type_info);
    type_register_static(&pnv_occ_power8_type_info);
    type_register_static(&pnv_occ_power9_type_info);
}

type_init(pnv_occ_register_types);
