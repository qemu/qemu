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
#include "qemu-common.h"
#include "qemu/log.h"
#include "target/ppc/cpu.h"
#include "hw/ppc/fdt.h"
#include "hw/pci-host/pnv_phb4_regs.h"
#include "hw/pci-host/pnv_phb4.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_bus.h"
#include "hw/ppc/pnv.h"
#include "hw/qdev-properties.h"

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

static uint64_t pnv_pec_stk_nest_xscom_read(void *opaque, hwaddr addr,
                                            unsigned size)
{
    PnvPhb4PecStack *stack = PNV_PHB4_PEC_STACK(opaque);
    uint32_t reg = addr >> 3;

    /* TODO: add list of allowed registers and error out if not */
    return stack->nest_regs[reg];
}

static void pnv_pec_stk_update_map(PnvPhb4PecStack *stack)
{
    PnvPhb4PecState *pec = stack->pec;
    MemoryRegion *sysmem = pec->system_memory;
    uint64_t bar_en = stack->nest_regs[PEC_NEST_STK_BAR_EN];
    uint64_t bar, mask, size;
    char name[64];

    /*
     * NOTE: This will really not work well if those are remapped
     * after the PHB has created its sub regions. We could do better
     * if we had a way to resize regions but we don't really care
     * that much in practice as the stuff below really only happens
     * once early during boot
     */

    /* Handle unmaps */
    if (memory_region_is_mapped(&stack->mmbar0) &&
        !(bar_en & PEC_NEST_STK_BAR_EN_MMIO0)) {
        memory_region_del_subregion(sysmem, &stack->mmbar0);
    }
    if (memory_region_is_mapped(&stack->mmbar1) &&
        !(bar_en & PEC_NEST_STK_BAR_EN_MMIO1)) {
        memory_region_del_subregion(sysmem, &stack->mmbar1);
    }
    if (memory_region_is_mapped(&stack->phbbar) &&
        !(bar_en & PEC_NEST_STK_BAR_EN_PHB)) {
        memory_region_del_subregion(sysmem, &stack->phbbar);
    }
    if (memory_region_is_mapped(&stack->intbar) &&
        !(bar_en & PEC_NEST_STK_BAR_EN_INT)) {
        memory_region_del_subregion(sysmem, &stack->intbar);
    }

    /* Update PHB */
    pnv_phb4_update_regions(stack);

    /* Handle maps */
    if (!memory_region_is_mapped(&stack->mmbar0) &&
        (bar_en & PEC_NEST_STK_BAR_EN_MMIO0)) {
        bar = stack->nest_regs[PEC_NEST_STK_MMIO_BAR0] >> 8;
        mask = stack->nest_regs[PEC_NEST_STK_MMIO_BAR0_MASK];
        size = ((~mask) >> 8) + 1;
        snprintf(name, sizeof(name), "pec-%d.%d-stack-%d-mmio0",
                 pec->chip_id, pec->index, stack->stack_no);
        memory_region_init(&stack->mmbar0, OBJECT(stack), name, size);
        memory_region_add_subregion(sysmem, bar, &stack->mmbar0);
        stack->mmio0_base = bar;
        stack->mmio0_size = size;
    }
    if (!memory_region_is_mapped(&stack->mmbar1) &&
        (bar_en & PEC_NEST_STK_BAR_EN_MMIO1)) {
        bar = stack->nest_regs[PEC_NEST_STK_MMIO_BAR1] >> 8;
        mask = stack->nest_regs[PEC_NEST_STK_MMIO_BAR1_MASK];
        size = ((~mask) >> 8) + 1;
        snprintf(name, sizeof(name), "pec-%d.%d-stack-%d-mmio1",
                 pec->chip_id, pec->index, stack->stack_no);
        memory_region_init(&stack->mmbar1, OBJECT(stack), name, size);
        memory_region_add_subregion(sysmem, bar, &stack->mmbar1);
        stack->mmio1_base = bar;
        stack->mmio1_size = size;
    }
    if (!memory_region_is_mapped(&stack->phbbar) &&
        (bar_en & PEC_NEST_STK_BAR_EN_PHB)) {
        bar = stack->nest_regs[PEC_NEST_STK_PHB_REGS_BAR] >> 8;
        size = PNV_PHB4_NUM_REGS << 3;
        snprintf(name, sizeof(name), "pec-%d.%d-stack-%d-phb",
                 pec->chip_id, pec->index, stack->stack_no);
        memory_region_init(&stack->phbbar, OBJECT(stack), name, size);
        memory_region_add_subregion(sysmem, bar, &stack->phbbar);
    }
    if (!memory_region_is_mapped(&stack->intbar) &&
        (bar_en & PEC_NEST_STK_BAR_EN_INT)) {
        bar = stack->nest_regs[PEC_NEST_STK_INT_BAR] >> 8;
        size = PNV_PHB4_MAX_INTs << 16;
        snprintf(name, sizeof(name), "pec-%d.%d-stack-%d-int",
                 stack->pec->chip_id, stack->pec->index, stack->stack_no);
        memory_region_init(&stack->intbar, OBJECT(stack), name, size);
        memory_region_add_subregion(sysmem, bar, &stack->intbar);
    }

    /* Update PHB */
    pnv_phb4_update_regions(stack);
}

