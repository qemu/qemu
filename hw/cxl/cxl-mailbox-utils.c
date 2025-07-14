/*
 * CXL Utility library for mailbox interface
 *
 * Copyright(C) 2020 Intel Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#include <math.h>

#include "qemu/osdep.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/cxl/cxl.h"
#include "hw/cxl/cxl_events.h"
#include "hw/cxl/cxl_mailbox.h"
#include "hw/pci/pci.h"
#include "hw/pci-bridge/cxl_upstream_port.h"
#include "qemu/cutils.h"
#include "qemu/host-utils.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qemu/uuid.h"
#include "system/hostmem.h"
#include "qemu/range.h"
#include "qapi/qapi-types-cxl.h"

#define CXL_CAPACITY_MULTIPLIER   (256 * MiB)
#define CXL_DC_EVENT_LOG_SIZE 8
#define CXL_NUM_TAGS_SUPPORTED 0
#define CXL_ALERTS_LIFE_USED_WARN_THRESH (1 << 0)
#define CXL_ALERTS_OVER_TEMP_WARN_THRESH (1 << 1)
#define CXL_ALERTS_UNDER_TEMP_WARN_THRESH (1 << 2)
#define CXL_ALERTS_COR_VMEM_ERR_WARN_THRESH (1 << 3)
#define CXL_ALERTS_COR_PMEM_ERR_WARN_THRESH (1 << 4)

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
        #define GET_RESPONSE_MSG_LIMIT         0x3
        #define SET_RESPONSE_MSG_LIMIT         0x4
        #define BACKGROUND_OPERATION_ABORT     0x5
    EVENTS      = 0x01,
        #define GET_RECORDS   0x0
        #define CLEAR_RECORDS   0x1
        #define GET_INTERRUPT_POLICY   0x2
        #define SET_INTERRUPT_POLICY   0x3
    FIRMWARE_UPDATE = 0x02,
        #define GET_INFO      0x0
        #define TRANSFER      0x1
        #define ACTIVATE      0x2
    TIMESTAMP   = 0x03,
        #define GET           0x0
        #define SET           0x1
    LOGS        = 0x04,
        #define GET_SUPPORTED 0x0
        #define GET_LOG       0x1
    FEATURES    = 0x05,
        #define GET_SUPPORTED 0x0
        #define GET_FEATURE   0x1
        #define SET_FEATURE   0x2
    IDENTIFY    = 0x40,
        #define MEMORY_DEVICE 0x0
    CCLS        = 0x41,
        #define GET_PARTITION_INFO     0x0
        #define GET_LSA       0x2
        #define SET_LSA       0x3
    HEALTH_INFO_ALERTS = 0x42,
        #define GET_ALERT_CONFIG 0x1
        #define SET_ALERT_CONFIG 0x2
    SANITIZE    = 0x44,
        #define OVERWRITE     0x0
        #define SECURE_ERASE  0x1
        #define MEDIA_OPERATIONS 0x2
    PERSISTENT_MEM = 0x45,
        #define GET_SECURITY_STATE     0x0
    MEDIA_AND_POISON = 0x43,
        #define GET_POISON_LIST        0x0
        #define INJECT_POISON          0x1
        #define CLEAR_POISON           0x2
        #define GET_SCAN_MEDIA_CAPABILITIES 0x3
        #define SCAN_MEDIA             0x4
        #define GET_SCAN_MEDIA_RESULTS 0x5
    DCD_CONFIG  = 0x48,
        #define GET_DC_CONFIG          0x0
        #define GET_DYN_CAP_EXT_LIST   0x1
        #define ADD_DYN_CAP_RSP        0x2
        #define RELEASE_DYN_CAP        0x3
    PHYSICAL_SWITCH = 0x51,
        #define IDENTIFY_SWITCH_DEVICE      0x0
        #define GET_PHYSICAL_PORT_STATE     0x1
    TUNNEL = 0x53,
        #define MANAGEMENT_COMMAND     0x0
    FMAPI_DCD_MGMT = 0x56,
        #define GET_DCD_INFO    0x0
        #define GET_HOST_DC_REGION_CONFIG   0x1
        #define SET_DC_REGION_CONFIG        0x2
        #define GET_DC_REGION_EXTENT_LIST   0x3
        #define INITIATE_DC_ADD             0x4
        #define INITIATE_DC_RELEASE         0x5
};

/* CCI Message Format CXL r3.1 Figure 7-19 */
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

    if (len_in < sizeof(*in)) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }
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

    if (len_in < sizeof(*pl) ||
        len_in < sizeof(*pl) + sizeof(*pl->handle) * pl->nr_recs) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }

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

/* CXL r3.1 section 8.2.9.1.1: Identify (Opcode 0001h) */
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

    is_identify->max_message_size = (uint8_t)log2(cci->payload_max);
    *len_out = sizeof(*is_identify);
    return CXL_MBOX_SUCCESS;
}

/* CXL r3.1 section 8.2.9.1.3: Get Response Message Limit (Opcode 0003h) */
static CXLRetCode cmd_get_response_msg_limit(const struct cxl_cmd *cmd,
                                             uint8_t *payload_in,
                                             size_t len_in,
                                             uint8_t *payload_out,
                                             size_t *len_out,
                                             CXLCCI *cci)
{
    struct {
        uint8_t rsp_limit;
    } QEMU_PACKED *get_rsp_msg_limit = (void *)payload_out;
    QEMU_BUILD_BUG_ON(sizeof(*get_rsp_msg_limit) != 1);

    get_rsp_msg_limit->rsp_limit = (uint8_t)log2(cci->payload_max);

    *len_out = sizeof(*get_rsp_msg_limit);
    return CXL_MBOX_SUCCESS;
}

/* CXL r3.1 section 8.2.9.1.4: Set Response Message Limit (Opcode 0004h) */
static CXLRetCode cmd_set_response_msg_limit(const struct cxl_cmd *cmd,
                                             uint8_t *payload_in,
                                             size_t len_in,
                                             uint8_t *payload_out,
                                             size_t *len_out,
                                             CXLCCI *cci)
{
    struct {
        uint8_t rsp_limit;
    } QEMU_PACKED *in = (void *)payload_in;
    QEMU_BUILD_BUG_ON(sizeof(*in) != 1);
    struct {
        uint8_t rsp_limit;
    } QEMU_PACKED *out = (void *)payload_out;
    QEMU_BUILD_BUG_ON(sizeof(*out) != 1);

    if (in->rsp_limit < 8 || in->rsp_limit > 10) {
        return CXL_MBOX_INVALID_INPUT;
    }

    cci->payload_max = 1 << in->rsp_limit;
    out->rsp_limit = in->rsp_limit;

    *len_out = sizeof(*out);
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

/* CXL r3.1 Section 7.6.7.1.1: Identify Switch Device (Opcode 5100h) */
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

/* CXL r3.1 Section 7.6.7.1.2: Get Physical Port State (Opcode 5101h) */
static CXLRetCode cmd_get_physical_port_state(const struct cxl_cmd *cmd,
                                              uint8_t *payload_in,
                                              size_t len_in,
                                              uint8_t *payload_out,
                                              size_t *len_out,
                                              CXLCCI *cci)
{
    /* CXL r3.1 Table 7-17: Get Physical Port State Request Payload */
    struct cxl_fmapi_get_phys_port_state_req_pl {
        uint8_t num_ports;
        uint8_t ports[];
    } QEMU_PACKED *in;

    /*
     * CXL r3.1 Table 7-19: Get Physical Port State Port Information Block
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

    /* CXL r3.1 Table 7-18: Get Physical Port State Response Payload */
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

    if (len_in < sizeof(*in)) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }
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

/* CXL r3.1 Section 8.2.9.1.2: Background Operation Status (Opcode 0002h) */
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
    bg_op_status->status = cci->bg.complete_pct << 1;
    if (cci->bg.runtime > 0) {
        bg_op_status->status |= 1U << 0;
    }
    bg_op_status->opcode = cci->bg.opcode;
    bg_op_status->returncode = cci->bg.ret_code;
    *len_out = sizeof(*bg_op_status);

    return CXL_MBOX_SUCCESS;
}

/*
 * CXL r3.1 Section 8.2.9.1.5:
 * Request Abort Background Operation (Opcode 0005h)
 */
static CXLRetCode cmd_infostat_bg_op_abort(const struct cxl_cmd *cmd,
                                           uint8_t *payload_in,
                                           size_t len_in,
                                           uint8_t *payload_out,
                                           size_t *len_out,
                                           CXLCCI *cci)
{
    int bg_set = cci->bg.opcode >> 8;
    int bg_cmd = cci->bg.opcode & 0xff;
    const struct cxl_cmd *bg_c = &cci->cxl_cmd_set[bg_set][bg_cmd];

    if (!(bg_c->effect & CXL_MBOX_BACKGROUND_OPERATION_ABORT)) {
        return CXL_MBOX_REQUEST_ABORT_NOTSUP;
    }

    qemu_mutex_lock(&cci->bg.lock);
    if (cci->bg.runtime) {
        /* operation is near complete, let it finish */
        if (cci->bg.complete_pct < 85) {
            timer_del(cci->bg.timer);
            cci->bg.ret_code = CXL_MBOX_ABORTED;
            cci->bg.starttime = 0;
            cci->bg.runtime = 0;
            cci->bg.aborted = true;
        }
    }
    qemu_mutex_unlock(&cci->bg.lock);

    return CXL_MBOX_SUCCESS;
}

#define CXL_FW_SLOTS 2
#define CXL_FW_SIZE  0x02000000 /* 32 mb */

/* CXL r3.1 Section 8.2.9.3.1: Get FW Info (Opcode 0200h) */
static CXLRetCode cmd_firmware_update_get_info(const struct cxl_cmd *cmd,
                                               uint8_t *payload_in,
                                               size_t len,
                                               uint8_t *payload_out,
                                               size_t *len_out,
                                               CXLCCI *cci)
{
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLDeviceState *cxl_dstate = &ct3d->cxl_dstate;
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

    if (!QEMU_IS_ALIGNED(cxl_dstate->vmem_size, CXL_CAPACITY_MULTIPLIER) ||
        !QEMU_IS_ALIGNED(cxl_dstate->pmem_size, CXL_CAPACITY_MULTIPLIER) ||
        !QEMU_IS_ALIGNED(ct3d->dc.total_capacity, CXL_CAPACITY_MULTIPLIER)) {
        return CXL_MBOX_INTERNAL_ERROR;
    }

    fw_info = (void *)payload_out;

    fw_info->slots_supported = CXL_FW_SLOTS;
    fw_info->slot_info = (cci->fw.active_slot & 0x7) |
            ((cci->fw.staged_slot & 0x7) << 3);
    fw_info->caps = BIT(0);  /* online update supported */

    if (cci->fw.slot[0]) {
        pstrcpy(fw_info->fw_rev1, sizeof(fw_info->fw_rev1), "BWFW VERSION 0");
    }
    if (cci->fw.slot[1]) {
        pstrcpy(fw_info->fw_rev2, sizeof(fw_info->fw_rev2), "BWFW VERSION 1");
    }

    *len_out = sizeof(*fw_info);
    return CXL_MBOX_SUCCESS;
}

/* CXL r3.1 section 8.2.9.3.2: Transfer FW (Opcode 0201h) */
#define CXL_FW_XFER_ALIGNMENT   128

#define CXL_FW_XFER_ACTION_FULL     0x0
#define CXL_FW_XFER_ACTION_INIT     0x1
#define CXL_FW_XFER_ACTION_CONTINUE 0x2
#define CXL_FW_XFER_ACTION_END      0x3
#define CXL_FW_XFER_ACTION_ABORT    0x4

static CXLRetCode cmd_firmware_update_transfer(const struct cxl_cmd *cmd,
                                               uint8_t *payload_in,
                                               size_t len,
                                               uint8_t *payload_out,
                                               size_t *len_out,
                                               CXLCCI *cci)
{
    struct {
        uint8_t action;
        uint8_t slot;
        uint8_t rsvd1[2];
        uint32_t offset;
        uint8_t rsvd2[0x78];
        uint8_t data[];
    } QEMU_PACKED *fw_transfer = (void *)payload_in;
    size_t offset, length;

    if (len < sizeof(*fw_transfer)) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }

    if (fw_transfer->action == CXL_FW_XFER_ACTION_ABORT) {
        /*
         * At this point there aren't any on-going transfers
         * running in the bg - this is serialized before this
         * call altogether. Just mark the state machine and
         * disregard any other input.
         */
        cci->fw.transferring = false;
        return CXL_MBOX_SUCCESS;
    }

    offset = fw_transfer->offset * CXL_FW_XFER_ALIGNMENT;
    length = len - sizeof(*fw_transfer);
    if (offset + length > CXL_FW_SIZE) {
        return CXL_MBOX_INVALID_INPUT;
    }

    if (cci->fw.transferring) {
        if (fw_transfer->action == CXL_FW_XFER_ACTION_FULL ||
            fw_transfer->action == CXL_FW_XFER_ACTION_INIT) {
            return CXL_MBOX_FW_XFER_IN_PROGRESS;
        }
        /*
         * Abort partitioned package transfer if over 30 secs
         * between parts. As opposed to the explicit ABORT action,
         * semantically treat this condition as an error - as
         * if a part action were passed without a previous INIT.
         */
        if (difftime(time(NULL), cci->fw.last_partxfer) > 30.0) {
            cci->fw.transferring = false;
            return CXL_MBOX_INVALID_INPUT;
        }
    } else if (fw_transfer->action == CXL_FW_XFER_ACTION_CONTINUE ||
               fw_transfer->action == CXL_FW_XFER_ACTION_END) {
        return CXL_MBOX_INVALID_INPUT;
    }

    /* allow back-to-back retransmission */
    if ((offset != cci->fw.prev_offset || length != cci->fw.prev_len) &&
        (fw_transfer->action == CXL_FW_XFER_ACTION_CONTINUE ||
         fw_transfer->action == CXL_FW_XFER_ACTION_END)) {
        /* verify no overlaps */
        if (offset < cci->fw.prev_offset + cci->fw.prev_len) {
            return CXL_MBOX_FW_XFER_OUT_OF_ORDER;
        }
    }

    switch (fw_transfer->action) {
    case CXL_FW_XFER_ACTION_FULL: /* ignores offset */
    case CXL_FW_XFER_ACTION_END:
        if (fw_transfer->slot == 0 ||
            fw_transfer->slot == cci->fw.active_slot ||
            fw_transfer->slot > CXL_FW_SLOTS) {
            return CXL_MBOX_FW_INVALID_SLOT;
        }

        /* mark the slot used upon bg completion */
        break;
    case CXL_FW_XFER_ACTION_INIT:
        if (offset != 0) {
            return CXL_MBOX_INVALID_INPUT;
        }

        cci->fw.transferring = true;
        cci->fw.prev_offset = offset;
        cci->fw.prev_len = length;
        break;
    case CXL_FW_XFER_ACTION_CONTINUE:
        cci->fw.prev_offset = offset;
        cci->fw.prev_len = length;
        break;
    default:
        return CXL_MBOX_INVALID_INPUT;
    }

    if (fw_transfer->action == CXL_FW_XFER_ACTION_FULL) {
        cci->bg.runtime = 10 * 1000UL;
    } else {
        cci->bg.runtime = 2 * 1000UL;
    }
    /* keep relevant context for bg completion */
    cci->fw.curr_action = fw_transfer->action;
    cci->fw.curr_slot = fw_transfer->slot;
    *len_out = 0;

    return CXL_MBOX_BG_STARTED;
}

