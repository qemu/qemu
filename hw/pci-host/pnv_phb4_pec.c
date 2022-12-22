/*
 * QEMU PowerPC PowerNV (POWER9) PHB4 model
 *
 * Copyright (c) 2018-2020, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "target/ppc/cpu.h"
#include "hw/ppc/fdt.h"
#include "hw/pci-host/pnv_phb4_regs.h"
#include "hw/pci-host/pnv_phb4.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_bus.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_chip.h"
#include "hw/qdev-properties.h"
#include "sysemu/sysemu.h"

#include <libfdt.h>

#define phb_pec_error(pec, fmt, ...)                                    \
    qemu_log_mask(LOG_GUEST_ERROR, "phb4_pec[%d:%d]: " fmt "\n",        \
                  (pec)->chip_id, (pec)->index, ## __VA_ARGS__)


static uint64_t pnv_pec_nest_xscom_read(void *opaque, hwaddr addr,
                                        unsigned size)
{
    PnvPhb4PecState *pec = PNV_PHB4_PEC(opaque);
    uint32_t reg = addr >> 3;

    /* TODO: add list of allowed registers and error out if not */
    return pec->nest_regs[reg];
}

static void pnv_pec_nest_xscom_write(void *opaque, hwaddr addr,
                                     uint64_t val, unsigned size)
{
    PnvPhb4PecState *pec = PNV_PHB4_PEC(opaque);
    uint32_t reg = addr >> 3;

    switch (reg) {
    case PEC_NEST_PBCQ_HW_CONFIG:
    case PEC_NEST_DROP_PRIO_CTRL:
    case PEC_NEST_PBCQ_ERR_INJECT:
    case PEC_NEST_PCI_NEST_CLK_TRACE_CTL:
    case PEC_NEST_PBCQ_PMON_CTRL:
    case PEC_NEST_PBCQ_PBUS_ADDR_EXT:
    case PEC_NEST_PBCQ_PRED_VEC_TIMEOUT:
    case PEC_NEST_CAPP_CTRL:
    case PEC_NEST_PBCQ_READ_STK_OVR:
    case PEC_NEST_PBCQ_WRITE_STK_OVR:
    case PEC_NEST_PBCQ_STORE_STK_OVR:
    case PEC_NEST_PBCQ_RETRY_BKOFF_CTRL:
        pec->nest_regs[reg] = val;
        break;
    default:
        phb_pec_error(pec, "%s @0x%"HWADDR_PRIx"=%"PRIx64"\n", __func__,
                      addr, val);
    }
}

