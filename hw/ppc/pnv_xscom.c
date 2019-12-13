/*
 * QEMU PowerPC PowerNV XSCOM bus
 *
 * Copyright (c) 2016, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "sysemu/hw_accel.h"
#include "target/ppc/cpu.h"
#include "hw/sysbus.h"

#include "hw/ppc/fdt.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_xscom.h"

#include <libfdt.h>

/* PRD registers */
#define PRD_P8_IPOLL_REG_MASK           0x01020013
#define PRD_P8_IPOLL_REG_STATUS         0x01020014
#define PRD_P9_IPOLL_REG_MASK           0x000F0033
#define PRD_P9_IPOLL_REG_STATUS         0x000F0034

static void xscom_complete(CPUState *cs, uint64_t hmer_bits)
{
    /*
     * TODO: When the read/write comes from the monitor, NULL is
     * passed for the cpu, and no CPU completion is generated.
     */
    if (cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);
        CPUPPCState *env = &cpu->env;

        /*
         * TODO: Need a CPU helper to set HMER, also handle generation
         * of HMIs
         */
        cpu_synchronize_state(cs);
        env->spr[SPR_HMER] |= hmer_bits;
    }
}

static uint32_t pnv_xscom_pcba(PnvChip *chip, uint64_t addr)
{
    return PNV_CHIP_GET_CLASS(chip)->xscom_pcba(chip, addr);
}

static uint64_t xscom_read_default(PnvChip *chip, uint32_t pcba)
{
    switch (pcba) {
    case 0xf000f:
        return PNV_CHIP_GET_CLASS(chip)->chip_cfam_id;
    case 0x18002:       /* ECID2 */
        return 0;

    case 0x1010c00:     /* PIBAM FIR */
    case 0x1010c03:     /* PIBAM FIR MASK */

        /* PRD registers */
    case PRD_P8_IPOLL_REG_MASK:
    case PRD_P8_IPOLL_REG_STATUS:
    case PRD_P9_IPOLL_REG_MASK:
    case PRD_P9_IPOLL_REG_STATUS:

        /* P9 xscom reset */
    case 0x0090018:     /* Receive status reg */
    case 0x0090012:     /* log register */
    case 0x0090013:     /* error register */

        /* P8 xscom reset */
    case 0x2020007:     /* ADU stuff, log register */
    case 0x2020009:     /* ADU stuff, error register */
    case 0x202000f:     /* ADU stuff, receive status register*/
        return 0;
    case 0x2013f01:     /* PBA stuff */
    case 0x2013f05:     /* PBA stuff */
        return 0;
    case 0x2013028:     /* CAPP stuff */
    case 0x201302a:     /* CAPP stuff */
    case 0x2013801:     /* CAPP stuff */
    case 0x2013802:     /* CAPP stuff */

        /* P9 CAPP regs */
    case 0x2010841:
    case 0x2010842:
    case 0x201082a:
    case 0x2010828:
    case 0x4010841:
    case 0x4010842:
    case 0x401082a:
    case 0x4010828:
        return 0;
    default:
        return -1;
    }
}

static bool xscom_write_default(PnvChip *chip, uint32_t pcba, uint64_t val)
{
    /* We ignore writes to these */
    switch (pcba) {
    case 0xf000f:       /* chip id is RO */
    case 0x1010c00:     /* PIBAM FIR */
    case 0x1010c01:     /* PIBAM FIR */
    case 0x1010c02:     /* PIBAM FIR */
    case 0x1010c03:     /* PIBAM FIR MASK */
    case 0x1010c04:     /* PIBAM FIR MASK */
    case 0x1010c05:     /* PIBAM FIR MASK */
        /* P9 xscom reset */
    case 0x0090018:     /* Receive status reg */
    case 0x0090012:     /* log register */
    case 0x0090013:     /* error register */

        /* P8 xscom reset */
    case 0x2020007:     /* ADU stuff, log register */
    case 0x2020009:     /* ADU stuff, error register */
    case 0x202000f:     /* ADU stuff, receive status register*/

    case 0x2013028:     /* CAPP stuff */
    case 0x201302a:     /* CAPP stuff */
    case 0x2013801:     /* CAPP stuff */
    case 0x2013802:     /* CAPP stuff */

        /* P9 CAPP regs */
    case 0x2010841:
    case 0x2010842:
    case 0x201082a:
    case 0x2010828:
    case 0x4010841:
    case 0x4010842:
    case 0x401082a:
    case 0x4010828:

        /* P8 PRD registers */
    case PRD_P8_IPOLL_REG_MASK:
    case PRD_P8_IPOLL_REG_STATUS:
    case PRD_P9_IPOLL_REG_MASK:
    case PRD_P9_IPOLL_REG_STATUS:
        return true;
    default:
        return false;
    }
}