static void __do_firmware_xfer(CXLCCI *cci)
{
    switch (cci->fw.curr_action) {
    case CXL_FW_XFER_ACTION_FULL:
    case CXL_FW_XFER_ACTION_END:
        cci->fw.slot[cci->fw.curr_slot - 1] = true;
        cci->fw.transferring = false;
        break;
    case CXL_FW_XFER_ACTION_INIT:
    case CXL_FW_XFER_ACTION_CONTINUE:
        time(&cci->fw.last_partxfer);
        break;
    default:
        break;
    }
}

/* CXL r3.1 section 8.2.9.3.3: Activate FW (Opcode 0202h) */
static CXLRetCode cmd_firmware_update_activate(const struct cxl_cmd *cmd,
                                               uint8_t *payload_in,
                                               size_t len,
                                               uint8_t *payload_out,
                                               size_t *len_out,
                                               CXLCCI *cci)
{
    struct {
        uint8_t action;
        uint8_t slot;
    } QEMU_PACKED *fw_activate = (void *)payload_in;
    QEMU_BUILD_BUG_ON(sizeof(*fw_activate) != 0x2);

    if (fw_activate->slot == 0 ||
        fw_activate->slot == cci->fw.active_slot ||
        fw_activate->slot > CXL_FW_SLOTS) {
        return CXL_MBOX_FW_INVALID_SLOT;
    }

    /* ensure that an actual fw package is there */
    if (!cci->fw.slot[fw_activate->slot - 1]) {
        return CXL_MBOX_FW_INVALID_SLOT;
    }

    switch (fw_activate->action) {
    case 0: /* online */
        cci->fw.active_slot = fw_activate->slot;
        break;
    case 1: /* reset */
        cci->fw.staged_slot = fw_activate->slot;
        break;
    default:
        return CXL_MBOX_INVALID_INPUT;
    }

    return CXL_MBOX_SUCCESS;
}

/* CXL r3.1 Section 8.2.9.4.1: Get Timestamp (Opcode 0300h) */
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

/* CXL r3.1 Section 8.2.9.4.2: Set Timestamp (Opcode 0301h) */
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

/* CXL r3.1 Section 8.2.9.5.2.1: Command Effects Log (CEL) */
static const QemuUUID cel_uuid = {
    .data = UUID(0x0da9c0b5, 0xbf41, 0x4b78, 0x8f, 0x79,
                 0x96, 0xb1, 0x62, 0x3b, 0x3f, 0x17)
};

/* CXL r3.1 Section 8.2.9.5.1: Get Supported Logs (Opcode 0400h) */
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

/* CXL r3.1 Section 8.2.9.5.2: Get Log (Opcode 0401h) */
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

    if (get_log->length > cci->payload_max) {
        return CXL_MBOX_INVALID_INPUT;
    }

    if (!qemu_uuid_is_equal(&get_log->uuid, &cel_uuid)) {
        return CXL_MBOX_INVALID_LOG;
    }

    /*
     * CXL r3.1 Section 8.2.9.5.2: Get Log (Opcode 0401h)
     *   The device shall return Invalid Input if the Offset or Length
     *   fields attempt to access beyond the size of the log as reported by Get
     *   Supported Log.
     *
     * Only valid for there to be one entry per opcode, but the length + offset
     * may still be greater than that if the inputs are not valid and so access
     * beyond the end of cci->cel_log.
     */
    if ((uint64_t)get_log->offset + get_log->length >= sizeof(cci->cel_log)) {
        return CXL_MBOX_INVALID_INPUT;
    }

    /* Store off everything to local variables so we can wipe out the payload */
    *len_out = get_log->length;

    memmove(payload_out, cci->cel_log + get_log->offset, get_log->length);

    return CXL_MBOX_SUCCESS;
}

/* CXL r3.1 section 8.2.9.6: Features */
/*
 * Get Supported Features output payload
 * CXL r3.1 section 8.2.9.6.1 Table 8-96
 */
typedef struct CXLSupportedFeatureHeader {
    uint16_t entries;
    uint16_t nsuppfeats_dev;
    uint32_t reserved;
} QEMU_PACKED CXLSupportedFeatureHeader;

/*
 * Get Supported Features Supported Feature Entry
 * CXL r3.1 section 8.2.9.6.1 Table 8-97
 */
typedef struct CXLSupportedFeatureEntry {
    QemuUUID uuid;
    uint16_t feat_index;
    uint16_t get_feat_size;
    uint16_t set_feat_size;
    uint32_t attr_flags;
    uint8_t get_feat_version;
    uint8_t set_feat_version;
    uint16_t set_feat_effects;
    uint8_t rsvd[18];
} QEMU_PACKED CXLSupportedFeatureEntry;

/*
 * Get Supported Features Supported Feature Entry
 * CXL rev 3.1 section 8.2.9.6.1 Table 8-97
 */
/* Supported Feature Entry : attribute flags */
#define CXL_FEAT_ENTRY_ATTR_FLAG_CHANGABLE BIT(0)
#define CXL_FEAT_ENTRY_ATTR_FLAG_DEEPEST_RESET_PERSISTENCE_MASK GENMASK(3, 1)
#define CXL_FEAT_ENTRY_ATTR_FLAG_PERSIST_ACROSS_FIRMWARE_UPDATE BIT(4)
#define CXL_FEAT_ENTRY_ATTR_FLAG_SUPPORT_DEFAULT_SELECTION BIT(5)
#define CXL_FEAT_ENTRY_ATTR_FLAG_SUPPORT_SAVED_SELECTION BIT(6)

/* Supported Feature Entry : set feature effects */
#define CXL_FEAT_ENTRY_SFE_CONFIG_CHANGE_COLD_RESET BIT(0)
#define CXL_FEAT_ENTRY_SFE_IMMEDIATE_CONFIG_CHANGE BIT(1)
#define CXL_FEAT_ENTRY_SFE_IMMEDIATE_DATA_CHANGE BIT(2)
#define CXL_FEAT_ENTRY_SFE_IMMEDIATE_POLICY_CHANGE BIT(3)
#define CXL_FEAT_ENTRY_SFE_IMMEDIATE_LOG_CHANGE BIT(4)
#define CXL_FEAT_ENTRY_SFE_SECURITY_STATE_CHANGE BIT(5)
#define CXL_FEAT_ENTRY_SFE_BACKGROUND_OPERATION BIT(6)
#define CXL_FEAT_ENTRY_SFE_SUPPORT_SECONDARY_MAILBOX BIT(7)
#define CXL_FEAT_ENTRY_SFE_SUPPORT_ABORT_BACKGROUND_OPERATION BIT(8)
#define CXL_FEAT_ENTRY_SFE_CEL_VALID BIT(9)
#define CXL_FEAT_ENTRY_SFE_CONFIG_CHANGE_CONV_RESET BIT(10)
#define CXL_FEAT_ENTRY_SFE_CONFIG_CHANGE_CXL_RESET BIT(11)

enum CXL_SUPPORTED_FEATURES_LIST {
    CXL_FEATURE_PATROL_SCRUB = 0,
    CXL_FEATURE_ECS,
    CXL_FEATURE_MAX
};

/* Get Feature CXL 3.1 Spec 8.2.9.6.2 */
/*
 * Get Feature input payload
 * CXL r3.1 section 8.2.9.6.2 Table 8-99
 */
/* Get Feature : Payload in selection */
enum CXL_GET_FEATURE_SELECTION {
    CXL_GET_FEATURE_SEL_CURRENT_VALUE,
    CXL_GET_FEATURE_SEL_DEFAULT_VALUE,
    CXL_GET_FEATURE_SEL_SAVED_VALUE,
    CXL_GET_FEATURE_SEL_MAX
};

/* Set Feature CXL 3.1 Spec 8.2.9.6.3 */
/*
 * Set Feature input payload
 * CXL r3.1 section 8.2.9.6.3 Table 8-101
 */
typedef struct CXLSetFeatureInHeader {
        QemuUUID uuid;
        uint32_t flags;
        uint16_t offset;
        uint8_t version;
        uint8_t rsvd[9];
} QEMU_PACKED QEMU_ALIGNED(16) CXLSetFeatureInHeader;

/* Set Feature : Payload in flags */
#define CXL_SET_FEATURE_FLAG_DATA_TRANSFER_MASK   0x7
enum CXL_SET_FEATURE_FLAG_DATA_TRANSFER {
    CXL_SET_FEATURE_FLAG_FULL_DATA_TRANSFER,
    CXL_SET_FEATURE_FLAG_INITIATE_DATA_TRANSFER,
    CXL_SET_FEATURE_FLAG_CONTINUE_DATA_TRANSFER,
    CXL_SET_FEATURE_FLAG_FINISH_DATA_TRANSFER,
    CXL_SET_FEATURE_FLAG_ABORT_DATA_TRANSFER,
    CXL_SET_FEATURE_FLAG_DATA_TRANSFER_MAX
};
#define CXL_SET_FEAT_DATA_SAVED_ACROSS_RESET BIT(3)

/* CXL r3.1 section 8.2.9.9.11.1: Device Patrol Scrub Control Feature */
static const QemuUUID patrol_scrub_uuid = {
    .data = UUID(0x96dad7d6, 0xfde8, 0x482b, 0xa7, 0x33,
                 0x75, 0x77, 0x4e, 0x06, 0xdb, 0x8a)
};

typedef struct CXLMemPatrolScrubSetFeature {
        CXLSetFeatureInHeader hdr;
        CXLMemPatrolScrubWriteAttrs feat_data;
} QEMU_PACKED QEMU_ALIGNED(16) CXLMemPatrolScrubSetFeature;

/*
 * CXL r3.1 section 8.2.9.9.11.2:
 * DDR5 Error Check Scrub (ECS) Control Feature
 */
static const QemuUUID ecs_uuid = {
    .data = UUID(0xe5b13f22, 0x2328, 0x4a14, 0xb8, 0xba,
                 0xb9, 0x69, 0x1e, 0x89, 0x33, 0x86)
};

typedef struct CXLMemECSSetFeature {
        CXLSetFeatureInHeader hdr;
        CXLMemECSWriteAttrs feat_data[];
} QEMU_PACKED QEMU_ALIGNED(16) CXLMemECSSetFeature;

/* CXL r3.1 section 8.2.9.6.1: Get Supported Features (Opcode 0500h) */
static CXLRetCode cmd_features_get_supported(const struct cxl_cmd *cmd,
                                             uint8_t *payload_in,
                                             size_t len_in,
                                             uint8_t *payload_out,
                                             size_t *len_out,
                                             CXLCCI *cci)
{
    struct {
        uint32_t count;
        uint16_t start_index;
        uint16_t reserved;
    } QEMU_PACKED QEMU_ALIGNED(16) * get_feats_in = (void *)payload_in;

    struct {
        CXLSupportedFeatureHeader hdr;
        CXLSupportedFeatureEntry feat_entries[];
    } QEMU_PACKED QEMU_ALIGNED(16) * get_feats_out = (void *)payload_out;
    uint16_t index, req_entries;
    uint16_t entry;

    if (!object_dynamic_cast(OBJECT(cci->d), TYPE_CXL_TYPE3)) {
        return CXL_MBOX_UNSUPPORTED;
    }
    if (get_feats_in->count < sizeof(CXLSupportedFeatureHeader) ||
        get_feats_in->start_index >= CXL_FEATURE_MAX) {
        return CXL_MBOX_INVALID_INPUT;
    }

    req_entries = (get_feats_in->count -
                   sizeof(CXLSupportedFeatureHeader)) /
                   sizeof(CXLSupportedFeatureEntry);
    req_entries = MIN(req_entries,
                      (CXL_FEATURE_MAX - get_feats_in->start_index));

    for (entry = 0, index = get_feats_in->start_index;
         entry < req_entries; index++) {
        switch (index) {
        case  CXL_FEATURE_PATROL_SCRUB:
            /* Fill supported feature entry for device patrol scrub control */
            get_feats_out->feat_entries[entry++] =
                           (struct CXLSupportedFeatureEntry) {
                .uuid = patrol_scrub_uuid,
                .feat_index = index,
                .get_feat_size = sizeof(CXLMemPatrolScrubReadAttrs),
                .set_feat_size = sizeof(CXLMemPatrolScrubWriteAttrs),
                .attr_flags = CXL_FEAT_ENTRY_ATTR_FLAG_CHANGABLE,
                .get_feat_version = CXL_MEMDEV_PS_GET_FEATURE_VERSION,
                .set_feat_version = CXL_MEMDEV_PS_SET_FEATURE_VERSION,
                .set_feat_effects = CXL_FEAT_ENTRY_SFE_IMMEDIATE_CONFIG_CHANGE |
                                    CXL_FEAT_ENTRY_SFE_CEL_VALID,
            };
            break;
        case  CXL_FEATURE_ECS:
            /* Fill supported feature entry for device DDR5 ECS control */
            get_feats_out->feat_entries[entry++] =
                         (struct CXLSupportedFeatureEntry) {
                .uuid = ecs_uuid,
                .feat_index = index,
                .get_feat_size = sizeof(CXLMemECSReadAttrs),
                .set_feat_size = sizeof(CXLMemECSWriteAttrs),
                .attr_flags = CXL_FEAT_ENTRY_ATTR_FLAG_CHANGABLE,
                .get_feat_version = CXL_ECS_GET_FEATURE_VERSION,
                .set_feat_version = CXL_ECS_SET_FEATURE_VERSION,
                .set_feat_effects = CXL_FEAT_ENTRY_SFE_IMMEDIATE_CONFIG_CHANGE |
                                    CXL_FEAT_ENTRY_SFE_CEL_VALID,
            };
            break;
        default:
            __builtin_unreachable();
        }
    }
    get_feats_out->hdr.nsuppfeats_dev = CXL_FEATURE_MAX;
    get_feats_out->hdr.entries = req_entries;
    *len_out = sizeof(CXLSupportedFeatureHeader) +
                      req_entries * sizeof(CXLSupportedFeatureEntry);

    return CXL_MBOX_SUCCESS;
}

/* CXL r3.1 section 8.2.9.6.2: Get Feature (Opcode 0501h) */
static CXLRetCode cmd_features_get_feature(const struct cxl_cmd *cmd,
                                           uint8_t *payload_in,
                                           size_t len_in,
                                           uint8_t *payload_out,
                                           size_t *len_out,
                                           CXLCCI *cci)
{
    struct {
        QemuUUID uuid;
        uint16_t offset;
        uint16_t count;
        uint8_t selection;
    } QEMU_PACKED QEMU_ALIGNED(16) * get_feature;
    uint16_t bytes_to_copy = 0;
    CXLType3Dev *ct3d;
    CXLSetFeatureInfo *set_feat_info;

    if (!object_dynamic_cast(OBJECT(cci->d), TYPE_CXL_TYPE3)) {
        return CXL_MBOX_UNSUPPORTED;
    }

    ct3d = CXL_TYPE3(cci->d);
    get_feature = (void *)payload_in;

    set_feat_info = &ct3d->set_feat_info;
    if (qemu_uuid_is_equal(&get_feature->uuid, &set_feat_info->uuid)) {
        return CXL_MBOX_FEATURE_TRANSFER_IN_PROGRESS;
    }

    if (get_feature->selection != CXL_GET_FEATURE_SEL_CURRENT_VALUE) {
        return CXL_MBOX_UNSUPPORTED;
    }
    if (get_feature->offset + get_feature->count > cci->payload_max) {
        return CXL_MBOX_INVALID_INPUT;
    }

    if (qemu_uuid_is_equal(&get_feature->uuid, &patrol_scrub_uuid)) {
        if (get_feature->offset >= sizeof(CXLMemPatrolScrubReadAttrs)) {
            return CXL_MBOX_INVALID_INPUT;
        }
        bytes_to_copy = sizeof(CXLMemPatrolScrubReadAttrs) -
                                             get_feature->offset;
        bytes_to_copy = MIN(bytes_to_copy, get_feature->count);
        memcpy(payload_out,
               (uint8_t *)&ct3d->patrol_scrub_attrs + get_feature->offset,
               bytes_to_copy);
    } else if (qemu_uuid_is_equal(&get_feature->uuid, &ecs_uuid)) {
        if (get_feature->offset >= sizeof(CXLMemECSReadAttrs)) {
            return CXL_MBOX_INVALID_INPUT;
        }
        bytes_to_copy = sizeof(CXLMemECSReadAttrs) - get_feature->offset;
        bytes_to_copy = MIN(bytes_to_copy, get_feature->count);
        memcpy(payload_out,
               (uint8_t *)&ct3d->ecs_attrs + get_feature->offset,
               bytes_to_copy);
    } else {
        return CXL_MBOX_UNSUPPORTED;
    }

    *len_out = bytes_to_copy;

    return CXL_MBOX_SUCCESS;
}