static const MemoryRegionOps pnv_pec_nest_xscom_ops = {
    .read = pnv_pec_nest_xscom_read,
    .write = pnv_pec_nest_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static uint64_t pnv_pec_pci_xscom_read(void *opaque, hwaddr addr,
                                       unsigned size)
{
    PnvPhb4PecState *pec = PNV_PHB4_PEC(opaque);
    uint32_t reg = addr >> 3;

    /* TODO: add list of allowed registers and error out if not */
    return pec->pci_regs[reg];
}

static void pnv_pec_pci_xscom_write(void *opaque, hwaddr addr,
                                    uint64_t val, unsigned size)
{
    PnvPhb4PecState *pec = PNV_PHB4_PEC(opaque);
    uint32_t reg = addr >> 3;

    switch (reg) {
    case PEC_PCI_PBAIB_HW_CONFIG:
    case PEC_PCI_PBAIB_READ_STK_OVR:
        pec->pci_regs[reg] = val;
        break;
    default:
        phb_pec_error(pec, "%s @0x%"HWADDR_PRIx"=%"PRIx64"\n", __func__,
                      addr, val);
    }
}

static const MemoryRegionOps pnv_pec_pci_xscom_ops = {
    .read = pnv_pec_pci_xscom_read,
    .write = pnv_pec_pci_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_pec_default_phb_realize(PnvPhb4PecState *pec,
                                        int stack_no,
                                        Error **errp)
{
    PnvPHB *phb = PNV_PHB(qdev_new(TYPE_PNV_PHB));
    int phb_id = pnv_phb4_pec_get_phb_id(pec, stack_no);

    object_property_add_child(OBJECT(pec), "phb[*]", OBJECT(phb));
    object_property_set_link(OBJECT(phb), "pec", OBJECT(pec),
                             &error_abort);
    object_property_set_int(OBJECT(phb), "chip-id", pec->chip_id,
                            &error_fatal);
    object_property_set_int(OBJECT(phb), "index", phb_id,
                            &error_fatal);

    if (!sysbus_realize(SYS_BUS_DEVICE(phb), errp)) {
        return;
    }
}

static void pnv_pec_realize(DeviceState *dev, Error **errp)
{
    PnvPhb4PecState *pec = PNV_PHB4_PEC(dev);
    PnvPhb4PecClass *pecc = PNV_PHB4_PEC_GET_CLASS(pec);
    char name[64];
    int i;

    if (pec->index >= PNV_CHIP_GET_CLASS(pec->chip)->num_pecs) {
        error_setg(errp, "invalid PEC index: %d", pec->index);
        return;
    }

    pec->num_phbs = pecc->num_phbs[pec->index];

    /* Create PHBs if running with defaults */
    if (defaults_enabled()) {
        for (i = 0; i < pec->num_phbs; i++) {
            pnv_pec_default_phb_realize(pec, i, errp);
        }
    }

    /* Initialize the XSCOM regions for the PEC registers */
    snprintf(name, sizeof(name), "xscom-pec-%d.%d-nest", pec->chip_id,
             pec->index);
    pnv_xscom_region_init(&pec->nest_regs_mr, OBJECT(dev),
                          &pnv_pec_nest_xscom_ops, pec, name,
                          PHB4_PEC_NEST_REGS_COUNT);

    snprintf(name, sizeof(name), "xscom-pec-%d.%d-pci", pec->chip_id,
             pec->index);
    pnv_xscom_region_init(&pec->pci_regs_mr, OBJECT(dev),
                          &pnv_pec_pci_xscom_ops, pec, name,
                          PHB4_PEC_PCI_REGS_COUNT);
}

static int pnv_pec_dt_xscom(PnvXScomInterface *dev, void *fdt,
                            int xscom_offset)
{
    PnvPhb4PecState *pec = PNV_PHB4_PEC(dev);
    PnvPhb4PecClass *pecc = PNV_PHB4_PEC_GET_CLASS(dev);
    uint32_t nbase = pecc->xscom_nest_base(pec);
    uint32_t pbase = pecc->xscom_pci_base(pec);
    int offset, i;
    char *name;
    uint32_t reg[] = {
        cpu_to_be32(nbase),
        cpu_to_be32(pecc->xscom_nest_size),
        cpu_to_be32(pbase),
        cpu_to_be32(pecc->xscom_pci_size),
    };

    name = g_strdup_printf("pbcq@%x", nbase);
    offset = fdt_add_subnode(fdt, xscom_offset, name);
    _FDT(offset);
    g_free(name);

    _FDT((fdt_setprop(fdt, offset, "reg", reg, sizeof(reg))));

    _FDT((fdt_setprop_cell(fdt, offset, "ibm,pec-index", pec->index)));
    _FDT((fdt_setprop_cell(fdt, offset, "#address-cells", 1)));
    _FDT((fdt_setprop_cell(fdt, offset, "#size-cells", 0)));
    _FDT((fdt_setprop(fdt, offset, "compatible", pecc->compat,
                      pecc->compat_size)));

    for (i = 0; i < pec->num_phbs; i++) {
        int phb_id = pnv_phb4_pec_get_phb_id(pec, i);
        int stk_offset;

        name = g_strdup_printf("stack@%x", i);
        stk_offset = fdt_add_subnode(fdt, offset, name);
        _FDT(stk_offset);
        g_free(name);
        _FDT((fdt_setprop(fdt, stk_offset, "compatible", pecc->stk_compat,
                          pecc->stk_compat_size)));
        _FDT((fdt_setprop_cell(fdt, stk_offset, "reg", i)));
        _FDT((fdt_setprop_cell(fdt, stk_offset, "ibm,phb-index", phb_id)));
    }

    return 0;
}

static Property pnv_pec_properties[] = {
    DEFINE_PROP_UINT32("index", PnvPhb4PecState, index, 0),
    DEFINE_PROP_UINT32("chip-id", PnvPhb4PecState, chip_id, 0),
    DEFINE_PROP_LINK("chip", PnvPhb4PecState, chip, TYPE_PNV_CHIP,
                     PnvChip *),
    DEFINE_PROP_END_OF_LIST(),
};

static uint32_t pnv_pec_xscom_pci_base(PnvPhb4PecState *pec)
{
    return PNV9_XSCOM_PEC_PCI_BASE + 0x1000000 * pec->index;
}

static uint32_t pnv_pec_xscom_nest_base(PnvPhb4PecState *pec)
{
    return PNV9_XSCOM_PEC_NEST_BASE + 0x400 * pec->index;
}

/*
 * PEC0 -> 1 phb
 * PEC1 -> 2 phb
 * PEC2 -> 3 phbs
 */
static const uint32_t pnv_pec_num_phbs[] = { 1, 2, 3 };

static void pnv_pec_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xdc = PNV_XSCOM_INTERFACE_CLASS(klass);
    PnvPhb4PecClass *pecc = PNV_PHB4_PEC_CLASS(klass);
    static const char compat[] = "ibm,power9-pbcq";
    static const char stk_compat[] = "ibm,power9-phb-stack";

    xdc->dt_xscom = pnv_pec_dt_xscom;

    dc->realize = pnv_pec_realize;
    device_class_set_props(dc, pnv_pec_properties);
    dc->user_creatable = false;

    pecc->xscom_nest_base = pnv_pec_xscom_nest_base;
    pecc->xscom_pci_base  = pnv_pec_xscom_pci_base;
    pecc->xscom_nest_size = PNV9_XSCOM_PEC_NEST_SIZE;
    pecc->xscom_pci_size  = PNV9_XSCOM_PEC_PCI_SIZE;
    pecc->compat = compat;
    pecc->compat_size = sizeof(compat);
    pecc->stk_compat = stk_compat;
    pecc->stk_compat_size = sizeof(stk_compat);
    pecc->version = PNV_PHB4_VERSION;
    pecc->phb_type = TYPE_PNV_PHB4;
    pecc->num_phbs = pnv_pec_num_phbs;
}

static const TypeInfo pnv_pec_type_info = {
    .name          = TYPE_PNV_PHB4_PEC,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvPhb4PecState),
    .class_init    = pnv_pec_class_init,
    .class_size    = sizeof(PnvPhb4PecClass),
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

/*
 * POWER10 definitions
 */

static uint32_t pnv_phb5_pec_xscom_pci_base(PnvPhb4PecState *pec)
{
    return PNV10_XSCOM_PEC_PCI_BASE + 0x1000000 * pec->index;
}

static uint32_t pnv_phb5_pec_xscom_nest_base(PnvPhb4PecState *pec)
{
    /* index goes down ... */
    return PNV10_XSCOM_PEC_NEST_BASE - 0x1000000 * pec->index;
}

/*
 * PEC0 -> 3 stacks
 * PEC1 -> 3 stacks
 */
static const uint32_t pnv_phb5_pec_num_stacks[] = { 3, 3 };

static void pnv_phb5_pec_class_init(ObjectClass *klass, void *data)
{
    PnvPhb4PecClass *pecc = PNV_PHB4_PEC_CLASS(klass);
    static const char compat[] = "ibm,power10-pbcq";
    static const char stk_compat[] = "ibm,power10-phb-stack";

    pecc->xscom_nest_base = pnv_phb5_pec_xscom_nest_base;
    pecc->xscom_pci_base  = pnv_phb5_pec_xscom_pci_base;
    pecc->xscom_nest_size = PNV10_XSCOM_PEC_NEST_SIZE;
    pecc->xscom_pci_size  = PNV10_XSCOM_PEC_PCI_SIZE;
    pecc->compat = compat;
    pecc->compat_size = sizeof(compat);
    pecc->stk_compat = stk_compat;
    pecc->stk_compat_size = sizeof(stk_compat);
    pecc->version = PNV_PHB5_VERSION;
    pecc->phb_type = TYPE_PNV_PHB5;
    pecc->num_phbs = pnv_phb5_pec_num_stacks;
}

static const TypeInfo pnv_phb5_pec_type_info = {
    .name          = TYPE_PNV_PHB5_PEC,
    .parent        = TYPE_PNV_PHB4_PEC,
    .instance_size = sizeof(PnvPhb4PecState),
    .class_init    = pnv_phb5_pec_class_init,
    .class_size    = sizeof(PnvPhb4PecClass),
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_pec_register_types(void)
{
    type_register_static(&pnv_pec_type_info);
    type_register_static(&pnv_phb5_pec_type_info);
}

type_init(pnv_pec_register_types);
