/*
 * CXL Utility library for mailbox interface
 *
 * Copyright(C) 2020 Intel Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/cxl/cxl.h"
#include "hw/cxl/cxl_events.h"
#include "hw/pci/pci.h"
#include "hw/pci-bridge/cxl_upstream_port.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qemu/uuid.h"
#include "sysemu/hostmem.h"

#define CXL_CAPACITY_MULTIPLIER   (256 * MiB)

/*
 * How to add a new command, example. The command set FOO, with cmd BAR.
 *  1. Add the command set and cmd to the enum.
 *     FOO    = 0x7f,
 *          #define BAR 0
 *  2. Implement the handler
 *    static CXLRetCode cmd_foo_bar(struct cxl_cmd *cmd,
 *                                  CXLDeviceState *cxl_dstate, uint16_t *len)
 *  3. Add the command to the cxl_cmd_set[][]
 *    [FOO][BAR] = { "FOO_BAR", cmd_foo_bar, x, y },
 *  4. Implement your handler
 *     define_mailbox_handler(FOO_BAR) { ... return CXL_MBOX_SUCCESS; }
 *
 *
 *  Writing the handler:
 *    The handler will provide the &struct cxl_cmd, the &CXLDeviceState, and the
 *    in/out length of the payload. The handler is responsible for consuming the
 *    payload from cmd->payload and operating upon it as necessary. It must then
 *    fill the output data into cmd->payload (overwriting what was there),
 *    setting the length, and returning a valid return code.
 *
 *  XXX: The handler need not worry about endianness. The payload is read out of
 *  a register interface that already deals with it.
 */

enum {
    INFOSTAT    = 0x00,
        #define IS_IDENTIFY   0x1
        #define BACKGROUND_OPERATION_STATUS    0x2
    EVENTS      = 0x01,
        #define GET_RECORDS   0x0
        #define CLEAR_RECORDS   0x1
        #define GET_INTERRUPT_POLICY   0x2
        #define SET_INTERRUPT_POLICY   0x3
    FIRMWARE_UPDATE = 0x02,
        #define GET_INFO      0x0
    TIMESTAMP   = 0x03,
        #define GET           0x0
        #define SET           0x1
    LOGS        = 0x04,
        #define GET_SUPPORTED 0x0
        #define GET_LOG       0x1
    IDENTIFY    = 0x40,
        #define MEMORY_DEVICE 0x0
    CCLS        = 0x41,
        #define GET_PARTITION_INFO     0x0
        #define GET_LSA       0x2
        #define SET_LSA       0x3
    SANITIZE    = 0x44,
        #define OVERWRITE     0x0
        #define SECURE_ERASE  0x1
    PERSISTENT_MEM = 0x45,
        #define GET_SECURITY_STATE     0x0
    MEDIA_AND_POISON = 0x43,
        #define GET_POISON_LIST        0x0
        #define INJECT_POISON          0x1
        #define CLEAR_POISON           0x2
    PHYSICAL_SWITCH = 0x51,
        #define IDENTIFY_SWITCH_DEVICE      0x0
        #define GET_PHYSICAL_PORT_STATE     0x1
    TUNNEL = 0x53,
        #define MANAGEMENT_COMMAND     0x0
};

/* CCI Message Format CXL r3.0 Figure 7-19 */
typedef struct CXLCCIMessage {
    uint8_t category;
#define CXL_CCI_CAT_REQ 0
#define CXL_CCI_CAT_RSP 1
    uint8_t tag;
    uint8_t resv1;
    uint8_t command;
    uint8_t command_set;
    uint8_t pl_length[3];
    uint16_t rc;
    uint16_t vendor_specific;
    uint8_t payload[];
} QEMU_PACKED CXLCCIMessage;

/* This command is only defined to an MLD FM Owned LD or an MHD */
static CXLRetCode cmd_tunnel_management_cmd(const struct cxl_cmd *cmd,
                                            uint8_t *payload_in,
                                            size_t len_in,
                                            uint8_t *payload_out,
                                            size_t *len_out,
                                            CXLCCI *cci)
{
    PCIDevice *tunnel_target;
    CXLCCI *target_cci;
    struct {
        uint8_t port_or_ld_id;
        uint8_t target_type;
        uint16_t size;
        CXLCCIMessage ccimessage;
    } QEMU_PACKED *in;
    struct {
        uint16_t resp_len;
        uint8_t resv[2];
        CXLCCIMessage ccimessage;
    } QEMU_PACKED *out;
    size_t pl_length, length_out;
    bool bg_started;
    int rc;

    if (cmd->in < sizeof(*in)) {
        return CXL_MBOX_INVALID_INPUT;
    }
    in = (void *)payload_in;
    out = (void *)payload_out;

    /* Enough room for minimum sized message - no payload */
    if (in->size < sizeof(in->ccimessage)) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }
    /* Length of input payload should be in->size + a wrapping tunnel header */
    if (in->size != len_in - offsetof(typeof(*out), ccimessage)) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }
    if (in->ccimessage.category != CXL_CCI_CAT_REQ) {
        return CXL_MBOX_INVALID_INPUT;
    }

    if (in->target_type != 0) {
        qemu_log_mask(LOG_UNIMP,
                      "Tunneled Command sent to non existent FM-LD");
        return CXL_MBOX_INVALID_INPUT;
    }

    /*
     * Target of a tunnel unfortunately depends on type of CCI readint
     * the message.
     * If in a switch, then it's the port number.
     * If in an MLD it is the ld number.
     * If in an MHD target type indicate where we are going.
     */
    if (object_dynamic_cast(OBJECT(cci->d), TYPE_CXL_TYPE3)) {
        CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
        if (in->port_or_ld_id != 0) {
            /* Only pretending to have one for now! */
            return CXL_MBOX_INVALID_INPUT;
        }
        target_cci = &ct3d->ld0_cci;
    } else if (object_dynamic_cast(OBJECT(cci->d), TYPE_CXL_USP)) {
        CXLUpstreamPort *usp = CXL_USP(cci->d);

        tunnel_target = pcie_find_port_by_pn(&PCI_BRIDGE(usp)->sec_bus,
                                             in->port_or_ld_id);
        if (!tunnel_target) {
            return CXL_MBOX_INVALID_INPUT;
        }
        tunnel_target =
            pci_bridge_get_sec_bus(PCI_BRIDGE(tunnel_target))->devices[0];
        if (!tunnel_target) {
            return CXL_MBOX_INVALID_INPUT;
        }
        if (object_dynamic_cast(OBJECT(tunnel_target), TYPE_CXL_TYPE3)) {
            CXLType3Dev *ct3d = CXL_TYPE3(tunnel_target);
            /* Tunneled VDMs always land on FM Owned LD */
            target_cci = &ct3d->vdm_fm_owned_ld_mctp_cci;
        } else {
            return CXL_MBOX_INVALID_INPUT;
        }
    } else {
        return CXL_MBOX_INVALID_INPUT;
    }

    pl_length = in->ccimessage.pl_length[2] << 16 |
        in->ccimessage.pl_length[1] << 8 | in->ccimessage.pl_length[0];
    rc = cxl_process_cci_message(target_cci,
                                 in->ccimessage.command_set,
                                 in->ccimessage.command,
                                 pl_length, in->ccimessage.payload,
                                 &length_out, out->ccimessage.payload,
                                 &bg_started);
    /* Payload should be in place. Rest of CCI header and needs filling */
    out->resp_len = length_out + sizeof(CXLCCIMessage);
    st24_le_p(out->ccimessage.pl_length, length_out);
    out->ccimessage.rc = rc;
    out->ccimessage.category = CXL_CCI_CAT_RSP;
    out->ccimessage.command = in->ccimessage.command;
    out->ccimessage.command_set = in->ccimessage.command_set;
    out->ccimessage.tag = in->ccimessage.tag;
    *len_out = length_out + sizeof(*out);

    return CXL_MBOX_SUCCESS;
}