/* CXL r3.1 section 8.2.9.6.3: Set Feature (Opcode 0502h) */
static CXLRetCode cmd_features_set_feature(const struct cxl_cmd *cmd,
                                           uint8_t *payload_in,
                                           size_t len_in,
                                           uint8_t *payload_out,
                                           size_t *len_out,
                                           CXLCCI *cci)
{
    CXLSetFeatureInHeader *hdr = (void *)payload_in;
    CXLMemPatrolScrubWriteAttrs *ps_write_attrs;
    CXLMemPatrolScrubSetFeature *ps_set_feature;
    CXLMemECSWriteAttrs *ecs_write_attrs;
    CXLMemECSSetFeature *ecs_set_feature;
    CXLSetFeatureInfo *set_feat_info;
    uint16_t bytes_to_copy = 0;
    uint8_t data_transfer_flag;
    CXLType3Dev *ct3d;
    uint16_t count;

    if (len_in < sizeof(*hdr)) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }

    if (!object_dynamic_cast(OBJECT(cci->d), TYPE_CXL_TYPE3)) {
        return CXL_MBOX_UNSUPPORTED;
    }
    ct3d = CXL_TYPE3(cci->d);
    set_feat_info = &ct3d->set_feat_info;

    if (!qemu_uuid_is_null(&set_feat_info->uuid) &&
        !qemu_uuid_is_equal(&hdr->uuid, &set_feat_info->uuid)) {
        return CXL_MBOX_FEATURE_TRANSFER_IN_PROGRESS;
    }
    if (hdr->flags & CXL_SET_FEAT_DATA_SAVED_ACROSS_RESET) {
        set_feat_info->data_saved_across_reset = true;
    } else {
        set_feat_info->data_saved_across_reset = false;
    }

    data_transfer_flag =
              hdr->flags & CXL_SET_FEATURE_FLAG_DATA_TRANSFER_MASK;
    if (data_transfer_flag == CXL_SET_FEATURE_FLAG_INITIATE_DATA_TRANSFER) {
        set_feat_info->uuid = hdr->uuid;
        set_feat_info->data_size = 0;
    }
    set_feat_info->data_transfer_flag = data_transfer_flag;
    set_feat_info->data_offset = hdr->offset;
    bytes_to_copy = len_in - sizeof(CXLSetFeatureInHeader);

    if (bytes_to_copy == 0) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }

    if (qemu_uuid_is_equal(&hdr->uuid, &patrol_scrub_uuid)) {
        if (hdr->version != CXL_MEMDEV_PS_SET_FEATURE_VERSION) {
            return CXL_MBOX_UNSUPPORTED;
        }

        ps_set_feature = (void *)payload_in;
        ps_write_attrs = &ps_set_feature->feat_data;

        if ((uint32_t)hdr->offset + bytes_to_copy >
            sizeof(ct3d->patrol_scrub_wr_attrs)) {
            return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
        }
        memcpy((uint8_t *)&ct3d->patrol_scrub_wr_attrs + hdr->offset,
               ps_write_attrs,
               bytes_to_copy);
        set_feat_info->data_size += bytes_to_copy;

        if (data_transfer_flag == CXL_SET_FEATURE_FLAG_FULL_DATA_TRANSFER ||
            data_transfer_flag ==  CXL_SET_FEATURE_FLAG_FINISH_DATA_TRANSFER) {
            ct3d->patrol_scrub_attrs.scrub_cycle &= ~0xFF;
            ct3d->patrol_scrub_attrs.scrub_cycle |=
                          ct3d->patrol_scrub_wr_attrs.scrub_cycle_hr & 0xFF;
            ct3d->patrol_scrub_attrs.scrub_flags &= ~0x1;
            ct3d->patrol_scrub_attrs.scrub_flags |=
                          ct3d->patrol_scrub_wr_attrs.scrub_flags & 0x1;
        }
    } else if (qemu_uuid_is_equal(&hdr->uuid,
                                  &ecs_uuid)) {
        if (hdr->version != CXL_ECS_SET_FEATURE_VERSION) {
            return CXL_MBOX_UNSUPPORTED;
        }

        ecs_set_feature = (void *)payload_in;
        ecs_write_attrs = ecs_set_feature->feat_data;

        if ((uint32_t)hdr->offset + bytes_to_copy >
            sizeof(ct3d->ecs_wr_attrs)) {
            return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
        }
        memcpy((uint8_t *)&ct3d->ecs_wr_attrs + hdr->offset,
               ecs_write_attrs,
               bytes_to_copy);
        set_feat_info->data_size += bytes_to_copy;

        if (data_transfer_flag == CXL_SET_FEATURE_FLAG_FULL_DATA_TRANSFER ||
            data_transfer_flag ==  CXL_SET_FEATURE_FLAG_FINISH_DATA_TRANSFER) {
            ct3d->ecs_attrs.ecs_log_cap = ct3d->ecs_wr_attrs.ecs_log_cap;
            for (count = 0; count < CXL_ECS_NUM_MEDIA_FRUS; count++) {
                ct3d->ecs_attrs.fru_attrs[count].ecs_config =
                        ct3d->ecs_wr_attrs.fru_attrs[count].ecs_config & 0x1F;
            }
        }
    } else {
        return CXL_MBOX_UNSUPPORTED;
    }

    if (data_transfer_flag == CXL_SET_FEATURE_FLAG_FULL_DATA_TRANSFER ||
        data_transfer_flag ==  CXL_SET_FEATURE_FLAG_FINISH_DATA_TRANSFER ||
        data_transfer_flag ==  CXL_SET_FEATURE_FLAG_ABORT_DATA_TRANSFER) {
        memset(&set_feat_info->uuid, 0, sizeof(QemuUUID));
        if (qemu_uuid_is_equal(&hdr->uuid, &patrol_scrub_uuid)) {
            memset(&ct3d->patrol_scrub_wr_attrs, 0, set_feat_info->data_size);
        } else if (qemu_uuid_is_equal(&hdr->uuid, &ecs_uuid)) {
            memset(&ct3d->ecs_wr_attrs, 0, set_feat_info->data_size);
        }
        set_feat_info->data_transfer_flag = 0;
        set_feat_info->data_saved_across_reset = false;
        set_feat_info->data_offset = 0;
        set_feat_info->data_size = 0;
    }

    return CXL_MBOX_SUCCESS;
}

/* CXL r3.1 Section 8.2.9.9.1.1: Identify Memory Device (Opcode 4000h) */
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
        uint16_t dc_event_log_size;
    } QEMU_PACKED *id;
    QEMU_BUILD_BUG_ON(sizeof(*id) != 0x45);
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLType3Class *cvc = CXL_TYPE3_GET_CLASS(ct3d);
    CXLDeviceState *cxl_dstate = &ct3d->cxl_dstate;

    if ((!QEMU_IS_ALIGNED(cxl_dstate->vmem_size, CXL_CAPACITY_MULTIPLIER)) ||
        (!QEMU_IS_ALIGNED(cxl_dstate->pmem_size, CXL_CAPACITY_MULTIPLIER)) ||
        (!QEMU_IS_ALIGNED(ct3d->dc.total_capacity, CXL_CAPACITY_MULTIPLIER))) {
        return CXL_MBOX_INTERNAL_ERROR;
    }

    id = (void *)payload_out;

    snprintf(id->fw_revision, 0x10, "BWFW VERSION %02d", 0);

    stq_le_p(&id->total_capacity,
             cxl_dstate->static_mem_size / CXL_CAPACITY_MULTIPLIER);
    stq_le_p(&id->persistent_capacity,
             cxl_dstate->pmem_size / CXL_CAPACITY_MULTIPLIER);
    stq_le_p(&id->volatile_capacity,
             cxl_dstate->vmem_size / CXL_CAPACITY_MULTIPLIER);
    stl_le_p(&id->lsa_size, cvc->get_lsa_size(ct3d));
    /* 256 poison records */
    st24_le_p(id->poison_list_max_mer, 256);
    /* No limit - so limited by main poison record limit */
    stw_le_p(&id->inject_poison_limit, 0);
    stw_le_p(&id->dc_event_log_size, CXL_DC_EVENT_LOG_SIZE);

    *len_out = sizeof(*id);
    return CXL_MBOX_SUCCESS;
}

/* CXL r3.1 Section 8.2.9.9.2.1: Get Partition Info (Opcode 4100h) */
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
    CXLType3Dev *ct3d = container_of(cxl_dstate, CXLType3Dev, cxl_dstate);

    if ((!QEMU_IS_ALIGNED(cxl_dstate->vmem_size, CXL_CAPACITY_MULTIPLIER)) ||
        (!QEMU_IS_ALIGNED(cxl_dstate->pmem_size, CXL_CAPACITY_MULTIPLIER)) ||
        (!QEMU_IS_ALIGNED(ct3d->dc.total_capacity, CXL_CAPACITY_MULTIPLIER))) {
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

/* CXL r3.1 Section 8.2.9.9.2.3: Get LSA (Opcode 4102h) */
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
    uint64_t offset, length;

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

/* CXL r3.1 Section 8.2.9.9.2.4: Set LSA (Opcode 4103h) */
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
    if (len_in < hdr_len) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }

    if (set_lsa_payload->offset + len_in > cvc->get_lsa_size(ct3d) + hdr_len) {
        return CXL_MBOX_INVALID_INPUT;
    }
    len_in -= hdr_len;

    cvc->set_lsa(ct3d, set_lsa_payload->data, len_in, set_lsa_payload->offset);
    return CXL_MBOX_SUCCESS;
}

/* CXL r3.2 Section 8.2.10.9.3.2 Get Alert Configuration (Opcode 4201h) */
static CXLRetCode cmd_get_alert_config(const struct cxl_cmd *cmd,
                                       uint8_t *payload_in,
                                       size_t len_in,
                                       uint8_t *payload_out,
                                       size_t *len_out,
                                       CXLCCI *cci)
{
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLAlertConfig *out = (CXLAlertConfig *)payload_out;

    memcpy(out, &ct3d->alert_config, sizeof(ct3d->alert_config));
    *len_out = sizeof(ct3d->alert_config);

    return CXL_MBOX_SUCCESS;
}

/* CXL r3.2 Section 8.2.10.9.3.3 Set Alert Configuration (Opcode 4202h) */
static CXLRetCode cmd_set_alert_config(const struct cxl_cmd *cmd,
                                       uint8_t *payload_in,
                                       size_t len_in,
                                       uint8_t *payload_out,
                                       size_t *len_out,
                                       CXLCCI *cci)
{
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLAlertConfig *alert_config = &ct3d->alert_config;
    struct {
        uint8_t valid_alert_actions;
        uint8_t enable_alert_actions;
        uint8_t life_used_warn_thresh;
        uint8_t rsvd;
        uint16_t over_temp_warn_thresh;
        uint16_t under_temp_warn_thresh;
        uint16_t cor_vmem_err_warn_thresh;
        uint16_t cor_pmem_err_warn_thresh;
    } QEMU_PACKED *in = (void *)payload_in;

    if (in->valid_alert_actions & CXL_ALERTS_LIFE_USED_WARN_THRESH) {
        /*
         * CXL r3.2 Table 8-149 The life used warning threshold shall be
         * less than the life used critical alert value.
         */
        if (in->life_used_warn_thresh >=
            alert_config->life_used_crit_alert_thresh) {
            return CXL_MBOX_INVALID_INPUT;
        }
        alert_config->life_used_warn_thresh = in->life_used_warn_thresh;
        alert_config->enable_alerts |= CXL_ALERTS_LIFE_USED_WARN_THRESH;
    }

    if (in->valid_alert_actions & CXL_ALERTS_OVER_TEMP_WARN_THRESH) {
        /*
         * CXL r3.2 Table 8-149 The Device Over-Temperature Warning Threshold
         * shall be less than the the Device Over-Temperature Critical
         * Alert Threshold.
         */
        if (in->over_temp_warn_thresh >=
            alert_config->over_temp_crit_alert_thresh) {
            return CXL_MBOX_INVALID_INPUT;
        }
        alert_config->over_temp_warn_thresh = in->over_temp_warn_thresh;
        alert_config->enable_alerts |= CXL_ALERTS_OVER_TEMP_WARN_THRESH;
    }

    if (in->valid_alert_actions & CXL_ALERTS_UNDER_TEMP_WARN_THRESH) {
        /*
         * CXL r3.2 Table 8-149 The Device Under-Temperature Warning Threshold
         * shall be higher than the the Device Under-Temperature Critical
         * Alert Threshold.
         */
        if (in->under_temp_warn_thresh <=
            alert_config->under_temp_crit_alert_thresh) {
            return CXL_MBOX_INVALID_INPUT;
        }
        alert_config->under_temp_warn_thresh = in->under_temp_warn_thresh;
        alert_config->enable_alerts |= CXL_ALERTS_UNDER_TEMP_WARN_THRESH;
    }

    if (in->valid_alert_actions & CXL_ALERTS_COR_VMEM_ERR_WARN_THRESH) {
        alert_config->cor_vmem_err_warn_thresh = in->cor_vmem_err_warn_thresh;
        alert_config->enable_alerts |= CXL_ALERTS_COR_VMEM_ERR_WARN_THRESH;
    }

    if (in->valid_alert_actions & CXL_ALERTS_COR_PMEM_ERR_WARN_THRESH) {
        alert_config->cor_pmem_err_warn_thresh = in->cor_pmem_err_warn_thresh;
        alert_config->enable_alerts |= CXL_ALERTS_COR_PMEM_ERR_WARN_THRESH;
    }
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
    cxl_discard_all_event_records(&ct3d->cxl_dstate);
}

static int get_sanitize_duration(uint64_t total_mem)
{
    int secs = 0;

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

    return secs;
}

/*
 * CXL r3.1 Section 8.2.9.9.5.1: Sanitize (Opcode 4400h)
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
    secs = get_sanitize_duration(total_mem);

    /* EBUSY other bg cmds as of now */
    cci->bg.runtime = secs * 1000UL;
    *len_out = 0;

    cxl_dev_disable_media(&ct3d->cxl_dstate);

    /* sanitize when done */
    return CXL_MBOX_BG_STARTED;
}

struct dpa_range_list_entry {
    uint64_t starting_dpa;
    uint64_t length;
} QEMU_PACKED;

