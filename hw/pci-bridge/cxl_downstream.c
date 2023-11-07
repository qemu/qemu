/*
 * Emulated CXL Switch Downstream Port
 *
 * Copyright (c) 2022 Huawei Technologies.
 *
 * Based on xio3130_downstream.c
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/pci/msi.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_port.h"
#include "hw/cxl/cxl.h"
#include "qapi/error.h"

typedef struct CXLDownstreamPort {
    /*< private >*/
    PCIESlot parent_obj;

    /*< public >*/
    CXLComponentState cxl_cstate;
} CXLDownstreamPort;

#define CXL_DOWNSTREAM_PORT_MSI_OFFSET 0x70
#define CXL_DOWNSTREAM_PORT_MSI_NR_VECTOR 1
#define CXL_DOWNSTREAM_PORT_EXP_OFFSET 0x90
#define CXL_DOWNSTREAM_PORT_AER_OFFSET 0x100
#define CXL_DOWNSTREAM_PORT_DVSEC_OFFSET        \
    (CXL_DOWNSTREAM_PORT_AER_OFFSET + PCI_ERR_SIZEOF)

static void latch_registers(CXLDownstreamPort *dsp)
{
    uint32_t *reg_state = dsp->cxl_cstate.crb.cache_mem_registers;
    uint32_t *write_msk = dsp->cxl_cstate.crb.cache_mem_regs_write_mask;

    cxl_component_register_init_common(reg_state, write_msk,
                                       CXL2_DOWNSTREAM_PORT);
}

/* TODO: Look at sharing this code across all CXL port types */
static void cxl_dsp_dvsec_write_config(PCIDevice *dev, uint32_t addr,
                                      uint32_t val, int len)
{
    CXLDownstreamPort *dsp = CXL_DSP(dev);
    CXLComponentState *cxl_cstate = &dsp->cxl_cstate;

    if (range_contains(&cxl_cstate->dvsecs[EXTENSIONS_PORT_DVSEC], addr)) {
        uint8_t *reg = &dev->config[addr];
        addr -= cxl_cstate->dvsecs[EXTENSIONS_PORT_DVSEC].lob;
        if (addr == PORT_CONTROL_OFFSET) {
            if (pci_get_word(reg) & PORT_CONTROL_UNMASK_SBR) {
                /* unmask SBR */
                qemu_log_mask(LOG_UNIMP, "SBR mask control is not supported\n");
            }
            if (pci_get_word(reg) & PORT_CONTROL_ALT_MEMID_EN) {
                /* Alt Memory & ID Space Enable */
                qemu_log_mask(LOG_UNIMP,
                              "Alt Memory & ID space is not supported\n");

            }
        }
    }
}

static void cxl_dsp_config_write(PCIDevice *d, uint32_t address,
                                 uint32_t val, int len)
{
    uint16_t slt_ctl, slt_sta;

    pcie_cap_slot_get(d, &slt_ctl, &slt_sta);
    pci_bridge_write_config(d, address, val, len);
    pcie_cap_flr_write_config(d, address, val, len);
    pcie_cap_slot_write_config(d, slt_ctl, slt_sta, address, val, len);
    pcie_aer_write_config(d, address, val, len);

    cxl_dsp_dvsec_write_config(d, address, val, len);
}

static void cxl_dsp_reset(DeviceState *qdev)
{
    PCIDevice *d = PCI_DEVICE(qdev);
    CXLDownstreamPort *dsp = CXL_DSP(qdev);

    pcie_cap_deverr_reset(d);
    pcie_cap_slot_reset(d);
    pcie_cap_arifwd_reset(d);
    pci_bridge_reset(qdev);

    latch_registers(dsp);
}

static void build_dvsecs(CXLComponentState *cxl)
{
    uint8_t *dvsec;

    dvsec = (uint8_t *)&(CXLDVSECPortExt){ 0 };
    cxl_component_create_dvsec(cxl, CXL2_DOWNSTREAM_PORT,
                               EXTENSIONS_PORT_DVSEC_LENGTH,
                               EXTENSIONS_PORT_DVSEC,
                               EXTENSIONS_PORT_DVSEC_REVID, dvsec);

    dvsec = (uint8_t *)&(CXLDVSECPortFlexBus){
        .cap                     = 0x27, /* Cache, IO, Mem, non-MLD */
        .ctrl                    = 0x02, /* IO always enabled */
        .status                  = 0x26, /* same */
        .rcvd_mod_ts_data_phase1 = 0xef, /* WTF? */
    };
    cxl_component_create_dvsec(cxl, CXL2_DOWNSTREAM_PORT,
                               PCIE_FLEXBUS_PORT_DVSEC_LENGTH_2_0,
                               PCIE_FLEXBUS_PORT_DVSEC,
                               PCIE_FLEXBUS_PORT_DVSEC_REVID_2_0, dvsec);

    dvsec = (uint8_t *)&(CXLDVSECPortGPF){
        .rsvd        = 0,
        .phase1_ctrl = 1, /* 1μs timeout */
        .phase2_ctrl = 1, /* 1μs timeout */
    };
    cxl_component_create_dvsec(cxl, CXL2_DOWNSTREAM_PORT,
                               GPF_PORT_DVSEC_LENGTH, GPF_PORT_DVSEC,
                               GPF_PORT_DVSEC_REVID, dvsec);

    dvsec = (uint8_t *)&(CXLDVSECRegisterLocator){
        .rsvd         = 0,
        .reg0_base_lo = RBI_COMPONENT_REG | CXL_COMPONENT_REG_BAR_IDX,
        .reg0_base_hi = 0,
    };
    cxl_component_create_dvsec(cxl, CXL2_DOWNSTREAM_PORT,
                               REG_LOC_DVSEC_LENGTH, REG_LOC_DVSEC,
                               REG_LOC_DVSEC_REVID, dvsec);
}

