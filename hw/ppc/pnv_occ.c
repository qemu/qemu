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
#include "hw/hw.h"
#include "sysemu/sysemu.h"
#include "target/ppc/cpu.h"
#include "qapi/error.h"
#include "qemu/log.h"

#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv_occ.h"

#define OCB_OCI_OCCMISC         0x4020
#define OCB_OCI_OCCMISC_AND     0x4021
#define OCB_OCI_OCCMISC_OR      0x4022

static void pnv_occ_set_misc(PnvOCC *occ, uint64_t val)
{
    bool irq_state;

    val &= 0xffff000000000000ull;

    occ->occmisc = val;
    irq_state = !!(val >> 63);
    pnv_psi_irq_set(occ->psi, PSIHB_IRQ_OCC, irq_state);
}

static uint64_t pnv_occ_xscom_read(void *opaque, hwaddr addr, unsigned size)
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
                      HWADDR_PRIx "\n", addr);
    }
    return val;
}

static void pnv_occ_xscom_write(void *opaque, hwaddr addr,
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
                      HWADDR_PRIx "\n", addr);
    }
}

static const MemoryRegionOps pnv_occ_xscom_ops = {
    .read = pnv_occ_xscom_read,
    .write = pnv_occ_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};


static void pnv_occ_realize(DeviceState *dev, Error **errp)
{
    PnvOCC *occ = PNV_OCC(dev);
    Object *obj;
    Error *error = NULL;

    occ->occmisc = 0;

    /* get PSI object from chip */
    obj = object_property_get_link(OBJECT(dev), "psi", &error);
    if (!obj) {
        error_setg(errp, "%s: required link 'psi' not found: %s",
                   __func__, error_get_pretty(error));
        return;
    }
    occ->psi = PNV_PSI(obj);

    /* XScom region for OCC registers */
    pnv_xscom_region_init(&occ->xscom_regs, OBJECT(dev), &pnv_occ_xscom_ops,
                  occ, "xscom-occ", PNV_XSCOM_OCC_SIZE);
}

static void pnv_occ_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_occ_realize;
}

static const TypeInfo pnv_occ_type_info = {
    .name          = TYPE_PNV_OCC,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvOCC),
    .class_init    = pnv_occ_class_init,
};

static void pnv_occ_register_types(void)
{
    type_register_static(&pnv_occ_type_info);
}

type_init(pnv_occ_register_types)
