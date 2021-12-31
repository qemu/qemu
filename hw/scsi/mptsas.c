/*
 * QEMU LSI SAS1068 Host Bus Adapter emulation
 * Based on the QEMU Megaraid emulator
 *
 * Copyright (c) 2009-2012 Hannes Reinecke, SUSE Labs
 * Copyright (c) 2012 Verizon, Inc.
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * Authors: Don Slutz, Paolo Bonzini
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "sysemu/dma.h"
#include "hw/pci/msi.h"
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "hw/scsi/scsi.h"
#include "scsi/constants.h"
#include "trace.h"
#include "qapi/error.h"
#include "mptsas.h"
#include "migration/qemu-file-types.h"
#include "migration/vmstate.h"
#include "mpi.h"

#define NAA_LOCALLY_ASSIGNED_ID 0x3ULL
#define IEEE_COMPANY_LOCALLY_ASSIGNED 0x525400

#define MPTSAS1068_PRODUCT_ID                  \
    (MPI_FW_HEADER_PID_FAMILY_1068_SAS |       \
     MPI_FW_HEADER_PID_PROD_INITIATOR_SCSI |   \
     MPI_FW_HEADER_PID_TYPE_SAS)

struct MPTSASRequest {
    MPIMsgSCSIIORequest scsi_io;
    SCSIRequest *sreq;
    QEMUSGList qsg;
    MPTSASState *dev;

    QTAILQ_ENTRY(MPTSASRequest) next;
};

static void mptsas_update_interrupt(MPTSASState *s)
{
    PCIDevice *pci = (PCIDevice *) s;
    uint32_t state = s->intr_status & ~(s->intr_mask | MPI_HIS_IOP_DOORBELL_STATUS);

    if (msi_enabled(pci)) {
        if (state) {
            trace_mptsas_irq_msi(s);
            msi_notify(pci, 0);
        }
    }

    trace_mptsas_irq_intx(s, !!state);
    pci_set_irq(pci, !!state);
}

static void mptsas_set_fault(MPTSASState *s, uint32_t code)
{
    if ((s->state & MPI_IOC_STATE_FAULT) == 0) {
        s->state = MPI_IOC_STATE_FAULT | code;
    }
}

#define MPTSAS_FIFO_INVALID(s, name)                     \
    ((s)->name##_head > ARRAY_SIZE((s)->name) ||         \
     (s)->name##_tail > ARRAY_SIZE((s)->name))

#define MPTSAS_FIFO_EMPTY(s, name)                       \
    ((s)->name##_head == (s)->name##_tail)

#define MPTSAS_FIFO_FULL(s, name)                        \
    ((s)->name##_head == ((s)->name##_tail + 1) % ARRAY_SIZE((s)->name))

#define MPTSAS_FIFO_GET(s, name) ({                      \
    uint32_t _val = (s)->name[(s)->name##_head++];       \
    (s)->name##_head %= ARRAY_SIZE((s)->name);           \
    _val;                                                \
})

#define MPTSAS_FIFO_PUT(s, name, val) do {       \
    (s)->name[(s)->name##_tail++] = (val);       \
    (s)->name##_tail %= ARRAY_SIZE((s)->name);   \
} while(0)

static void mptsas_post_reply(MPTSASState *s, MPIDefaultReply *reply)
{
    PCIDevice *pci = (PCIDevice *) s;
    uint32_t addr_lo;

    if (MPTSAS_FIFO_EMPTY(s, reply_free) || MPTSAS_FIFO_FULL(s, reply_post)) {
        mptsas_set_fault(s, MPI_IOCSTATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    addr_lo = MPTSAS_FIFO_GET(s, reply_free);

    pci_dma_write(pci, addr_lo | s->host_mfa_high_addr, reply,
                  MIN(s->reply_frame_size, 4 * reply->MsgLength));

    MPTSAS_FIFO_PUT(s, reply_post, MPI_ADDRESS_REPLY_A_BIT | (addr_lo >> 1));

    s->intr_status |= MPI_HIS_REPLY_MESSAGE_INTERRUPT;
    if (s->doorbell_state == DOORBELL_WRITE) {
        s->doorbell_state = DOORBELL_NONE;
        s->intr_status |= MPI_HIS_DOORBELL_INTERRUPT;
    }
    mptsas_update_interrupt(s);
}

void mptsas_reply(MPTSASState *s, MPIDefaultReply *reply)
{
    if (s->doorbell_state == DOORBELL_WRITE) {
        /* The reply is sent out in 16 bit chunks, while the size
         * in the reply is in 32 bit units.
         */
        s->doorbell_state = DOORBELL_READ;
        s->doorbell_reply_idx = 0;
        s->doorbell_reply_size = reply->MsgLength * 2;
        memcpy(s->doorbell_reply, reply, s->doorbell_reply_size * 2);
        s->intr_status |= MPI_HIS_DOORBELL_INTERRUPT;
        mptsas_update_interrupt(s);
    } else {
        mptsas_post_reply(s, reply);
    }
}

