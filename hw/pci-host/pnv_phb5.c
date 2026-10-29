/*
 * QEMU PowerPC PowerNV (POWER10) PHB5 model
 *
 * Copyright (c) 2018-2026, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/bswap.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "target/ppc/cpu.h"
#include "hw/pci-host/pnv_phb4_regs.h"
#include "hw/pci-host/pnv_phb4.h"
#include "hw/pci-host/pnv_phb5.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "qom/object.h"
#include "trace.h"
#include "system/reset.h"

/*
 * Get the PCI-E capability offset from the root-port
 */
static uint32_t get_exp_offset(PCIDevice *pdev)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(pdev);

    return rpc->exp_offset;
}

void pnv_phb5_cfg_core_reset(PCIDevice *d)
{
    uint8_t *conf = d->config;
    uint32_t exp_offset = get_exp_offset(d);

    pci_set_word(conf + PCI_COMMAND, PCI_COMMAND_SERR);
    pci_set_word(conf + PCI_STATUS, PCI_STATUS_CAP_LIST);
    pci_set_long(conf + PCI_CLASS_REVISION, 0x06040000);
    pci_set_long(conf + PCI_CACHE_LINE_SIZE, BIT(16));
    pci_set_word(conf + PCI_MEMORY_BASE, BIT(4));
    pci_set_word(conf + PCI_PREF_MEMORY_BASE, BIT(0) | BIT(4));
    pci_set_word(conf + PCI_PREF_MEMORY_LIMIT, PCI_PREF_RANGE_TYPE_64);
    pci_set_long(conf + PCI_CAPABILITY_LIST, BIT(6));
    pci_set_word(conf + PCI_BRIDGE_CONTROL, PCI_BRIDGE_CTL_SERR);
    pci_set_long(conf + PCI_BRIDGE_CONTROL + PCI_PM_PMC, 0xC8034801);

    pci_set_long(conf + exp_offset, 0x420010);
    pci_set_long(conf + exp_offset + PCI_EXP_DEVCAP,  0x8022);
    pci_set_long(conf + exp_offset + PCI_EXP_DEVCTL, PCI_EXP_DEVCTL_EXT_TAG
                                              | PCI_EXP_DEVCTL_PAYLOAD_512B);
    pci_set_long(conf + exp_offset + PCI_EXP_LNKCAP, PCI_EXP_LNKCAP_LBNC
                 | PCI_EXP_LNKCAP_DLLLARC | BIT(8) | PCI_EXP_LNKCAP_SLS_32_0GB);
    pci_set_word(conf + exp_offset + PCI_EXP_LNKCTL, PCI_EXP_LNKCTL_RCB);
    pci_set_long(conf + exp_offset + PCI_EXP_SLTCTL,
                                                   PCI_EXP_SLTCTL_ASPL_DISABLE);
    pci_set_long(conf + exp_offset + PCI_EXP_DEVCAP2, BIT(16)
                  | PCI_EXP_DEVCAP2_ARI | PCI_EXP_DEVCAP2_COMP_TMOUT_DIS | 0xF);
    pci_set_long(conf + exp_offset + PCI_EXP_DEVCTL2, PCI_EXP_DEVCTL2_ARI);
    pci_set_long(conf + exp_offset + PCI_EXP_LNKCAP2, BIT(23)
                       | PCI_EXP_LNKCAP2_SLS_32_0GB
                       | PCI_EXP_LNKCAP2_SLS_16_0GB | PCI_EXP_LNKCAP2_SLS_8_0GB
                       | PCI_EXP_LNKCAP2_SLS_5_0GB | PCI_EXP_LNKCAP2_SLS_2_5GB);
    pci_set_long(conf + PHB_AER_ECAP, PCI_EXT_CAP(0x1, 0x1, 0x148));
    pci_set_long(conf + PHB_SEC_ECAP, (0x1A0 << 20) | BIT(16)
                                                       | PCI_EXT_CAP_ID_SECPCI);
    pci_set_long(conf + PHB_LMR_ECAP, 0x1E810027);
    /* LMR - Margining Lane Control / Status Register # 2 to 16 */
    for (int i = PHB_LMR_CTLSTA_2 ; i <= PHB_LMR_CTLSTA_16 ; i += 4) {
        pci_set_long(conf + i, 0x9C38);
    }

    pci_set_long(conf + PHB_DLF_ECAP, 0x1F410025);
    pci_set_long(conf + PHB_DLF_CAP,  0x80000001);
    pci_set_long(conf + P16_ECAP, 0x22410026);
    pci_set_long(conf + P32_ECAP, 0x1002A);
    pci_set_long(conf + P32_CAP,  0x103);
}

static void pnv_phb5_pbl_core_reset(PnvPHB4 *phb)
{
    /* Zero all PBL registers initially */
    for (int i = PHB_PBL_CONTROL ; i <= PHB_PBL_ERR1_STATUS_MASK ; i += 8) {
        phb->regs[i >> 3] = 0x0;
    }

    /* Set PHB5 specific register values */
    phb->regs[PHB_PBL_CONTROL       >> 3] = 0xC009000000000000;
    phb->regs[PHB_PBL_TIMEOUT_CTRL  >> 3] = 0x2020000000000000;
    phb->regs[PHB_PBL_NPTAG_ENABLE  >> 3] = 0xFFFFFFFF00000000;
    phb->regs[PHB_PBL_SYS_LINK_INIT >> 3] = 0x80088B4642473000;
}

static void pnv_phb5_reset(Object *obj, ResetType type)
{
    PnvPHB4 *phb = PNV_PHB4(obj);

    pnv_phb5_pbl_core_reset(phb);
}

static void pnv_phb5_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = pnv_phb5_reset;
}

static const TypeInfo pnv_phb5_type_info = {
    .name          = TYPE_PNV_PHB5,
    .parent        = TYPE_PNV_PHB4,
    .instance_size = sizeof(PnvPHB4),
    .class_init    = pnv_phb5_class_init
};

static void pnv_phb5_register_types(void)
{
    type_register_static(&pnv_phb5_type_info);
}

type_init(pnv_phb5_register_types);
