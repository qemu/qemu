/*
 * CXL 2.0 Root Port Implementation
 *
 * Copyright(C) 2020 Intel Corporation.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/range.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pcie_port.h"
#include "hw/pci/msi.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "hw/cxl/cxl.h"

#define CXL_ROOT_PORT_DID 0x7075

#define CXL_RP_MSI_OFFSET               0x60
#define CXL_RP_MSI_SUPPORTED_FLAGS      PCI_MSI_FLAGS_MASKBIT
#define CXL_RP_MSI_NR_VECTOR            2

/* Copied from the gen root port which we derive */
#define GEN_PCIE_ROOT_PORT_AER_OFFSET 0x100
#define GEN_PCIE_ROOT_PORT_ACS_OFFSET \
    (GEN_PCIE_ROOT_PORT_AER_OFFSET + PCI_ERR_SIZEOF)
#define CXL_ROOT_PORT_DVSEC_OFFSET \
    (GEN_PCIE_ROOT_PORT_ACS_OFFSET + PCI_ACS_SIZEOF)

typedef struct CXLRootPort {
    /*< private >*/
    PCIESlot parent_obj;

    CXLComponentState cxl_cstate;
    PCIResReserve res_reserve;
} CXLRootPort;

#define TYPE_CXL_ROOT_PORT "cxl-rp"
DECLARE_INSTANCE_CHECKER(CXLRootPort, CXL_ROOT_PORT, TYPE_CXL_ROOT_PORT)

/*
 * If two MSI vector are allocated, Advanced Error Interrupt Message Number
 * is 1. otherwise 0.
 * 17.12.5.10 RPERRSTS,  32:27 bit Advanced Error Interrupt Message Number.
 */
static uint8_t cxl_rp_aer_vector(const PCIDevice *d)
{
    switch (msi_nr_vectors_allocated(d)) {
    case 1:
        return 0;
    case 2:
        return 1;
    case 4:
    case 8:
    case 16:
    case 32:
    default:
        break;
    }
    abort();
    return 0;
}

static int cxl_rp_interrupts_init(PCIDevice *d, Error **errp)
{
    int rc;

    rc = msi_init(d, CXL_RP_MSI_OFFSET, CXL_RP_MSI_NR_VECTOR,
                  CXL_RP_MSI_SUPPORTED_FLAGS & PCI_MSI_FLAGS_64BIT,
                  CXL_RP_MSI_SUPPORTED_FLAGS & PCI_MSI_FLAGS_MASKBIT,
                  errp);
    if (rc < 0) {
        assert(rc == -ENOTSUP);
    }

    return rc;
}

static void cxl_rp_interrupts_uninit(PCIDevice *d)
{
    msi_uninit(d);
}

static void latch_registers(CXLRootPort *crp)
{
    uint32_t *reg_state = crp->cxl_cstate.crb.cache_mem_registers;
    uint32_t *write_msk = crp->cxl_cstate.crb.cache_mem_regs_write_mask;

    cxl_component_register_init_common(reg_state, write_msk, CXL2_ROOT_PORT);
}

static void build_dvsecs(CXLComponentState *cxl)
{
    uint8_t *dvsec;

    dvsec = (uint8_t *)&(CXLDVSECPortExt){ 0 };
    cxl_component_create_dvsec(cxl, CXL2_ROOT_PORT,
                               EXTENSIONS_PORT_DVSEC_LENGTH,
                               EXTENSIONS_PORT_DVSEC,
                               EXTENSIONS_PORT_DVSEC_REVID, dvsec);

    dvsec = (uint8_t *)&(CXLDVSECPortGPF){
        .rsvd        = 0,
        .phase1_ctrl = 1, /* 1μs timeout */
        .phase2_ctrl = 1, /* 1μs timeout */
    };
    cxl_component_create_dvsec(cxl, CXL2_ROOT_PORT,
                               GPF_PORT_DVSEC_LENGTH, GPF_PORT_DVSEC,
                               GPF_PORT_DVSEC_REVID, dvsec);

    dvsec = (uint8_t *)&(CXLDVSECPortFlexBus){
        .cap                     = 0x26, /* IO, Mem, non-MLD */
        .ctrl                    = 0x2,
        .status                  = 0x26, /* same */
        .rcvd_mod_ts_data_phase1 = 0xef,
    };
    cxl_component_create_dvsec(cxl, CXL2_ROOT_PORT,
                               PCIE_FLEXBUS_PORT_DVSEC_LENGTH_2_0,
                               PCIE_FLEXBUS_PORT_DVSEC,
                               PCIE_FLEXBUS_PORT_DVSEC_REVID_2_0, dvsec);

    dvsec = (uint8_t *)&(CXLDVSECRegisterLocator){
        .rsvd         = 0,
        .reg0_base_lo = RBI_COMPONENT_REG | CXL_COMPONENT_REG_BAR_IDX,
        .reg0_base_hi = 0,
    };
    cxl_component_create_dvsec(cxl, CXL2_ROOT_PORT,
                               REG_LOC_DVSEC_LENGTH, REG_LOC_DVSEC,
                               REG_LOC_DVSEC_REVID, dvsec);
}

