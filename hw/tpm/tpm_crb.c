/*
 * tpm_crb.c - QEMU's TPM CRB interface emulator
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Authors:
 *   Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * tpm_crb is a device for TPM 2.0 Command Response Buffer (CRB) Interface
 * as defined in TCG PC Client Platform TPM Profile (PTP) Specification
 * Family “2.0” Level 00 Revision 01.03 v22
 */

#include "qemu/osdep.h"

#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "system/address-spaces.h"
#include "hw/core/qdev-properties.h"
#include "hw/pci/pci_ids.h"
#include "hw/acpi/tpm.h"
#include "migration/vmstate.h"
#include "system/tpm_backend.h"
#include "system/tpm_util.h"
#include "system/reset.h"
#include "system/xen.h"
#include "tpm_prop.h"
#include "tpm_ppi.h"
#include "trace.h"
#include "qom/object.h"

struct CRBState {
    DeviceState parent_obj;

    TPMBackend *tpmbe;
    TPMBackendCmd cmd;
    uint32_t regs[TPM_CRB_R_MAX];
    size_t be_buffer_size;
    MemoryRegion mmio;
    MemoryRegion cmdmem;

    GByteArray *command_buffer;
    GByteArray *response_buffer;
    uint32_t response_offset;

    TPMPPI ppi;

    bool cap_chunk;
};
typedef struct CRBState CRBState;

DECLARE_INSTANCE_CHECKER(CRBState, CRB,
                         TYPE_TPM_CRB)

#define CRB_INTF_TYPE_CRB_ACTIVE 0b1
#define CRB_INTF_VERSION_CRB 0b1
#define CRB_INTF_CAP_LOCALITY_0_ONLY 0b0
#define CRB_INTF_CAP_IDLE_FAST 0b0
#define CRB_INTF_CAP_XFER_SIZE_64 0b11
#define CRB_INTF_CAP_FIFO_NOT_SUPPORTED 0b0
#define CRB_INTF_CAP_CRB_SUPPORTED 0b1
#define CRB_INTF_IF_SELECTOR_CRB 0b1
#define CRB_INTF_CAP_CRB_CHUNK 0b1

#define CRB_CTRL_CMD_SIZE (TPM_CRB_ADDR_SIZE - A_CRB_DATA_BUFFER)
#define TPM_HEADER_SIZE 10

enum crb_loc_ctrl {
    CRB_LOC_CTRL_REQUEST_ACCESS = BIT(0),
    CRB_LOC_CTRL_RELINQUISH = BIT(1),
    CRB_LOC_CTRL_SEIZE = BIT(2),
    CRB_LOC_CTRL_RESET_ESTABLISHMENT_BIT = BIT(3),
};

enum crb_ctrl_req {
    CRB_CTRL_REQ_CMD_READY = BIT(0),
    CRB_CTRL_REQ_GO_IDLE = BIT(1),
};

enum crb_start {
    CRB_START_INVOKE = BIT(0),
    CRB_START_RSP_RETRY = BIT(1),
    CRB_START_NEXT_CHUNK = BIT(2),
};

enum crb_cancel {
    CRB_CANCEL_INVOKE = BIT(0),
};

#define TPM_CRB_NO_LOCALITY 0xff

static void tpm_crb_clear_internal_buffers(CRBState *s)
{
    g_byte_array_set_size(s->response_buffer, 0);
    g_byte_array_set_size(s->command_buffer, 0);
    s->response_offset = 0;
}

static uint64_t tpm_crb_mmio_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    CRBState *s = CRB(opaque);
    void *regs = (void *)&s->regs + (addr & ~3);
    unsigned offset = addr & 3;
    uint32_t val = *(uint32_t *)regs >> (8 * offset);

    switch (addr) {
    case A_CRB_LOC_STATE:
        val |= !tpm_backend_get_tpm_established_flag(s->tpmbe);
        break;
    }

    trace_tpm_crb_mmio_read(addr, size, val);

    return val;
}

static uint8_t tpm_crb_get_active_locty(CRBState *s)
{
    if (!ARRAY_FIELD_EX32(s->regs, CRB_LOC_STATE, locAssigned)) {
        return TPM_CRB_NO_LOCALITY;
    }
    return ARRAY_FIELD_EX32(s->regs, CRB_LOC_STATE, activeLocality);
}