static CXLRetCode cmd_events_get_records(const struct cxl_cmd *cmd,
                                         uint8_t *payload_in, size_t len_in,
                                         uint8_t *payload_out, size_t *len_out,
                                         CXLCCI *cci)
{
    CXLDeviceState *cxlds = &CXL_TYPE3(cci->d)->cxl_dstate;
    CXLGetEventPayload *pl;
    uint8_t log_type;
    int max_recs;

    if (cmd->in < sizeof(log_type)) {
        return CXL_MBOX_INVALID_INPUT;
    }

    log_type = payload_in[0];

    pl = (CXLGetEventPayload *)payload_out;
    memset(pl, 0, sizeof(*pl));

    max_recs = (cxlds->payload_size - CXL_EVENT_PAYLOAD_HDR_SIZE) /
                CXL_EVENT_RECORD_SIZE;
    if (max_recs > 0xFFFF) {
        max_recs = 0xFFFF;
    }

    return cxl_event_get_records(cxlds, pl, log_type, max_recs, len_out);
}

static CXLRetCode cmd_events_clear_records(const struct cxl_cmd *cmd,
                                           uint8_t *payload_in,
                                           size_t len_in,
                                           uint8_t *payload_out,
                                           size_t *len_out,
                                           CXLCCI *cci)
{
    CXLDeviceState *cxlds = &CXL_TYPE3(cci->d)->cxl_dstate;
    CXLClearEventPayload *pl;

    pl = (CXLClearEventPayload *)payload_in;
    *len_out = 0;
    return cxl_event_clear_records(cxlds, pl);
}

static CXLRetCode cmd_events_get_interrupt_policy(const struct cxl_cmd *cmd,
                                                  uint8_t *payload_in,
                                                  size_t len_in,
                                                  uint8_t *payload_out,
                                                  size_t *len_out,
                                                  CXLCCI *cci)
{
    CXLDeviceState *cxlds = &CXL_TYPE3(cci->d)->cxl_dstate;
    CXLEventInterruptPolicy *policy;
    CXLEventLog *log;

    policy = (CXLEventInterruptPolicy *)payload_out;
    memset(policy, 0, sizeof(*policy));

    log = &cxlds->event_logs[CXL_EVENT_TYPE_INFO];
    if (log->irq_enabled) {
        policy->info_settings = CXL_EVENT_INT_SETTING(log->irq_vec);
    }

    log = &cxlds->event_logs[CXL_EVENT_TYPE_WARN];
    if (log->irq_enabled) {
        policy->warn_settings = CXL_EVENT_INT_SETTING(log->irq_vec);
    }

    log = &cxlds->event_logs[CXL_EVENT_TYPE_FAIL];
    if (log->irq_enabled) {
        policy->failure_settings = CXL_EVENT_INT_SETTING(log->irq_vec);
    }

    log = &cxlds->event_logs[CXL_EVENT_TYPE_FATAL];
    if (log->irq_enabled) {
        policy->fatal_settings = CXL_EVENT_INT_SETTING(log->irq_vec);
    }

    log = &cxlds->event_logs[CXL_EVENT_TYPE_DYNAMIC_CAP];
    if (log->irq_enabled) {
        /* Dynamic Capacity borrows the same vector as info */
        policy->dyn_cap_settings = CXL_INT_MSI_MSIX;
    }

    *len_out = sizeof(*policy);
    return CXL_MBOX_SUCCESS;
}

static CXLRetCode cmd_events_set_interrupt_policy(const struct cxl_cmd *cmd,
                                                  uint8_t *payload_in,
                                                  size_t len_in,
                                                  uint8_t *payload_out,
                                                  size_t *len_out,
                                                  CXLCCI *cci)
{
    CXLDeviceState *cxlds = &CXL_TYPE3(cci->d)->cxl_dstate;
    CXLEventInterruptPolicy *policy;
    CXLEventLog *log;

    if (len_in < CXL_EVENT_INT_SETTING_MIN_LEN) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }

    policy = (CXLEventInterruptPolicy *)payload_in;

    log = &cxlds->event_logs[CXL_EVENT_TYPE_INFO];
    log->irq_enabled = (policy->info_settings & CXL_EVENT_INT_MODE_MASK) ==
                        CXL_INT_MSI_MSIX;

    log = &cxlds->event_logs[CXL_EVENT_TYPE_WARN];
    log->irq_enabled = (policy->warn_settings & CXL_EVENT_INT_MODE_MASK) ==
                        CXL_INT_MSI_MSIX;

    log = &cxlds->event_logs[CXL_EVENT_TYPE_FAIL];
    log->irq_enabled = (policy->failure_settings & CXL_EVENT_INT_MODE_MASK) ==
                        CXL_INT_MSI_MSIX;

    log = &cxlds->event_logs[CXL_EVENT_TYPE_FATAL];
    log->irq_enabled = (policy->fatal_settings & CXL_EVENT_INT_MODE_MASK) ==
                        CXL_INT_MSI_MSIX;

    /* DCD is optional */
    if (len_in < sizeof(*policy)) {
        return CXL_MBOX_SUCCESS;
    }

    log = &cxlds->event_logs[CXL_EVENT_TYPE_DYNAMIC_CAP];
    log->irq_enabled = (policy->dyn_cap_settings & CXL_EVENT_INT_MODE_MASK) ==
                        CXL_INT_MSI_MSIX;

    *len_out = 0;
    return CXL_MBOX_SUCCESS;
}

/* CXL r3.0 section 8.2.9.1.1: Identify (Opcode 0001h) */
static CXLRetCode cmd_infostat_identify(const struct cxl_cmd *cmd,
                                        uint8_t *payload_in,
                                        size_t len_in,
                                        uint8_t *payload_out,
                                        size_t *len_out,
                                        CXLCCI *cci)
{
    PCIDeviceClass *class = PCI_DEVICE_GET_CLASS(cci->d);
    struct {
        uint16_t pcie_vid;
        uint16_t pcie_did;
        uint16_t pcie_subsys_vid;
        uint16_t pcie_subsys_id;
        uint64_t sn;
    uint8_t max_message_size;
        uint8_t component_type;
    } QEMU_PACKED *is_identify;
    QEMU_BUILD_BUG_ON(sizeof(*is_identify) != 18);

    is_identify = (void *)payload_out;
    memset(is_identify, 0, sizeof(*is_identify));
    is_identify->pcie_vid = class->vendor_id;
    is_identify->pcie_did = class->device_id;
    if (object_dynamic_cast(OBJECT(cci->d), TYPE_CXL_USP)) {
        is_identify->sn = CXL_USP(cci->d)->sn;
        /* Subsystem info not defined for a USP */
        is_identify->pcie_subsys_vid = 0;
        is_identify->pcie_subsys_id = 0;
        is_identify->component_type = 0x0; /* Switch */
    } else if (object_dynamic_cast(OBJECT(cci->d), TYPE_CXL_TYPE3)) {
        PCIDevice *pci_dev = PCI_DEVICE(cci->d);

        is_identify->sn = CXL_TYPE3(cci->d)->sn;
        /*
         * We can't always use class->subsystem_vendor_id as
         * it is not set if the defaults are used.
         */
        is_identify->pcie_subsys_vid =
            pci_get_word(pci_dev->config + PCI_SUBSYSTEM_VENDOR_ID);
        is_identify->pcie_subsys_id =
            pci_get_word(pci_dev->config + PCI_SUBSYSTEM_ID);
        is_identify->component_type = 0x3; /* Type 3 */
    }

    /* TODO: Allow this to vary across different CCIs */
    is_identify->max_message_size = 9; /* 512 bytes - MCTP_CXL_MAILBOX_BYTES */
    *len_out = sizeof(*is_identify);
    return CXL_MBOX_SUCCESS;
}