static uint64_t xscom_read(void *opaque, hwaddr addr, unsigned width)
{
    PnvChip *chip = opaque;
    uint32_t pcba = pnv_xscom_pcba(chip, addr);
    uint64_t val = 0;
    MemTxResult result;

    /* Handle some SCOMs here before dispatch */
    val = xscom_read_default(chip, pcba);
    if (val != -1) {
        goto complete;
    }

    val = address_space_ldq(&chip->xscom_as, (uint64_t) pcba << 3,
                            MEMTXATTRS_UNSPECIFIED, &result);
    if (result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "XSCOM read failed at @0x%"
                      HWADDR_PRIx " pcba=0x%08x\n", addr, pcba);
        xscom_complete(current_cpu, HMER_XSCOM_FAIL | HMER_XSCOM_DONE);
        return 0;
    }

complete:
    xscom_complete(current_cpu, HMER_XSCOM_DONE);
    return val;
}

static void xscom_write(void *opaque, hwaddr addr, uint64_t val,
                        unsigned width)
{
    PnvChip *chip = opaque;
    uint32_t pcba = pnv_xscom_pcba(chip, addr);
    MemTxResult result;

    /* Handle some SCOMs here before dispatch */
    if (xscom_write_default(chip, pcba, val)) {
        goto complete;
    }

    address_space_stq(&chip->xscom_as, (uint64_t) pcba << 3, val,
                      MEMTXATTRS_UNSPECIFIED, &result);
    if (result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "XSCOM write failed at @0x%"
                      HWADDR_PRIx " pcba=0x%08x data=0x%" PRIx64 "\n",
                      addr, pcba, val);
        xscom_complete(current_cpu, HMER_XSCOM_FAIL | HMER_XSCOM_DONE);
        return;
    }

complete:
    xscom_complete(current_cpu, HMER_XSCOM_DONE);
}

const MemoryRegionOps pnv_xscom_ops = {
    .read = xscom_read,
    .write = xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

void pnv_xscom_realize(PnvChip *chip, uint64_t size, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(chip);
    char *name;

    name = g_strdup_printf("xscom-%x", chip->chip_id);
    memory_region_init_io(&chip->xscom_mmio, OBJECT(chip), &pnv_xscom_ops,
                          chip, name, size);
    sysbus_init_mmio(sbd, &chip->xscom_mmio);

    memory_region_init(&chip->xscom, OBJECT(chip), name, size);
    address_space_init(&chip->xscom_as, &chip->xscom, name);
    g_free(name);
}

static const TypeInfo pnv_xscom_interface_info = {
    .name = TYPE_PNV_XSCOM_INTERFACE,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(PnvXScomInterfaceClass),
};

static void pnv_xscom_register_types(void)
{
    type_register_static(&pnv_xscom_interface_info);
}

type_init(pnv_xscom_register_types)

typedef struct ForeachPopulateArgs {
    void *fdt;
    int xscom_offset;
} ForeachPopulateArgs;

static int xscom_dt_child(Object *child, void *opaque)
{
    if (object_dynamic_cast(child, TYPE_PNV_XSCOM_INTERFACE)) {
        ForeachPopulateArgs *args = opaque;
        PnvXScomInterface *xd = PNV_XSCOM_INTERFACE(child);
        PnvXScomInterfaceClass *xc = PNV_XSCOM_INTERFACE_GET_CLASS(xd);

        /*
         * Only "realized" devices should be configured in the DT
         */
        if (xc->dt_xscom && DEVICE(child)->realized) {
            _FDT((xc->dt_xscom(xd, args->fdt, args->xscom_offset)));
        }
    }
    return 0;
}

int pnv_dt_xscom(PnvChip *chip, void *fdt, int root_offset,
                 uint64_t xscom_base, uint64_t xscom_size,
                 const char *compat, int compat_size)
{
    uint64_t reg[] = { xscom_base, xscom_size };
    int xscom_offset;
    ForeachPopulateArgs args;
    char *name;

    name = g_strdup_printf("xscom@%" PRIx64, be64_to_cpu(reg[0]));
    xscom_offset = fdt_add_subnode(fdt, root_offset, name);
    _FDT(xscom_offset);
    g_free(name);
    _FDT((fdt_setprop_cell(fdt, xscom_offset, "ibm,chip-id", chip->chip_id)));
    _FDT((fdt_setprop_cell(fdt, xscom_offset, "#address-cells", 1)));
    _FDT((fdt_setprop_cell(fdt, xscom_offset, "#size-cells", 1)));
    _FDT((fdt_setprop(fdt, xscom_offset, "reg", reg, sizeof(reg))));
    _FDT((fdt_setprop(fdt, xscom_offset, "compatible", compat, compat_size)));
    _FDT((fdt_setprop(fdt, xscom_offset, "scom-controller", NULL, 0)));

    args.fdt = fdt;
    args.xscom_offset = xscom_offset;

    /*
     * Loop on the whole object hierarchy to catch all
     * PnvXScomInterface objects which can lie a bit deeper than the
     * first layer.
     */
    object_child_foreach_recursive(OBJECT(chip), xscom_dt_child, &args);
    return 0;
}

void pnv_xscom_add_subregion(PnvChip *chip, hwaddr offset, MemoryRegion *mr)
{
    memory_region_add_subregion(&chip->xscom, offset << 3, mr);
}

void pnv_xscom_region_init(MemoryRegion *mr,
                           struct Object *owner,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size)
{
    memory_region_init_io(mr, owner, ops, opaque, name, size << 3);
}