static bool tpm_crb_append_command_request(CRBState *s)
{
    /*
     * The linux guest writes the TPM command to the MMIO region in chunks.
     * This function appends a chunk from the MMIO region to internal
     * command_buffer.
     */
    void *mem = memory_region_get_ram_ptr(&s->cmdmem);
    uint32_t to_copy = 0;
    uint32_t total_request_size = 0;

    /*
     * The initial call extracts the total TPM command size
     * from its header. For the subsequent calls, the data already
     * appended in the command_buffer is used to calculate the total
     * size, as its header stays the same.
     */
    if (s->command_buffer->len == 0) {
        total_request_size = tpm_cmd_get_size(mem);
        if (total_request_size < TPM_HEADER_SIZE) {
            ARRAY_FIELD_DP32(s->regs, CRB_CTRL_STS, tpmSts, 1);
            ARRAY_FIELD_DP32(s->regs, CRB_CTRL_START, Start, 0);
            ARRAY_FIELD_DP32(s->regs, CRB_CTRL_START, nextChunk, 0);
            tpm_crb_clear_internal_buffers(s);
            error_report("Command size %" PRIu32 " less than "
                         "TPM header size %" PRIu32,
                         total_request_size, (uint32_t)TPM_HEADER_SIZE);
            return false;
        }
    } else {
        total_request_size = tpm_cmd_get_size(s->command_buffer->data);
    }
    total_request_size = MIN(total_request_size, s->be_buffer_size);

    if (total_request_size > s->command_buffer->len) {
        uint32_t remaining = total_request_size - s->command_buffer->len;
        to_copy = MIN(remaining, CRB_CTRL_CMD_SIZE);
        g_byte_array_append(s->command_buffer, (guint8 *)mem, to_copy);
    }
    return true;
}

static void tpm_crb_fill_command_response(CRBState *s)
{
    /*
     * Response from the tpm backend will be stored in the internal
     * response_buffer. This function will serve that accumulated response
     * to the linux guest in chunks by writing it back to MMIO region.
     */
    void *mem = memory_region_get_ram_ptr(&s->cmdmem);
    uint32_t remaining = s->response_buffer->len - s->response_offset;
    uint32_t to_copy = MIN(CRB_CTRL_CMD_SIZE, remaining);

    memcpy(mem, s->response_buffer->data + s->response_offset, to_copy);

    if (to_copy < CRB_CTRL_CMD_SIZE) {
        memset((guint8 *)mem + to_copy, 0, CRB_CTRL_CMD_SIZE - to_copy);
    }

    s->response_offset += to_copy;
    memory_region_set_dirty(&s->cmdmem, 0, CRB_CTRL_CMD_SIZE);
}