static void mptsas_turbo_reply(MPTSASState *s, uint32_t msgctx)
{
    if (MPTSAS_FIFO_FULL(s, reply_post)) {
        mptsas_set_fault(s, MPI_IOCSTATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    /* The reply is just the message context ID (bit 31 = clear). */
    MPTSAS_FIFO_PUT(s, reply_post, msgctx);

    s->intr_status |= MPI_HIS_REPLY_MESSAGE_INTERRUPT;
    mptsas_update_interrupt(s);
}

#define MPTSAS_MAX_REQUEST_SIZE 52

static const int mpi_request_sizes[] = {
    [MPI_FUNCTION_SCSI_IO_REQUEST]    = sizeof(MPIMsgSCSIIORequest),
    [MPI_FUNCTION_SCSI_TASK_MGMT]     = sizeof(MPIMsgSCSITaskMgmt),
    [MPI_FUNCTION_IOC_INIT]           = sizeof(MPIMsgIOCInit),
    [MPI_FUNCTION_IOC_FACTS]          = sizeof(MPIMsgIOCFacts),
    [MPI_FUNCTION_CONFIG]             = sizeof(MPIMsgConfig),
    [MPI_FUNCTION_PORT_FACTS]         = sizeof(MPIMsgPortFacts),
    [MPI_FUNCTION_PORT_ENABLE]        = sizeof(MPIMsgPortEnable),
    [MPI_FUNCTION_EVENT_NOTIFICATION] = sizeof(MPIMsgEventNotify),
};

static dma_addr_t mptsas_ld_sg_base(MPTSASState *s, uint32_t flags_and_length,
                                    dma_addr_t *sgaddr)
{
    const MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    PCIDevice *pci = (PCIDevice *) s;
    dma_addr_t addr;

    if (flags_and_length & MPI_SGE_FLAGS_64_BIT_ADDRESSING) {
        uint64_t addr64;

        ldq_le_pci_dma(pci, *sgaddr + 4, &addr64, attrs);
        addr = addr64;
        *sgaddr += 12;
    } else {
        uint32_t addr32;

        ldl_le_pci_dma(pci, *sgaddr + 4, &addr32, attrs);
        addr = addr32;
        *sgaddr += 8;
    }
    return addr;
}

static int mptsas_build_sgl(MPTSASState *s, MPTSASRequest *req, hwaddr addr)
{
    PCIDevice *pci = (PCIDevice *) s;
    hwaddr next_chain_addr;
    uint32_t left;
    hwaddr sgaddr;
    uint32_t chain_offset;

    chain_offset = req->scsi_io.ChainOffset;
    next_chain_addr = addr + chain_offset * sizeof(uint32_t);
    sgaddr = addr + sizeof(MPIMsgSCSIIORequest);
    pci_dma_sglist_init(&req->qsg, pci, 4);
    left = req->scsi_io.DataLength;

    for(;;) {
        dma_addr_t addr, len;
        uint32_t flags_and_length;

        ldl_le_pci_dma(pci, sgaddr, &flags_and_length, MEMTXATTRS_UNSPECIFIED);
        len = flags_and_length & MPI_SGE_LENGTH_MASK;
        if ((flags_and_length & MPI_SGE_FLAGS_ELEMENT_TYPE_MASK)
            != MPI_SGE_FLAGS_SIMPLE_ELEMENT ||
            (!len &&
             !(flags_and_length & MPI_SGE_FLAGS_END_OF_LIST) &&
             !(flags_and_length & MPI_SGE_FLAGS_END_OF_BUFFER))) {
            return MPI_IOCSTATUS_INVALID_SGL;
        }

        len = MIN(len, left);
        if (!len) {
            /* We reached the desired transfer length, ignore extra
             * elements of the s/g list.
             */
            break;
        }

        addr = mptsas_ld_sg_base(s, flags_and_length, &sgaddr);
        qemu_sglist_add(&req->qsg, addr, len);
        left -= len;

        if (flags_and_length & MPI_SGE_FLAGS_END_OF_LIST) {
            break;
        }

        if (flags_and_length & MPI_SGE_FLAGS_LAST_ELEMENT) {
            if (!chain_offset) {
                break;
            }

            ldl_le_pci_dma(pci, next_chain_addr, &flags_and_length,
                           MEMTXATTRS_UNSPECIFIED);
            if ((flags_and_length & MPI_SGE_FLAGS_ELEMENT_TYPE_MASK)
                != MPI_SGE_FLAGS_CHAIN_ELEMENT) {
                return MPI_IOCSTATUS_INVALID_SGL;
            }

            sgaddr = mptsas_ld_sg_base(s, flags_and_length, &next_chain_addr);
            chain_offset =
                (flags_and_length & MPI_SGE_CHAIN_OFFSET_MASK) >> MPI_SGE_CHAIN_OFFSET_SHIFT;
            next_chain_addr = sgaddr + chain_offset * sizeof(uint32_t);
        }
    }
    return 0;
}

static void mptsas_free_request(MPTSASRequest *req)
{
    if (req->sreq != NULL) {
        req->sreq->hba_private = NULL;
        scsi_req_unref(req->sreq);
        req->sreq = NULL;
    }
    qemu_sglist_destroy(&req->qsg);
    g_free(req);
}

static int mptsas_scsi_device_find(MPTSASState *s, int bus, int target,
                                   uint8_t *lun, SCSIDevice **sdev)
{
    if (bus != 0) {
        return MPI_IOCSTATUS_SCSI_INVALID_BUS;
    }

    if (target >= s->max_devices) {
        return MPI_IOCSTATUS_SCSI_INVALID_TARGETID;
    }

    *sdev = scsi_device_find(&s->bus, bus, target, lun[1]);
    if (!*sdev) {
        return MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE;
    }

    return 0;
}

static int mptsas_process_scsi_io_request(MPTSASState *s,
                                          MPIMsgSCSIIORequest *scsi_io,
                                          hwaddr addr)
{
    MPTSASRequest *req;
    MPIMsgSCSIIOReply reply;
    SCSIDevice *sdev;
    int status;

    mptsas_fix_scsi_io_endianness(scsi_io);

    trace_mptsas_process_scsi_io_request(s, scsi_io->Bus, scsi_io->TargetID,
                                         scsi_io->LUN[1], scsi_io->DataLength);

    status = mptsas_scsi_device_find(s, scsi_io->Bus, scsi_io->TargetID,
                                     scsi_io->LUN, &sdev);
    if (status) {
        goto bad;
    }

    req = g_new0(MPTSASRequest, 1);
    req->scsi_io = *scsi_io;
    req->dev = s;

    status = mptsas_build_sgl(s, req, addr);
    if (status) {
        goto free_bad;
    }

    if (req->qsg.size < scsi_io->DataLength) {
        trace_mptsas_sgl_overflow(s, scsi_io->MsgContext, scsi_io->DataLength,
                                  req->qsg.size);
        status = MPI_IOCSTATUS_INVALID_SGL;
        goto free_bad;
    }

    req->sreq = scsi_req_new(sdev, scsi_io->MsgContext,
                            scsi_io->LUN[1], scsi_io->CDB, req);

    if (req->sreq->cmd.xfer > scsi_io->DataLength) {
        goto overrun;
    }
    switch (scsi_io->Control & MPI_SCSIIO_CONTROL_DATADIRECTION_MASK) {
    case MPI_SCSIIO_CONTROL_NODATATRANSFER:
        if (req->sreq->cmd.mode != SCSI_XFER_NONE) {
            goto overrun;
        }
        break;

    case MPI_SCSIIO_CONTROL_WRITE:
        if (req->sreq->cmd.mode != SCSI_XFER_TO_DEV) {
            goto overrun;
        }
        break;

    case MPI_SCSIIO_CONTROL_READ:
        if (req->sreq->cmd.mode != SCSI_XFER_FROM_DEV) {
            goto overrun;
        }
        break;
    }

    if (scsi_req_enqueue(req->sreq)) {
        scsi_req_continue(req->sreq);
    }
    return 0;

overrun:
    trace_mptsas_scsi_overflow(s, scsi_io->MsgContext, req->sreq->cmd.xfer,
                               scsi_io->DataLength);
    status = MPI_IOCSTATUS_SCSI_DATA_OVERRUN;
free_bad:
    mptsas_free_request(req);
bad:
    memset(&reply, 0, sizeof(reply));
    reply.TargetID          = scsi_io->TargetID;
    reply.Bus               = scsi_io->Bus;
    reply.MsgLength         = sizeof(reply) / 4;
    reply.Function          = scsi_io->Function;
    reply.CDBLength         = scsi_io->CDBLength;
    reply.SenseBufferLength = scsi_io->SenseBufferLength;
    reply.MsgContext        = scsi_io->MsgContext;
    reply.SCSIState         = MPI_SCSI_STATE_NO_SCSI_STATUS;
    reply.IOCStatus         = status;

    mptsas_fix_scsi_io_reply_endianness(&reply);
    mptsas_reply(s, (MPIDefaultReply *)&reply);

    return 0;
}

typedef struct {
    Notifier                notifier;
    MPTSASState             *s;
    MPIMsgSCSITaskMgmtReply *reply;
} MPTSASCancelNotifier;

static void mptsas_cancel_notify(Notifier *notifier, void *data)
{
    MPTSASCancelNotifier *n = container_of(notifier,
                                           MPTSASCancelNotifier,
                                           notifier);

    /* Abusing IOCLogInfo to store the expected number of requests... */
    if (++n->reply->TerminationCount == n->reply->IOCLogInfo) {
        n->reply->IOCLogInfo = 0;
        mptsas_fix_scsi_task_mgmt_reply_endianness(n->reply);
        mptsas_post_reply(n->s, (MPIDefaultReply *)n->reply);
        g_free(n->reply);
    }
    g_free(n);
}

static void mptsas_process_scsi_task_mgmt(MPTSASState *s, MPIMsgSCSITaskMgmt *req)
{
    MPIMsgSCSITaskMgmtReply reply;
    MPIMsgSCSITaskMgmtReply *reply_async;
    int status, count;
    SCSIDevice *sdev;
    SCSIRequest *r, *next;
    BusChild *kid;

    mptsas_fix_scsi_task_mgmt_endianness(req);

    QEMU_BUILD_BUG_ON(MPTSAS_MAX_REQUEST_SIZE < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_msg) < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_reply) < sizeof(reply));

    memset(&reply, 0, sizeof(reply));
    reply.TargetID   = req->TargetID;
    reply.Bus        = req->Bus;
    reply.MsgLength  = sizeof(reply) / 4;
    reply.Function   = req->Function;
    reply.TaskType   = req->TaskType;
    reply.MsgContext = req->MsgContext;

    switch (req->TaskType) {
    case MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK:
    case MPI_SCSITASKMGMT_TASKTYPE_QUERY_TASK:
        status = mptsas_scsi_device_find(s, req->Bus, req->TargetID,
                                         req->LUN, &sdev);
        if (status) {
            reply.IOCStatus = status;
            goto out;
        }
        if (sdev->lun != req->LUN[1]) {
            reply.ResponseCode = MPI_SCSITASKMGMT_RSP_TM_INVALID_LUN;
            goto out;
        }

        QTAILQ_FOREACH_SAFE(r, &sdev->requests, next, next) {
            MPTSASRequest *cmd_req = r->hba_private;
            if (cmd_req && cmd_req->scsi_io.MsgContext == req->TaskMsgContext) {
                break;
            }
        }
        if (r) {
            /*
             * Assert that the request has not been completed yet, we
             * check for it in the loop above.
             */
            assert(r->hba_private);
            if (req->TaskType == MPI_SCSITASKMGMT_TASKTYPE_QUERY_TASK) {
                /* "If the specified command is present in the task set, then
                 * return a service response set to FUNCTION SUCCEEDED".
                 */
                reply.ResponseCode = MPI_SCSITASKMGMT_RSP_TM_SUCCEEDED;
            } else {
                MPTSASCancelNotifier *notifier;

                reply_async = g_memdup(&reply, sizeof(MPIMsgSCSITaskMgmtReply));
                reply_async->IOCLogInfo = INT_MAX;

                count = 1;
                notifier = g_new(MPTSASCancelNotifier, 1);
                notifier->s = s;
                notifier->reply = reply_async;
                notifier->notifier.notify = mptsas_cancel_notify;
                scsi_req_cancel_async(r, &notifier->notifier);
                goto reply_maybe_async;
            }
        }
        break;

    case MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET:
    case MPI_SCSITASKMGMT_TASKTYPE_CLEAR_TASK_SET:
        status = mptsas_scsi_device_find(s, req->Bus, req->TargetID,
                                         req->LUN, &sdev);
        if (status) {
            reply.IOCStatus = status;
            goto out;
        }
        if (sdev->lun != req->LUN[1]) {
            reply.ResponseCode = MPI_SCSITASKMGMT_RSP_TM_INVALID_LUN;
            goto out;
        }

        reply_async = g_memdup(&reply, sizeof(MPIMsgSCSITaskMgmtReply));
        reply_async->IOCLogInfo = INT_MAX;

        count = 0;
        QTAILQ_FOREACH_SAFE(r, &sdev->requests, next, next) {
            if (r->hba_private) {
                MPTSASCancelNotifier *notifier;

                count++;
                notifier = g_new(MPTSASCancelNotifier, 1);
                notifier->s = s;
                notifier->reply = reply_async;
                notifier->notifier.notify = mptsas_cancel_notify;
                scsi_req_cancel_async(r, &notifier->notifier);
            }
        }

reply_maybe_async:
        if (reply_async->TerminationCount < count) {
            reply_async->IOCLogInfo = count;
            return;
        }
        g_free(reply_async);
        reply.TerminationCount = count;
        break;

    case MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET:
        status = mptsas_scsi_device_find(s, req->Bus, req->TargetID,
                                         req->LUN, &sdev);
        if (status) {
            reply.IOCStatus = status;
            goto out;
        }
        if (sdev->lun != req->LUN[1]) {
            reply.ResponseCode = MPI_SCSITASKMGMT_RSP_TM_INVALID_LUN;
            goto out;
        }
        qdev_reset_all(&sdev->qdev);
        break;

    case MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET:
        if (req->Bus != 0) {
            reply.IOCStatus = MPI_IOCSTATUS_SCSI_INVALID_BUS;
            goto out;
        }
        if (req->TargetID > s->max_devices) {
            reply.IOCStatus = MPI_IOCSTATUS_SCSI_INVALID_TARGETID;
            goto out;
        }

        QTAILQ_FOREACH(kid, &s->bus.qbus.children, sibling) {
            sdev = SCSI_DEVICE(kid->child);
            if (sdev->channel == 0 && sdev->id == req->TargetID) {
                qdev_reset_all(kid->child);
            }
        }
        break;

    case MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS:
        qbus_reset_all(BUS(&s->bus));
        break;

    default:
        reply.ResponseCode = MPI_SCSITASKMGMT_RSP_TM_NOT_SUPPORTED;
        break;
    }

out:
    mptsas_fix_scsi_task_mgmt_reply_endianness(&reply);
    mptsas_post_reply(s, (MPIDefaultReply *)&reply);
}

