/*
 * PCIe Data Object Exchange
 *
 * Copyright (C) 2021 Avery Design Systems, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/range.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_doe.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"

#define DWORD_BYTE 4

typedef struct DoeDiscoveryReq {
    DOEHeader header;
    uint8_t index;
    uint8_t reserved[3];
} QEMU_PACKED DoeDiscoveryReq;

typedef struct DoeDiscoveryRsp {
    DOEHeader header;
    uint16_t vendor_id;
    uint8_t data_obj_type;
    uint8_t next_index;
} QEMU_PACKED DoeDiscoveryRsp;

static bool pcie_doe_discovery(DOECap *doe_cap)
{
    DoeDiscoveryReq *req = pcie_doe_get_write_mbox_ptr(doe_cap);
    DoeDiscoveryRsp rsp;
    uint8_t index = req->index;
    DOEProtocol *prot;

    /* Discard request if length does not match DoeDiscoveryReq */
    if (pcie_doe_get_obj_len(req) <
        DIV_ROUND_UP(sizeof(DoeDiscoveryReq), DWORD_BYTE)) {
        return false;
    }

    rsp.header = (DOEHeader) {
        .vendor_id = PCI_VENDOR_ID_PCI_SIG,
        .data_obj_type = PCI_SIG_DOE_DISCOVERY,
        .length = DIV_ROUND_UP(sizeof(DoeDiscoveryRsp), DWORD_BYTE),
    };

    /* Point to the requested protocol, index 0 must be Discovery */
    if (index == 0) {
        rsp.vendor_id = PCI_VENDOR_ID_PCI_SIG;
        rsp.data_obj_type = PCI_SIG_DOE_DISCOVERY;
    } else {
        if (index < doe_cap->protocol_num) {
            prot = &doe_cap->protocols[index - 1];
            rsp.vendor_id = prot->vendor_id;
            rsp.data_obj_type = prot->data_obj_type;
        } else {
            rsp.vendor_id = 0xFFFF;
            rsp.data_obj_type = 0xFF;
        }
    }

    if (index + 1 == doe_cap->protocol_num) {
        rsp.next_index = 0;
    } else {
        rsp.next_index = index + 1;
    }

    pcie_doe_set_rsp(doe_cap, &rsp);

    return true;
}

static void pcie_doe_reset_mbox(DOECap *st)
{
    st->read_mbox_idx = 0;
    st->read_mbox_len = 0;
    st->write_mbox_len = 0;

    memset(st->read_mbox, 0, PCI_DOE_DW_SIZE_MAX * DWORD_BYTE);
    memset(st->write_mbox, 0, PCI_DOE_DW_SIZE_MAX * DWORD_BYTE);
}

void pcie_doe_init(PCIDevice *dev, DOECap *doe_cap, uint16_t offset,
                   DOEProtocol *protocols, bool intr, uint16_t vec)
{
    pcie_add_capability(dev, PCI_EXT_CAP_ID_DOE, 0x1, offset,
                        PCI_DOE_SIZEOF);

    doe_cap->pdev = dev;
    doe_cap->offset = offset;

    if (intr && (msi_present(dev) || msix_present(dev))) {
        doe_cap->cap.intr = intr;
        doe_cap->cap.vec = vec;
    }

    doe_cap->write_mbox = g_malloc0(PCI_DOE_DW_SIZE_MAX * DWORD_BYTE);
    doe_cap->read_mbox = g_malloc0(PCI_DOE_DW_SIZE_MAX * DWORD_BYTE);

    pcie_doe_reset_mbox(doe_cap);

    doe_cap->protocols = protocols;
    for (; protocols->vendor_id; protocols++) {
        doe_cap->protocol_num++;
    }
    assert(doe_cap->protocol_num < PCI_DOE_PROTOCOL_NUM_MAX);

    /* Increment to allow for the discovery protocol */
    doe_cap->protocol_num++;
}

void pcie_doe_fini(DOECap *doe_cap)
{
    g_free(doe_cap->read_mbox);
    g_free(doe_cap->write_mbox);
    g_free(doe_cap);
}

uint32_t pcie_doe_build_protocol(DOEProtocol *p)
{
    return DATA_OBJ_BUILD_HEADER1(p->vendor_id, p->data_obj_type);
}