static void pnv_pec_stk_nest_xscom_write(void *opaque, hwaddr addr,
                                         uint64_t val, unsigned size)
{
    PnvPhb4PecStack *stack = PNV_PHB4_PEC_STACK(opaque);
    PnvPhb4PecState *pec = stack->pec;
    uint32_t reg = addr >> 3;

    switch (reg) {
    case PEC_NEST_STK_PCI_NEST_FIR:
        stack->nest_regs[PEC_NEST_STK_PCI_NEST_FIR] = val;
        break;
    case PEC_NEST_STK_PCI_NEST_FIR_CLR:
        stack->nest_regs[PEC_NEST_STK_PCI_NEST_FIR] &= val;
        break;
    case PEC_NEST_STK_PCI_NEST_FIR_SET:
        stack->nest_regs[PEC_NEST_STK_PCI_NEST_FIR] |= val;
        break;
    case PEC_NEST_STK_PCI_NEST_FIR_MSK:
        stack->nest_regs[PEC_NEST_STK_PCI_NEST_FIR_MSK] = val;
        break;
    case PEC_NEST_STK_PCI_NEST_FIR_MSKC:
        stack->nest_regs[PEC_NEST_STK_PCI_NEST_FIR_MSK] &= val;
        break;
    case PEC_NEST_STK_PCI_NEST_FIR_MSKS:
        stack->nest_regs[PEC_NEST_STK_PCI_NEST_FIR_MSK] |= val;
        break;
    case PEC_NEST_STK_PCI_NEST_FIR_ACT0:
    case PEC_NEST_STK_PCI_NEST_FIR_ACT1:
        stack->nest_regs[reg] = val;
        break;
    case PEC_NEST_STK_PCI_NEST_FIR_WOF:
        stack->nest_regs[reg] = 0;
        break;
    case PEC_NEST_STK_ERR_REPORT_0:
    case PEC_NEST_STK_ERR_REPORT_1:
    case PEC_NEST_STK_PBCQ_GNRL_STATUS:
        /* Flag error ? */
        break;
    case PEC_NEST_STK_PBCQ_MODE:
        stack->nest_regs[reg] = val & 0xff00000000000000ull;
        break;
    case PEC_NEST_STK_MMIO_BAR0:
    case PEC_NEST_STK_MMIO_BAR0_MASK:
    case PEC_NEST_STK_MMIO_BAR1:
    case PEC_NEST_STK_MMIO_BAR1_MASK:
        if (stack->nest_regs[PEC_NEST_STK_BAR_EN] &
            (PEC_NEST_STK_BAR_EN_MMIO0 |
             PEC_NEST_STK_BAR_EN_MMIO1)) {
            phb_pec_error(pec, "Changing enabled BAR unsupported\n");
        }
        stack->nest_regs[reg] = val & 0xffffffffff000000ull;
        break;
    case PEC_NEST_STK_PHB_REGS_BAR:
        if (stack->nest_regs[PEC_NEST_STK_BAR_EN] & PEC_NEST_STK_BAR_EN_PHB) {
            phb_pec_error(pec, "Changing enabled BAR unsupported\n");
        }
        stack->nest_regs[reg] = val & 0xffffffffffc00000ull;
        break;
    case PEC_NEST_STK_INT_BAR:
        if (stack->nest_regs[PEC_NEST_STK_BAR_EN] & PEC_NEST_STK_BAR_EN_INT) {
            phb_pec_error(pec, "Changing enabled BAR unsupported\n");
        }
        stack->nest_regs[reg] = val & 0xfffffff000000000ull;
        break;
    case PEC_NEST_STK_BAR_EN:
        stack->nest_regs[reg] = val & 0xf000000000000000ull;
        pnv_pec_stk_update_map(stack);
        break;
    case PEC_NEST_STK_DATA_FRZ_TYPE:
    case PEC_NEST_STK_PBCQ_TUN_BAR:
        /* Not used for now */
        stack->nest_regs[reg] = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "phb4_pec: nest_xscom_write 0x%"HWADDR_PRIx
                      "=%"PRIx64"\n", addr, val);
    }
}