static void mptsas_process_ioc_init(MPTSASState *s, MPIMsgIOCInit *req)
{
    MPIMsgIOCInitReply reply;

    mptsas_fix_ioc_init_endianness(req);

    QEMU_BUILD_BUG_ON(MPTSAS_MAX_REQUEST_SIZE < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_msg) < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_reply) < sizeof(reply));

    s->who_init               = req->WhoInit;
    s->reply_frame_size       = req->ReplyFrameSize;
    s->max_buses              = req->MaxBuses;
    s->max_devices            = req->MaxDevices ? req->MaxDevices : 256;
    s->host_mfa_high_addr     = (hwaddr)req->HostMfaHighAddr << 32;
    s->sense_buffer_high_addr = (hwaddr)req->SenseBufferHighAddr << 32;

    if (s->state == MPI_IOC_STATE_READY) {
        s->state = MPI_IOC_STATE_OPERATIONAL;
    }

    memset(&reply, 0, sizeof(reply));
    reply.WhoInit    = s->who_init;
    reply.MsgLength  = sizeof(reply) / 4;
    reply.Function   = req->Function;
    reply.MaxDevices = s->max_devices;
    reply.MaxBuses   = s->max_buses;
    reply.MsgContext = req->MsgContext;

    mptsas_fix_ioc_init_reply_endianness(&reply);
    mptsas_reply(s, (MPIDefaultReply *)&reply);
}

