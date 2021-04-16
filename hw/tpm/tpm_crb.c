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
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/pci/pci_ids.h"
#include "hw/acpi/tpm.h"
#include "migration/vmstate.h"
#include "sysemu/tpm_backend.h"
#include "sysemu/tpm_util.h"
#include "sysemu/reset.h"
#include "tpm_prop.h"
#include "tpm_ppi.h"
#include "trace.h"
#include "qom/object.h"

struct CRBState {
    DeviceState parent_obj;

    TPMBackend *tpmbe;
    TPMBackendCmd cmd;
    uint32_t regs[TPM_CRB_R_MAX];
    MemoryRegion mmio;
    MemoryRegion cmdmem;

    size_t be_buffer_size;

    bool ppi_enabled;
    TPMPPI ppi;
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

#define CRB_CTRL_CMD_SIZE (TPM_CRB_ADDR_SIZE - A_CRB_DATA_BUFFER)

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
};

enum crb_cancel {
    CRB_CANCEL_INVOKE = BIT(0),
};

#define TPM_CRB_NO_LOCALITY 0xff

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
        if (val == CRB_CANCEL_INVOKE &&
            s->regs[R_CRB_CTRL_START] & CRB_START_INVOKE) {
            tpm_backend_cancel_cmd(s->tpmbe);
        }
        break;
    case A_CRB_CTRL_START:
        if (val == CRB_START_INVOKE &&
            !(s->regs[R_CRB_CTRL_START] & CRB_START_INVOKE) &&
            tpm_crb_get_active_locty(s) == locty) {
            void *mem = memory_region_get_ram_ptr(&s->cmdmem);

            s->regs[R_CRB_CTRL_START] |= CRB_START_INVOKE;
            s->cmd = (TPMBackendCmd) {
                .in = mem,
                .in_len = MIN(tpm_cmd_get_size(mem), s->be_buffer_size),
                .out = mem,
                .out_len = s->be_buffer_size,
            };

            tpm_backend_deliver_request(s->tpmbe, &s->cmd);
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

    s->regs[R_CRB_CTRL_START] &= ~CRB_START_INVOKE;
    if (ret != 0) {
        ARRAY_FIELD_DP32(s->regs, CRB_CTRL_STS,
                         tpmSts, 1); /* fatal error */
    }
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
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, CRBState, TPM_CRB_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static Property tpm_crb_properties[] = {
    DEFINE_PROP_TPMBE("tpmdev", CRBState, tpmbe),
    DEFINE_PROP_BOOL("ppi", CRBState, ppi_enabled, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void tpm_crb_reset(void *dev)
{
    CRBState *s = CRB(dev);

    if (s->ppi_enabled) {
        tpm_ppi_reset(&s->ppi);
    }
    tpm_backend_reset(s->tpmbe);

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
                     RID, 0b0000);
    ARRAY_FIELD_DP32(s->regs, CRB_INTF_ID2,
                     VID, PCI_VENDOR_ID_IBM);

    s->regs[R_CRB_CTRL_CMD_SIZE] = CRB_CTRL_CMD_SIZE;
    s->regs[R_CRB_CTRL_CMD_LADDR] = TPM_CRB_ADDR_BASE + A_CRB_DATA_BUFFER;
    s->regs[R_CRB_CTRL_RSP_SIZE] = CRB_CTRL_CMD_SIZE;
    s->regs[R_CRB_CTRL_RSP_ADDR] = TPM_CRB_ADDR_BASE + A_CRB_DATA_BUFFER;

    s->be_buffer_size = MIN(tpm_backend_get_buffer_size(s->tpmbe),
                            CRB_CTRL_CMD_SIZE);

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

    if (s->ppi_enabled) {
        tpm_ppi_init(&s->ppi, get_system_memory(),
                     TPM_PPI_ADDR_BASE, OBJECT(s));
    }

    qemu_register_reset(tpm_crb_reset, dev);
}

static void tpm_crb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);

    dc->realize = tpm_crb_realize;
    device_class_set_props(dc, tpm_crb_properties);
    dc->vmsd  = &vmstate_tpm_crb;
    dc->user_creatable = true;
    tc->model = TPM_MODEL_TPM_CRB;
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
    .interfaces = (InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { }
    }
};

static void tpm_crb_register(void)
{
    type_register_static(&tpm_crb_info);
}

type_init(tpm_crb_register)