void *pcie_doe_get_write_mbox_ptr(DOECap *doe_cap)
{
    return doe_cap->write_mbox;
}

/*
 * Copy the response to read mailbox buffer
 * This might be called in self-defined handle_request() if a DOE response is
 * required in the corresponding protocol
 */
void pcie_doe_set_rsp(DOECap *doe_cap, void *rsp)
{
    uint32_t len = pcie_doe_get_obj_len(rsp);

    memcpy(doe_cap->read_mbox + doe_cap->read_mbox_len, rsp, len * DWORD_BYTE);
    doe_cap->read_mbox_len += len;
}

uint32_t pcie_doe_get_obj_len(void *obj)
{
    uint32_t len;

    if (!obj) {
        return 0;
    }

    /* Only lower 18 bits are valid */
    len = DATA_OBJ_LEN_MASK(((DOEHeader *)obj)->length);

    /* PCIe r6.0 Table 6.29: a value of 00000h indicates 2^18 DW */
    return (len) ? len : PCI_DOE_DW_SIZE_MAX;
}

static void pcie_doe_irq_assert(DOECap *doe_cap)
{
    PCIDevice *dev = doe_cap->pdev;

    if (doe_cap->cap.intr && doe_cap->ctrl.intr) {
        if (doe_cap->status.intr) {
            return;
        }
        doe_cap->status.intr = 1;

        if (msix_enabled(dev)) {
            msix_notify(dev, doe_cap->cap.vec);
        } else if (msi_enabled(dev)) {
            msi_notify(dev, doe_cap->cap.vec);
        }
    }
}

static void pcie_doe_set_ready(DOECap *doe_cap, bool rdy)
{
    doe_cap->status.ready = rdy;

    if (rdy) {
        pcie_doe_irq_assert(doe_cap);
    }
}

static void pcie_doe_set_error(DOECap *doe_cap, bool err)
{
    doe_cap->status.error = err;

    if (err) {
        pcie_doe_irq_assert(doe_cap);
    }
}

/*
 * Check incoming request in write_mbox for protocol format
 */
static void pcie_doe_prepare_rsp(DOECap *doe_cap)
{
    bool success = false;
    int p;
    bool (*handle_request)(DOECap *) = NULL;

    if (doe_cap->status.error) {
        return;
    }

    if (doe_cap->write_mbox[0] ==
        DATA_OBJ_BUILD_HEADER1(PCI_VENDOR_ID_PCI_SIG, PCI_SIG_DOE_DISCOVERY)) {
        handle_request = pcie_doe_discovery;
    } else {
        for (p = 0; p < doe_cap->protocol_num - 1; p++) {
            if (doe_cap->write_mbox[0] ==
                pcie_doe_build_protocol(&doe_cap->protocols[p])) {
                handle_request = doe_cap->protocols[p].handle_request;
                break;
            }
        }
    }

    /*
     * PCIe r6 DOE 6.30.1:
     * If the number of DW transferred does not match the
     * indicated Length for a data object, then the
     * data object must be silently discarded.
     */
    if (handle_request && (doe_cap->write_mbox_len ==
        pcie_doe_get_obj_len(pcie_doe_get_write_mbox_ptr(doe_cap)))) {
        success = handle_request(doe_cap);
    }

    if (success) {
        pcie_doe_set_ready(doe_cap, 1);
    } else {
        pcie_doe_reset_mbox(doe_cap);
    }
}

/*
 * Read from DOE config space.
 * Return false if the address not within DOE_CAP range.
 */