static void mptsas_process_ioc_facts(MPTSASState *s,
                                     MPIMsgIOCFacts *req)
{
    MPIMsgIOCFactsReply reply;

    mptsas_fix_ioc_facts_endianness(req);

    QEMU_BUILD_BUG_ON(MPTSAS_MAX_REQUEST_SIZE < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_msg) < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_reply) < sizeof(reply));

    memset(&reply, 0, sizeof(reply));
    reply.MsgVersion                 = 0x0105;
    reply.MsgLength                  = sizeof(reply) / 4;
    reply.Function                   = req->Function;
    reply.MsgContext                 = req->MsgContext;
    reply.MaxChainDepth              = MPTSAS_MAXIMUM_CHAIN_DEPTH;
    reply.WhoInit                    = s->who_init;
    reply.BlockSize                  = MPTSAS_MAX_REQUEST_SIZE / sizeof(uint32_t);
    reply.ReplyQueueDepth            = ARRAY_SIZE(s->reply_post) - 1;
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(s->reply_post) != ARRAY_SIZE(s->reply_free));

    reply.RequestFrameSize           = 128;
    reply.ProductID                  = MPTSAS1068_PRODUCT_ID;
    reply.CurrentHostMfaHighAddr     = s->host_mfa_high_addr >> 32;
    reply.GlobalCredits              = ARRAY_SIZE(s->request_post) - 1;
    reply.NumberOfPorts              = MPTSAS_NUM_PORTS;
    reply.CurrentSenseBufferHighAddr = s->sense_buffer_high_addr >> 32;
    reply.CurReplyFrameSize          = s->reply_frame_size;
    reply.MaxDevices                 = s->max_devices;
    reply.MaxBuses                   = s->max_buses;
    reply.FWVersionDev               = 0;
    reply.FWVersionUnit              = 0x92;
    reply.FWVersionMinor             = 0x32;
    reply.FWVersionMajor             = 0x1;

    mptsas_fix_ioc_facts_reply_endianness(&reply);
    mptsas_reply(s, (MPIDefaultReply *)&reply);
}

static void mptsas_process_port_facts(MPTSASState *s,
                                     MPIMsgPortFacts *req)
{
    MPIMsgPortFactsReply reply;

    mptsas_fix_port_facts_endianness(req);

    QEMU_BUILD_BUG_ON(MPTSAS_MAX_REQUEST_SIZE < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_msg) < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_reply) < sizeof(reply));

    memset(&reply, 0, sizeof(reply));
    reply.MsgLength  = sizeof(reply) / 4;
    reply.Function   = req->Function;
    reply.PortNumber = req->PortNumber;
    reply.MsgContext = req->MsgContext;

    if (req->PortNumber < MPTSAS_NUM_PORTS) {
        reply.PortType      = MPI_PORTFACTS_PORTTYPE_SAS;
        reply.MaxDevices    = MPTSAS_NUM_PORTS;
        reply.PortSCSIID    = MPTSAS_NUM_PORTS;
        reply.ProtocolFlags = MPI_PORTFACTS_PROTOCOL_LOGBUSADDR | MPI_PORTFACTS_PROTOCOL_INITIATOR;
    }

    mptsas_fix_port_facts_reply_endianness(&reply);
    mptsas_reply(s, (MPIDefaultReply *)&reply);
}

static void mptsas_process_port_enable(MPTSASState *s,
                                       MPIMsgPortEnable *req)
{
    MPIMsgPortEnableReply reply;

    mptsas_fix_port_enable_endianness(req);

    QEMU_BUILD_BUG_ON(MPTSAS_MAX_REQUEST_SIZE < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_msg) < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_reply) < sizeof(reply));

    memset(&reply, 0, sizeof(reply));
    reply.MsgLength  = sizeof(reply) / 4;
    reply.PortNumber = req->PortNumber;
    reply.Function   = req->Function;
    reply.MsgContext = req->MsgContext;

    mptsas_fix_port_enable_reply_endianness(&reply);
    mptsas_reply(s, (MPIDefaultReply *)&reply);
}

static void mptsas_process_event_notification(MPTSASState *s,
                                              MPIMsgEventNotify *req)
{
    MPIMsgEventNotifyReply reply;

    mptsas_fix_event_notification_endianness(req);

    QEMU_BUILD_BUG_ON(MPTSAS_MAX_REQUEST_SIZE < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_msg) < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_reply) < sizeof(reply));

    /* Don't even bother storing whether event notification is enabled,
     * since it is not accessible.
     */

    memset(&reply, 0, sizeof(reply));
    reply.EventDataLength = sizeof(reply.Data) / 4;
    reply.MsgLength       = sizeof(reply) / 4;
    reply.Function        = req->Function;

    /* This is set because events are sent through the reply FIFOs.  */
    reply.MsgFlags        = MPI_MSGFLAGS_CONTINUATION_REPLY;

    reply.MsgContext      = req->MsgContext;
    reply.Event           = MPI_EVENT_EVENT_CHANGE;
    reply.Data[0]         = !!req->Switch;

    mptsas_fix_event_notification_reply_endianness(&reply);
    mptsas_reply(s, (MPIDefaultReply *)&reply);
}