static void cxl_rp_realize(DeviceState *dev, Error **errp)
{
    PCIDevice *pci_dev     = PCI_DEVICE(dev);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(dev);
    CXLRootPort *crp       = CXL_ROOT_PORT(dev);
    CXLComponentState *cxl_cstate = &crp->cxl_cstate;
    ComponentRegisters *cregs = &cxl_cstate->crb;
    MemoryRegion *component_bar = &cregs->component_registers;
    Error *local_err = NULL;

    rpc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    int rc =
        pci_bridge_qemu_reserve_cap_init(pci_dev, 0, crp->res_reserve, errp);
    if (rc < 0) {
        rpc->parent_class.exit(pci_dev);
        return;
    }

    if (!crp->res_reserve.io || crp->res_reserve.io == -1) {
        pci_word_test_and_clear_mask(pci_dev->wmask + PCI_COMMAND,
                                     PCI_COMMAND_IO);
        pci_dev->wmask[PCI_IO_BASE]  = 0;
        pci_dev->wmask[PCI_IO_LIMIT] = 0;
    }

    cxl_cstate->dvsec_offset = CXL_ROOT_PORT_DVSEC_OFFSET;
    cxl_cstate->pdev = pci_dev;
    build_dvsecs(&crp->cxl_cstate);

    cxl_component_register_block_init(OBJECT(pci_dev), cxl_cstate,
                                      TYPE_CXL_ROOT_PORT);

    pci_register_bar(pci_dev, CXL_COMPONENT_REG_BAR_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_TYPE_64,
                     component_bar);
}

static void cxl_rp_reset_hold(Object *obj)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(obj);
    CXLRootPort *crp = CXL_ROOT_PORT(obj);

    if (rpc->parent_phases.hold) {
        rpc->parent_phases.hold(obj);
    }

    latch_registers(crp);
}

static Property gen_rp_props[] = {
    DEFINE_PROP_UINT32("bus-reserve", CXLRootPort, res_reserve.bus, -1),
    DEFINE_PROP_SIZE("io-reserve", CXLRootPort, res_reserve.io, -1),
    DEFINE_PROP_SIZE("mem-reserve", CXLRootPort, res_reserve.mem_non_pref, -1),
    DEFINE_PROP_SIZE("pref32-reserve", CXLRootPort, res_reserve.mem_pref_32,
                     -1),
    DEFINE_PROP_SIZE("pref64-reserve", CXLRootPort, res_reserve.mem_pref_64,
                     -1),
    DEFINE_PROP_END_OF_LIST()
};

static void cxl_rp_dvsec_write_config(PCIDevice *dev, uint32_t addr,
                                      uint32_t val, int len)
{
    CXLRootPort *crp = CXL_ROOT_PORT(dev);

    if (range_contains(&crp->cxl_cstate.dvsecs[EXTENSIONS_PORT_DVSEC], addr)) {
        uint8_t *reg = &dev->config[addr];
        addr -= crp->cxl_cstate.dvsecs[EXTENSIONS_PORT_DVSEC].lob;
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

static void cxl_rp_aer_vector_update(PCIDevice *d)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(d);

    if (rpc->aer_vector) {
        pcie_aer_root_set_vector(d, rpc->aer_vector(d));
    }
}

static void cxl_rp_write_config(PCIDevice *d, uint32_t address, uint32_t val,
                                int len)
{
    uint16_t slt_ctl, slt_sta;
    uint32_t root_cmd =
        pci_get_long(d->config + d->exp.aer_cap + PCI_ERR_ROOT_COMMAND);

    pcie_cap_slot_get(d, &slt_ctl, &slt_sta);
    pci_bridge_write_config(d, address, val, len);
    cxl_rp_aer_vector_update(d);
    pcie_cap_flr_write_config(d, address, val, len);
    pcie_cap_slot_write_config(d, slt_ctl, slt_sta, address, val, len);
    pcie_aer_write_config(d, address, val, len);
    pcie_aer_root_write_config(d, address, val, len, root_cmd);

    cxl_rp_dvsec_write_config(d, address, val, len);
}

static void cxl_root_port_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc        = DEVICE_CLASS(oc);
    PCIDeviceClass *k      = PCI_DEVICE_CLASS(oc);
    ResettableClass *rc    = RESETTABLE_CLASS(oc);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_CLASS(oc);

    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = CXL_ROOT_PORT_DID;
    dc->desc     = "CXL Root Port";
    k->revision  = 0;
    device_class_set_props(dc, gen_rp_props);
    k->config_write = cxl_rp_write_config;

    device_class_set_parent_realize(dc, cxl_rp_realize, &rpc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, cxl_rp_reset_hold, NULL,
                                       &rpc->parent_phases);

    rpc->aer_offset = GEN_PCIE_ROOT_PORT_AER_OFFSET;
    rpc->acs_offset = GEN_PCIE_ROOT_PORT_ACS_OFFSET;
    rpc->aer_vector = cxl_rp_aer_vector;
    rpc->interrupts_init = cxl_rp_interrupts_init;
    rpc->interrupts_uninit = cxl_rp_interrupts_uninit;

    dc->hotpluggable = false;
}

static const TypeInfo cxl_root_port_info = {
    .name = TYPE_CXL_ROOT_PORT,
    .parent = TYPE_PCIE_ROOT_PORT,
    .instance_size = sizeof(CXLRootPort),
    .class_init = cxl_root_port_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CXL_DEVICE },
        { }
    },
};

static void cxl_register(void)
{
    type_register_static(&cxl_root_port_info);
}

type_init(cxl_register);