static void tpm_crb_mmio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    CRBState *s = CRB(opaque);
    uint8_t locty =  addr >> 12;

    trace_tpm_crb_mmio_write(addr, size, val);

    switch (addr) {
    case A_CRB_CTRL_REQ:
        switch (val) {
        case CRB_CTRL_REQ_CMD_READY:
            ARRAY_FIELD_DP32(s->regs, CRB_CTRL_STS,
                             tpmIdle, 0);
            break;
        case CRB_CTRL_REQ_GO_IDLE:
            ARRAY_FIELD_DP32(s->regs, CRB_CTRL_STS,
                             tpmIdle, 1);
            break;
        }
        break;
    case A_CRB_CTRL_CANCEL:
        if (val == CRB_CANCEL_INVOKE) {
            if (s->regs[R_CRB_CTRL_START] & CRB_START_INVOKE) {
                tpm_backend_cancel_cmd(s->tpmbe);
            }
            tpm_crb_clear_internal_buffers(s);
        }
        break;
    case A_CRB_CTRL_START:
        if (tpm_crb_get_active_locty(s) != locty) {
            break;
        }
        if (s->regs[R_CRB_CTRL_START] & CRB_START_INVOKE) {
            /*
             * Backend TPM is busy processing a request.
             */
            break;
        }
        if (val & CRB_START_INVOKE) {
            if (!tpm_crb_append_command_request(s)) {
                break;
            }
            ARRAY_FIELD_DP32(s->regs, CRB_CTRL_START, Start, 1);
            g_byte_array_set_size(s->response_buffer, s->be_buffer_size);
            s->cmd = (TPMBackendCmd) {
                .in = s->command_buffer->data,
                .in_len = s->command_buffer->len,
                .out = s->response_buffer->data,
                .out_len = s->response_buffer->len,
            };
            tpm_backend_deliver_request(s->tpmbe, &s->cmd);
        } else if (val & CRB_START_NEXT_CHUNK) {
            if (!s->cap_chunk) {
                break;
            }
            /*
             * nextChunk is used both while sending and receiving data.
             * To distinguish between the two, response_buffer is checked.
             * If it does not have data, then that means we have not yet
             * sent the command to the tpm backend, and therefore call
             * tpm_crb_append_command_request().
             */
            if (s->response_buffer->len > 0 &&
                s->response_offset < s->response_buffer->len) {
                tpm_crb_fill_command_response(s);
            } else {
                if (!tpm_crb_append_command_request(s)) {
                    break;
                }
            }
            ARRAY_FIELD_DP32(s->regs, CRB_CTRL_START, nextChunk, 0);
        } else if (val & CRB_START_RSP_RETRY) {
            if (!s->cap_chunk) {
                break;
            }
            if (s->response_buffer->len > 0) {
                s->response_offset = 0;
                tpm_crb_fill_command_response(s);
            }
            ARRAY_FIELD_DP32(s->regs, CRB_CTRL_START, crbRspRetry, 0);
            ARRAY_FIELD_DP32(s->regs, CRB_CTRL_START, nextChunk, 0);
        }
        break;
    case A_CRB_LOC_CTRL:
        switch (val) {
        case CRB_LOC_CTRL_RESET_ESTABLISHMENT_BIT:
            /* not loc 3 or 4 */
            break;
        case CRB_LOC_CTRL_RELINQUISH:
            ARRAY_FIELD_DP32(s->regs, CRB_LOC_STATE,
                             locAssigned, 0);
            ARRAY_FIELD_DP32(s->regs, CRB_LOC_STS,
                             Granted, 0);
            break;
        case CRB_LOC_CTRL_REQUEST_ACCESS:
            ARRAY_FIELD_DP32(s->regs, CRB_LOC_STS,
                             Granted, 1);
            ARRAY_FIELD_DP32(s->regs, CRB_LOC_STS,
                             beenSeized, 0);
            ARRAY_FIELD_DP32(s->regs, CRB_LOC_STATE,
                             locAssigned, 1);
            break;
        }
        break;
    }
}

static const MemoryRegionOps tpm_crb_memory_ops = {
    .read = tpm_crb_mmio_read,
    .write = tpm_crb_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void tpm_crb_request_completed(TPMIf *ti, int ret)
{
    CRBState *s = CRB(ti);

    ARRAY_FIELD_DP32(s->regs, CRB_CTRL_START, Start, 0);
    if (ret != 0) {
        ARRAY_FIELD_DP32(s->regs, CRB_CTRL_STS,
                         tpmSts, 1); /* fatal error */
        tpm_crb_clear_internal_buffers(s);
    } else {
        uint32_t actual_resp_size = tpm_cmd_get_size(s->response_buffer->data);
        uint32_t total_resp_size = MIN(actual_resp_size, s->be_buffer_size);
        g_byte_array_set_size(s->response_buffer, total_resp_size);
        s->response_offset = 0;
    }
    /*
     * Send the first chunk. Subsequent chunks will be sent
     * on receiving nextChunk from the guest
     */
    tpm_crb_fill_command_response(s);
    ARRAY_FIELD_DP32(s->regs, CRB_CTRL_START, nextChunk, 0);
    ARRAY_FIELD_DP32(s->regs, CRB_CTRL_START, crbRspRetry, 0);
    g_byte_array_set_size(s->command_buffer, 0);
}

static enum TPMVersion tpm_crb_get_version(TPMIf *ti)
{
    CRBState *s = CRB(ti);

    return tpm_backend_get_tpm_version(s->tpmbe);
}

static int tpm_crb_pre_save(void *opaque)
{
    CRBState *s = opaque;

    tpm_backend_finish_sync(s->tpmbe);

    return 0;
}

static const VMStateDescription vmstate_tpm_crb = {
    .name = "tpm-crb",
    .pre_save = tpm_crb_pre_save,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, CRBState, TPM_CRB_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static const Property tpm_crb_properties[] = {
    DEFINE_PROP_TPMBE("tpmdev", CRBState, tpmbe),
    DEFINE_PROP_BOOL("cap-chunk", CRBState, cap_chunk, true),
};

static void tpm_crb_reset(void *dev)
{
    CRBState *s = CRB(dev);

    tpm_ppi_reset(&s->ppi);
    tpm_backend_reset(s->tpmbe);
    tpm_crb_clear_internal_buffers(s);

    memset(s->regs, 0, sizeof(s->regs));

    ARRAY_FIELD_DP32(s->regs, CRB_LOC_STATE,
                     tpmRegValidSts, 1);
    ARRAY_FIELD_DP32(s->regs, CRB_CTRL_STS,
                     tpmIdle, 1);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     InterfaceType, CRB_INTF_TYPE_CRB_ACTIVE);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     InterfaceVersion, CRB_INTF_VERSION_CRB);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     CapLocality, CRB_INTF_CAP_LOCALITY_0_ONLY);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     CapCRBIdleBypass, CRB_INTF_CAP_IDLE_FAST);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     CapDataXferSizeSupport, CRB_INTF_CAP_XFER_SIZE_64);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     CapFIFO, CRB_INTF_CAP_FIFO_NOT_SUPPORTED);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     CapCRB, CRB_INTF_CAP_CRB_SUPPORTED);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     InterfaceSelector, CRB_INTF_IF_SELECTOR_CRB);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     CapCRBChunk, s->cap_chunk ? CRB_INTF_CAP_CRB_CHUNK : 0);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID,
                     RID, 0b0000);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID2,
                     VID, PCI_VENDOR_ID_IBM);

    s->regs[R_CRB_CTRL_CMD_SIZE] = CRB_CTRL_CMD_SIZE;
    s->regs[R_CRB_CTRL_CMD_LADDR] = TPM_CRB_ADDR_BASE + A_CRB_DATA_BUFFER;
    s->regs[R_CRB_CTRL_RSP_SIZE] = CRB_CTRL_CMD_SIZE;
    s->regs[R_CRB_CTRL_RSP_ADDR] = TPM_CRB_ADDR_BASE + A_CRB_DATA_BUFFER;

    s->be_buffer_size = tpm_backend_get_buffer_size(s->tpmbe);

    if (tpm_backend_startup_tpm(s->tpmbe, s->be_buffer_size) < 0) {
        exit(1);
    }
}