struct CXLSanitizeInfo {
    uint32_t dpa_range_count;
    uint8_t fill_value;
    struct dpa_range_list_entry dpa_range_list[];
} QEMU_PACKED;

static uint64_t get_vmr_size(CXLType3Dev *ct3d, MemoryRegion **vmr)
{
    MemoryRegion *mr;
    if (ct3d->hostvmem) {
        mr = host_memory_backend_get_memory(ct3d->hostvmem);
        if (vmr) {
            *vmr = mr;
        }
        return memory_region_size(mr);
    }
    return 0;
}

static uint64_t get_pmr_size(CXLType3Dev *ct3d, MemoryRegion **pmr)
{
    MemoryRegion *mr;
    if (ct3d->hostpmem) {
        mr = host_memory_backend_get_memory(ct3d->hostpmem);
        if (pmr) {
            *pmr = mr;
        }
        return memory_region_size(mr);
    }
    return 0;
}

static uint64_t get_dc_size(CXLType3Dev *ct3d, MemoryRegion **dc_mr)
{
    MemoryRegion *mr;
    if (ct3d->dc.host_dc) {
        mr = host_memory_backend_get_memory(ct3d->dc.host_dc);
        if (dc_mr) {
            *dc_mr = mr;
        }
        return memory_region_size(mr);
    }
    return 0;
}

static int validate_dpa_addr(CXLType3Dev *ct3d, uint64_t dpa_addr,
                             size_t length)
{
    uint64_t vmr_size, pmr_size, dc_size;

    if ((dpa_addr % CXL_CACHE_LINE_SIZE) ||
        (length % CXL_CACHE_LINE_SIZE)  ||
        (length <= 0)) {
        return -EINVAL;
    }

    vmr_size = get_vmr_size(ct3d, NULL);
    pmr_size = get_pmr_size(ct3d, NULL);
    dc_size = get_dc_size(ct3d, NULL);

    if (dpa_addr + length > vmr_size + pmr_size + dc_size) {
        return -EINVAL;
    }

    if (dpa_addr > vmr_size + pmr_size) {
        if (!ct3_test_region_block_backed(ct3d, dpa_addr, length)) {
            return -ENODEV;
        }
    }

    return 0;
}

static int sanitize_range(CXLType3Dev *ct3d, uint64_t dpa_addr, size_t length,
                          uint8_t fill_value)
{

    uint64_t vmr_size, pmr_size;
    AddressSpace *as = NULL;
    MemTxAttrs mem_attrs = {};

    vmr_size = get_vmr_size(ct3d, NULL);
    pmr_size = get_pmr_size(ct3d, NULL);

    if (dpa_addr < vmr_size) {
        as = &ct3d->hostvmem_as;
    } else if (dpa_addr < vmr_size + pmr_size) {
        as = &ct3d->hostpmem_as;
    } else {
        if (!ct3_test_region_block_backed(ct3d, dpa_addr, length)) {
            return -ENODEV;
        }
        as = &ct3d->dc.host_dc_as;
    }

    return address_space_set(as, dpa_addr, fill_value, length, mem_attrs);
}

/* Perform the actual device zeroing */
static void __do_sanitize(CXLType3Dev *ct3d)
{
    struct CXLSanitizeInfo  *san_info = ct3d->media_op_sanitize;
    int dpa_range_count = san_info->dpa_range_count;
    int rc = 0;
    int i;

    for (i = 0; i < dpa_range_count; i++) {
        rc = sanitize_range(ct3d, san_info->dpa_range_list[i].starting_dpa,
                            san_info->dpa_range_list[i].length,
                            san_info->fill_value);
        if (rc) {
            goto exit;
        }
    }
exit:
    g_free(ct3d->media_op_sanitize);
    ct3d->media_op_sanitize = NULL;
    return;
}

enum {
    MEDIA_OP_CLASS_GENERAL  = 0x0,
        #define MEDIA_OP_GEN_SUBC_DISCOVERY 0x0
    MEDIA_OP_CLASS_SANITIZE = 0x1,
        #define MEDIA_OP_SAN_SUBC_SANITIZE 0x0
        #define MEDIA_OP_SAN_SUBC_ZERO 0x1
};

struct media_op_supported_list_entry {
    uint8_t media_op_class;
    uint8_t media_op_subclass;
};

struct media_op_discovery_out_pl {
    uint64_t dpa_range_granularity;
    uint16_t total_supported_operations;
    uint16_t num_of_supported_operations;
    struct media_op_supported_list_entry entry[];
} QEMU_PACKED;

static const struct media_op_supported_list_entry media_op_matrix[] = {
    { MEDIA_OP_CLASS_GENERAL, MEDIA_OP_GEN_SUBC_DISCOVERY },
    { MEDIA_OP_CLASS_SANITIZE, MEDIA_OP_SAN_SUBC_SANITIZE },
    { MEDIA_OP_CLASS_SANITIZE, MEDIA_OP_SAN_SUBC_ZERO },
};

static CXLRetCode media_operations_discovery(uint8_t *payload_in,
                                             size_t len_in,
                                             uint8_t *payload_out,
                                             size_t *len_out)
{
    struct {
        uint8_t media_operation_class;
        uint8_t media_operation_subclass;
        uint8_t rsvd[2];
        uint32_t dpa_range_count;
        struct {
            uint16_t start_index;
            uint16_t num_ops;
        } discovery_osa;
    } QEMU_PACKED *media_op_in_disc_pl = (void *)payload_in;
    struct media_op_discovery_out_pl *media_out_pl =
        (struct media_op_discovery_out_pl *)payload_out;
    int num_ops, start_index, i;
    int count = 0;

    if (len_in < sizeof(*media_op_in_disc_pl)) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }

    num_ops = media_op_in_disc_pl->discovery_osa.num_ops;
    start_index = media_op_in_disc_pl->discovery_osa.start_index;

    /*
     * As per spec CXL r3.2 8.2.10.9.5.3 dpa_range_count should be zero and
     * start index should not exceed the total number of entries for discovery
     * sub class command.
     */
    if (media_op_in_disc_pl->dpa_range_count ||
        start_index > ARRAY_SIZE(media_op_matrix)) {
        return CXL_MBOX_INVALID_INPUT;
    }

    media_out_pl->dpa_range_granularity = CXL_CACHE_LINE_SIZE;
    media_out_pl->total_supported_operations =
                                     ARRAY_SIZE(media_op_matrix);
    if (num_ops > 0) {
        for (i = start_index; i < start_index + num_ops; i++) {
            media_out_pl->entry[count].media_op_class =
                    media_op_matrix[i].media_op_class;
            media_out_pl->entry[count].media_op_subclass =
                        media_op_matrix[i].media_op_subclass;
            count++;
            if (count == num_ops) {
                break;
            }
        }
    }

    media_out_pl->num_of_supported_operations = count;
    *len_out = sizeof(*media_out_pl) + count * sizeof(*media_out_pl->entry);
    return CXL_MBOX_SUCCESS;
}

static CXLRetCode media_operations_sanitize(CXLType3Dev *ct3d,
                                            uint8_t *payload_in,
                                            size_t len_in,
                                            uint8_t *payload_out,
                                            size_t *len_out,
                                            uint8_t fill_value,
                                            CXLCCI *cci)
{
    struct media_operations_sanitize {
        uint8_t media_operation_class;
        uint8_t media_operation_subclass;
        uint8_t rsvd[2];
        uint32_t dpa_range_count;
        struct dpa_range_list_entry dpa_range_list[];
    } QEMU_PACKED *media_op_in_sanitize_pl = (void *)payload_in;
    uint32_t dpa_range_count = media_op_in_sanitize_pl->dpa_range_count;
    uint64_t total_mem = 0;
    size_t dpa_range_list_size;
    int secs = 0, i;

    if (dpa_range_count == 0) {
        return CXL_MBOX_SUCCESS;
    }

    dpa_range_list_size = dpa_range_count * sizeof(struct dpa_range_list_entry);
    if (len_in < (sizeof(*media_op_in_sanitize_pl) + dpa_range_list_size)) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }

    for (i = 0; i < dpa_range_count; i++) {
        uint64_t start_dpa =
            media_op_in_sanitize_pl->dpa_range_list[i].starting_dpa;
        uint64_t length = media_op_in_sanitize_pl->dpa_range_list[i].length;

        if (validate_dpa_addr(ct3d, start_dpa, length)) {
            return CXL_MBOX_INVALID_INPUT;
        }
        total_mem += length;
    }
    ct3d->media_op_sanitize = g_malloc0(sizeof(struct CXLSanitizeInfo) +
                                        dpa_range_list_size);

    ct3d->media_op_sanitize->dpa_range_count = dpa_range_count;
    ct3d->media_op_sanitize->fill_value = fill_value;
    memcpy(ct3d->media_op_sanitize->dpa_range_list,
           media_op_in_sanitize_pl->dpa_range_list,
           dpa_range_list_size);
    secs = get_sanitize_duration(total_mem >> 20);

    /* EBUSY other bg cmds as of now */
    cci->bg.runtime = secs * 1000UL;
    *len_out = 0;
    /*
     * media op sanitize is targeted so no need to disable media or
     * clear event logs
     */
    return CXL_MBOX_BG_STARTED;
}

