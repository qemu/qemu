/*
 * Emulated CXL Switch Upstream Port
 *
 * Copyright (c) 2022 Huawei Technologies.
 *
 * Based on xio3130_upstream.c
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/pci/msi.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_port.h"

#define CXL_UPSTREAM_PORT_MSI_NR_VECTOR 2

#define CXL_UPSTREAM_PORT_MSI_OFFSET 0x70
#define CXL_UPSTREAM_PORT_PCIE_CAP_OFFSET 0x90
#define CXL_UPSTREAM_PORT_AER_OFFSET 0x100
#define CXL_UPSTREAM_PORT_DVSEC_OFFSET \
    (CXL_UPSTREAM_PORT_AER_OFFSET + PCI_ERR_SIZEOF)

typedef struct CXLUpstreamPort {
    /*< private >*/
    PCIEPort parent_obj;

    /*< public >*/
    CXLComponentState cxl_cstate;
    DOECap doe_cdat;
} CXLUpstreamPort;

CXLComponentState *cxl_usp_to_cstate(CXLUpstreamPort *usp)
{
    return &usp->cxl_cstate;
}

static void cxl_usp_dvsec_write_config(PCIDevice *dev, uint32_t addr,
                                       uint32_t val, int len)
{
    CXLUpstreamPort *usp = CXL_USP(dev);

    if (range_contains(&usp->cxl_cstate.dvsecs[EXTENSIONS_PORT_DVSEC], addr)) {
        uint8_t *reg = &dev->config[addr];
        addr -= usp->cxl_cstate.dvsecs[EXTENSIONS_PORT_DVSEC].lob;
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

static void cxl_usp_write_config(PCIDevice *d, uint32_t address,
                                 uint32_t val, int len)
{
    CXLUpstreamPort *usp = CXL_USP(d);

    pcie_doe_write_config(&usp->doe_cdat, address, val, len);
    pci_bridge_write_config(d, address, val, len);
    pcie_cap_flr_write_config(d, address, val, len);
    pcie_aer_write_config(d, address, val, len);

    cxl_usp_dvsec_write_config(d, address, val, len);
}

static uint32_t cxl_usp_read_config(PCIDevice *d, uint32_t address, int len)
{
    CXLUpstreamPort *usp = CXL_USP(d);
    uint32_t val;

    if (pcie_doe_read_config(&usp->doe_cdat, address, len, &val)) {
        return val;
    }

    return pci_default_read_config(d, address, len);
}

static void latch_registers(CXLUpstreamPort *usp)
{
    uint32_t *reg_state = usp->cxl_cstate.crb.cache_mem_registers;
    uint32_t *write_msk = usp->cxl_cstate.crb.cache_mem_regs_write_mask;

    cxl_component_register_init_common(reg_state, write_msk,
                                       CXL2_UPSTREAM_PORT);
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, TARGET_COUNT, 8);
}

static void cxl_usp_reset(DeviceState *qdev)
{
    PCIDevice *d = PCI_DEVICE(qdev);
    CXLUpstreamPort *usp = CXL_USP(qdev);

    pci_bridge_reset(qdev);
    pcie_cap_deverr_reset(d);
    latch_registers(usp);
}

static void build_dvsecs(CXLComponentState *cxl)
{
    uint8_t *dvsec;

    dvsec = (uint8_t *)&(CXLDVSECPortExtensions){
        .status = 0x1, /* Port Power Management Init Complete */
    };
    cxl_component_create_dvsec(cxl, CXL2_UPSTREAM_PORT,
                               EXTENSIONS_PORT_DVSEC_LENGTH,
                               EXTENSIONS_PORT_DVSEC,
                               EXTENSIONS_PORT_DVSEC_REVID, dvsec);
    dvsec = (uint8_t *)&(CXLDVSECPortFlexBus){
        .cap                     = 0x27, /* Cache, IO, Mem, non-MLD */
        .ctrl                    = 0x27, /* Cache, IO, Mem */
        .status                  = 0x26, /* same */
        .rcvd_mod_ts_data_phase1 = 0xef, /* WTF? */
    };
    cxl_component_create_dvsec(cxl, CXL2_UPSTREAM_PORT,
                               PCIE_FLEXBUS_PORT_DVSEC_LENGTH_2_0,
                               PCIE_FLEXBUS_PORT_DVSEC,
                               PCIE_FLEXBUS_PORT_DVSEC_REVID_2_0, dvsec);

    dvsec = (uint8_t *)&(CXLDVSECRegisterLocator){
        .rsvd         = 0,
        .reg0_base_lo = RBI_COMPONENT_REG | CXL_COMPONENT_REG_BAR_IDX,
        .reg0_base_hi = 0,
    };
    cxl_component_create_dvsec(cxl, CXL2_UPSTREAM_PORT,
                               REG_LOC_DVSEC_LENGTH, REG_LOC_DVSEC,
                               REG_LOC_DVSEC_REVID, dvsec);
}

static bool cxl_doe_cdat_rsp(DOECap *doe_cap)
{
    CDATObject *cdat = &CXL_USP(doe_cap->pdev)->cxl_cstate.cdat;
    uint16_t ent;
    void *base;
    uint32_t len;
    CDATReq *req = pcie_doe_get_write_mbox_ptr(doe_cap);
    CDATRsp rsp;

    cxl_doe_cdat_update(&CXL_USP(doe_cap->pdev)->cxl_cstate, &error_fatal);
    assert(cdat->entry_len);

    /* Discard if request length mismatched */
    if (pcie_doe_get_obj_len(req) <
        DIV_ROUND_UP(sizeof(CDATReq), sizeof(uint32_t))) {
        return false;
    }

    ent = req->entry_handle;
    base = cdat->entry[ent].base;
    len = cdat->entry[ent].length;

    rsp = (CDATRsp) {
        .header = {
            .vendor_id = CXL_VENDOR_ID,
            .data_obj_type = CXL_DOE_TABLE_ACCESS,
            .reserved = 0x0,
            .length = DIV_ROUND_UP((sizeof(rsp) + len), sizeof(uint32_t)),
        },
        .rsp_code = CXL_DOE_TAB_RSP,
        .table_type = CXL_DOE_TAB_TYPE_CDAT,
        .entry_handle = (ent < cdat->entry_len - 1) ?
                        ent + 1 : CXL_DOE_TAB_ENT_MAX,
    };

    memcpy(doe_cap->read_mbox, &rsp, sizeof(rsp));
        memcpy(doe_cap->read_mbox + DIV_ROUND_UP(sizeof(rsp), sizeof(uint32_t)),
           base, len);

    doe_cap->read_mbox_len += rsp.header.length;

    return true;
}

static DOEProtocol doe_cdat_prot[] = {
    { CXL_VENDOR_ID, CXL_DOE_TABLE_ACCESS, cxl_doe_cdat_rsp },
    { }
};

enum {
    CXL_USP_CDAT_SSLBIS_LAT,
    CXL_USP_CDAT_SSLBIS_BW,
    CXL_USP_CDAT_NUM_ENTRIES
};

static int build_cdat_table(CDATSubHeader ***cdat_table, void *priv)
{
    g_autofree CDATSslbis *sslbis_latency = NULL;
    g_autofree CDATSslbis *sslbis_bandwidth = NULL;
    CXLUpstreamPort *us = CXL_USP(priv);
    PCIBus *bus = &PCI_BRIDGE(us)->sec_bus;
    int devfn, sslbis_size, i;
    int count = 0;
    uint16_t port_ids[256];

    for (devfn = 0; devfn < ARRAY_SIZE(bus->devices); devfn++) {
        PCIDevice *d = bus->devices[devfn];
        PCIEPort *port;

        if (!d || !pci_is_express(d) || !d->exp.exp_cap) {
            continue;
        }

        /*
         * Whilst the PCI express spec doesn't allow anything other than
         * downstream ports on this bus, let us be a little paranoid
         */
        if (!object_dynamic_cast(OBJECT(d), TYPE_PCIE_PORT)) {
            continue;
        }

        port = PCIE_PORT(d);
        port_ids[count] = port->port;
        count++;
    }

    /* May not yet have any ports - try again later */
    if (count == 0) {
        return 0;
    }

    sslbis_size = sizeof(CDATSslbis) + sizeof(*sslbis_latency->sslbe) * count;
    sslbis_latency = g_malloc(sslbis_size);
    if (!sslbis_latency) {
        return -ENOMEM;
    }
    *sslbis_latency = (CDATSslbis) {
        .sslbis_header = {
            .header = {
                .type = CDAT_TYPE_SSLBIS,
                .length = sslbis_size,
            },
            .data_type = HMATLB_DATA_TYPE_ACCESS_LATENCY,
            .entry_base_unit = 10000,
        },
    };

    for (i = 0; i < count; i++) {
        sslbis_latency->sslbe[i] = (CDATSslbe) {
            .port_x_id = CDAT_PORT_ID_USP,
            .port_y_id = port_ids[i],
            .latency_bandwidth = 15, /* 150ns */
        };
    }

    sslbis_bandwidth = g_malloc(sslbis_size);
    if (!sslbis_bandwidth) {
        return 0;
    }
    *sslbis_bandwidth = (CDATSslbis) {
        .sslbis_header = {
            .header = {
                .type = CDAT_TYPE_SSLBIS,
                .length = sslbis_size,
            },
            .data_type = HMATLB_DATA_TYPE_ACCESS_BANDWIDTH,
            .entry_base_unit = 1000,
        },
    };

    for (i = 0; i < count; i++) {
        sslbis_bandwidth->sslbe[i] = (CDATSslbe) {
            .port_x_id = CDAT_PORT_ID_USP,
            .port_y_id = port_ids[i],
            .latency_bandwidth = 16, /* 16 GB/s */
        };
    }

    *cdat_table = g_new0(CDATSubHeader *, CXL_USP_CDAT_NUM_ENTRIES);

    /* Header always at start of structure */
    (*cdat_table)[CXL_USP_CDAT_SSLBIS_LAT] = g_steal_pointer(&sslbis_latency);
    (*cdat_table)[CXL_USP_CDAT_SSLBIS_BW] = g_steal_pointer(&sslbis_bandwidth);

    return CXL_USP_CDAT_NUM_ENTRIES;
}

static void free_default_cdat_table(CDATSubHeader **cdat_table, int num,
                                    void *priv)
{
    int i;

    for (i = 0; i < num; i++) {
        g_free(cdat_table[i]);
    }
    g_free(cdat_table);
}

static void cxl_usp_realize(PCIDevice *d, Error **errp)
{
    PCIEPort *p = PCIE_PORT(d);
    CXLUpstreamPort *usp = CXL_USP(d);
    CXLComponentState *cxl_cstate = &usp->cxl_cstate;
    ComponentRegisters *cregs = &cxl_cstate->crb;
    MemoryRegion *component_bar = &cregs->component_registers;
    int rc;

    pci_bridge_initfn(d, TYPE_PCIE_BUS);
    pcie_port_init_reg(d);

    rc = msi_init(d, CXL_UPSTREAM_PORT_MSI_OFFSET,
                  CXL_UPSTREAM_PORT_MSI_NR_VECTOR, true, true, errp);
    if (rc) {
        assert(rc == -ENOTSUP);
        goto err_bridge;
    }

    rc = pcie_cap_init(d, CXL_UPSTREAM_PORT_PCIE_CAP_OFFSET,
                       PCI_EXP_TYPE_UPSTREAM, p->port, errp);
    if (rc < 0) {
        goto err_msi;
    }

    pcie_cap_flr_init(d);
    pcie_cap_deverr_init(d);
    rc = pcie_aer_init(d, PCI_ERR_VER, CXL_UPSTREAM_PORT_AER_OFFSET,
                       PCI_ERR_SIZEOF, errp);
    if (rc) {
        goto err_cap;
    }

    cxl_cstate->dvsec_offset = CXL_UPSTREAM_PORT_DVSEC_OFFSET;
    cxl_cstate->pdev = d;
    build_dvsecs(cxl_cstate);
    cxl_component_register_block_init(OBJECT(d), cxl_cstate, TYPE_CXL_USP);
    pci_register_bar(d, CXL_COMPONENT_REG_BAR_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64,
                     component_bar);

    pcie_doe_init(d, &usp->doe_cdat, cxl_cstate->dvsec_offset, doe_cdat_prot,
                  true, 1);

    cxl_cstate->cdat.build_cdat_table = build_cdat_table;
    cxl_cstate->cdat.free_cdat_table = free_default_cdat_table;
    cxl_cstate->cdat.private = d;
    cxl_doe_cdat_init(cxl_cstate, errp);
    if (*errp) {
        goto err_cap;
    }

    return;

err_cap:
    pcie_cap_exit(d);
err_msi:
    msi_uninit(d);
err_bridge:
    pci_bridge_exitfn(d);
}

static void cxl_usp_exitfn(PCIDevice *d)
{
    pcie_aer_exit(d);
    pcie_cap_exit(d);
    msi_uninit(d);
    pci_bridge_exitfn(d);
}

static Property cxl_upstream_props[] = {
    DEFINE_PROP_STRING("cdat", CXLUpstreamPort, cxl_cstate.cdat.filename),
    DEFINE_PROP_END_OF_LIST()
};

static void cxl_upstream_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(oc);

    k->config_write = cxl_usp_write_config;
    k->config_read = cxl_usp_read_config;
    k->realize = cxl_usp_realize;
    k->exit = cxl_usp_exitfn;
    k->vendor_id = 0x19e5; /* Huawei */
    k->device_id = 0xa128; /* Emulated CXL Switch Upstream Port */
    k->revision = 0;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "CXL Switch Upstream Port";
    dc->reset = cxl_usp_reset;
    device_class_set_props(dc, cxl_upstream_props);
}

static const TypeInfo cxl_usp_info = {
    .name = TYPE_CXL_USP,
    .parent = TYPE_PCIE_PORT,
    .instance_size = sizeof(CXLUpstreamPort),
    .class_init = cxl_upstream_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CXL_DEVICE },
        { }
    },
};

static void cxl_usp_register_type(void)
{
    type_register_static(&cxl_usp_info);
}

type_init(cxl_usp_register_type);