static void mptsas_process_message(MPTSASState *s, MPIRequestHeader *req)
{
    trace_mptsas_process_message(s, req->Function, req->MsgContext);
    switch (req->Function) {
    case MPI_FUNCTION_SCSI_TASK_MGMT:
        mptsas_process_scsi_task_mgmt(s, (MPIMsgSCSITaskMgmt *)req);
        break;

    case MPI_FUNCTION_IOC_INIT:
        mptsas_process_ioc_init(s, (MPIMsgIOCInit *)req);
        break;

    case MPI_FUNCTION_IOC_FACTS:
        mptsas_process_ioc_facts(s, (MPIMsgIOCFacts *)req);
        break;

    case MPI_FUNCTION_PORT_FACTS:
        mptsas_process_port_facts(s, (MPIMsgPortFacts *)req);
        break;

    case MPI_FUNCTION_PORT_ENABLE:
        mptsas_process_port_enable(s, (MPIMsgPortEnable *)req);
        break;

    case MPI_FUNCTION_EVENT_NOTIFICATION:
        mptsas_process_event_notification(s, (MPIMsgEventNotify *)req);
        break;

    case MPI_FUNCTION_CONFIG:
        mptsas_process_config(s, (MPIMsgConfig *)req);
        break;

    default:
        trace_mptsas_unhandled_cmd(s, req->Function, 0);
        mptsas_set_fault(s, MPI_IOCSTATUS_INVALID_FUNCTION);
        break;
    }
}

static void mptsas_fetch_request(MPTSASState *s)
{
    PCIDevice *pci = (PCIDevice *) s;
    char req[MPTSAS_MAX_REQUEST_SIZE];
    MPIRequestHeader *hdr = (MPIRequestHeader *)req;
    hwaddr addr;
    int size;

    /* Read the message header from the guest first. */
    addr = s->host_mfa_high_addr | MPTSAS_FIFO_GET(s, request_post);
    pci_dma_read(pci, addr, req, sizeof(*hdr));

    if (hdr->Function < ARRAY_SIZE(mpi_request_sizes) &&
        mpi_request_sizes[hdr->Function]) {
        /* Read the rest of the request based on the type.  Do not
         * reread everything, as that could cause a TOC/TOU mismatch
         * and leak data from the QEMU stack.
         */
        size = mpi_request_sizes[hdr->Function];
        assert(size <= MPTSAS_MAX_REQUEST_SIZE);
        pci_dma_read(pci, addr + sizeof(*hdr), &req[sizeof(*hdr)],
                     size - sizeof(*hdr));
    }

    if (hdr->Function == MPI_FUNCTION_SCSI_IO_REQUEST) {
        /* SCSI I/O requests are separate from mptsas_process_message
         * because they cannot be sent through the doorbell yet.
         */
        mptsas_process_scsi_io_request(s, (MPIMsgSCSIIORequest *)req, addr);
    } else {
        mptsas_process_message(s, (MPIRequestHeader *)req);
    }
}

static void mptsas_fetch_requests(void *opaque)
{
    MPTSASState *s = opaque;

    if (s->state != MPI_IOC_STATE_OPERATIONAL) {
        mptsas_set_fault(s, MPI_IOCSTATUS_INVALID_STATE);
        return;
    }
    while (!MPTSAS_FIFO_EMPTY(s, request_post)) {
        mptsas_fetch_request(s);
    }
}

static void mptsas_soft_reset(MPTSASState *s)
{
    uint32_t save_mask;

    trace_mptsas_reset(s);

    /* Temporarily disable interrupts */
    save_mask = s->intr_mask;
    s->intr_mask = MPI_HIM_DIM | MPI_HIM_RIM;
    mptsas_update_interrupt(s);

    qbus_reset_all(BUS(&s->bus));
    s->intr_status = 0;
    s->intr_mask = save_mask;

    s->reply_free_tail = 0;
    s->reply_free_head = 0;
    s->reply_post_tail = 0;
    s->reply_post_head = 0;
    s->request_post_tail = 0;
    s->request_post_head = 0;
    qemu_bh_cancel(s->request_bh);

    s->state = MPI_IOC_STATE_READY;
}

static uint32_t mptsas_doorbell_read(MPTSASState *s)
{
    uint32_t ret;

    ret = (s->who_init << MPI_DOORBELL_WHO_INIT_SHIFT) & MPI_DOORBELL_WHO_INIT_MASK;
    ret |= s->state;
    switch (s->doorbell_state) {
    case DOORBELL_NONE:
        break;

    case DOORBELL_WRITE:
        ret |= MPI_DOORBELL_ACTIVE;
        break;

    case DOORBELL_READ:
        /* Get rid of the IOC fault code.  */
        ret &= ~MPI_DOORBELL_DATA_MASK;

        assert(s->intr_status & MPI_HIS_DOORBELL_INTERRUPT);
        assert(s->doorbell_reply_idx <= s->doorbell_reply_size);

        ret |= MPI_DOORBELL_ACTIVE;
        if (s->doorbell_reply_idx < s->doorbell_reply_size) {
            /* For more information about this endian switch, see the
             * commit message for commit 36b62ae ("fw_cfg: fix endianness in
             * fw_cfg_data_mem_read() / _write()", 2015-01-16).
             */
            ret |= le16_to_cpu(s->doorbell_reply[s->doorbell_reply_idx++]);
        }
        break;

    default:
        abort();
    }

    return ret;
}

static void mptsas_doorbell_write(MPTSASState *s, uint32_t val)
{
    if (s->doorbell_state == DOORBELL_WRITE) {
        if (s->doorbell_idx < s->doorbell_cnt) {
            /* For more information about this endian switch, see the
             * commit message for commit 36b62ae ("fw_cfg: fix endianness in
             * fw_cfg_data_mem_read() / _write()", 2015-01-16).
             */
            s->doorbell_msg[s->doorbell_idx++] = cpu_to_le32(val);
            if (s->doorbell_idx == s->doorbell_cnt) {
                mptsas_process_message(s, (MPIRequestHeader *)s->doorbell_msg);
            }
        }
        return;
    }

    switch ((val & MPI_DOORBELL_FUNCTION_MASK) >> MPI_DOORBELL_FUNCTION_SHIFT) {
    case MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET:
        mptsas_soft_reset(s);
        break;
    case MPI_FUNCTION_IO_UNIT_RESET:
        break;
    case MPI_FUNCTION_HANDSHAKE:
        s->doorbell_state = DOORBELL_WRITE;
        s->doorbell_idx = 0;
        s->doorbell_cnt = (val & MPI_DOORBELL_ADD_DWORDS_MASK)
            >> MPI_DOORBELL_ADD_DWORDS_SHIFT;
        s->intr_status |= MPI_HIS_DOORBELL_INTERRUPT;
        mptsas_update_interrupt(s);
        break;
    default:
        trace_mptsas_unhandled_doorbell_cmd(s, val);
        break;
    }
}