static void cxl_set_dsp_active_bm(PCIBus *b, PCIDevice *d,
                                  void *private)
{
    uint8_t *bm = private;
    if (object_dynamic_cast(OBJECT(d), TYPE_CXL_DSP)) {
        uint8_t port = PCIE_PORT(d)->port;
        bm[port / 8] |= 1 << (port % 8);
    }
}

/* CXL r3 8.2.9.1.1 */
static CXLRetCode cmd_identify_switch_device(const struct cxl_cmd *cmd,
                                             uint8_t *payload_in,
                                             size_t len_in,
                                             uint8_t *payload_out,
                                             size_t *len_out,
                                             CXLCCI *cci)
{
    PCIEPort *usp = PCIE_PORT(cci->d);
    PCIBus *bus = &PCI_BRIDGE(cci->d)->sec_bus;
    int num_phys_ports = pcie_count_ds_ports(bus);

    struct cxl_fmapi_ident_switch_dev_resp_pl {
        uint8_t ingress_port_id;
        uint8_t rsvd;
        uint8_t num_physical_ports;
        uint8_t num_vcss;
        uint8_t active_port_bitmask[0x20];
        uint8_t active_vcs_bitmask[0x20];
        uint16_t total_vppbs;
        uint16_t bound_vppbs;
        uint8_t num_hdm_decoders_per_usp;
    } QEMU_PACKED *out;
    QEMU_BUILD_BUG_ON(sizeof(*out) != 0x49);

    out = (struct cxl_fmapi_ident_switch_dev_resp_pl *)payload_out;
    *out = (struct cxl_fmapi_ident_switch_dev_resp_pl) {
        .num_physical_ports = num_phys_ports + 1, /* 1 USP */
        .num_vcss = 1, /* Not yet support multiple VCS - potentially tricky */
        .active_vcs_bitmask[0] = 0x1,
        .total_vppbs = num_phys_ports + 1,
        .bound_vppbs = num_phys_ports + 1,
        .num_hdm_decoders_per_usp = 4,
    };

    /* Depends on the CCI type */
    if (object_dynamic_cast(OBJECT(cci->intf), TYPE_PCIE_PORT)) {
        out->ingress_port_id = PCIE_PORT(cci->intf)->port;
    } else {
        /* MCTP? */
        out->ingress_port_id = 0;
    }

    pci_for_each_device_under_bus(bus, cxl_set_dsp_active_bm,
                                  out->active_port_bitmask);
    out->active_port_bitmask[usp->port / 8] |= (1 << usp->port % 8);

    *len_out = sizeof(*out);

    return CXL_MBOX_SUCCESS;
}

/* CXL r3.0 Section 7.6.7.1.2: Get Physical Port State (Opcode 5101h) */
static CXLRetCode cmd_get_physical_port_state(const struct cxl_cmd *cmd,
                                              uint8_t *payload_in,
                                              size_t len_in,
                                              uint8_t *payload_out,
                                              size_t *len_out,
                                              CXLCCI *cci)
{
    /* CXL r3.0 Table 7-18: Get Physical Port State Request Payload */
    struct cxl_fmapi_get_phys_port_state_req_pl {
        uint8_t num_ports;
        uint8_t ports[];
    } QEMU_PACKED *in;

    /*
     * CXL r3.0 Table 7-20: Get Physical Port State Port Information Block
     * Format
     */
    struct cxl_fmapi_port_state_info_block {
        uint8_t port_id;
        uint8_t config_state;
        uint8_t connected_device_cxl_version;
        uint8_t rsv1;
        uint8_t connected_device_type;
        uint8_t port_cxl_version_bitmask;
        uint8_t max_link_width;
        uint8_t negotiated_link_width;
        uint8_t supported_link_speeds_vector;
        uint8_t max_link_speed;
        uint8_t current_link_speed;
        uint8_t ltssm_state;
        uint8_t first_lane_num;
        uint16_t link_state;
        uint8_t supported_ld_count;
    } QEMU_PACKED;

    /* CXL r3.0 Table 7-19: Get Physical Port State Response Payload */
    struct cxl_fmapi_get_phys_port_state_resp_pl {
        uint8_t num_ports;
        uint8_t rsv1[3];
        struct cxl_fmapi_port_state_info_block ports[];
    } QEMU_PACKED *out;
    PCIBus *bus = &PCI_BRIDGE(cci->d)->sec_bus;
    PCIEPort *usp = PCIE_PORT(cci->d);
    size_t pl_size;
    int i;

    in = (struct cxl_fmapi_get_phys_port_state_req_pl *)payload_in;
    out = (struct cxl_fmapi_get_phys_port_state_resp_pl *)payload_out;

    /* Check if what was requested can fit */
    if (sizeof(*out) + sizeof(*out->ports) * in->num_ports > cci->payload_max) {
        return CXL_MBOX_INVALID_INPUT;
    }

    /* For success there should be a match for each requested */
    out->num_ports = in->num_ports;

    for (i = 0; i < in->num_ports; i++) {
        struct cxl_fmapi_port_state_info_block *port;
        /* First try to match on downstream port */
        PCIDevice *port_dev;
        uint16_t lnkcap, lnkcap2, lnksta;

        port = &out->ports[i];

        port_dev = pcie_find_port_by_pn(bus, in->ports[i]);
        if (port_dev) { /* DSP */
            PCIDevice *ds_dev = pci_bridge_get_sec_bus(PCI_BRIDGE(port_dev))
                ->devices[0];
            port->config_state = 3;
            if (ds_dev) {
                if (object_dynamic_cast(OBJECT(ds_dev), TYPE_CXL_TYPE3)) {
                    port->connected_device_type = 5; /* Assume MLD for now */
                } else {
                    port->connected_device_type = 1;
                }
            } else {
                port->connected_device_type = 0;
            }
            port->supported_ld_count = 3;
        } else if (usp->port == in->ports[i]) { /* USP */
            port_dev = PCI_DEVICE(usp);
            port->config_state = 4;
            port->connected_device_type = 0;
        } else {
            return CXL_MBOX_INVALID_INPUT;
        }

        port->port_id = in->ports[i];
        /* Information on status of this port in lnksta, lnkcap */
        if (!port_dev->exp.exp_cap) {
            return CXL_MBOX_INTERNAL_ERROR;
        }
        lnksta = port_dev->config_read(port_dev,
                                       port_dev->exp.exp_cap + PCI_EXP_LNKSTA,
                                       sizeof(lnksta));
        lnkcap = port_dev->config_read(port_dev,
                                       port_dev->exp.exp_cap + PCI_EXP_LNKCAP,
                                       sizeof(lnkcap));
        lnkcap2 = port_dev->config_read(port_dev,
                                        port_dev->exp.exp_cap + PCI_EXP_LNKCAP2,
                                        sizeof(lnkcap2));

        port->max_link_width = (lnkcap & PCI_EXP_LNKCAP_MLW) >> 4;
        port->negotiated_link_width = (lnksta & PCI_EXP_LNKSTA_NLW) >> 4;
        /* No definition for SLS field in linux/pci_regs.h */
        port->supported_link_speeds_vector = (lnkcap2 & 0xFE) >> 1;
        port->max_link_speed = lnkcap & PCI_EXP_LNKCAP_SLS;
        port->current_link_speed = lnksta & PCI_EXP_LNKSTA_CLS;
        /* TODO: Track down if we can get the rest of the info */
        port->ltssm_state = 0x7;
        port->first_lane_num = 0;
        port->link_state = 0;
        port->port_cxl_version_bitmask = 0x2;
        port->connected_device_cxl_version = 0x2;
    }

    pl_size = sizeof(*out) + sizeof(*out->ports) * in->num_ports;
    *len_out = pl_size;

    return CXL_MBOX_SUCCESS;
}