static CXLRetCode cmd_media_operations(const struct cxl_cmd *cmd,
                                       uint8_t *payload_in,
                                       size_t len_in,
                                       uint8_t *payload_out,
                                       size_t *len_out,
                                       CXLCCI *cci)
{
    struct {
        uint8_t media_operation_class;
        uint8_t media_operation_subclass;
        uint8_t rsvd[2];
        uint32_t dpa_range_count;
    } QEMU_PACKED *media_op_in_common_pl = (void *)payload_in;
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    uint8_t media_op_cl = 0;
    uint8_t media_op_subclass = 0;

    if (len_in < sizeof(*media_op_in_common_pl)) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }

    media_op_cl = media_op_in_common_pl->media_operation_class;
    media_op_subclass = media_op_in_common_pl->media_operation_subclass;

    switch (media_op_cl) {
    case MEDIA_OP_CLASS_GENERAL:
        if (media_op_subclass != MEDIA_OP_GEN_SUBC_DISCOVERY) {
            return CXL_MBOX_UNSUPPORTED;
        }

        return media_operations_discovery(payload_in, len_in, payload_out,
                                             len_out);
    case MEDIA_OP_CLASS_SANITIZE:
        switch (media_op_subclass) {
        case MEDIA_OP_SAN_SUBC_SANITIZE:
            return media_operations_sanitize(ct3d, payload_in, len_in,
                                             payload_out, len_out, 0xF,
                                             cci);
        case MEDIA_OP_SAN_SUBC_ZERO:
            return media_operations_sanitize(ct3d, payload_in, len_in,
                                             payload_out, len_out, 0,
                                             cci);
        default:
            return CXL_MBOX_UNSUPPORTED;
        }
    default:
        return CXL_MBOX_UNSUPPORTED;
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
 * CXL r3.1 Section 8.2.9.9.4.1: Get Poison List (Opcode 4300h)
 *
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
        if (!ranges_overlap(ent->start, ent->length,
                            query_start, query_length)) {
            continue;
        }
        record_count++;
    }
    out_pl_len = sizeof(*out) + record_count * sizeof(out->records[0]);
    assert(out_pl_len <= CXL_MAILBOX_MAX_PAYLOAD_SIZE);

    QLIST_FOREACH(ent, poison_list, node) {
        uint64_t start, stop;

        /* Check for no overlap */
        if (!ranges_overlap(ent->start, ent->length,
                            query_start, query_length)) {
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
    if (scan_media_running(cci)) {
        out->flags |= (1 << 2);
    }

    stw_le_p(&out->count, record_count);
    *len_out = out_pl_len;
    return CXL_MBOX_SUCCESS;
}

/* CXL r3.1 Section 8.2.9.9.4.2: Inject Poison (Opcode 4301h) */
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
    /*
     * Freeze the list if there is an on-going scan media operation.
     */
    if (scan_media_running(cci)) {
        /*
         * XXX: Spec is ambiguous - is this case considered
         * a successful return despite not adding to the list?
         */
        goto success;
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
success:
    *len_out = 0;

    return CXL_MBOX_SUCCESS;
}

/* CXL r3.1 Section 8.2.9.9.4.3: Clear Poison (Opcode 4302h */
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
    if (dpa + CXL_CACHE_LINE_SIZE > cxl_dstate->static_mem_size +
        ct3d->dc.total_capacity) {
        return CXL_MBOX_INVALID_PA;
    }

    /* Clearing a region with no poison is not an error so always do so */
    if (cvc->set_cacheline) {
        if (!cvc->set_cacheline(ct3d, dpa, in->data)) {
            return CXL_MBOX_INTERNAL_ERROR;
        }
    }

    /*
     * Freeze the list if there is an on-going scan media operation.
     */
    if (scan_media_running(cci)) {
        /*
         * XXX: Spec is ambiguous - is this case considered
         * a successful return despite not removing from the list?
         */
        goto success;
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
        goto success;
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
success:
    *len_out = 0;

    return CXL_MBOX_SUCCESS;
}

/*
 * CXL r3.1 section 8.2.9.9.4.4: Get Scan Media Capabilities
 */
static CXLRetCode
cmd_media_get_scan_media_capabilities(const struct cxl_cmd *cmd,
                                      uint8_t *payload_in,
                                      size_t len_in,
                                      uint8_t *payload_out,
                                      size_t *len_out,
                                      CXLCCI *cci)
{
    struct get_scan_media_capabilities_pl {
        uint64_t pa;
        uint64_t length;
    } QEMU_PACKED;

    struct get_scan_media_capabilities_out_pl {
        uint32_t estimated_runtime_ms;
    };

    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLDeviceState *cxl_dstate = &ct3d->cxl_dstate;
    struct get_scan_media_capabilities_pl *in = (void *)payload_in;
    struct get_scan_media_capabilities_out_pl *out = (void *)payload_out;
    uint64_t query_start;
    uint64_t query_length;

    query_start = ldq_le_p(&in->pa);
    /* 64 byte alignment required */
    if (query_start & 0x3f) {
        return CXL_MBOX_INVALID_INPUT;
    }
    query_length = ldq_le_p(&in->length) * CXL_CACHE_LINE_SIZE;

    if (query_start + query_length > cxl_dstate->static_mem_size) {
        return CXL_MBOX_INVALID_PA;
    }

    /*
     * Just use 400 nanosecond access/read latency + 100 ns for
     * the cost of updating the poison list. For small enough
     * chunks return at least 1 ms.
     */
    stl_le_p(&out->estimated_runtime_ms,
             MAX(1, query_length * (0.0005L / 64)));

    *len_out = sizeof(*out);
    return CXL_MBOX_SUCCESS;
}

static void __do_scan_media(CXLType3Dev *ct3d)
{
    CXLPoison *ent;
    unsigned int results_cnt = 0;

    QLIST_FOREACH(ent, &ct3d->scan_media_results, node) {
        results_cnt++;
    }

    /* only scan media may clear the overflow */
    if (ct3d->poison_list_overflowed &&
        ct3d->poison_list_cnt == results_cnt) {
        cxl_clear_poison_list_overflowed(ct3d);
    }
    /* scan media has run since last conventional reset */
    ct3d->scan_media_hasrun = true;
}

/*
 * CXL r3.1 section 8.2.9.9.4.5: Scan Media
 */
static CXLRetCode cmd_media_scan_media(const struct cxl_cmd *cmd,
                                       uint8_t *payload_in,
                                       size_t len_in,
                                       uint8_t *payload_out,
                                       size_t *len_out,
                                       CXLCCI *cci)
{
    struct scan_media_pl {
        uint64_t pa;
        uint64_t length;
        uint8_t flags;
    } QEMU_PACKED;

    struct scan_media_pl *in = (void *)payload_in;
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLDeviceState *cxl_dstate = &ct3d->cxl_dstate;
    uint64_t query_start;
    uint64_t query_length;
    CXLPoison *ent, *next;

    query_start = ldq_le_p(&in->pa);
    /* 64 byte alignment required */
    if (query_start & 0x3f) {
        return CXL_MBOX_INVALID_INPUT;
    }
    query_length = ldq_le_p(&in->length) * CXL_CACHE_LINE_SIZE;

    if (query_start + query_length > cxl_dstate->static_mem_size) {
        return CXL_MBOX_INVALID_PA;
    }
    if (ct3d->dc.num_regions && query_start + query_length >=
            cxl_dstate->static_mem_size + ct3d->dc.total_capacity) {
        return CXL_MBOX_INVALID_PA;
    }

    if (in->flags == 0) { /* TODO */
        qemu_log_mask(LOG_UNIMP,
                      "Scan Media Event Log is unsupported\n");
    }

    /* any previous results are discarded upon a new Scan Media */
    QLIST_FOREACH_SAFE(ent, &ct3d->scan_media_results, node, next) {
        QLIST_REMOVE(ent, node);
        g_free(ent);
    }

    /* kill the poison list - it will be recreated */
    if (ct3d->poison_list_overflowed) {
        QLIST_FOREACH_SAFE(ent, &ct3d->poison_list, node, next) {
            QLIST_REMOVE(ent, node);
            g_free(ent);
            ct3d->poison_list_cnt--;
        }
    }

    /*
     * Scan the backup list and move corresponding entries
     * into the results list, updating the poison list
     * when possible.
     */
    QLIST_FOREACH_SAFE(ent, &ct3d->poison_list_bkp, node, next) {
        CXLPoison *res;

        if (ent->start >= query_start + query_length ||
            ent->start + ent->length <= query_start) {
            continue;
        }

        /*
         * If a Get Poison List cmd comes in while this
         * scan is being done, it will see the new complete
         * list, while setting the respective flag.
         */
        if (ct3d->poison_list_cnt < CXL_POISON_LIST_LIMIT) {
            CXLPoison *p = g_new0(CXLPoison, 1);

            p->start = ent->start;
            p->length = ent->length;
            p->type = ent->type;
            QLIST_INSERT_HEAD(&ct3d->poison_list, p, node);
            ct3d->poison_list_cnt++;
        }

        res = g_new0(CXLPoison, 1);
        res->start = ent->start;
        res->length = ent->length;
        res->type = ent->type;
        QLIST_INSERT_HEAD(&ct3d->scan_media_results, res, node);

        QLIST_REMOVE(ent, node);
        g_free(ent);
    }

    cci->bg.runtime = MAX(1, query_length * (0.0005L / 64));
    *len_out = 0;

    return CXL_MBOX_BG_STARTED;
}

/*
 * CXL r3.1 section 8.2.9.9.4.6: Get Scan Media Results
 */
static CXLRetCode cmd_media_get_scan_media_results(const struct cxl_cmd *cmd,
                                                   uint8_t *payload_in,
                                                   size_t len_in,
                                                   uint8_t *payload_out,
                                                   size_t *len_out,
                                                   CXLCCI *cci)
{
    struct get_scan_media_results_out_pl {
        uint64_t dpa_restart;
        uint64_t length;
        uint8_t flags;
        uint8_t rsvd1;
        uint16_t count;
        uint8_t rsvd2[0xc];
        struct {
            uint64_t addr;
            uint32_t length;
            uint32_t resv;
        } QEMU_PACKED records[];
    } QEMU_PACKED;

    struct get_scan_media_results_out_pl *out = (void *)payload_out;
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLPoisonList *scan_media_results = &ct3d->scan_media_results;
    CXLPoison *ent, *next;
    uint16_t total_count = 0, record_count = 0, i = 0;
    uint16_t out_pl_len;

    if (!ct3d->scan_media_hasrun) {
        return CXL_MBOX_UNSUPPORTED;
    }

    /*
     * Calculate limits, all entries are within the same address range of the
     * last scan media call.
     */
    QLIST_FOREACH(ent, scan_media_results, node) {
        size_t rec_size = record_count * sizeof(out->records[0]);

        if (sizeof(*out) + rec_size < CXL_MAILBOX_MAX_PAYLOAD_SIZE) {
            record_count++;
        }
        total_count++;
    }

    out_pl_len = sizeof(*out) + record_count * sizeof(out->records[0]);
    assert(out_pl_len <= CXL_MAILBOX_MAX_PAYLOAD_SIZE);

    memset(out, 0, out_pl_len);
    QLIST_FOREACH_SAFE(ent, scan_media_results, node, next) {
        uint64_t start, stop;

        if (i == record_count) {
            break;
        }

        start = ROUND_DOWN(ent->start, 64ull);
        stop = ROUND_DOWN(ent->start, 64ull) + ent->length;
        stq_le_p(&out->records[i].addr, start);
        stl_le_p(&out->records[i].length, (stop - start) / CXL_CACHE_LINE_SIZE);
        i++;

        /* consume the returning entry */
        QLIST_REMOVE(ent, node);
        g_free(ent);
    }

    stw_le_p(&out->count, record_count);
    if (total_count > record_count) {
        out->flags = (1 << 0); /* More Media Error Records */
    }

    *len_out = out_pl_len;
    return CXL_MBOX_SUCCESS;
}

/*
 * CXL r3.1 section 8.2.9.9.9.1: Get Dynamic Capacity Configuration
 * (Opcode: 4800h)
 */
static CXLRetCode cmd_dcd_get_dyn_cap_config(const struct cxl_cmd *cmd,
                                             uint8_t *payload_in,
                                             size_t len_in,
                                             uint8_t *payload_out,
                                             size_t *len_out,
                                             CXLCCI *cci)
{
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    struct {
        uint8_t region_cnt;
        uint8_t start_rid;
    } QEMU_PACKED *in = (void *)payload_in;
    struct {
        uint8_t num_regions;
        uint8_t regions_returned;
        uint8_t rsvd1[6];
        struct {
            uint64_t base;
            uint64_t decode_len;
            uint64_t region_len;
            uint64_t block_size;
            uint32_t dsmadhandle;
            uint8_t flags;
            uint8_t rsvd2[3];
        } QEMU_PACKED records[];
    } QEMU_PACKED *out = (void *)payload_out;
    struct {
        uint32_t num_extents_supported;
        uint32_t num_extents_available;
        uint32_t num_tags_supported;
        uint32_t num_tags_available;
    } QEMU_PACKED *extra_out;
    uint16_t record_count;
    uint16_t i;
    uint16_t out_pl_len;
    uint8_t start_rid;

    start_rid = in->start_rid;
    if (start_rid >= ct3d->dc.num_regions) {
        return CXL_MBOX_INVALID_INPUT;
    }

    record_count = MIN(ct3d->dc.num_regions - in->start_rid, in->region_cnt);

    out_pl_len = sizeof(*out) + record_count * sizeof(out->records[0]);
    extra_out = (void *)(payload_out + out_pl_len);
    out_pl_len += sizeof(*extra_out);
    assert(out_pl_len <= CXL_MAILBOX_MAX_PAYLOAD_SIZE);

    out->num_regions = ct3d->dc.num_regions;
    out->regions_returned = record_count;
    for (i = 0; i < record_count; i++) {
        stq_le_p(&out->records[i].base,
                 ct3d->dc.regions[start_rid + i].base);
        stq_le_p(&out->records[i].decode_len,
                 ct3d->dc.regions[start_rid + i].decode_len /
                 CXL_CAPACITY_MULTIPLIER);
        stq_le_p(&out->records[i].region_len,
                 ct3d->dc.regions[start_rid + i].len);
        stq_le_p(&out->records[i].block_size,
                 ct3d->dc.regions[start_rid + i].block_size);
        stl_le_p(&out->records[i].dsmadhandle,
                 ct3d->dc.regions[start_rid + i].dsmadhandle);
        out->records[i].flags = ct3d->dc.regions[start_rid + i].flags;
    }
    /*
     * TODO: Assign values once extents and tags are introduced
     * to use.
     */
    stl_le_p(&extra_out->num_extents_supported, CXL_NUM_EXTENTS_SUPPORTED);
    stl_le_p(&extra_out->num_extents_available, CXL_NUM_EXTENTS_SUPPORTED -
             ct3d->dc.total_extent_count);
    stl_le_p(&extra_out->num_tags_supported, CXL_NUM_TAGS_SUPPORTED);
    stl_le_p(&extra_out->num_tags_available, CXL_NUM_TAGS_SUPPORTED);

    *len_out = out_pl_len;
    return CXL_MBOX_SUCCESS;
}

/*
 * CXL r3.1 section 8.2.9.9.9.2:
 * Get Dynamic Capacity Extent List (Opcode 4801h)
 */
static CXLRetCode cmd_dcd_get_dyn_cap_ext_list(const struct cxl_cmd *cmd,
                                               uint8_t *payload_in,
                                               size_t len_in,
                                               uint8_t *payload_out,
                                               size_t *len_out,
                                               CXLCCI *cci)
{
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    struct {
        uint32_t extent_cnt;
        uint32_t start_extent_id;
    } QEMU_PACKED *in = (void *)payload_in;
    struct {
        uint32_t count;
        uint32_t total_extents;
        uint32_t generation_num;
        uint8_t rsvd[4];
        CXLDCExtentRaw records[];
    } QEMU_PACKED *out = (void *)payload_out;
    uint32_t start_extent_id = in->start_extent_id;
    CXLDCExtentList *extent_list = &ct3d->dc.extents;
    uint16_t record_count = 0, i = 0, record_done = 0;
    uint16_t out_pl_len, size;
    CXLDCExtent *ent;

    if (start_extent_id > ct3d->dc.nr_extents_accepted) {
        return CXL_MBOX_INVALID_INPUT;
    }

    record_count = MIN(in->extent_cnt,
                       ct3d->dc.total_extent_count - start_extent_id);
    size = CXL_MAILBOX_MAX_PAYLOAD_SIZE - sizeof(*out);
    record_count = MIN(record_count, size / sizeof(out->records[0]));
    out_pl_len = sizeof(*out) + record_count * sizeof(out->records[0]);

    stl_le_p(&out->count, record_count);
    stl_le_p(&out->total_extents, ct3d->dc.nr_extents_accepted);
    stl_le_p(&out->generation_num, ct3d->dc.ext_list_gen_seq);

    if (record_count > 0) {
        CXLDCExtentRaw *out_rec = &out->records[record_done];

        QTAILQ_FOREACH(ent, extent_list, node) {
            if (i++ < start_extent_id) {
                continue;
            }
            stq_le_p(&out_rec->start_dpa, ent->start_dpa);
            stq_le_p(&out_rec->len, ent->len);
            memcpy(&out_rec->tag, ent->tag, 0x10);
            stw_le_p(&out_rec->shared_seq, ent->shared_seq);

            record_done++;
            out_rec++;
            if (record_done == record_count) {
                break;
            }
        }
    }

    *len_out = out_pl_len;
    return CXL_MBOX_SUCCESS;
}

/*
 * Check whether any bit between addr[nr, nr+size) is set,
 * return true if any bit is set, otherwise return false
 */
bool test_any_bits_set(const unsigned long *addr, unsigned long nr,
                              unsigned long size)
{
    unsigned long res = find_next_bit(addr, size + nr, nr);

    return res < nr + size;
}

CXLDCRegion *cxl_find_dc_region(CXLType3Dev *ct3d, uint64_t dpa, uint64_t len)
{
    int i;
    CXLDCRegion *region = &ct3d->dc.regions[0];

    if (dpa < region->base ||
        dpa >= region->base + ct3d->dc.total_capacity) {
        return NULL;
    }

    /*
     * CXL r3.1 section 9.13.3: Dynamic Capacity Device (DCD)
     *
     * Regions are used in increasing-DPA order, with Region 0 being used for
     * the lowest DPA of Dynamic Capacity and Region 7 for the highest DPA.
     * So check from the last region to find where the dpa belongs. Extents that
     * cross multiple regions are not allowed.
     */
    for (i = ct3d->dc.num_regions - 1; i >= 0; i--) {
        region = &ct3d->dc.regions[i];
        if (dpa >= region->base) {
            if (dpa + len > region->base + region->len) {
                return NULL;
            }
            return region;
        }
    }

    return NULL;
}

void cxl_insert_extent_to_extent_list(CXLDCExtentList *list,
                                             uint64_t dpa,
                                             uint64_t len,
                                             uint8_t *tag,
                                             uint16_t shared_seq)
{
    CXLDCExtent *extent;

    extent = g_new0(CXLDCExtent, 1);
    extent->start_dpa = dpa;
    extent->len = len;
    if (tag) {
        memcpy(extent->tag, tag, 0x10);
    }
    extent->shared_seq = shared_seq;

    QTAILQ_INSERT_TAIL(list, extent, node);
}

void cxl_remove_extent_from_extent_list(CXLDCExtentList *list,
                                        CXLDCExtent *extent)
{
    QTAILQ_REMOVE(list, extent, node);
    g_free(extent);
}

/*
 * Add a new extent to the extent "group" if group exists;
 * otherwise, create a new group
 * Return value: the extent group where the extent is inserted.
 */
CXLDCExtentGroup *cxl_insert_extent_to_extent_group(CXLDCExtentGroup *group,
                                                    uint64_t dpa,
                                                    uint64_t len,
                                                    uint8_t *tag,
                                                    uint16_t shared_seq)
{
    if (!group) {
        group = g_new0(CXLDCExtentGroup, 1);
        QTAILQ_INIT(&group->list);
    }
    cxl_insert_extent_to_extent_list(&group->list, dpa, len,
                                     tag, shared_seq);
    return group;
}

void cxl_extent_group_list_insert_tail(CXLDCExtentGroupList *list,
                                       CXLDCExtentGroup *group)
{
    QTAILQ_INSERT_TAIL(list, group, node);
}

uint32_t cxl_extent_group_list_delete_front(CXLDCExtentGroupList *list)
{
    CXLDCExtent *ent, *ent_next;
    CXLDCExtentGroup *group = QTAILQ_FIRST(list);
    uint32_t extents_deleted = 0;

    QTAILQ_REMOVE(list, group, node);
    QTAILQ_FOREACH_SAFE(ent, &group->list, node, ent_next) {
        cxl_remove_extent_from_extent_list(&group->list, ent);
        extents_deleted++;
    }
    g_free(group);

    return extents_deleted;
}

/*
 * CXL r3.1 Table 8-168: Add Dynamic Capacity Response Input Payload
 * CXL r3.1 Table 8-170: Release Dynamic Capacity Input Payload
 */
typedef struct CXLUpdateDCExtentListInPl {
    uint32_t num_entries_updated;
    uint8_t flags;
    uint8_t rsvd[3];
    /* CXL r3.1 Table 8-169: Updated Extent */
    struct {
        uint64_t start_dpa;
        uint64_t len;
        uint8_t rsvd[8];
    } QEMU_PACKED updated_entries[];
} QEMU_PACKED CXLUpdateDCExtentListInPl;

/*
 * For the extents in the extent list to operate, check whether they are valid
 * 1. The extent should be in the range of a valid DC region;
 * 2. The extent should not cross multiple regions;
 * 3. The start DPA and the length of the extent should align with the block
 * size of the region;
 * 4. The address range of multiple extents in the list should not overlap.
 */
static CXLRetCode cxl_detect_malformed_extent_list(CXLType3Dev *ct3d,
        const CXLUpdateDCExtentListInPl *in)
{
    uint64_t min_block_size = UINT64_MAX;
    CXLDCRegion *region;
    CXLDCRegion *lastregion = &ct3d->dc.regions[ct3d->dc.num_regions - 1];
    g_autofree unsigned long *blk_bitmap = NULL;
    uint64_t dpa, len;
    uint32_t i;

    for (i = 0; i < ct3d->dc.num_regions; i++) {
        region = &ct3d->dc.regions[i];
        min_block_size = MIN(min_block_size, region->block_size);
    }

    blk_bitmap = bitmap_new((lastregion->base + lastregion->len -
                             ct3d->dc.regions[0].base) / min_block_size);

    for (i = 0; i < in->num_entries_updated; i++) {
        dpa = in->updated_entries[i].start_dpa;
        len = in->updated_entries[i].len;

        region = cxl_find_dc_region(ct3d, dpa, len);
        if (!region) {
            return CXL_MBOX_INVALID_PA;
        }

        dpa -= ct3d->dc.regions[0].base;
        if (dpa % region->block_size || len % region->block_size) {
            return CXL_MBOX_INVALID_EXTENT_LIST;
        }
        /* the dpa range already covered by some other extents in the list */
        if (test_any_bits_set(blk_bitmap, dpa / min_block_size,
            len / min_block_size)) {
            return CXL_MBOX_INVALID_EXTENT_LIST;
        }
        bitmap_set(blk_bitmap, dpa / min_block_size, len / min_block_size);
   }

    return CXL_MBOX_SUCCESS;
}

static CXLRetCode cxl_dcd_add_dyn_cap_rsp_dry_run(CXLType3Dev *ct3d,
        const CXLUpdateDCExtentListInPl *in)
{
    uint32_t i;
    CXLDCExtent *ent;
    CXLDCExtentGroup *ext_group;
    uint64_t dpa, len;
    Range range1, range2;

    for (i = 0; i < in->num_entries_updated; i++) {
        dpa = in->updated_entries[i].start_dpa;
        len = in->updated_entries[i].len;

        range_init_nofail(&range1, dpa, len);

        /*
         * The host-accepted DPA range must be contained by the first extent
         * group in the pending list
         */
        ext_group = QTAILQ_FIRST(&ct3d->dc.extents_pending);
        if (!cxl_extents_contains_dpa_range(&ext_group->list, dpa, len)) {
            return CXL_MBOX_INVALID_PA;
        }

        /* to-be-added range should not overlap with range already accepted */
        QTAILQ_FOREACH(ent, &ct3d->dc.extents, node) {
            range_init_nofail(&range2, ent->start_dpa, ent->len);
            if (range_overlaps_range(&range1, &range2)) {
                return CXL_MBOX_INVALID_PA;
            }
        }
    }
    return CXL_MBOX_SUCCESS;
}

/*
 * CXL r3.1 section 8.2.9.9.9.3: Add Dynamic Capacity Response (Opcode 4802h)
 * An extent is added to the extent list and becomes usable only after the
 * response is processed successfully.
 */
static CXLRetCode cmd_dcd_add_dyn_cap_rsp(const struct cxl_cmd *cmd,
                                          uint8_t *payload_in,
                                          size_t len_in,
                                          uint8_t *payload_out,
                                          size_t *len_out,
                                          CXLCCI *cci)
{
    CXLUpdateDCExtentListInPl *in = (void *)payload_in;
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLDCExtentList *extent_list = &ct3d->dc.extents;
    uint32_t i, num;
    uint64_t dpa, len;
    CXLRetCode ret;

    if (len_in < sizeof(*in)) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }

    if (in->num_entries_updated == 0) {
        num = cxl_extent_group_list_delete_front(&ct3d->dc.extents_pending);
        ct3d->dc.total_extent_count -= num;
        return CXL_MBOX_SUCCESS;
    }

    if (len_in <
        sizeof(*in) + sizeof(*in->updated_entries) * in->num_entries_updated) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }

    /* Adding extents causes exceeding device's extent tracking ability. */
    if (in->num_entries_updated + ct3d->dc.total_extent_count >
        CXL_NUM_EXTENTS_SUPPORTED) {
        return CXL_MBOX_RESOURCES_EXHAUSTED;
    }

    ret = cxl_detect_malformed_extent_list(ct3d, in);
    if (ret != CXL_MBOX_SUCCESS) {
        return ret;
    }

    ret = cxl_dcd_add_dyn_cap_rsp_dry_run(ct3d, in);
    if (ret != CXL_MBOX_SUCCESS) {
        return ret;
    }

    for (i = 0; i < in->num_entries_updated; i++) {
        dpa = in->updated_entries[i].start_dpa;
        len = in->updated_entries[i].len;

        cxl_insert_extent_to_extent_list(extent_list, dpa, len, NULL, 0);
        ct3d->dc.total_extent_count += 1;
        ct3d->dc.nr_extents_accepted += 1;
        ct3_set_region_block_backed(ct3d, dpa, len);
    }
    /* Remove the first extent group in the pending list */
    num = cxl_extent_group_list_delete_front(&ct3d->dc.extents_pending);
    ct3d->dc.total_extent_count -= num;

    return CXL_MBOX_SUCCESS;
}