static void mptsas_write_sequence_write(MPTSASState *s, uint32_t val)
{
    /* If the diagnostic register is enabled, any write to this register
     * will disable it.  Otherwise, the guest has to do a magic five-write
     * sequence.
     */
    if (s->diagnostic & MPI_DIAG_DRWE) {
        goto disable;
    }

    switch (s->diagnostic_idx) {
    case 0:
        if ((val & MPI_WRSEQ_KEY_VALUE_MASK) != MPI_WRSEQ_1ST_KEY_VALUE) {
            goto disable;
        }
        break;
    case 1:
        if ((val & MPI_WRSEQ_KEY_VALUE_MASK) != MPI_WRSEQ_2ND_KEY_VALUE) {
            goto disable;
        }
        break;
    case 2:
        if ((val & MPI_WRSEQ_KEY_VALUE_MASK) != MPI_WRSEQ_3RD_KEY_VALUE) {
            goto disable;
        }
        break;
    case 3:
        if ((val & MPI_WRSEQ_KEY_VALUE_MASK) != MPI_WRSEQ_4TH_KEY_VALUE) {
            goto disable;
        }
        break;
    case 4:
        if ((val & MPI_WRSEQ_KEY_VALUE_MASK) != MPI_WRSEQ_5TH_KEY_VALUE) {
            goto disable;
        }
        /* Prepare Spaceball One for departure, and change the
         * combination on my luggage!
         */
        s->diagnostic |= MPI_DIAG_DRWE;
        break;
    }
    s->diagnostic_idx++;
    return;

disable:
    s->diagnostic &= ~MPI_DIAG_DRWE;
    s->diagnostic_idx = 0;
}

static int mptsas_hard_reset(MPTSASState *s)
{
    mptsas_soft_reset(s);

    s->intr_mask = MPI_HIM_DIM | MPI_HIM_RIM;

    s->host_mfa_high_addr = 0;
    s->sense_buffer_high_addr = 0;
    s->reply_frame_size = 0;
    s->max_devices = MPTSAS_NUM_PORTS;
    s->max_buses = 1;

    return 0;
}

static void mptsas_interrupt_status_write(MPTSASState *s)
{
    switch (s->doorbell_state) {
    case DOORBELL_NONE:
    case DOORBELL_WRITE:
        s->intr_status &= ~MPI_HIS_DOORBELL_INTERRUPT;
        break;

    case DOORBELL_READ:
        /* The reply can be read continuously, so leave the interrupt up.  */
        assert(s->intr_status & MPI_HIS_DOORBELL_INTERRUPT);
        if (s->doorbell_reply_idx == s->doorbell_reply_size) {
            s->doorbell_state = DOORBELL_NONE;
        }
        break;

    default:
        abort();
    }
    mptsas_update_interrupt(s);
}

static uint32_t mptsas_reply_post_read(MPTSASState *s)
{
    uint32_t ret;

    if (!MPTSAS_FIFO_EMPTY(s, reply_post)) {
        ret = MPTSAS_FIFO_GET(s, reply_post);
    } else {
        ret = -1;
        s->intr_status &= ~MPI_HIS_REPLY_MESSAGE_INTERRUPT;
        mptsas_update_interrupt(s);
    }

    return ret;
}

static uint64_t mptsas_mmio_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    MPTSASState *s = opaque;
    uint32_t ret = 0;

    switch (addr & ~3) {
    case MPI_DOORBELL_OFFSET:
        ret = mptsas_doorbell_read(s);
        break;

    case MPI_DIAGNOSTIC_OFFSET:
        ret = s->diagnostic;
        break;

    case MPI_HOST_INTERRUPT_STATUS_OFFSET:
        ret = s->intr_status;
        break;

    case MPI_HOST_INTERRUPT_MASK_OFFSET:
        ret = s->intr_mask;
        break;

    case MPI_REPLY_POST_FIFO_OFFSET:
        ret = mptsas_reply_post_read(s);
        break;

    default:
        trace_mptsas_mmio_unhandled_read(s, addr);
        break;
    }
    trace_mptsas_mmio_read(s, addr, ret);
    return ret;
}

static void mptsas_mmio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    MPTSASState *s = opaque;

    trace_mptsas_mmio_write(s, addr, val);
    switch (addr) {
    case MPI_DOORBELL_OFFSET:
        mptsas_doorbell_write(s, val);
        break;

    case MPI_WRITE_SEQUENCE_OFFSET:
        mptsas_write_sequence_write(s, val);
        break;

    case MPI_DIAGNOSTIC_OFFSET:
        if (val & MPI_DIAG_RESET_ADAPTER) {
            mptsas_hard_reset(s);
        }
        break;

    case MPI_HOST_INTERRUPT_STATUS_OFFSET:
        mptsas_interrupt_status_write(s);
        break;

    case MPI_HOST_INTERRUPT_MASK_OFFSET:
        s->intr_mask = val & (MPI_HIM_RIM | MPI_HIM_DIM);
        mptsas_update_interrupt(s);
        break;

    case MPI_REQUEST_POST_FIFO_OFFSET:
        if (MPTSAS_FIFO_FULL(s, request_post)) {
            mptsas_set_fault(s, MPI_IOCSTATUS_INSUFFICIENT_RESOURCES);
        } else {
            MPTSAS_FIFO_PUT(s, request_post, val & ~0x03);
            qemu_bh_schedule(s->request_bh);
        }
        break;

    case MPI_REPLY_FREE_FIFO_OFFSET:
        if (MPTSAS_FIFO_FULL(s, reply_free)) {
            mptsas_set_fault(s, MPI_IOCSTATUS_INSUFFICIENT_RESOURCES);
        } else {
            MPTSAS_FIFO_PUT(s, reply_free, val);
        }
        break;

    default:
        trace_mptsas_mmio_unhandled_write(s, addr, val);
        break;
    }
}

static const MemoryRegionOps mptsas_mmio_ops = {
    .read = mptsas_mmio_read,
    .write = mptsas_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static const MemoryRegionOps mptsas_port_ops = {
    .read = mptsas_mmio_read,
    .write = mptsas_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static uint64_t mptsas_diag_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    MPTSASState *s = opaque;
    trace_mptsas_diag_read(s, addr, 0);
    return 0;
}

static void mptsas_diag_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    MPTSASState *s = opaque;
    trace_mptsas_diag_write(s, addr, val);
}