static const MemoryRegionOps pnv_pec_stk_nest_xscom_ops = {
    .read = pnv_pec_stk_nest_xscom_read,
    .write = pnv_pec_stk_nest_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static uint64_t pnv_pec_stk_pci_xscom_read(void *opaque, hwaddr addr,
                                           unsigned size)
{
    PnvPhb4PecStack *stack = PNV_PHB4_PEC_STACK(opaque);
    uint32_t reg = addr >> 3;

    /* TODO: add list of allowed registers and error out if not */
    return stack->pci_regs[reg];
}

static void pnv_pec_stk_pci_xscom_write(void *opaque, hwaddr addr,
                                        uint64_t val, unsigned size)
{
    PnvPhb4PecStack *stack = PNV_PHB4_PEC_STACK(opaque);
    uint32_t reg = addr >> 3;

    switch (reg) {
    case PEC_PCI_STK_PCI_FIR:
        stack->nest_regs[reg] = val;
        break;
    case PEC_PCI_STK_PCI_FIR_CLR:
        stack->nest_regs[PEC_PCI_STK_PCI_FIR] &= val;
        break;
    case PEC_PCI_STK_PCI_FIR_SET:
        stack->nest_regs[PEC_PCI_STK_PCI_FIR] |= val;
        break;
    case PEC_PCI_STK_PCI_FIR_MSK:
        stack->nest_regs[reg] = val;
        break;
    case PEC_PCI_STK_PCI_FIR_MSKC:
        stack->nest_regs[PEC_PCI_STK_PCI_FIR_MSK] &= val;
        break;
    case PEC_PCI_STK_PCI_FIR_MSKS:
        stack->nest_regs[PEC_PCI_STK_PCI_FIR_MSK] |= val;
        break;
    case PEC_PCI_STK_PCI_FIR_ACT0:
    case PEC_PCI_STK_PCI_FIR_ACT1:
        stack->nest_regs[reg] = val;
        break;
    case PEC_PCI_STK_PCI_FIR_WOF:
        stack->nest_regs[reg] = 0;
        break;
    case PEC_PCI_STK_ETU_RESET:
        stack->nest_regs[reg] = val & 0x8000000000000000ull;
        /* TODO: Implement reset */
        break;
    case PEC_PCI_STK_PBAIB_ERR_REPORT:
        break;
    case PEC_PCI_STK_PBAIB_TX_CMD_CRED:
    case PEC_PCI_STK_PBAIB_TX_DAT_CRED:
        stack->nest_regs[reg] = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "phb4_pec_stk: pci_xscom_write 0x%"HWADDR_PRIx
                      "=%"PRIx64"\n", addr, val);
    }
}