/*
 * Copy extent list from src to dst
 * Return value: number of extents copied
 */
static uint32_t copy_extent_list(CXLDCExtentList *dst,
                                 const CXLDCExtentList *src)
{
    uint32_t cnt = 0;
    CXLDCExtent *ent;

    if (!dst || !src) {
        return 0;
    }

    QTAILQ_FOREACH(ent, src, node) {
        cxl_insert_extent_to_extent_list(dst, ent->start_dpa, ent->len,
                                         ent->tag, ent->shared_seq);
        cnt++;
    }
    return cnt;
}

static CXLRetCode cxl_dc_extent_release_dry_run(CXLType3Dev *ct3d,
        const CXLUpdateDCExtentListInPl *in, CXLDCExtentList *updated_list,
        uint32_t *updated_list_size)
{
    CXLDCExtent *ent, *ent_next;
    uint64_t dpa, len;
    uint32_t i;
    int cnt_delta = 0;
    CXLRetCode ret = CXL_MBOX_SUCCESS;

    QTAILQ_INIT(updated_list);
    copy_extent_list(updated_list, &ct3d->dc.extents);

    for (i = 0; i < in->num_entries_updated; i++) {
        Range range;

        dpa = in->updated_entries[i].start_dpa;
        len = in->updated_entries[i].len;

        /* Check if the DPA range is not fully backed with valid extents */
        if (!ct3_test_region_block_backed(ct3d, dpa, len)) {
            ret = CXL_MBOX_INVALID_PA;
            goto free_and_exit;
        }

        /* After this point, extent overflow is the only error can happen */
        while (len > 0) {
            QTAILQ_FOREACH(ent, updated_list, node) {
                range_init_nofail(&range, ent->start_dpa, ent->len);

                if (range_contains(&range, dpa)) {
                    uint64_t len1, len2 = 0, len_done = 0;
                    uint64_t ent_start_dpa = ent->start_dpa;
                    uint64_t ent_len = ent->len;

                    len1 = dpa - ent->start_dpa;
                    /* Found the extent or the subset of an existing extent */
                    if (range_contains(&range, dpa + len - 1)) {
                        len2 = ent_start_dpa + ent_len - dpa - len;
                    } else {
                        dpa = ent_start_dpa + ent_len;
                    }
                    len_done = ent_len - len1 - len2;

                    cxl_remove_extent_from_extent_list(updated_list, ent);
                    cnt_delta--;

                    if (len1) {
                        cxl_insert_extent_to_extent_list(updated_list,
                                                         ent_start_dpa,
                                                         len1, NULL, 0);
                        cnt_delta++;
                    }
                    if (len2) {
                        cxl_insert_extent_to_extent_list(updated_list,
                                                         dpa + len,
                                                         len2, NULL, 0);
                        cnt_delta++;
                    }

                    if (cnt_delta + ct3d->dc.total_extent_count >
                            CXL_NUM_EXTENTS_SUPPORTED) {
                        ret = CXL_MBOX_RESOURCES_EXHAUSTED;
                        goto free_and_exit;
                    }

                    len -= len_done;
                    break;
                }
            }
        }
    }
free_and_exit:
    if (ret != CXL_MBOX_SUCCESS) {
        QTAILQ_FOREACH_SAFE(ent, updated_list, node, ent_next) {
            cxl_remove_extent_from_extent_list(updated_list, ent);
        }
        *updated_list_size = 0;
    } else {
        *updated_list_size = ct3d->dc.nr_extents_accepted + cnt_delta;
    }

    return ret;
}

/*
 * CXL r3.1 section 8.2.9.9.9.4: Release Dynamic Capacity (Opcode 4803h)
 */
static CXLRetCode cmd_dcd_release_dyn_cap(const struct cxl_cmd *cmd,
                                          uint8_t *payload_in,
                                          size_t len_in,
                                          uint8_t *payload_out,
                                          size_t *len_out,
                                          CXLCCI *cci)
{
    CXLUpdateDCExtentListInPl *in = (void *)payload_in;
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLDCExtentList updated_list;
    CXLDCExtent *ent, *ent_next;
    uint32_t updated_list_size;
    CXLRetCode ret;

    if (len_in < sizeof(*in)) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }

    if (in->num_entries_updated == 0) {
        return CXL_MBOX_INVALID_INPUT;
    }

    if (len_in <
        sizeof(*in) + sizeof(*in->updated_entries) * in->num_entries_updated) {
        return CXL_MBOX_INVALID_PAYLOAD_LENGTH;
    }

    ret = cxl_detect_malformed_extent_list(ct3d, in);
    if (ret != CXL_MBOX_SUCCESS) {
        return ret;
    }

    ret = cxl_dc_extent_release_dry_run(ct3d, in, &updated_list,
                                        &updated_list_size);
    if (ret != CXL_MBOX_SUCCESS) {
        return ret;
    }

    /*
     * If the dry run release passes, the returned updated_list will
     * be the updated extent list and we just need to clear the extents
     * in the accepted list and copy extents in the updated_list to accepted
     * list and update the extent count;
     */
    QTAILQ_FOREACH_SAFE(ent, &ct3d->dc.extents, node, ent_next) {
        ct3_clear_region_block_backed(ct3d, ent->start_dpa, ent->len);
        cxl_remove_extent_from_extent_list(&ct3d->dc.extents, ent);
    }
    copy_extent_list(&ct3d->dc.extents, &updated_list);
    QTAILQ_FOREACH_SAFE(ent, &updated_list, node, ent_next) {
        ct3_set_region_block_backed(ct3d, ent->start_dpa, ent->len);
        cxl_remove_extent_from_extent_list(&updated_list, ent);
    }
    ct3d->dc.total_extent_count += (updated_list_size -
                                    ct3d->dc.nr_extents_accepted);

    ct3d->dc.nr_extents_accepted = updated_list_size;

    return CXL_MBOX_SUCCESS;
}

/* CXL r3.2 section 7.6.7.6.1: Get DCD Info (Opcode 5600h) */
static CXLRetCode cmd_fm_get_dcd_info(const struct cxl_cmd *cmd,
                                      uint8_t *payload_in,
                                      size_t len_in,
                                      uint8_t *payload_out,
                                      size_t *len_out,
                                      CXLCCI *cci)
{
    struct {
        uint8_t num_hosts;
        uint8_t num_regions_supported;
        uint8_t rsvd1[2];
        uint16_t supported_add_sel_policy_bitmask;
        uint8_t rsvd2[2];
        uint16_t supported_removal_policy_bitmask;
        uint8_t sanitize_on_release_bitmask;
        uint8_t rsvd3;
        uint64_t total_dynamic_capacity;
        uint64_t region_blk_size_bitmasks[8];
    } QEMU_PACKED *out = (void *)payload_out;
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLDCRegion *region;
    int i;

    out->num_hosts = 1;
    out->num_regions_supported = ct3d->dc.num_regions;
    stw_le_p(&out->supported_add_sel_policy_bitmask,
             BIT(CXL_EXTENT_SELECTION_POLICY_PRESCRIPTIVE));
    stw_le_p(&out->supported_removal_policy_bitmask,
             BIT(CXL_EXTENT_REMOVAL_POLICY_PRESCRIPTIVE));
    out->sanitize_on_release_bitmask = 0;

    stq_le_p(&out->total_dynamic_capacity,
             ct3d->dc.total_capacity / CXL_CAPACITY_MULTIPLIER);

    for (i = 0; i < ct3d->dc.num_regions; i++) {
        region = &ct3d->dc.regions[i];
        memcpy(&out->region_blk_size_bitmasks[i],
               &region->supported_blk_size_bitmask,
               sizeof(out->region_blk_size_bitmasks[i]));
    }

    *len_out = sizeof(*out);
    return CXL_MBOX_SUCCESS;
}

static void build_dsmas_flags(uint8_t *flags, CXLDCRegion *region)
{
    *flags = 0;

    if (region->nonvolatile) {
        *flags |= BIT(CXL_DSMAS_FLAGS_NONVOLATILE);
    }
    if (region->sharable) {
        *flags |= BIT(CXL_DSMAS_FLAGS_SHARABLE);
    }
    if (region->hw_managed_coherency) {
        *flags |= BIT(CXL_DSMAS_FLAGS_HW_MANAGED_COHERENCY);
    }
    if (region->ic_specific_dc_management) {
        *flags |= BIT(CXL_DSMAS_FLAGS_IC_SPECIFIC_DC_MANAGEMENT);
    }
    if (region->rdonly) {
        *flags |= BIT(CXL_DSMAS_FLAGS_RDONLY);
    }
}

/*
 * CXL r3.2 section 7.6.7.6.2:
 * Get Host DC Region Configuration (Opcode 5601h)
 */