static void cxl_dsp_realize(PCIDevice *d, Error **errp)
{
    PCIEPort *p = PCIE_PORT(d);
    PCIESlot *s = PCIE_SLOT(d);
    CXLDownstreamPort *dsp = CXL_DSP(d);
    CXLComponentState *cxl_cstate = &dsp->cxl_cstate;
    ComponentRegisters *cregs = &cxl_cstate->crb;
    MemoryRegion *component_bar = &cregs->component_registers;
    int rc;

    pci_bridge_initfn(d, TYPE_PCIE_BUS);
    pcie_port_init_reg(d);

    rc = msi_init(d, CXL_DOWNSTREAM_PORT_MSI_OFFSET,
                  CXL_DOWNSTREAM_PORT_MSI_NR_VECTOR,
                  true, true, errp);
    if (rc) {
        assert(rc == -ENOTSUP);
        goto err_bridge;
    }

    rc = pcie_cap_init(d, CXL_DOWNSTREAM_PORT_EXP_OFFSET,
                       PCI_EXP_TYPE_DOWNSTREAM, p->port,
                       errp);
    if (rc < 0) {
        goto err_msi;
    }

    pcie_cap_flr_init(d);
    pcie_cap_deverr_init(d);
    pcie_cap_slot_init(d, s);
    pcie_cap_arifwd_init(d);

    pcie_chassis_create(s->chassis);
    rc = pcie_chassis_add_slot(s);
    if (rc < 0) {
        error_setg(errp, "Can't add chassis slot, error %d", rc);
        goto err_pcie_cap;
    }

    rc = pcie_aer_init(d, PCI_ERR_VER, CXL_DOWNSTREAM_PORT_AER_OFFSET,
                       PCI_ERR_SIZEOF, errp);
    if (rc < 0) {
        goto err_chassis;
    }

    cxl_cstate->dvsec_offset = CXL_DOWNSTREAM_PORT_DVSEC_OFFSET;
    cxl_cstate->pdev = d;
    build_dvsecs(cxl_cstate);
    cxl_component_register_block_init(OBJECT(d), cxl_cstate, TYPE_CXL_DSP);
    pci_register_bar(d, CXL_COMPONENT_REG_BAR_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_TYPE_64,
                     component_bar);

    return;

 err_chassis:
    pcie_chassis_del_slot(s);
 err_pcie_cap:
    pcie_cap_exit(d);
 err_msi:
    msi_uninit(d);
 err_bridge:
    pci_bridge_exitfn(d);
}

static void cxl_dsp_exitfn(PCIDevice *d)
{
    PCIESlot *s = PCIE_SLOT(d);

    pcie_aer_exit(d);
    pcie_chassis_del_slot(s);
    pcie_cap_exit(d);
    msi_uninit(d);
    pci_bridge_exitfn(d);
}

static void cxl_dsp_instance_post_init(Object *obj)
{
    PCIESlot *s = PCIE_SLOT(obj);

    if (!s->speed) {
        s->speed = QEMU_PCI_EXP_LNK_2_5GT;
    }

    if (!s->width) {
        s->width = QEMU_PCI_EXP_LNK_X1;
    }
}

static void cxl_dsp_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(oc);

    k->config_write = cxl_dsp_config_write;
    k->realize = cxl_dsp_realize;
    k->exit = cxl_dsp_exitfn;
    k->vendor_id = 0x19e5; /* Huawei */
    k->device_id = 0xa129; /* Emulated CXL Switch Downstream Port */
    k->revision = 0;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "CXL Switch Downstream Port";
    dc->reset = cxl_dsp_reset;
}

static const TypeInfo cxl_dsp_info = {
    .name = TYPE_CXL_DSP,
    .instance_size = sizeof(CXLDownstreamPort),
    .parent = TYPE_PCIE_SLOT,
    .instance_post_init = cxl_dsp_instance_post_init,
    .class_init = cxl_dsp_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CXL_DEVICE },
        { }
    },
};

static void cxl_dsp_register_type(void)
{
    type_register_static(&cxl_dsp_info);
}

type_init(cxl_dsp_register_type);