static const MemoryRegionOps pnv_pec_stk_pci_xscom_ops = {
    .read = pnv_pec_stk_pci_xscom_read,
    .write = pnv_pec_stk_pci_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_pec_instance_init(Object *obj)
{
    PnvPhb4PecState *pec = PNV_PHB4_PEC(obj);
    int i;

    for (i = 0; i < PHB4_PEC_MAX_STACKS; i++) {
        object_initialize_child(obj, "stack[*]", &pec->stacks[i],
                                TYPE_PNV_PHB4_PEC_STACK);
    }
}

static void pnv_pec_realize(DeviceState *dev, Error **errp)
{
    PnvPhb4PecState *pec = PNV_PHB4_PEC(dev);
    char name[64];
    int i;

    assert(pec->system_memory);

    /* Create stacks */
    for (i = 0; i < pec->num_stacks; i++) {
        PnvPhb4PecStack *stack = &pec->stacks[i];
        Object *stk_obj = OBJECT(stack);

        object_property_set_int(stk_obj, "stack-no", i, &error_abort);
        object_property_set_link(stk_obj, "pec", OBJECT(pec), &error_abort);
        if (!qdev_realize(DEVICE(stk_obj), NULL, errp)) {
            return;
        }
    }
    for (; i < PHB4_PEC_MAX_STACKS; i++) {
        object_unparent(OBJECT(&pec->stacks[i]));
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

    for (i = 0; i < pec->num_stacks; i++) {
        PnvPhb4PecStack *stack = &pec->stacks[i];
        PnvPHB4 *phb = &stack->phb;
        int stk_offset;

        name = g_strdup_printf("stack@%x", i);
        stk_offset = fdt_add_subnode(fdt, offset, name);
        _FDT(stk_offset);
        g_free(name);
        _FDT((fdt_setprop(fdt, stk_offset, "compatible", pecc->stk_compat,
                          pecc->stk_compat_size)));
        _FDT((fdt_setprop_cell(fdt, stk_offset, "reg", i)));
        _FDT((fdt_setprop_cell(fdt, stk_offset, "ibm,phb-index", phb->phb_id)));
    }

    return 0;
}

static Property pnv_pec_properties[] = {
        DEFINE_PROP_UINT32("index", PnvPhb4PecState, index, 0),
        DEFINE_PROP_UINT32("num-stacks", PnvPhb4PecState, num_stacks, 0),
        DEFINE_PROP_UINT32("chip-id", PnvPhb4PecState, chip_id, 0),
        DEFINE_PROP_LINK("system-memory", PnvPhb4PecState, system_memory,
                     TYPE_MEMORY_REGION, MemoryRegion *),
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
}

static const TypeInfo pnv_pec_type_info = {
    .name          = TYPE_PNV_PHB4_PEC,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvPhb4PecState),
    .instance_init = pnv_pec_instance_init,
    .class_init    = pnv_pec_class_init,
    .class_size    = sizeof(PnvPhb4PecClass),
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_pec_stk_instance_init(Object *obj)
{
    PnvPhb4PecStack *stack = PNV_PHB4_PEC_STACK(obj);

    object_initialize_child(obj, "phb", &stack->phb, TYPE_PNV_PHB4);
}

static void pnv_pec_stk_realize(DeviceState *dev, Error **errp)
{
    PnvPhb4PecStack *stack = PNV_PHB4_PEC_STACK(dev);
    PnvPhb4PecState *pec = stack->pec;
    char name[64];

    assert(pec);

    /* Initialize the XSCOM regions for the stack registers */
    snprintf(name, sizeof(name), "xscom-pec-%d.%d-nest-stack-%d",
             pec->chip_id, pec->index, stack->stack_no);
    pnv_xscom_region_init(&stack->nest_regs_mr, OBJECT(stack),
                          &pnv_pec_stk_nest_xscom_ops, stack, name,
                          PHB4_PEC_NEST_STK_REGS_COUNT);

    snprintf(name, sizeof(name), "xscom-pec-%d.%d-pci-stack-%d",
             pec->chip_id, pec->index, stack->stack_no);
    pnv_xscom_region_init(&stack->pci_regs_mr, OBJECT(stack),
                          &pnv_pec_stk_pci_xscom_ops, stack, name,
                          PHB4_PEC_PCI_STK_REGS_COUNT);

    /* PHB pass-through */
    snprintf(name, sizeof(name), "xscom-pec-%d.%d-pci-stack-%d-phb",
             pec->chip_id, pec->index, stack->stack_no);
    pnv_xscom_region_init(&stack->phb_regs_mr, OBJECT(&stack->phb),
                          &pnv_phb4_xscom_ops, &stack->phb, name, 0x40);

    /*
     * Let the machine/chip realize the PHB object to customize more
     * easily some fields
     */
}

static Property pnv_pec_stk_properties[] = {
        DEFINE_PROP_UINT32("stack-no", PnvPhb4PecStack, stack_no, 0),
        DEFINE_PROP_LINK("pec", PnvPhb4PecStack, pec, TYPE_PNV_PHB4_PEC,
                         PnvPhb4PecState *),
        DEFINE_PROP_END_OF_LIST(),
};

static void pnv_pec_stk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, pnv_pec_stk_properties);
    dc->realize = pnv_pec_stk_realize;
    dc->user_creatable = false;

    /* TODO: reset regs ? */
}

static const TypeInfo pnv_pec_stk_type_info = {
    .name          = TYPE_PNV_PHB4_PEC_STACK,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvPhb4PecStack),
    .instance_init = pnv_pec_stk_instance_init,
    .class_init    = pnv_pec_stk_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_pec_register_types(void)
{
    type_register_static(&pnv_pec_type_info);
    type_register_static(&pnv_pec_stk_type_info);
}

type_init(pnv_pec_register_types);