static CXLRetCode cmd_fm_get_host_dc_region_config(const struct cxl_cmd *cmd,
                                                   uint8_t *payload_in,
                                                   size_t len_in,
                                                   uint8_t *payload_out,
                                                   size_t *len_out,
                                                   CXLCCI *cci)
{
    struct {
        uint16_t host_id;
        uint8_t region_cnt;
        uint8_t start_rid;
    } QEMU_PACKED *in = (void *)payload_in;
    struct {
        uint16_t host_id;
        uint8_t num_regions;
        uint8_t regions_returned;
        struct {
            uint64_t base;
            uint64_t decode_len;
            uint64_t region_len;
            uint64_t block_size;
            uint8_t flags;
            uint8_t rsvd1[3];
            uint8_t sanitize;
            uint8_t rsvd2[3];
        } QEMU_PACKED records[];
    } QEMU_PACKED *out = (void *)payload_out;
    struct {
        uint32_t num_extents_supported;
        uint32_t num_extents_available;
        uint32_t num_tags_supported;
        uint32_t num_tags_available;
    } QEMU_PACKED *extra_out;
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    uint16_t record_count, out_pl_len, i;

    if (in->start_rid >= ct3d->dc.num_regions) {
        return CXL_MBOX_INVALID_INPUT;
    }
    record_count = MIN(ct3d->dc.num_regions - in->start_rid, in->region_cnt);

    out_pl_len = sizeof(*out) + record_count * sizeof(out->records[0]);
    extra_out = (void *)out + out_pl_len;
    out_pl_len += sizeof(*extra_out);

    assert(out_pl_len <= CXL_MAILBOX_MAX_PAYLOAD_SIZE);

    stw_le_p(&out->host_id, 0);
    out->num_regions = ct3d->dc.num_regions;
    out->regions_returned = record_count;

    for (i = 0; i < record_count; i++) {
        stq_le_p(&out->records[i].base,
                 ct3d->dc.regions[in->start_rid + i].base);
        stq_le_p(&out->records[i].decode_len,
                 ct3d->dc.regions[in->start_rid + i].decode_len /
                 CXL_CAPACITY_MULTIPLIER);
        stq_le_p(&out->records[i].region_len,
                 ct3d->dc.regions[in->start_rid + i].len);
        stq_le_p(&out->records[i].block_size,
                 ct3d->dc.regions[in->start_rid + i].block_size);
        build_dsmas_flags(&out->records[i].flags,
                          &ct3d->dc.regions[in->start_rid + i]);
        /* Sanitize is bit 0 of flags. */
        out->records[i].sanitize =
            ct3d->dc.regions[in->start_rid + i].flags & BIT(0);
    }

    stl_le_p(&extra_out->num_extents_supported, CXL_NUM_EXTENTS_SUPPORTED);
    stl_le_p(&extra_out->num_extents_available, CXL_NUM_EXTENTS_SUPPORTED -
             ct3d->dc.total_extent_count);
    stl_le_p(&extra_out->num_tags_supported, CXL_NUM_TAGS_SUPPORTED);
    stl_le_p(&extra_out->num_tags_available, CXL_NUM_TAGS_SUPPORTED);

    *len_out = out_pl_len;
    return CXL_MBOX_SUCCESS;
}

/* CXL r3.2 section 7.6.7.6.3: Set Host DC Region Configuration (Opcode 5602) */
static CXLRetCode cmd_fm_set_dc_region_config(const struct cxl_cmd *cmd,
                                              uint8_t *payload_in,
                                              size_t len_in,
                                              uint8_t *payload_out,
                                              size_t *len_out,
                                              CXLCCI *cci)
{
    struct {
        uint8_t reg_id;
        uint8_t rsvd[3];
        uint64_t block_sz;
        uint8_t flags;
        uint8_t rsvd2[3];
    } QEMU_PACKED *in = (void *)payload_in;
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLEventDynamicCapacity dcEvent = {};
    CXLDCRegion *region = &ct3d->dc.regions[in->reg_id];

    /*
     * CXL r3.2 7.6.7.6.3: Set DC Region Configuration
     * This command shall fail with Unsupported when the Sanitize on Release
     * field does not match the region’s configuration... and the device
     * does not support reconfiguration of the Sanitize on Release setting.
     *
     * Currently not reconfigurable, so always fail if sanitize bit (bit 0)
     * doesn't match.
     */
    if ((in->flags & 0x1) != (region->flags & 0x1)) {
        return CXL_MBOX_UNSUPPORTED;
    }

    if (in->reg_id >= DCD_MAX_NUM_REGION) {
        return CXL_MBOX_UNSUPPORTED;
    }

    /* Check that no extents are in the region being reconfigured */
    if (!bitmap_empty(region->blk_bitmap, region->len / region->block_size)) {
        return CXL_MBOX_UNSUPPORTED;
    }

    /* Check that new block size is supported */
    if (!is_power_of_2(in->block_sz) ||
        !(in->block_sz & region->supported_blk_size_bitmask)) {
        return CXL_MBOX_INVALID_INPUT;
    }

    /* Return success if new block size == current block size */
    if (in->block_sz == region->block_size) {
        return CXL_MBOX_SUCCESS;
    }

    /* Free bitmap and create new one for new block size. */
    qemu_mutex_lock(&region->bitmap_lock);
    g_free(region->blk_bitmap);
    region->blk_bitmap = bitmap_new(region->len / in->block_sz);
    qemu_mutex_unlock(&region->bitmap_lock);
    region->block_size = in->block_sz;

    /* Create event record and insert into event log */
    cxl_assign_event_header(&dcEvent.hdr,
                            &dynamic_capacity_uuid,
                            (1 << CXL_EVENT_TYPE_INFO),
                            sizeof(dcEvent),
                            cxl_device_get_timestamp(&ct3d->cxl_dstate));
    dcEvent.type = DC_EVENT_REGION_CONFIG_UPDATED;
    dcEvent.validity_flags = 1;
    dcEvent.host_id = 0;
    dcEvent.updated_region_id = in->reg_id;

    if (cxl_event_insert(&ct3d->cxl_dstate,
                         CXL_EVENT_TYPE_DYNAMIC_CAP,
                         (CXLEventRecordRaw *)&dcEvent)) {
        cxl_event_irq_assert(ct3d);
    }
    return CXL_MBOX_SUCCESS;
}

/* CXL r3.2 section 7.6.7.6.4: Get DC Region Extent Lists (Opcode 5603h) */
static CXLRetCode cmd_fm_get_dc_region_extent_list(const struct cxl_cmd *cmd,
                                                   uint8_t *payload_in,
                                                   size_t len_in,
                                                   uint8_t *payload_out,
                                                   size_t *len_out,
                                                   CXLCCI *cci)
{
    struct {
        uint16_t host_id;
        uint8_t rsvd[2];
        uint32_t extent_cnt;
        uint32_t start_extent_id;
    } QEMU_PACKED *in = (void *)payload_in;
    struct {
        uint16_t host_id;
        uint8_t rsvd[2];
        uint32_t start_extent_id;
        uint32_t extents_returned;
        uint32_t total_extents;
        uint32_t list_generation_num;
        uint8_t rsvd2[4];
        CXLDCExtentRaw records[];
    } QEMU_PACKED *out = (void *)payload_out;
    QEMU_BUILD_BUG_ON(sizeof(*in) != 0xc);
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    CXLDCExtent *ent;
    CXLDCExtentRaw *out_rec;
    uint16_t record_count = 0, record_done = 0, i = 0;
    uint16_t out_pl_len, max_size;

    if (in->host_id != 0) {
        return CXL_MBOX_INVALID_INPUT;
    }

    if (in->start_extent_id > ct3d->dc.nr_extents_accepted) {
        return CXL_MBOX_INVALID_INPUT;
    }

    record_count = MIN(in->extent_cnt,
                       ct3d->dc.nr_extents_accepted - in->start_extent_id);
    max_size = CXL_MAILBOX_MAX_PAYLOAD_SIZE - sizeof(*out);
    record_count = MIN(record_count, max_size / sizeof(out->records[0]));
    out_pl_len = sizeof(*out) + record_count * sizeof(out->records[0]);

    stw_le_p(&out->host_id, in->host_id);
    stl_le_p(&out->start_extent_id, in->start_extent_id);
    stl_le_p(&out->extents_returned, record_count);
    stl_le_p(&out->total_extents, ct3d->dc.nr_extents_accepted);
    stl_le_p(&out->list_generation_num, ct3d->dc.ext_list_gen_seq);

    if (record_count > 0) {
        QTAILQ_FOREACH(ent, &ct3d->dc.extents, node) {
            if (i++ < in->start_extent_id) {
                continue;
            }
            out_rec = &out->records[record_done];
            stq_le_p(&out_rec->start_dpa, ent->start_dpa);
            stq_le_p(&out_rec->len, ent->len);
            memcpy(&out_rec->tag, ent->tag, 0x10);
            stw_le_p(&out_rec->shared_seq, ent->shared_seq);

            record_done++;
            if (record_done == record_count) {
                break;
            }
        }
    }

    *len_out = out_pl_len;
    return CXL_MBOX_SUCCESS;
}

/*
 * Helper function to convert CXLDCExtentRaw to CXLUpdateDCExtentListInPl
 * in order to reuse cxl_detect_malformed_extent_list() function which accepts
 * CXLUpdateDCExtentListInPl as a parameter.
 */
static void convert_raw_extents(CXLDCExtentRaw raw_extents[],
                                CXLUpdateDCExtentListInPl *extent_list,
                                int count)
{
    int i;

    extent_list->num_entries_updated = count;

    for (i = 0; i < count; i++) {
        extent_list->updated_entries[i].start_dpa = raw_extents[i].start_dpa;
        extent_list->updated_entries[i].len = raw_extents[i].len;
    }
}

/* CXL r3.2 Section 7.6.7.6.5: Initiate Dynamic Capacity Add (Opcode 5604h) */
static CXLRetCode cmd_fm_initiate_dc_add(const struct cxl_cmd *cmd,
                                         uint8_t *payload_in,
                                         size_t len_in,
                                         uint8_t *payload_out,
                                         size_t *len_out,
                                         CXLCCI *cci)
{
    struct {
        uint16_t host_id;
        uint8_t selection_policy;
        uint8_t reg_num;
        uint64_t length;
        uint8_t tag[0x10];
        uint32_t ext_count;
        CXLDCExtentRaw extents[];
    } QEMU_PACKED *in = (void *)payload_in;
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    int i, rc;

    switch (in->selection_policy) {
        case CXL_EXTENT_SELECTION_POLICY_PRESCRIPTIVE: {
            /* Adding extents exceeds device's extent tracking ability. */
            if (in->ext_count + ct3d->dc.total_extent_count >
                CXL_NUM_EXTENTS_SUPPORTED) {
                return CXL_MBOX_RESOURCES_EXHAUSTED;
            }

            g_autofree CXLUpdateDCExtentListInPl *list =
                g_malloc0(sizeof(*list) +
                    in->ext_count * sizeof(*list->updated_entries));

            convert_raw_extents(in->extents, list, in->ext_count);
            rc = cxl_detect_malformed_extent_list(ct3d, list);

            for (i = 0; i < in->ext_count; i++) {
                CXLDCExtentRaw *ext = &in->extents[i];

                /* Check requested extents do not overlap with pending ones. */
                if (cxl_extent_groups_overlaps_dpa_range(&ct3d->dc.extents_pending,
                                                         ext->start_dpa,
                                                         ext->len)) {
                    return CXL_MBOX_INVALID_EXTENT_LIST;
                }
                /* Check requested extents do not overlap with existing ones. */
                if (cxl_extents_overlaps_dpa_range(&ct3d->dc.extents,
                                                   ext->start_dpa,
                                                   ext->len)) {
                    return CXL_MBOX_INVALID_EXTENT_LIST;
                }
            }

            if (rc) {
                return rc;
            }

            CXLDCExtentGroup *group = NULL;
            for (i = 0; i < in->ext_count; i++) {
                CXLDCExtentRaw *ext = &in->extents[i];

                group = cxl_insert_extent_to_extent_group(group, ext->start_dpa,
                                                          ext->len, ext->tag,
                                                          ext->shared_seq);
            }

            cxl_extent_group_list_insert_tail(&ct3d->dc.extents_pending, group);
            ct3d->dc.total_extent_count += in->ext_count;
            cxl_create_dc_event_records_for_extents(ct3d,
                                                    DC_EVENT_ADD_CAPACITY,
                                                    in->extents,
                                                    in->ext_count);

            return CXL_MBOX_SUCCESS;
        }
        default: {
            qemu_log_mask(LOG_UNIMP,
                          "CXL extent selection policy not supported.\n");
            return CXL_MBOX_INVALID_INPUT;
        }
    }
}

#define CXL_EXTENT_REMOVAL_POLICY_MASK 0x0F
#define CXL_FORCED_REMOVAL_MASK (1 << 4)
/*
 * CXL r3.2 Section 7.6.7.6.6:
 * Initiate Dynamic Capacity Release (Opcode 5605h)
 */
static CXLRetCode cmd_fm_initiate_dc_release(const struct cxl_cmd *cmd,
                                             uint8_t *payload_in,
                                             size_t len_in,
                                             uint8_t *payload_out,
                                             size_t *len_out,
                                             CXLCCI *cci)
{
    struct {
        uint16_t host_id;
        uint8_t flags;
        uint8_t reg_num;
        uint64_t length;
        uint8_t tag[0x10];
        uint32_t ext_count;
        CXLDCExtentRaw extents[];
    } QEMU_PACKED *in = (void *)payload_in;
    CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
    int i, rc;

    switch (in->flags & CXL_EXTENT_REMOVAL_POLICY_MASK) {
        case CXL_EXTENT_REMOVAL_POLICY_PRESCRIPTIVE: {
            CXLDCExtentList updated_list;
            uint32_t updated_list_size;
            g_autofree CXLUpdateDCExtentListInPl *list =
                g_malloc0(sizeof(*list) +
                    in->ext_count * sizeof(*list->updated_entries));

            convert_raw_extents(in->extents, list, in->ext_count);
            rc = cxl_detect_malformed_extent_list(ct3d, list);
            if (rc) {
                return rc;
            }

            /*
             * Fail with Invalid PA if an extent is pending and Forced Removal
             * flag not set.
             */
            if (!(in->flags & CXL_FORCED_REMOVAL_MASK)) {
                for (i = 0; i < in->ext_count; i++) {
                    CXLDCExtentRaw ext = in->extents[i];
                    /*
                     * Check requested extents don't overlap with pending
                     * extents.
                     */
                    if (cxl_extent_groups_overlaps_dpa_range(
                            &ct3d->dc.extents_pending,
                            ext.start_dpa,
                            ext.len)) {
                        return CXL_MBOX_INVALID_PA;
                    }
                }
            }

            rc = cxl_dc_extent_release_dry_run(ct3d,
                                               list,
                                               &updated_list,
                                               &updated_list_size);
            if (rc) {
                return rc;
            }
            cxl_create_dc_event_records_for_extents(ct3d,
                                                    DC_EVENT_RELEASE_CAPACITY,
                                                    in->extents,
                                                    in->ext_count);
            return CXL_MBOX_SUCCESS;
        }
        default: {
            qemu_log_mask(LOG_UNIMP,
                "CXL extent removal policy not supported.\n");
            return CXL_MBOX_INVALID_INPUT;
        }
    }
}