/* CXL r3.0 8.2.9.1.2 */
static CXLRetCode cmd_infostat_bg_op_sts(const struct cxl_cmd *cmd,
                                         uint8_t *payload_in,
                                         size_t len_in,
                                         uint8_t *payload_out,
                                         size_t *len_out,
                                         CXLCCI *cci)
{
    struct {
        uint8_t status;
        uint8_t rsvd;
        uint16_t opcode;
        uint16_t returncode;
        uint16_t vendor_ext_status;
    } QEMU_PACKED *bg_op_status;
    QEMU_BUILD_BUG_ON(sizeof(*bg_op_status) != 8);

    bg_op_status = (void *)payload_out;
    memset(bg_op_status, 0, sizeof(*bg_op_status));
    bg_op_status->status = cci->bg.complete_pct << 1;
    if (cci->bg.runtime > 0) {
        bg_op_status->status |= 1U << 0;
    }
    bg_op_status->opcode = cci->bg.opcode;
    bg_op_status->returncode = cci->bg.ret_code;
    *len_out = sizeof(*bg_op_status);

    return CXL_MBOX_SUCCESS;
}

/* 8.2.9.2.1 */
static CXLRetCode cmd_firmware_update_get_info(const struct cxl_cmd *cmd,
                                               uint8_t *payload_in,
                                               size_t len,
                                               uint8_t *payload_out,
                                               size_t *len_out,
                                               CXLCCI *cci)
{
    CXLDeviceState *cxl_dstate = &CXL_TYPE3(cci->d)->cxl_dstate;
    struct {
        uint8_t slots_supported;
        uint8_t slot_info;
        uint8_t caps;
        uint8_t rsvd[0xd];
        char fw_rev1[0x10];
        char fw_rev2[0x10];
        char fw_rev3[0x10];
        char fw_rev4[0x10];
    } QEMU_PACKED *fw_info;
    QEMU_BUILD_BUG_ON(sizeof(*fw_info) != 0x50);

    if ((cxl_dstate->vmem_size < CXL_CAPACITY_MULTIPLIER) ||
        (cxl_dstate->pmem_size < CXL_CAPACITY_MULTIPLIER)) {
        return CXL_MBOX_INTERNAL_ERROR;
    }

    fw_info = (void *)payload_out;
    memset(fw_info, 0, sizeof(*fw_info));

    fw_info->slots_supported = 2;
    fw_info->slot_info = BIT(0) | BIT(3);
    fw_info->caps = 0;
    pstrcpy(fw_info->fw_rev1, sizeof(fw_info->fw_rev1), "BWFW VERSION 0");

    *len_out = sizeof(*fw_info);
    return CXL_MBOX_SUCCESS;
}

/* 8.2.9.3.1 */
static CXLRetCode cmd_timestamp_get(const struct cxl_cmd *cmd,
                                    uint8_t *payload_in,
                                    size_t len_in,
                                    uint8_t *payload_out,
                                    size_t *len_out,
                                    CXLCCI *cci)
{
    CXLDeviceState *cxl_dstate = &CXL_TYPE3(cci->d)->cxl_dstate;
    uint64_t final_time = cxl_device_get_timestamp(cxl_dstate);

    stq_le_p(payload_out, final_time);
    *len_out = 8;

    return CXL_MBOX_SUCCESS;
}

/* 8.2.9.3.2 */
static CXLRetCode cmd_timestamp_set(const struct cxl_cmd *cmd,
                                    uint8_t *payload_in,
                                    size_t len_in,
                                    uint8_t *payload_out,
                                    size_t *len_out,
                                    CXLCCI *cci)
{
    CXLDeviceState *cxl_dstate = &CXL_TYPE3(cci->d)->cxl_dstate;

    cxl_dstate->timestamp.set = true;
    cxl_dstate->timestamp.last_set = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    cxl_dstate->timestamp.host_set = le64_to_cpu(*(uint64_t *)payload_in);

    *len_out = 0;
    return CXL_MBOX_SUCCESS;
}

/* CXL 3.0 8.2.9.5.2.1 Command Effects Log (CEL) */
static const QemuUUID cel_uuid = {
    .data = UUID(0x0da9c0b5, 0xbf41, 0x4b78, 0x8f, 0x79,
                 0x96, 0xb1, 0x62, 0x3b, 0x3f, 0x17)
};

/* 8.2.9.4.1 */
static CXLRetCode cmd_logs_get_supported(const struct cxl_cmd *cmd,
                                         uint8_t *payload_in,
                                         size_t len_in,
                                         uint8_t *payload_out,
                                         size_t *len_out,
                                         CXLCCI *cci)
{
    struct {
        uint16_t entries;
        uint8_t rsvd[6];
        struct {
            QemuUUID uuid;
            uint32_t size;
        } log_entries[1];
    } QEMU_PACKED *supported_logs = (void *)payload_out;
    QEMU_BUILD_BUG_ON(sizeof(*supported_logs) != 0x1c);

    supported_logs->entries = 1;
    supported_logs->log_entries[0].uuid = cel_uuid;
    supported_logs->log_entries[0].size = 4 * cci->cel_size;

    *len_out = sizeof(*supported_logs);
    return CXL_MBOX_SUCCESS;
}

/* 8.2.9.4.2 */
static CXLRetCode cmd_logs_get_log(const struct cxl_cmd *cmd,
                                   uint8_t *payload_in,
                                   size_t len_in,
                                   uint8_t *payload_out,
                                   size_t *len_out,
                                   CXLCCI *cci)
{
    struct {
        QemuUUID uuid;
        uint32_t offset;
        uint32_t length;
    } QEMU_PACKED QEMU_ALIGNED(16) *get_log;

    get_log = (void *)payload_in;

    /*
     * 8.2.9.4.2
     *   The device shall return Invalid Parameter if the Offset or Length
     *   fields attempt to access beyond the size of the log as reported by Get
     *   Supported Logs.
     *
     * XXX: Spec is wrong, "Invalid Parameter" isn't a thing.
     * XXX: Spec doesn't address incorrect UUID incorrectness.
     *
     * The CEL buffer is large enough to fit all commands in the emulation, so
     * the only possible failure would be if the mailbox itself isn't big
     * enough.
     */
    if (get_log->offset + get_log->length > cci->payload_max) {
        return CXL_MBOX_INVALID_INPUT;
    }

    if (!qemu_uuid_is_equal(&get_log->uuid, &cel_uuid)) {
        return CXL_MBOX_UNSUPPORTED;
    }

    /* Store off everything to local variables so we can wipe out the payload */
    *len_out = get_log->length;

    memmove(payload_out, cci->cel_log + get_log->offset, get_log->length);

    return CXL_MBOX_SUCCESS;
}