static const MemoryRegionOps mptsas_diag_ops = {
    .read = mptsas_diag_read,
    .write = mptsas_diag_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static QEMUSGList *mptsas_get_sg_list(SCSIRequest *sreq)
{
    MPTSASRequest *req = sreq->hba_private;

    return &req->qsg;
}

static void mptsas_command_complete(SCSIRequest *sreq,
        size_t resid)
{
    MPTSASRequest *req = sreq->hba_private;
    MPTSASState *s = req->dev;
    uint8_t sense_buf[SCSI_SENSE_BUF_SIZE];
    uint8_t sense_len;

    hwaddr sense_buffer_addr = req->dev->sense_buffer_high_addr |
            req->scsi_io.SenseBufferLowAddr;

    trace_mptsas_command_complete(s, req->scsi_io.MsgContext,
                                  sreq->status, resid);

    sense_len = scsi_req_get_sense(sreq, sense_buf, SCSI_SENSE_BUF_SIZE);
    if (sense_len > 0) {
        pci_dma_write(PCI_DEVICE(s), sense_buffer_addr, sense_buf,
                      MIN(req->scsi_io.SenseBufferLength, sense_len));
    }

    if (sreq->status != GOOD || resid ||
        req->dev->doorbell_state == DOORBELL_WRITE) {
        MPIMsgSCSIIOReply reply;

        memset(&reply, 0, sizeof(reply));
        reply.TargetID          = req->scsi_io.TargetID;
        reply.Bus               = req->scsi_io.Bus;
        reply.MsgLength         = sizeof(reply) / 4;
        reply.Function          = req->scsi_io.Function;
        reply.CDBLength         = req->scsi_io.CDBLength;
        reply.SenseBufferLength = req->scsi_io.SenseBufferLength;
        reply.MsgFlags          = req->scsi_io.MsgFlags;
        reply.MsgContext        = req->scsi_io.MsgContext;
        reply.SCSIStatus        = sreq->status;
        if (sreq->status == GOOD) {
            reply.TransferCount = req->scsi_io.DataLength - resid;
            if (resid) {
                reply.IOCStatus     = MPI_IOCSTATUS_SCSI_DATA_UNDERRUN;
            }
        } else {
            reply.SCSIState     = MPI_SCSI_STATE_AUTOSENSE_VALID;
            reply.SenseCount    = sense_len;
            reply.IOCStatus     = MPI_IOCSTATUS_SCSI_DATA_UNDERRUN;
        }

        mptsas_fix_scsi_io_reply_endianness(&reply);
        mptsas_post_reply(req->dev, (MPIDefaultReply *)&reply);
    } else {
        mptsas_turbo_reply(req->dev, req->scsi_io.MsgContext);
    }

    mptsas_free_request(req);
}

static void mptsas_request_cancelled(SCSIRequest *sreq)
{
    MPTSASRequest *req = sreq->hba_private;
    MPIMsgSCSIIOReply reply;

    memset(&reply, 0, sizeof(reply));
    reply.TargetID          = req->scsi_io.TargetID;
    reply.Bus               = req->scsi_io.Bus;
    reply.MsgLength         = sizeof(reply) / 4;
    reply.Function          = req->scsi_io.Function;
    reply.CDBLength         = req->scsi_io.CDBLength;
    reply.SenseBufferLength = req->scsi_io.SenseBufferLength;
    reply.MsgFlags          = req->scsi_io.MsgFlags;
    reply.MsgContext        = req->scsi_io.MsgContext;
    reply.SCSIState         = MPI_SCSI_STATE_NO_SCSI_STATUS;
    reply.IOCStatus         = MPI_IOCSTATUS_SCSI_TASK_TERMINATED;

    mptsas_fix_scsi_io_reply_endianness(&reply);
    mptsas_post_reply(req->dev, (MPIDefaultReply *)&reply);
    mptsas_free_request(req);
}

static void mptsas_save_request(QEMUFile *f, SCSIRequest *sreq)
{
    MPTSASRequest *req = sreq->hba_private;
    int i;

    qemu_put_buffer(f, (unsigned char *)&req->scsi_io, sizeof(req->scsi_io));
    qemu_put_be32(f, req->qsg.nsg);
    for (i = 0; i < req->qsg.nsg; i++) {
        qemu_put_be64(f, req->qsg.sg[i].base);
        qemu_put_be64(f, req->qsg.sg[i].len);
    }
}

static void *mptsas_load_request(QEMUFile *f, SCSIRequest *sreq)
{
    SCSIBus *bus = sreq->bus;
    MPTSASState *s = container_of(bus, MPTSASState, bus);
    PCIDevice *pci = PCI_DEVICE(s);
    MPTSASRequest *req;
    int i, n;

    req = g_new(MPTSASRequest, 1);
    qemu_get_buffer(f, (unsigned char *)&req->scsi_io, sizeof(req->scsi_io));

    n = qemu_get_be32(f);
    /* TODO: add a way for SCSIBusInfo's load_request to fail,
     * and fail migration instead of asserting here.
     * This is just one thing (there are probably more) that must be
     * fixed before we can allow NDEBUG compilation.
     */
    assert(n >= 0);

    pci_dma_sglist_init(&req->qsg, pci, n);
    for (i = 0; i < n; i++) {
        uint64_t base = qemu_get_be64(f);
        uint64_t len = qemu_get_be64(f);
        qemu_sglist_add(&req->qsg, base, len);
    }

    scsi_req_ref(sreq);
    req->sreq = sreq;
    req->dev = s;

    return req;
}

static const struct SCSIBusInfo mptsas_scsi_info = {
    .tcq = true,
    .max_target = MPTSAS_NUM_PORTS,
    .max_lun = 1,

    .get_sg_list = mptsas_get_sg_list,
    .complete = mptsas_command_complete,
    .cancel = mptsas_request_cancelled,
    .save_request = mptsas_save_request,
    .load_request = mptsas_load_request,
};

static void mptsas_scsi_realize(PCIDevice *dev, Error **errp)
{
    MPTSASState *s = MPT_SAS(dev);
    Error *err = NULL;
    int ret;

    dev->config[PCI_LATENCY_TIMER] = 0;
    dev->config[PCI_INTERRUPT_PIN] = 0x01;

    if (s->msi != ON_OFF_AUTO_OFF) {
        ret = msi_init(dev, 0, 1, true, false, &err);
        /* Any error other than -ENOTSUP(board's MSI support is broken)
         * is a programming error */
        assert(!ret || ret == -ENOTSUP);
        if (ret && s->msi == ON_OFF_AUTO_ON) {
            /* Can't satisfy user's explicit msi=on request, fail */
            error_append_hint(&err, "You have to use msi=auto (default) or "
                    "msi=off with this machine type.\n");
            error_propagate(errp, err);
            return;
        }
        assert(!err || s->msi == ON_OFF_AUTO_AUTO);
        /* With msi=auto, we fall back to MSI off silently */
        error_free(err);

        /* Only used for migration.  */
        s->msi_in_use = (ret == 0);
    }

    memory_region_init_io(&s->mmio_io, OBJECT(s), &mptsas_mmio_ops, s,
                          "mptsas-mmio", 0x4000);
    memory_region_init_io(&s->port_io, OBJECT(s), &mptsas_port_ops, s,
                          "mptsas-io", 256);
    memory_region_init_io(&s->diag_io, OBJECT(s), &mptsas_diag_ops, s,
                          "mptsas-diag", 0x10000);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->port_io);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY |
                                 PCI_BASE_ADDRESS_MEM_TYPE_32, &s->mmio_io);
    pci_register_bar(dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY |
                                 PCI_BASE_ADDRESS_MEM_TYPE_32, &s->diag_io);

    if (!s->sas_addr) {
        s->sas_addr = ((NAA_LOCALLY_ASSIGNED_ID << 24) |
                       IEEE_COMPANY_LOCALLY_ASSIGNED) << 36;
        s->sas_addr |= (pci_dev_bus_num(dev) << 16);
        s->sas_addr |= (PCI_SLOT(dev->devfn) << 8);
        s->sas_addr |= PCI_FUNC(dev->devfn);
    }
    s->max_devices = MPTSAS_NUM_PORTS;

    s->request_bh = qemu_bh_new(mptsas_fetch_requests, s);

    scsi_bus_init(&s->bus, sizeof(s->bus), &dev->qdev, &mptsas_scsi_info);
}

