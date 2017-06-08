/*
 * QEMU PowerPC PowerNV Interrupt Control Presenter (ICP) model
 *
 * Copyright (c) 2017, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/ppc/xics.h"

#define ICP_XIRR_POLL    0 /* 1 byte (CPRR) or 4 bytes */
#define ICP_XIRR         4 /* 1 byte (CPRR) or 4 bytes */
#define ICP_MFRR        12 /* 1 byte access only */

#define ICP_LINKA       16 /* unused */
#define ICP_LINKB       20 /* unused */
#define ICP_LINKC       24 /* unused */

static uint64_t pnv_icp_read(void *opaque, hwaddr addr, unsigned width)
{
    ICPState *icp = ICP(opaque);
    PnvICPState *picp = PNV_ICP(opaque);
    bool byte0 = (width == 1 && (addr & 0x3) == 0);
    uint64_t val = 0xffffffff;

    switch (addr & 0xffc) {
    case ICP_XIRR_POLL:
        val = icp_ipoll(icp, NULL);
        if (byte0) {
            val >>= 24;
        } else if (width != 4) {
            goto bad_access;
        }
        break;
    case ICP_XIRR:
        if (byte0) {
            val = icp_ipoll(icp, NULL) >> 24;
        } else if (width == 4) {
            val = icp_accept(icp);
        } else {
            goto bad_access;
        }
        break;
    case ICP_MFRR:
        if (byte0) {
            val = icp->mfrr;
        } else {
            goto bad_access;
        }
        break;
    case ICP_LINKA:
        if (width == 4) {
            val = picp->links[0];
        } else {
            goto bad_access;
        }
        break;
    case ICP_LINKB:
        if (width == 4) {
            val = picp->links[1];
        } else {
            goto bad_access;
        }
        break;
    case ICP_LINKC:
        if (width == 4) {
            val = picp->links[2];
        } else {
            goto bad_access;
        }
        break;
    default:
bad_access:
        qemu_log_mask(LOG_GUEST_ERROR, "XICS: Bad ICP access 0x%"
                      HWADDR_PRIx"/%d\n", addr, width);
    }

    return val;
}

static void pnv_icp_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned width)
{
    ICPState *icp = ICP(opaque);
    PnvICPState *picp = PNV_ICP(opaque);
    bool byte0 = (width == 1 && (addr & 0x3) == 0);

    switch (addr & 0xffc) {
    case ICP_XIRR:
        if (byte0) {
            icp_set_cppr(icp, val);
        } else if (width == 4) {
            icp_eoi(icp, val);
        } else {
            goto bad_access;
        }
        break;
    case ICP_MFRR:
        if (byte0) {
            icp_set_mfrr(icp, val);
        } else {
            goto bad_access;
        }
        break;
    case ICP_LINKA:
        if (width == 4) {
            picp->links[0] = val;
        } else {
            goto bad_access;
        }
        break;
    case ICP_LINKB:
        if (width == 4) {
            picp->links[1] = val;
        } else {
            goto bad_access;
        }
        break;
    case ICP_LINKC:
        if (width == 4) {
            picp->links[2] = val;
        } else {
            goto bad_access;
        }
        break;
    default:
bad_access:
        qemu_log_mask(LOG_GUEST_ERROR, "XICS: Bad ICP access 0x%"
                      HWADDR_PRIx"/%d\n", addr, width);
    }
}

static const MemoryRegionOps pnv_icp_ops = {
    .read = pnv_icp_read,
    .write = pnv_icp_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void pnv_icp_realize(ICPState *icp, Error **errp)
{
    PnvICPState *pnv_icp = PNV_ICP(icp);

    memory_region_init_io(&pnv_icp->mmio, OBJECT(icp), &pnv_icp_ops,
                          icp, "icp-thread", 0x1000);
}

static void pnv_icp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ICPStateClass *icpc = ICP_CLASS(klass);

    icpc->realize = pnv_icp_realize;
    dc->desc = "PowerNV ICP";
}

static const TypeInfo pnv_icp_info = {
    .name          = TYPE_PNV_ICP,
    .parent        = TYPE_ICP,
    .instance_size = sizeof(PnvICPState),
    .class_init    = pnv_icp_class_init,
    .class_size    = sizeof(ICPStateClass),
};

static void pnv_icp_register_types(void)
{
    type_register_static(&pnv_icp_info);
}

type_init(pnv_icp_register_types)