/* 8.2.9.5.1.1 */
static CXLRetCode cmd_identify_memory_device(const struct cxl_cmd *cmd,
                                             uint8_t *payload_in,
                                             size_t len_in,
                                             uint8_t *payload_out,
                                             size_t *len_out,
                                             CXLCCI *cci)
{
    struct {
        char fw_revision[0x10];
        uint64_t total_capacity;
        uint64_t volatile_capacity;
        uint64_t persistent_capacity;
        uint64_t partition_align;
        uint16_t info_event_log_size;
        uint16_t warning_event_log_size;
        uint16_t failure_event_log_size;
        uint16_t fatal_event_log_size;
        uint32_t lsa_size;
        uint8_t poison_list_max_mer[3];
        uint16_t inject_poison_limit;
        uint8_t poison_caps;
        uint8_t qos_telemetry_caps;
    } QEMU_PACKED *id;
    QEMU_BUILD_BUG_ON(sizeof(*id) != 0x43);
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLType3Class *cvc = CXL_TYPE3_GET_CLASS(ct3d);
    CXLDeviceState *cxl_dstate = &ct3d->cxl_dstate;

    if ((!QEMU_IS_ALIGNED(cxl_dstate->vmem_size, CXL_CAPACITY_MULTIPLIER)) ||
        (!QEMU_IS_ALIGNED(cxl_dstate->pmem_size, CXL_CAPACITY_MULTIPLIER))) {
        return CXL_MBOX_INTERNAL_ERROR;
    }

    id = (void *)payload_out;
    memset(id, 0, sizeof(*id));

    snprintf(id->fw_revision, 0x10, "BWFW VERSION %02d", 0);

    stq_le_p(&id->total_capacity,
             cxl_dstate->mem_size / CXL_CAPACITY_MULTIPLIER);
    stq_le_p(&id->persistent_capacity,
             cxl_dstate->pmem_size / CXL_CAPACITY_MULTIPLIER);
    stq_le_p(&id->volatile_capacity,
             cxl_dstate->vmem_size / CXL_CAPACITY_MULTIPLIER);
    stl_le_p(&id->lsa_size, cvc->get_lsa_size(ct3d));
    /* 256 poison records */
    st24_le_p(id->poison_list_max_mer, 256);
    /* No limit - so limited by main poison record limit */
    stw_le_p(&id->inject_poison_limit, 0);

    *len_out = sizeof(*id);
    return CXL_MBOX_SUCCESS;
}

static CXLRetCode cmd_ccls_get_partition_info(const struct cxl_cmd *cmd,
                                              uint8_t *payload_in,
                                              size_t len_in,
                                              uint8_t *payload_out,
                                              size_t *len_out,
                                              CXLCCI *cci)
{
    CXLDeviceState *cxl_dstate = &CXL_TYPE3(cci->d)->cxl_dstate;
    struct {
        uint64_t active_vmem;
        uint64_t active_pmem;
        uint64_t next_vmem;
        uint64_t next_pmem;
    } QEMU_PACKED *part_info = (void *)payload_out;
    QEMU_BUILD_BUG_ON(sizeof(*part_info) != 0x20);

    if ((!QEMU_IS_ALIGNED(cxl_dstate->vmem_size, CXL_CAPACITY_MULTIPLIER)) ||
        (!QEMU_IS_ALIGNED(cxl_dstate->pmem_size, CXL_CAPACITY_MULTIPLIER))) {
        return CXL_MBOX_INTERNAL_ERROR;
    }

    stq_le_p(&part_info->active_vmem,
             cxl_dstate->vmem_size / CXL_CAPACITY_MULTIPLIER);
    /*
     * When both next_vmem and next_pmem are 0, there is no pending change to
     * partitioning.
     */
    stq_le_p(&part_info->next_vmem, 0);
    stq_le_p(&part_info->active_pmem,
             cxl_dstate->pmem_size / CXL_CAPACITY_MULTIPLIER);
    stq_le_p(&part_info->next_pmem, 0);

    *len_out = sizeof(*part_info);
    return CXL_MBOX_SUCCESS;
}

static CXLRetCode cmd_ccls_get_lsa(const struct cxl_cmd *cmd,
                                   uint8_t *payload_in,
                                   size_t len_in,
                                   uint8_t *payload_out,
                                   size_t *len_out,
                                   CXLCCI *cci)
{
    struct {
        uint32_t offset;
        uint32_t length;
    } QEMU_PACKED *get_lsa;
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLType3Class *cvc = CXL_TYPE3_GET_CLASS(ct3d);
    uint32_t offset, length;

    get_lsa = (void *)payload_in;
    offset = get_lsa->offset;
    length = get_lsa->length;

    if (offset + length > cvc->get_lsa_size(ct3d)) {
        *len_out = 0;
        return CXL_MBOX_INVALID_INPUT;
    }

    *len_out = cvc->get_lsa(ct3d, payload_out, length, offset);
    return CXL_MBOX_SUCCESS;
}

static CXLRetCode cmd_ccls_set_lsa(const struct cxl_cmd *cmd,
                                   uint8_t *payload_in,
                                   size_t len_in,
                                   uint8_t *payload_out,
                                   size_t *len_out,
                                   CXLCCI *cci)
{
    struct set_lsa_pl {
        uint32_t offset;
        uint32_t rsvd;
        uint8_t data[];
    } QEMU_PACKED;
    struct set_lsa_pl *set_lsa_payload = (void *)payload_in;
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLType3Class *cvc = CXL_TYPE3_GET_CLASS(ct3d);
    const size_t hdr_len = offsetof(struct set_lsa_pl, data);

    *len_out = 0;
    if (!len_in) {
        return CXL_MBOX_SUCCESS;
    }

    if (set_lsa_payload->offset + len_in > cvc->get_lsa_size(ct3d) + hdr_len) {
        return CXL_MBOX_INVALID_INPUT;
    }
    len_in -= hdr_len;

    cvc->set_lsa(ct3d, set_lsa_payload->data, len_in, set_lsa_payload->offset);
    return CXL_MBOX_SUCCESS;
}

/* Perform the actual device zeroing */
static void __do_sanitization(CXLType3Dev *ct3d)
{
    MemoryRegion *mr;

    if (ct3d->hostvmem) {
        mr = host_memory_backend_get_memory(ct3d->hostvmem);
        if (mr) {
            void *hostmem = memory_region_get_ram_ptr(mr);
            memset(hostmem, 0, memory_region_size(mr));
        }
    }

    if (ct3d->hostpmem) {
        mr = host_memory_backend_get_memory(ct3d->hostpmem);
        if (mr) {
            void *hostmem = memory_region_get_ram_ptr(mr);
            memset(hostmem, 0, memory_region_size(mr));
        }
    }
    if (ct3d->lsa) {
        mr = host_memory_backend_get_memory(ct3d->lsa);
        if (mr) {
            void *lsa = memory_region_get_ram_ptr(mr);
            memset(lsa, 0, memory_region_size(mr));
        }
    }
}

/*
 * CXL 3.0 spec section 8.2.9.8.5.1 - Sanitize.
 *
 * Once the Sanitize command has started successfully, the device shall be
 * placed in the media disabled state. If the command fails or is interrupted
 * by a reset or power failure, it shall remain in the media disabled state
 * until a successful Sanitize command has been completed. During this state:
 *
 * 1. Memory writes to the device will have no effect, and all memory reads
 * will return random values (no user data returned, even for locations that
 * the failed Sanitize operation didn’t sanitize yet).
 *
 * 2. Mailbox commands shall still be processed in the disabled state, except
 * that commands that access Sanitized areas shall fail with the Media Disabled
 * error code.
 */