static void tpm_crb_realize(DeviceState *dev, Error **errp)
{
    CRBState *s = CRB(dev);

    if (!tpm_find()) {
        error_setg(errp, "at most one TPM device is permitted");
        return;
    }
    if (!s->tpmbe) {
        error_setg(errp, "'tpmdev' property is required");
        return;
    }

    memory_region_init_io(&s->mmio, OBJECT(s), &tpm_crb_memory_ops, s,
        "tpm-crb-mmio", sizeof(s->regs));
    memory_region_init_ram(&s->cmdmem, OBJECT(s),
        "tpm-crb-cmd", CRB_CTRL_CMD_SIZE, errp);

    memory_region_add_subregion(get_system_memory(),
        TPM_CRB_ADDR_BASE, &s->mmio);
    memory_region_add_subregion(get_system_memory(),
        TPM_CRB_ADDR_BASE + sizeof(s->regs), &s->cmdmem);

    s->command_buffer = g_byte_array_new();
    s->response_buffer = g_byte_array_new();

    tpm_ppi_init(&s->ppi, get_system_memory(),
                 TPM_PPI_ADDR_BASE, OBJECT(s));

    if (xen_enabled()) {
        tpm_crb_reset(dev);
    } else {
        qemu_register_reset(tpm_crb_reset, dev);
    }
}

static void tpm_crb_unrealize(DeviceState *dev)
{
    CRBState *s = CRB(dev);

    g_clear_pointer(&s->command_buffer, g_byte_array_unref);
    g_clear_pointer(&s->response_buffer, g_byte_array_unref);
}

static void tpm_crb_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);

    dc->realize = tpm_crb_realize;
    dc->unrealize = tpm_crb_unrealize;
    device_class_set_props(dc, tpm_crb_properties);
    dc->vmsd  = &vmstate_tpm_crb;
    dc->user_creatable = true;
    tc->model = TPM_MODEL_TPM_CRB;
    tc->ppi_enabled = true;
    tc->get_version = tpm_crb_get_version;
    tc->request_completed = tpm_crb_request_completed;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo tpm_crb_info = {
    .name = TYPE_TPM_CRB,
    /* could be TYPE_SYS_BUS_DEVICE (or LPC etc) */
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(CRBState),
    .class_init  = tpm_crb_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { }
    }
};

static void tpm_crb_register(void)
{
    type_register_static(&tpm_crb_info);
}

type_init(tpm_crb_register)