bool pcie_doe_read_config(DOECap *doe_cap, uint32_t addr, int size,
                          uint32_t *buf)
{
    uint32_t shift;
    uint16_t doe_offset = doe_cap->offset;

    if (!range_covers_byte(doe_offset + PCI_EXP_DOE_CAP,
                           PCI_DOE_SIZEOF - 4, addr)) {
        return false;
    }

    addr -= doe_offset;
    *buf = 0;

    if (range_covers_byte(PCI_EXP_DOE_CAP, DWORD_BYTE, addr)) {
        *buf = FIELD_DP32(*buf, PCI_DOE_CAP_REG, INTR_SUPP,
                          doe_cap->cap.intr);
        *buf = FIELD_DP32(*buf, PCI_DOE_CAP_REG, DOE_INTR_MSG_NUM,
                          doe_cap->cap.vec);
    } else if (range_covers_byte(PCI_EXP_DOE_CTRL, DWORD_BYTE, addr)) {
        /* Must return ABORT=0 and GO=0 */
        *buf = FIELD_DP32(*buf, PCI_DOE_CAP_CONTROL, DOE_INTR_EN,
                          doe_cap->ctrl.intr);
    } else if (range_covers_byte(PCI_EXP_DOE_STATUS, DWORD_BYTE, addr)) {
        *buf = FIELD_DP32(*buf, PCI_DOE_CAP_STATUS, DOE_BUSY,
                          doe_cap->status.busy);
        *buf = FIELD_DP32(*buf, PCI_DOE_CAP_STATUS, DOE_INTR_STATUS,
                          doe_cap->status.intr);
        *buf = FIELD_DP32(*buf, PCI_DOE_CAP_STATUS, DOE_ERROR,
                          doe_cap->status.error);
        *buf = FIELD_DP32(*buf, PCI_DOE_CAP_STATUS, DATA_OBJ_RDY,
                          doe_cap->status.ready);
    /* Mailbox should be DW accessed */
    } else if (addr == PCI_EXP_DOE_RD_DATA_MBOX && size == DWORD_BYTE) {
        if (doe_cap->status.ready && !doe_cap->status.error) {
            *buf = doe_cap->read_mbox[doe_cap->read_mbox_idx];
        }
    }

    /* Process Alignment */
    shift = addr % DWORD_BYTE;
    *buf = extract32(*buf, shift * 8, size * 8);

    return true;
}

/*
 * Write to DOE config space.
 * Return if the address not within DOE_CAP range or receives an abort
 */
void pcie_doe_write_config(DOECap *doe_cap,
                           uint32_t addr, uint32_t val, int size)
{
    uint16_t doe_offset = doe_cap->offset;
    uint32_t shift;

    if (!range_covers_byte(doe_offset + PCI_EXP_DOE_CAP,
                           PCI_DOE_SIZEOF - 4, addr)) {
        return;
    }

    /* Process Alignment */
    shift = addr % DWORD_BYTE;
    addr -= (doe_offset + shift);
    val = deposit32(val, shift * 8, size * 8, val);

    switch (addr) {
    case PCI_EXP_DOE_CTRL:
        if (FIELD_EX32(val, PCI_DOE_CAP_CONTROL, DOE_ABORT)) {
            pcie_doe_set_ready(doe_cap, 0);
            pcie_doe_set_error(doe_cap, 0);
            pcie_doe_reset_mbox(doe_cap);
            return;
        }

        if (FIELD_EX32(val, PCI_DOE_CAP_CONTROL, DOE_GO)) {
            pcie_doe_prepare_rsp(doe_cap);
        }

        if (FIELD_EX32(val, PCI_DOE_CAP_CONTROL, DOE_INTR_EN)) {
            doe_cap->ctrl.intr = 1;
        /* Clear interrupt bit located within the first byte */
        } else if (shift == 0) {
            doe_cap->ctrl.intr = 0;
        }
        break;
    case PCI_EXP_DOE_STATUS:
        if (FIELD_EX32(val, PCI_DOE_CAP_STATUS, DOE_INTR_STATUS)) {
            doe_cap->status.intr = 0;
        }
        break;
    case PCI_EXP_DOE_RD_DATA_MBOX:
        /* Mailbox should be DW accessed */
        if (size != DWORD_BYTE) {
            return;
        }
        doe_cap->read_mbox_idx++;
        if (doe_cap->read_mbox_idx == doe_cap->read_mbox_len) {
            pcie_doe_reset_mbox(doe_cap);
            pcie_doe_set_ready(doe_cap, 0);
        } else if (doe_cap->read_mbox_idx > doe_cap->read_mbox_len) {
            /* Underflow */
            pcie_doe_set_error(doe_cap, 1);
        }
        break;
    case PCI_EXP_DOE_WR_DATA_MBOX:
        /* Mailbox should be DW accessed */
        if (size != DWORD_BYTE) {
            return;
        }
        doe_cap->write_mbox[doe_cap->write_mbox_len] = val;
        doe_cap->write_mbox_len++;
        break;
    case PCI_EXP_DOE_CAP:
        /* fallthrough */
    default:
        break;
    }
}