static CXLRetCode cmd_sanitize_overwrite(const struct cxl_cmd *cmd,
                                         uint8_t *payload_in,
                                         size_t len_in,
                                         uint8_t *payload_out,
                                         size_t *len_out,
                                         CXLCCI *cci)
{
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    uint64_t total_mem; /* in Mb */
    int secs;

    total_mem = (ct3d->cxl_dstate.vmem_size + ct3d->cxl_dstate.pmem_size) >> 20;
    if (total_mem <= 512) {
        secs = 4;
    } else if (total_mem <= 1024) {
        secs = 8;
    } else if (total_mem <= 2 * 1024) {
        secs = 15;
    } else if (total_mem <= 4 * 1024) {
        secs = 30;
    } else if (total_mem <= 8 * 1024) {
        secs = 60;
    } else if (total_mem <= 16 * 1024) {
        secs = 2 * 60;
    } else if (total_mem <= 32 * 1024) {
        secs = 4 * 60;
    } else if (total_mem <= 64 * 1024) {
        secs = 8 * 60;
    } else if (total_mem <= 128 * 1024) {
        secs = 15 * 60;
    } else if (total_mem <= 256 * 1024) {
        secs = 30 * 60;
    } else if (total_mem <= 512 * 1024) {
        secs = 60 * 60;
    } else if (total_mem <= 1024 * 1024) {
        secs = 120 * 60;
    } else {
        secs = 240 * 60; /* max 4 hrs */
    }

    /* EBUSY other bg cmds as of now */
    cci->bg.runtime = secs * 1000UL;
    *len_out = 0;

    cxl_dev_disable_media(&ct3d->cxl_dstate);

    if (secs > 2) {
        /* sanitize when done */
        return CXL_MBOX_BG_STARTED;
    } else {
        __do_sanitization(ct3d);
        cxl_dev_enable_media(&ct3d->cxl_dstate);

        return CXL_MBOX_SUCCESS;
    }
}

static CXLRetCode cmd_get_security_state(const struct cxl_cmd *cmd,
                                         uint8_t *payload_in,
                                         size_t len_in,
                                         uint8_t *payload_out,
                                         size_t *len_out,
                                         CXLCCI *cci)
{
    uint32_t *state = (uint32_t *)payload_out;

    *state = 0;
    *len_out = 4;
    return CXL_MBOX_SUCCESS;
}
/*
 * This is very inefficient, but good enough for now!
 * Also the payload will always fit, so no need to handle the MORE flag and
 * make this stateful. We may want to allow longer poison lists to aid
 * testing that kernel functionality.
 */
static CXLRetCode cmd_media_get_poison_list(const struct cxl_cmd *cmd,
                                            uint8_t *payload_in,
                                            size_t len_in,
                                            uint8_t *payload_out,
                                            size_t *len_out,
                                            CXLCCI *cci)
{
    struct get_poison_list_pl {
        uint64_t pa;
        uint64_t length;
    } QEMU_PACKED;

    struct get_poison_list_out_pl {
        uint8_t flags;
        uint8_t rsvd1;
        uint64_t overflow_timestamp;
        uint16_t count;
        uint8_t rsvd2[0x14];
        struct {
            uint64_t addr;
            uint32_t length;
            uint32_t resv;
        } QEMU_PACKED records[];
    } QEMU_PACKED;

    struct get_poison_list_pl *in = (void *)payload_in;
    struct get_poison_list_out_pl *out = (void *)payload_out;
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    uint16_t record_count = 0, i = 0;
    uint64_t query_start, query_length;
    CXLPoisonList *poison_list = &ct3d->poison_list;
    CXLPoison *ent;
    uint16_t out_pl_len;

    query_start = ldq_le_p(&in->pa);
    /* 64 byte alignment required */
    if (query_start & 0x3f) {
        return CXL_MBOX_INVALID_INPUT;
    }
    query_length = ldq_le_p(&in->length) * CXL_CACHE_LINE_SIZE;

    QLIST_FOREACH(ent, poison_list, node) {
        /* Check for no overlap */
        if (ent->start >= query_start + query_length ||
            ent->start + ent->length <= query_start) {
            continue;
        }
        record_count++;
    }
    out_pl_len = sizeof(*out) + record_count * sizeof(out->records[0]);
    assert(out_pl_len <= CXL_MAILBOX_MAX_PAYLOAD_SIZE);

    memset(out, 0, out_pl_len);
    QLIST_FOREACH(ent, poison_list, node) {
        uint64_t start, stop;

        /* Check for no overlap */
        if (ent->start >= query_start + query_length ||
            ent->start + ent->length <= query_start) {
            continue;
        }

        /* Deal with overlap */
        start = MAX(ROUND_DOWN(ent->start, 64ull), query_start);
        stop = MIN(ROUND_DOWN(ent->start, 64ull) + ent->length,
                   query_start + query_length);
        stq_le_p(&out->records[i].addr, start | (ent->type & 0x7));
        stl_le_p(&out->records[i].length, (stop - start) / CXL_CACHE_LINE_SIZE);
        i++;
    }
    if (ct3d->poison_list_overflowed) {
        out->flags = (1 << 1);
        stq_le_p(&out->overflow_timestamp, ct3d->poison_list_overflow_ts);
    }
    stw_le_p(&out->count, record_count);
    *len_out = out_pl_len;
    return CXL_MBOX_SUCCESS;
}

static CXLRetCode cmd_media_inject_poison(const struct cxl_cmd *cmd,
                                          uint8_t *payload_in,
                                          size_t len_in,
                                          uint8_t *payload_out,
                                          size_t *len_out,
                                          CXLCCI *cci)
{
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLPoisonList *poison_list = &ct3d->poison_list;
    CXLPoison *ent;
    struct inject_poison_pl {
        uint64_t dpa;
    };
    struct inject_poison_pl *in = (void *)payload_in;
    uint64_t dpa = ldq_le_p(&in->dpa);
    CXLPoison *p;

    QLIST_FOREACH(ent, poison_list, node) {
        if (dpa >= ent->start &&
            dpa + CXL_CACHE_LINE_SIZE <= ent->start + ent->length) {
            return CXL_MBOX_SUCCESS;
        }
    }

    if (ct3d->poison_list_cnt == CXL_POISON_LIST_LIMIT) {
        return CXL_MBOX_INJECT_POISON_LIMIT;
    }
    p = g_new0(CXLPoison, 1);

    p->length = CXL_CACHE_LINE_SIZE;
    p->start = dpa;
    p->type = CXL_POISON_TYPE_INJECTED;

    /*
     * Possible todo: Merge with existing entry if next to it and if same type
     */
    QLIST_INSERT_HEAD(poison_list, p, node);
    ct3d->poison_list_cnt++;
    *len_out = 0;

    return CXL_MBOX_SUCCESS;
}