static const struct cxl_cmd cxl_cmd_set[256][256] = {
    [INFOSTAT][BACKGROUND_OPERATION_ABORT] = { "BACKGROUND_OPERATION_ABORT",
        cmd_infostat_bg_op_abort, 0, 0 },
    [EVENTS][GET_RECORDS] = { "EVENTS_GET_RECORDS",
        cmd_events_get_records, 1, 0 },
    [EVENTS][CLEAR_RECORDS] = { "EVENTS_CLEAR_RECORDS",
        cmd_events_clear_records, ~0, CXL_MBOX_IMMEDIATE_LOG_CHANGE },
    [EVENTS][GET_INTERRUPT_POLICY] = { "EVENTS_GET_INTERRUPT_POLICY",
                                      cmd_events_get_interrupt_policy, 0, 0 },
    [EVENTS][SET_INTERRUPT_POLICY] = { "EVENTS_SET_INTERRUPT_POLICY",
                                      cmd_events_set_interrupt_policy,
                                      ~0, CXL_MBOX_IMMEDIATE_CONFIG_CHANGE },
    [FIRMWARE_UPDATE][GET_INFO] = { "FIRMWARE_UPDATE_GET_INFO",
        cmd_firmware_update_get_info, 0, 0 },
    [FIRMWARE_UPDATE][TRANSFER] = { "FIRMWARE_UPDATE_TRANSFER",
        cmd_firmware_update_transfer, ~0,
        CXL_MBOX_BACKGROUND_OPERATION | CXL_MBOX_BACKGROUND_OPERATION_ABORT },
    [FIRMWARE_UPDATE][ACTIVATE] = { "FIRMWARE_UPDATE_ACTIVATE",
        cmd_firmware_update_activate, 2,
        CXL_MBOX_BACKGROUND_OPERATION | CXL_MBOX_BACKGROUND_OPERATION_ABORT },
    [TIMESTAMP][GET] = { "TIMESTAMP_GET", cmd_timestamp_get, 0, 0 },
    [TIMESTAMP][SET] = { "TIMESTAMP_SET", cmd_timestamp_set,
                         8, CXL_MBOX_IMMEDIATE_POLICY_CHANGE },
    [LOGS][GET_SUPPORTED] = { "LOGS_GET_SUPPORTED", cmd_logs_get_supported,
                              0, 0 },
    [LOGS][GET_LOG] = { "LOGS_GET_LOG", cmd_logs_get_log, 0x18, 0 },
    [FEATURES][GET_SUPPORTED] = { "FEATURES_GET_SUPPORTED",
                                  cmd_features_get_supported, 0x8, 0 },
    [FEATURES][GET_FEATURE] = { "FEATURES_GET_FEATURE",
                                cmd_features_get_feature, 0x15, 0 },
    [FEATURES][SET_FEATURE] = { "FEATURES_SET_FEATURE",
                                cmd_features_set_feature,
                                ~0,
                                (CXL_MBOX_IMMEDIATE_CONFIG_CHANGE |
                                 CXL_MBOX_IMMEDIATE_DATA_CHANGE |
                                 CXL_MBOX_IMMEDIATE_POLICY_CHANGE |
                                 CXL_MBOX_IMMEDIATE_LOG_CHANGE |
                                 CXL_MBOX_SECURITY_STATE_CHANGE)},
    [IDENTIFY][MEMORY_DEVICE] = { "IDENTIFY_MEMORY_DEVICE",
        cmd_identify_memory_device, 0, 0 },
    [CCLS][GET_PARTITION_INFO] = { "CCLS_GET_PARTITION_INFO",
        cmd_ccls_get_partition_info, 0, 0 },
    [CCLS][GET_LSA] = { "CCLS_GET_LSA", cmd_ccls_get_lsa, 8, 0 },
    [CCLS][SET_LSA] = { "CCLS_SET_LSA", cmd_ccls_set_lsa,
        ~0, CXL_MBOX_IMMEDIATE_CONFIG_CHANGE | CXL_MBOX_IMMEDIATE_DATA_CHANGE },
    [HEALTH_INFO_ALERTS][GET_ALERT_CONFIG] = {
        "HEALTH_INFO_ALERTS_GET_ALERT_CONFIG",
        cmd_get_alert_config, 0, 0 },
    [HEALTH_INFO_ALERTS][SET_ALERT_CONFIG] = {
        "HEALTH_INFO_ALERTS_SET_ALERT_CONFIG",
        cmd_set_alert_config, 12, CXL_MBOX_IMMEDIATE_POLICY_CHANGE },
    [SANITIZE][OVERWRITE] = { "SANITIZE_OVERWRITE", cmd_sanitize_overwrite, 0,
        (CXL_MBOX_IMMEDIATE_DATA_CHANGE |
         CXL_MBOX_SECURITY_STATE_CHANGE |
         CXL_MBOX_BACKGROUND_OPERATION |
         CXL_MBOX_BACKGROUND_OPERATION_ABORT)},
    [SANITIZE][MEDIA_OPERATIONS] = { "MEDIA_OPERATIONS", cmd_media_operations,
        ~0,
        (CXL_MBOX_IMMEDIATE_DATA_CHANGE |
         CXL_MBOX_BACKGROUND_OPERATION)},
    [PERSISTENT_MEM][GET_SECURITY_STATE] = { "GET_SECURITY_STATE",
        cmd_get_security_state, 0, 0 },
    [MEDIA_AND_POISON][GET_POISON_LIST] = { "MEDIA_AND_POISON_GET_POISON_LIST",
        cmd_media_get_poison_list, 16, 0 },
    [MEDIA_AND_POISON][INJECT_POISON] = { "MEDIA_AND_POISON_INJECT_POISON",
        cmd_media_inject_poison, 8, 0 },
    [MEDIA_AND_POISON][CLEAR_POISON] = { "MEDIA_AND_POISON_CLEAR_POISON",
        cmd_media_clear_poison, 72, 0 },
    [MEDIA_AND_POISON][GET_SCAN_MEDIA_CAPABILITIES] = {
        "MEDIA_AND_POISON_GET_SCAN_MEDIA_CAPABILITIES",
        cmd_media_get_scan_media_capabilities, 16, 0 },
    [MEDIA_AND_POISON][SCAN_MEDIA] = { "MEDIA_AND_POISON_SCAN_MEDIA",
        cmd_media_scan_media, 17,
        (CXL_MBOX_BACKGROUND_OPERATION | CXL_MBOX_BACKGROUND_OPERATION_ABORT)},
    [MEDIA_AND_POISON][GET_SCAN_MEDIA_RESULTS] = {
        "MEDIA_AND_POISON_GET_SCAN_MEDIA_RESULTS",
        cmd_media_get_scan_media_results, 0, 0 },
};

static const struct cxl_cmd cxl_cmd_set_dcd[256][256] = {
    [DCD_CONFIG][GET_DC_CONFIG] = { "DCD_GET_DC_CONFIG",
        cmd_dcd_get_dyn_cap_config, 2, 0 },
    [DCD_CONFIG][GET_DYN_CAP_EXT_LIST] = {
        "DCD_GET_DYNAMIC_CAPACITY_EXTENT_LIST", cmd_dcd_get_dyn_cap_ext_list,
        8, 0 },
    [DCD_CONFIG][ADD_DYN_CAP_RSP] = {
        "DCD_ADD_DYNAMIC_CAPACITY_RESPONSE", cmd_dcd_add_dyn_cap_rsp,
        ~0, CXL_MBOX_IMMEDIATE_DATA_CHANGE },
    [DCD_CONFIG][RELEASE_DYN_CAP] = {
        "DCD_RELEASE_DYNAMIC_CAPACITY", cmd_dcd_release_dyn_cap,
        ~0, CXL_MBOX_IMMEDIATE_DATA_CHANGE },
};

static const struct cxl_cmd cxl_cmd_set_sw[256][256] = {
    [INFOSTAT][IS_IDENTIFY] = { "IDENTIFY", cmd_infostat_identify, 0, 0 },
    [INFOSTAT][BACKGROUND_OPERATION_STATUS] = { "BACKGROUND_OPERATION_STATUS",
        cmd_infostat_bg_op_sts, 0, 0 },
    [INFOSTAT][BACKGROUND_OPERATION_ABORT] = { "BACKGROUND_OPERATION_ABORT",
        cmd_infostat_bg_op_abort, 0, 0 },
    [TIMESTAMP][GET] = { "TIMESTAMP_GET", cmd_timestamp_get, 0, 0 },
    [TIMESTAMP][SET] = { "TIMESTAMP_SET", cmd_timestamp_set, 8,
                         CXL_MBOX_IMMEDIATE_POLICY_CHANGE },
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

static const struct cxl_cmd cxl_cmd_set_fm_dcd[256][256] = {
    [FMAPI_DCD_MGMT][GET_DCD_INFO] = { "GET_DCD_INFO",
        cmd_fm_get_dcd_info, 0, 0 },
    [FMAPI_DCD_MGMT][GET_HOST_DC_REGION_CONFIG] = { "GET_HOST_DC_REGION_CONFIG",
        cmd_fm_get_host_dc_region_config, 4, 0 },
    [FMAPI_DCD_MGMT][SET_DC_REGION_CONFIG] = { "SET_DC_REGION_CONFIG",
        cmd_fm_set_dc_region_config, 16,
        (CXL_MBOX_CONFIG_CHANGE_COLD_RESET |
         CXL_MBOX_CONFIG_CHANGE_CONV_RESET |
         CXL_MBOX_CONFIG_CHANGE_CXL_RESET |
         CXL_MBOX_IMMEDIATE_CONFIG_CHANGE |
         CXL_MBOX_IMMEDIATE_DATA_CHANGE) },
    [FMAPI_DCD_MGMT][GET_DC_REGION_EXTENT_LIST] = { "GET_DC_REGION_EXTENT_LIST",
        cmd_fm_get_dc_region_extent_list, 12, 0 },
    [FMAPI_DCD_MGMT][INITIATE_DC_ADD] = { "INIT_DC_ADD",
        cmd_fm_initiate_dc_add, ~0,
        (CXL_MBOX_CONFIG_CHANGE_COLD_RESET |
        CXL_MBOX_CONFIG_CHANGE_CONV_RESET |
        CXL_MBOX_CONFIG_CHANGE_CXL_RESET |
        CXL_MBOX_IMMEDIATE_CONFIG_CHANGE |
        CXL_MBOX_IMMEDIATE_DATA_CHANGE) },
    [FMAPI_DCD_MGMT][INITIATE_DC_RELEASE] = { "INIT_DC_RELEASE",
        cmd_fm_initiate_dc_release, ~0,
        (CXL_MBOX_CONFIG_CHANGE_COLD_RESET |
         CXL_MBOX_CONFIG_CHANGE_CONV_RESET |
         CXL_MBOX_CONFIG_CHANGE_CXL_RESET |
         CXL_MBOX_IMMEDIATE_CONFIG_CHANGE |
         CXL_MBOX_IMMEDIATE_DATA_CHANGE) },
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
    CXLDeviceState *cxl_dstate;

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
    if ((cxl_cmd->effect & CXL_MBOX_BACKGROUND_OPERATION) &&
        cci->bg.runtime > 0) {
        return CXL_MBOX_BUSY;
    }

    /* forbid any selected commands while the media is disabled */
    if (object_dynamic_cast(OBJECT(cci->d), TYPE_CXL_TYPE3)) {
        cxl_dstate = &CXL_TYPE3(cci->d)->cxl_dstate;

        if (cxl_dev_media_disabled(cxl_dstate)) {
            if (h == cmd_events_get_records ||
                h == cmd_ccls_get_partition_info ||
                h == cmd_ccls_set_lsa ||
                h == cmd_ccls_get_lsa ||
                h == cmd_logs_get_log ||
                h == cmd_media_get_poison_list ||
                h == cmd_media_inject_poison ||
                h == cmd_media_clear_poison ||
                h == cmd_sanitize_overwrite ||
                h == cmd_firmware_update_transfer ||
                h == cmd_firmware_update_activate) {
                return CXL_MBOX_MEDIA_DISABLED;
            }
        }
    }

    ret = (*h)(cxl_cmd, pl_in, len_in, pl_out, len_out, cci);
    if ((cxl_cmd->effect & CXL_MBOX_BACKGROUND_OPERATION) &&
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
        cci->bg.aborted = false;
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
    uint64_t now, total_time;

    qemu_mutex_lock(&cci->bg.lock);

    now = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    total_time = cci->bg.starttime + cci->bg.runtime;

    if (now >= total_time) { /* we are done */
        uint16_t ret = CXL_MBOX_SUCCESS;

        cci->bg.complete_pct = 100;
        cci->bg.ret_code = ret;
        switch (cci->bg.opcode) {
        case 0x0201: /* fw transfer */
            __do_firmware_xfer(cci);
            break;
        case 0x4400: /* sanitize */
        {
            CXLType3Dev *ct3d = CXL_TYPE3(cci->d);

            __do_sanitization(ct3d);
            cxl_dev_enable_media(&ct3d->cxl_dstate);
        }
        break;
        case 0x4402: /* Media Operations sanitize */
        {
            CXLType3Dev *ct3d = CXL_TYPE3(cci->d);
            __do_sanitize(ct3d);
        }
        break;
        case 0x4304: /* scan media */
        {
            CXLType3Dev *ct3d = CXL_TYPE3(cci->d);

            __do_scan_media(ct3d);
            break;
        }
        default:
            __builtin_unreachable();
            break;
        }
    } else {
        /* estimate only */
        cci->bg.complete_pct =
            100 * (now - cci->bg.starttime) / cci->bg.runtime;
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

    qemu_mutex_unlock(&cci->bg.lock);
}

static void cxl_rebuild_cel(CXLCCI *cci)
{
    cci->cel_size = 0; /* Reset for a fresh build */
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
}

void cxl_init_cci(CXLCCI *cci, size_t payload_max)
{
    cci->payload_max = payload_max;
    cxl_rebuild_cel(cci);

    cci->bg.complete_pct = 0;
    cci->bg.starttime = 0;
    cci->bg.runtime = 0;
    cci->bg.aborted = false;
    cci->bg.timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                 bg_timercb, cci);
    qemu_mutex_init(&cci->bg.lock);

    memset(&cci->fw, 0, sizeof(cci->fw));
    cci->fw.active_slot = 1;
    cci->fw.slot[cci->fw.active_slot - 1] = true;
    cci->initialized = true;
}

void cxl_destroy_cci(CXLCCI *cci)
{
    qemu_mutex_destroy(&cci->bg.lock);
    cci->initialized = false;
}

static void cxl_copy_cci_commands(CXLCCI *cci, const struct cxl_cmd (*cxl_cmds)[256])
{
    for (int set = 0; set < 256; set++) {
        for (int cmd = 0; cmd < 256; cmd++) {
            if (cxl_cmds[set][cmd].handler) {
                cci->cxl_cmd_set[set][cmd] = cxl_cmds[set][cmd];
            }
        }
    }
}

void cxl_add_cci_commands(CXLCCI *cci, const struct cxl_cmd (*cxl_cmd_set)[256],
                                 size_t payload_max)
{
    cci->payload_max = MAX(payload_max, cci->payload_max);
    cxl_copy_cci_commands(cci, cxl_cmd_set);
    cxl_rebuild_cel(cci);
}

void cxl_initialize_mailbox_swcci(CXLCCI *cci, DeviceState *intf,
                                  DeviceState *d, size_t payload_max)
{
    cxl_copy_cci_commands(cci, cxl_cmd_set_sw);
    cci->d = d;
    cci->intf = intf;
    cxl_init_cci(cci, payload_max);
}

void cxl_initialize_mailbox_t3(CXLCCI *cci, DeviceState *d, size_t payload_max)
{
    CXLType3Dev *ct3d = CXL_TYPE3(d);

    cxl_copy_cci_commands(cci, cxl_cmd_set);
    if (ct3d->dc.num_regions) {
        cxl_copy_cci_commands(cci, cxl_cmd_set_dcd);
    }
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
    cxl_copy_cci_commands(cci, cxl_cmd_set_t3_ld);
    cci->d = d;
    cci->intf = intf;
    cxl_init_cci(cci, payload_max);
}

static const struct cxl_cmd cxl_cmd_set_t3_fm_owned_ld_mctp[256][256] = {
    [INFOSTAT][IS_IDENTIFY] = { "IDENTIFY", cmd_infostat_identify, 0,  0},
    [INFOSTAT][GET_RESPONSE_MSG_LIMIT] = { "GET_RESPONSE_MSG_LIMIT",
                                           cmd_get_response_msg_limit, 0, 0 },
    [INFOSTAT][SET_RESPONSE_MSG_LIMIT] = { "SET_RESPONSE_MSG_LIMIT",
                                           cmd_set_response_msg_limit, 1, 0 },
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
    CXLType3Dev *ct3d = CXL_TYPE3(d);

    cxl_copy_cci_commands(cci, cxl_cmd_set_t3_fm_owned_ld_mctp);
    if (ct3d->dc.num_regions) {
        cxl_copy_cci_commands(cci, cxl_cmd_set_fm_dcd);
    }
    cci->d = d;
    cci->intf = intf;
    cxl_init_cci(cci, payload_max);
}