static void mptsas_scsi_uninit(PCIDevice *dev)
{
    MPTSASState *s = MPT_SAS(dev);

    qemu_bh_delete(s->request_bh);
    msi_uninit(dev);
}

static void mptsas_reset(DeviceState *dev)
{
    MPTSASState *s = MPT_SAS(dev);

    mptsas_hard_reset(s);
}

static int mptsas_post_load(void *opaque, int version_id)
{
    MPTSASState *s = opaque;

    if (s->doorbell_idx > s->doorbell_cnt ||
        s->doorbell_cnt > ARRAY_SIZE(s->doorbell_msg) ||
        s->doorbell_reply_idx > s->doorbell_reply_size ||
        s->doorbell_reply_size > ARRAY_SIZE(s->doorbell_reply) ||
        MPTSAS_FIFO_INVALID(s, request_post) ||
        MPTSAS_FIFO_INVALID(s, reply_post) ||
        MPTSAS_FIFO_INVALID(s, reply_free) ||
        s->diagnostic_idx > 4) {
        return -EINVAL;
    }

    return 0;
}

static const VMStateDescription vmstate_mptsas = {
    .name = "mptsas",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .post_load = mptsas_post_load,
    .fields      = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, MPTSASState),
        VMSTATE_BOOL(msi_in_use, MPTSASState),
        VMSTATE_UINT32(state, MPTSASState),
        VMSTATE_UINT8(who_init, MPTSASState),
        VMSTATE_UINT8(doorbell_state, MPTSASState),
        VMSTATE_UINT32_ARRAY(doorbell_msg, MPTSASState, 256),
        VMSTATE_INT32(doorbell_idx, MPTSASState),
        VMSTATE_INT32(doorbell_cnt, MPTSASState),

        VMSTATE_UINT16_ARRAY(doorbell_reply, MPTSASState, 256),
        VMSTATE_INT32(doorbell_reply_idx, MPTSASState),
        VMSTATE_INT32(doorbell_reply_size, MPTSASState),

        VMSTATE_UINT32(diagnostic, MPTSASState),
        VMSTATE_UINT8(diagnostic_idx, MPTSASState),

        VMSTATE_UINT32(intr_status, MPTSASState),
        VMSTATE_UINT32(intr_mask, MPTSASState),

        VMSTATE_UINT32_ARRAY(request_post, MPTSASState,
                             MPTSAS_REQUEST_QUEUE_DEPTH + 1),
        VMSTATE_UINT16(request_post_head, MPTSASState),
        VMSTATE_UINT16(request_post_tail, MPTSASState),

        VMSTATE_UINT32_ARRAY(reply_post, MPTSASState,
                             MPTSAS_REPLY_QUEUE_DEPTH + 1),
        VMSTATE_UINT16(reply_post_head, MPTSASState),
        VMSTATE_UINT16(reply_post_tail, MPTSASState),

        VMSTATE_UINT32_ARRAY(reply_free, MPTSASState,
                             MPTSAS_REPLY_QUEUE_DEPTH + 1),
        VMSTATE_UINT16(reply_free_head, MPTSASState),
        VMSTATE_UINT16(reply_free_tail, MPTSASState),

        VMSTATE_UINT16(max_buses, MPTSASState),
        VMSTATE_UINT16(max_devices, MPTSASState),
        VMSTATE_UINT16(reply_frame_size, MPTSASState),
        VMSTATE_UINT64(host_mfa_high_addr, MPTSASState),
        VMSTATE_UINT64(sense_buffer_high_addr, MPTSASState),
        VMSTATE_END_OF_LIST()
    }
};

static Property mptsas_properties[] = {
    DEFINE_PROP_UINT64("sas_address", MPTSASState, sas_addr, 0),
    /* TODO: test MSI support under Windows */
    DEFINE_PROP_ON_OFF_AUTO("msi", MPTSASState, msi, ON_OFF_AUTO_AUTO),
    DEFINE_PROP_END_OF_LIST(),
};

static void mptsas1068_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->realize = mptsas_scsi_realize;
    pc->exit = mptsas_scsi_uninit;
    pc->romfile = 0;
    pc->vendor_id = PCI_VENDOR_ID_LSI_LOGIC;
    pc->device_id = PCI_DEVICE_ID_LSI_SAS1068;
    pc->subsystem_vendor_id = PCI_VENDOR_ID_LSI_LOGIC;
    pc->subsystem_id = 0x8000;
    pc->class_id = PCI_CLASS_STORAGE_SCSI;
    device_class_set_props(dc, mptsas_properties);
    dc->reset = mptsas_reset;
    dc->vmsd = &vmstate_mptsas;
    dc->desc = "LSI SAS 1068";
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo mptsas_info = {
    .name = TYPE_MPTSAS1068,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MPTSASState),
    .class_init = mptsas1068_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void mptsas_register_types(void)
{
    type_register(&mptsas_info);
}

type_init(mptsas_register_types)