static CXLRetCode cmd_media_clear_poison(const struct cxl_cmd *cmd,
                                         uint8_t *payload_in,
                                         size_t len_in,
                                         uint8_t *payload_out,
                                         size_t *len_out,
                                         CXLCCI *cci)
{
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLDeviceState *cxl_dstate = &ct3d->cxl_dstate;
    CXLPoisonList *poison_list = &ct3d->poison_list;
    CXLType3Class *cvc = CXL_TYPE3_GET_CLASS(ct3d);
    struct clear_poison_pl {
        uint64_t dpa;
        uint8_t data[64];
    };
    CXLPoison *ent;
    uint64_t dpa;

    struct clear_poison_pl *in = (void *)payload_in;

    dpa = ldq_le_p(&in->dpa);
    if (dpa + CXL_CACHE_LINE_SIZE > cxl_dstate->mem_size) {
        return CXL_MBOX_INVALID_PA;
    }

    /* Clearing a region with no poison is not an error so always do so */
    if (cvc->set_cacheline) {
        if (!cvc->set_cacheline(ct3d, dpa, in->data)) {
            return CXL_MBOX_INTERNAL_ERROR;
        }
    }

    QLIST_FOREACH(ent, poison_list, node) {
        /*
         * Test for contained in entry. Simpler than general case
         * as clearing 64 bytes and entries 64 byte aligned
         */
        if ((dpa >= ent->start) && (dpa < ent->start + ent->length)) {
            break;
        }
    }
    if (!ent) {
        return CXL_MBOX_SUCCESS;
    }

    QLIST_REMOVE(ent, node);
    ct3d->poison_list_cnt--;

    if (dpa > ent->start) {
        CXLPoison *frag;
        /* Cannot overflow as replacing existing entry */

        frag = g_new0(CXLPoison, 1);

        frag->start = ent->start;
        frag->length = dpa - ent->start;
        frag->type = ent->type;

        QLIST_INSERT_HEAD(poison_list, frag, node);
        ct3d->poison_list_cnt++;
    }

    if (dpa + CXL_CACHE_LINE_SIZE < ent->start + ent->length) {
        CXLPoison *frag;

        if (ct3d->poison_list_cnt == CXL_POISON_LIST_LIMIT) {
            cxl_set_poison_list_overflowed(ct3d);
        } else {
            frag = g_new0(CXLPoison, 1);

            frag->start = dpa + CXL_CACHE_LINE_SIZE;
            frag->length = ent->start + ent->length - frag->start;
            frag->type = ent->type;
            QLIST_INSERT_HEAD(poison_list, frag, node);
            ct3d->poison_list_cnt++;
        }
    }
    /* Any fragments have been added, free original entry */
    g_free(ent);
    *len_out = 0;

    return CXL_MBOX_SUCCESS;
}

#define IMMEDIATE_CONFIG_CHANGE (1 << 1)
#define IMMEDIATE_DATA_CHANGE (1 << 2)
#define IMMEDIATE_POLICY_CHANGE (1 << 3)
#define IMMEDIATE_LOG_CHANGE (1 << 4)
#define SECURITY_STATE_CHANGE (1 << 5)
#define BACKGROUND_OPERATION (1 << 6)

static const struct cxl_cmd cxl_cmd_set[256][256] = {
    [EVENTS][GET_RECORDS] = { "EVENTS_GET_RECORDS",
        cmd_events_get_records, 1, 0 },
    [EVENTS][CLEAR_RECORDS] = { "EVENTS_CLEAR_RECORDS",
        cmd_events_clear_records, ~0, IMMEDIATE_LOG_CHANGE },
    [EVENTS][GET_INTERRUPT_POLICY] = { "EVENTS_GET_INTERRUPT_POLICY",
                                      cmd_events_get_interrupt_policy, 0, 0 },
    [EVENTS][SET_INTERRUPT_POLICY] = { "EVENTS_SET_INTERRUPT_POLICY",
                                      cmd_events_set_interrupt_policy,
                                      ~0, IMMEDIATE_CONFIG_CHANGE },
    [FIRMWARE_UPDATE][GET_INFO] = { "FIRMWARE_UPDATE_GET_INFO",
        cmd_firmware_update_get_info, 0, 0 },
    [TIMESTAMP][GET] = { "TIMESTAMP_GET", cmd_timestamp_get, 0, 0 },
    [TIMESTAMP][SET] = { "TIMESTAMP_SET", cmd_timestamp_set,
                         8, IMMEDIATE_POLICY_CHANGE },
    [LOGS][GET_SUPPORTED] = { "LOGS_GET_SUPPORTED", cmd_logs_get_supported,
                              0, 0 },
    [LOGS][GET_LOG] = { "LOGS_GET_LOG", cmd_logs_get_log, 0x18, 0 },
    [IDENTIFY][MEMORY_DEVICE] = { "IDENTIFY_MEMORY_DEVICE",
        cmd_identify_memory_device, 0, 0 },
    [CCLS][GET_PARTITION_INFO] = { "CCLS_GET_PARTITION_INFO",
        cmd_ccls_get_partition_info, 0, 0 },
    [CCLS][GET_LSA] = { "CCLS_GET_LSA", cmd_ccls_get_lsa, 8, 0 },
    [CCLS][SET_LSA] = { "CCLS_SET_LSA", cmd_ccls_set_lsa,
        ~0, IMMEDIATE_CONFIG_CHANGE | IMMEDIATE_DATA_CHANGE },
    [SANITIZE][OVERWRITE] = { "SANITIZE_OVERWRITE", cmd_sanitize_overwrite, 0,
        IMMEDIATE_DATA_CHANGE | SECURITY_STATE_CHANGE | BACKGROUND_OPERATION },
    [PERSISTENT_MEM][GET_SECURITY_STATE] = { "GET_SECURITY_STATE",
        cmd_get_security_state, 0, 0 },
    [MEDIA_AND_POISON][GET_POISON_LIST] = { "MEDIA_AND_POISON_GET_POISON_LIST",
        cmd_media_get_poison_list, 16, 0 },
    [MEDIA_AND_POISON][INJECT_POISON] = { "MEDIA_AND_POISON_INJECT_POISON",
        cmd_media_inject_poison, 8, 0 },
    [MEDIA_AND_POISON][CLEAR_POISON] = { "MEDIA_AND_POISON_CLEAR_POISON",
        cmd_media_clear_poison, 72, 0 },
};

static const struct cxl_cmd cxl_cmd_set_sw[256][256] = {
    [INFOSTAT][IS_IDENTIFY] = { "IDENTIFY", cmd_infostat_identify, 0, 0 },
    [INFOSTAT][BACKGROUND_OPERATION_STATUS] = { "BACKGROUND_OPERATION_STATUS",
        cmd_infostat_bg_op_sts, 0, 0 },
    [TIMESTAMP][GET] = { "TIMESTAMP_GET", cmd_timestamp_get, 0, 0 },
    [TIMESTAMP][SET] = { "TIMESTAMP_SET", cmd_timestamp_set, 0,
                         IMMEDIATE_POLICY_CHANGE },
    [LOGS][GET_SUPPORTED] = { "LOGS_GET_SUPPORTED", cmd_logs_get_supported, 0,
                              0 },
    [LOGS][GET_LOG] = { "LOGS_GET_LOG", cmd_logs_get_log, 0x18, 0 },
    [PHYSICAL_SWITCH][IDENTIFY_SWITCH_DEVICE] = { "IDENTIFY_SWITCH_DEVICE",
        cmd_identify_switch_device, 0, 0 },
    [PHYSICAL_SWITCH][GET_PHYSICAL_PORT_STATE] = { "SWITCH_PHYSICAL_PORT_STATS",
        cmd_get_physical_port_state, ~0, 0 },
    [TUNNEL][MANAGEMENT_COMMAND] = { "TUNNEL_MANAGEMENT_COMMAND",
                                     cmd_tunnel_management_cmd, ~0, 0 },
};

/*
 * While the command is executing in the background, the device should
 * update the percentage complete in the Background Command Status Register
 * at least once per second.
 */

