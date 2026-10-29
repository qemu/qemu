/*
 * QEMU PowerPC PowerNV (POWER10) PHB5 PEC model
 *
 * Copyright (c) 2018-2026, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/pci-host/pnv_phb4.h"
#include "hw/pci-host/pnv_phb5.h"
#include "hw/ppc/pnv_xscom.h"

#define XPEC_PCI_CPLT_OFFSET                        0x1000000ULL

/*
 * POWER10 definitions
 */
static uint32_t pnv_phb5_pec_xscom_cplt_base(PnvPhb4PecState *pec)
{
    return PNV10_XSCOM_PEC_NEST_CPLT_BASE + XPEC_PCI_CPLT_OFFSET * pec->index;
}

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

static void pnv_phb5_pec_class_init(ObjectClass *klass, const void *data)
{
    PnvPhb4PecClass *pecc = PNV_PHB4_PEC_CLASS(klass);
    static const char compat[] = "ibm,power10-pbcq";
    static const char stk_compat[] = "ibm,power10-phb-stack";

    pecc->xscom_cplt_base = pnv_phb5_pec_xscom_cplt_base;
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
    .interfaces    = (const InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_phb5_pec_register_types(void)
{
    type_register_static(&pnv_phb5_pec_type_info);
}

type_init(pnv_phb5_pec_register_types);