#define CXL_MBOX_BG_UPDATE_FREQ 1000UL

int cxl_process_cci_message(CXLCCI *cci, uint8_t set, uint8_t cmd,
                            size_t len_in, uint8_t *pl_in, size_t *len_out,
                            uint8_t *pl_out, bool *bg_started)
{
    int ret;
    const struct cxl_cmd *cxl_cmd;
    opcode_handler h;

    *len_out = 0;
    cxl_cmd = &cci->cxl_cmd_set[set][cmd];
    h = cxl_cmd->handler;
    if (!h) {
        qemu_log_mask(LOG_UNIMP, "Command %04xh not implemented\n",
                      set << 8 | cmd);
        return CXL_MBOX_UNSUPPORTED;
    }

    if (len_in != cxl_cmd->in && cxl_cmd->in != ~0) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }

    /* Only one bg command at a time */
    if ((cxl_cmd->effect & BACKGROUND_OPERATION) &&
        cci->bg.runtime > 0) {
        return CXL_MBOX_BUSY;
    }

    /* forbid any selected commands while overwriting */
    if (sanitize_running(cci)) {
        if (h == cmd_events_get_records ||
            h == cmd_ccls_get_partition_info ||
            h == cmd_ccls_set_lsa ||
            h == cmd_ccls_get_lsa ||
            h == cmd_logs_get_log ||
            h == cmd_media_get_poison_list ||
            h == cmd_media_inject_poison ||
            h == cmd_media_clear_poison ||
            h == cmd_sanitize_overwrite) {
            return CXL_MBOX_MEDIA_DISABLED;
        }
    }

    ret = (*h)(cxl_cmd, pl_in, len_in, pl_out, len_out, cci);
    if ((cxl_cmd->effect & BACKGROUND_OPERATION) &&
        ret == CXL_MBOX_BG_STARTED) {
        *bg_started = true;
    } else {
        *bg_started = false;
    }

    /* Set bg and the return code */
    if (*bg_started) {
        uint64_t now;

        cci->bg.opcode = (set << 8) | cmd;

        cci->bg.complete_pct = 0;
        cci->bg.ret_code = 0;

        now = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
        cci->bg.starttime = now;
        timer_mod(cci->bg.timer, now + CXL_MBOX_BG_UPDATE_FREQ);
    }

    return ret;
}

static void bg_timercb(void *opaque)
{
    CXLCCI *cci = opaque;
    uint64_t now = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    uint64_t total_time = cci->bg.starttime + cci->bg.runtime;

    assert(cci->bg.runtime > 0);

    if (now >= total_time) { /* we are done */
        uint16_t ret = CXL_MBOX_SUCCESS;

        cci->bg.complete_pct = 100;
        cci->bg.ret_code = ret;
        if (ret == CXL_MBOX_SUCCESS) {
            switch (cci->bg.opcode) {
            case 0x4400: /* sanitize */
            {
                CXLType3Dev *ct3d = CXL_TYPE3(cci->d);

                __do_sanitization(ct3d);
                cxl_dev_enable_media(&ct3d->cxl_dstate);
            }
            break;
            case 0x4304: /* TODO: scan media */
                break;
            default:
                __builtin_unreachable();
                break;
            }
        }

        qemu_log("Background command %04xh finished: %s\n",
                 cci->bg.opcode,
                 ret == CXL_MBOX_SUCCESS ? "success" : "aborted");
    } else {
        /* estimate only */
        cci->bg.complete_pct = 100 * now / total_time;
        timer_mod(cci->bg.timer, now + CXL_MBOX_BG_UPDATE_FREQ);
    }

    if (cci->bg.complete_pct == 100) {
        /* TODO: generalize to switch CCI */
        CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
        CXLDeviceState *cxl_dstate = &ct3d->cxl_dstate;
        PCIDevice *pdev = PCI_DEVICE(cci->d);

        cci->bg.starttime = 0;
        /* registers are updated, allow new bg-capable cmds */
        cci->bg.runtime = 0;

        if (msix_enabled(pdev)) {
            msix_notify(pdev, cxl_dstate->mbox_msi_n);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, cxl_dstate->mbox_msi_n);
        }
    }
}

void cxl_init_cci(CXLCCI *cci, size_t payload_max)
{
    cci->payload_max = payload_max;
    for (int set = 0; set < 256; set++) {
        for (int cmd = 0; cmd < 256; cmd++) {
            if (cci->cxl_cmd_set[set][cmd].handler) {
                const struct cxl_cmd *c = &cci->cxl_cmd_set[set][cmd];
                struct cel_log *log =
                    &cci->cel_log[cci->cel_size];

                log->opcode = (set << 8) | cmd;
                log->effect = c->effect;
                cci->cel_size++;
            }
        }
    }
    cci->bg.complete_pct = 0;
    cci->bg.starttime = 0;
    cci->bg.runtime = 0;
    cci->bg.timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                 bg_timercb, cci);
}

void cxl_initialize_mailbox_swcci(CXLCCI *cci, DeviceState *intf,
                                  DeviceState *d, size_t payload_max)
{
    cci->cxl_cmd_set = cxl_cmd_set_sw;
    cci->d = d;
    cci->intf = intf;
    cxl_init_cci(cci, payload_max);
}

void cxl_initialize_mailbox_t3(CXLCCI *cci, DeviceState *d, size_t payload_max)
{
    cci->cxl_cmd_set = cxl_cmd_set;
    cci->d = d;

    /* No separation for PCI MB as protocol handled in PCI device */
    cci->intf = d;
    cxl_init_cci(cci, payload_max);
}

static const struct cxl_cmd cxl_cmd_set_t3_ld[256][256] = {
    [INFOSTAT][IS_IDENTIFY] = { "IDENTIFY", cmd_infostat_identify, 0, 0 },
    [LOGS][GET_SUPPORTED] = { "LOGS_GET_SUPPORTED", cmd_logs_get_supported, 0,
                              0 },
    [LOGS][GET_LOG] = { "LOGS_GET_LOG", cmd_logs_get_log, 0x18, 0 },
};

void cxl_initialize_t3_ld_cci(CXLCCI *cci, DeviceState *d, DeviceState *intf,
                               size_t payload_max)
{
    cci->cxl_cmd_set = cxl_cmd_set_t3_ld;
    cci->d = d;
    cci->intf = intf;
    cxl_init_cci(cci, payload_max);
}

static const struct cxl_cmd cxl_cmd_set_t3_fm_owned_ld_mctp[256][256] = {
    [INFOSTAT][IS_IDENTIFY] = { "IDENTIFY", cmd_infostat_identify, 0,  0},
    [LOGS][GET_SUPPORTED] = { "LOGS_GET_SUPPORTED", cmd_logs_get_supported, 0,
                              0 },
    [LOGS][GET_LOG] = { "LOGS_GET_LOG", cmd_logs_get_log, 0x18, 0 },
    [TIMESTAMP][GET] = { "TIMESTAMP_GET", cmd_timestamp_get, 0, 0 },
    [TUNNEL][MANAGEMENT_COMMAND] = { "TUNNEL_MANAGEMENT_COMMAND",
                                     cmd_tunnel_management_cmd, ~0, 0 },
};

void cxl_initialize_t3_fm_owned_ld_mctpcci(CXLCCI *cci, DeviceState *d,
                                           DeviceState *intf,
                                           size_t payload_max)
{
    cci->cxl_cmd_set = cxl_cmd_set_t3_fm_owned_ld_mctp;
    cci->d = d;
    cci->intf = intf;
    cxl_init_cci(cci, payload_max);
}
