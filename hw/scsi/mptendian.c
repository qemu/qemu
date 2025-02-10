/*
 * QEMU LSI SAS1068 Host Bus Adapter emulation
 * Endianness conversion for MPI data structures
 *
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * Authors: Paolo Bonzini <pbonzini@redhat.com>
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
#include "system/dma.h"
#include "hw/pci/msi.h"
#include "qemu/iov.h"
#include "hw/scsi/scsi.h"
#include "scsi/constants.h"
#include "trace.h"

#include "mptsas.h"
#include "mpi.h"

static void mptsas_fix_sgentry_endianness(MPISGEntry *sge)
{
    sge->FlagsLength = le32_to_cpu(sge->FlagsLength);
    if (sge->FlagsLength & MPI_SGE_FLAGS_64_BIT_ADDRESSING) {
        sge->u.Address64 = le64_to_cpu(sge->u.Address64);
    } else {
        sge->u.Address32 = le32_to_cpu(sge->u.Address32);
    }
}

static void mptsas_fix_sgentry_endianness_reply(MPISGEntry *sge)
{
    if (sge->FlagsLength & MPI_SGE_FLAGS_64_BIT_ADDRESSING) {
        sge->u.Address64 = cpu_to_le64(sge->u.Address64);
    } else {
        sge->u.Address32 = cpu_to_le32(sge->u.Address32);
    }
    sge->FlagsLength = cpu_to_le32(sge->FlagsLength);
}

void mptsas_fix_scsi_io_endianness(MPIMsgSCSIIORequest *req)
{
    req->MsgContext = le32_to_cpu(req->MsgContext);
    req->Control = le32_to_cpu(req->Control);
    req->DataLength = le32_to_cpu(req->DataLength);
    req->SenseBufferLowAddr = le32_to_cpu(req->SenseBufferLowAddr);
}

void mptsas_fix_scsi_io_reply_endianness(MPIMsgSCSIIOReply *reply)
{
    reply->MsgContext = cpu_to_le32(reply->MsgContext);
    reply->IOCStatus = cpu_to_le16(reply->IOCStatus);
    reply->IOCLogInfo = cpu_to_le32(reply->IOCLogInfo);
    reply->TransferCount = cpu_to_le32(reply->TransferCount);
    reply->SenseCount = cpu_to_le32(reply->SenseCount);
    reply->ResponseInfo = cpu_to_le32(reply->ResponseInfo);
    reply->TaskTag = cpu_to_le16(reply->TaskTag);
}

void mptsas_fix_scsi_task_mgmt_endianness(MPIMsgSCSITaskMgmt *req)
{
    req->MsgContext = le32_to_cpu(req->MsgContext);
    req->TaskMsgContext = le32_to_cpu(req->TaskMsgContext);
}

void mptsas_fix_scsi_task_mgmt_reply_endianness(MPIMsgSCSITaskMgmtReply *reply)
{
    reply->MsgContext = cpu_to_le32(reply->MsgContext);
    reply->IOCStatus = cpu_to_le16(reply->IOCStatus);
    reply->IOCLogInfo = cpu_to_le32(reply->IOCLogInfo);
    reply->TerminationCount = cpu_to_le32(reply->TerminationCount);
}

void mptsas_fix_ioc_init_endianness(MPIMsgIOCInit *req)
{
    req->MsgContext = le32_to_cpu(req->MsgContext);
    req->ReplyFrameSize = le16_to_cpu(req->ReplyFrameSize);
    req->HostMfaHighAddr = le32_to_cpu(req->HostMfaHighAddr);
    req->SenseBufferHighAddr = le32_to_cpu(req->SenseBufferHighAddr);
    req->ReplyFifoHostSignalingAddr =
        le32_to_cpu(req->ReplyFifoHostSignalingAddr);
    mptsas_fix_sgentry_endianness(&req->HostPageBufferSGE);
    req->MsgVersion = le16_to_cpu(req->MsgVersion);
    req->HeaderVersion = le16_to_cpu(req->HeaderVersion);
}

void mptsas_fix_ioc_init_reply_endianness(MPIMsgIOCInitReply *reply)
{
    reply->MsgContext = cpu_to_le32(reply->MsgContext);
    reply->IOCStatus = cpu_to_le16(reply->IOCStatus);
    reply->IOCLogInfo = cpu_to_le32(reply->IOCLogInfo);
}

void mptsas_fix_ioc_facts_endianness(MPIMsgIOCFacts *req)
{
    req->MsgContext = le32_to_cpu(req->MsgContext);
}

void mptsas_fix_ioc_facts_reply_endianness(MPIMsgIOCFactsReply *reply)
{
    reply->MsgVersion = cpu_to_le16(reply->MsgVersion);
    reply->HeaderVersion = cpu_to_le16(reply->HeaderVersion);
    reply->MsgContext = cpu_to_le32(reply->MsgContext);
    reply->IOCExceptions = cpu_to_le16(reply->IOCExceptions);
    reply->IOCStatus = cpu_to_le16(reply->IOCStatus);
    reply->IOCLogInfo = cpu_to_le32(reply->IOCLogInfo);
    reply->ReplyQueueDepth = cpu_to_le16(reply->ReplyQueueDepth);
    reply->RequestFrameSize = cpu_to_le16(reply->RequestFrameSize);
    reply->ProductID = cpu_to_le16(reply->ProductID);
    reply->CurrentHostMfaHighAddr = cpu_to_le32(reply->CurrentHostMfaHighAddr);
    reply->GlobalCredits = cpu_to_le16(reply->GlobalCredits);
    reply->CurrentSenseBufferHighAddr =
        cpu_to_le32(reply->CurrentSenseBufferHighAddr);
    reply->CurReplyFrameSize = cpu_to_le16(reply->CurReplyFrameSize);
    reply->FWImageSize = cpu_to_le32(reply->FWImageSize);
    reply->IOCCapabilities = cpu_to_le32(reply->IOCCapabilities);
    reply->HighPriorityQueueDepth = cpu_to_le16(reply->HighPriorityQueueDepth);
    mptsas_fix_sgentry_endianness_reply(&reply->HostPageBufferSGE);
    reply->ReplyFifoHostSignalingAddr =
        cpu_to_le32(reply->ReplyFifoHostSignalingAddr);
}

void mptsas_fix_config_endianness(MPIMsgConfig *req)
{
    req->ExtPageLength = le16_to_cpu(req->ExtPageLength);
    req->MsgContext = le32_to_cpu(req->MsgContext);
    req->PageAddress = le32_to_cpu(req->PageAddress);
    mptsas_fix_sgentry_endianness(&req->PageBufferSGE);
}

void mptsas_fix_config_reply_endianness(MPIMsgConfigReply *reply)
{
    reply->ExtPageLength = cpu_to_le16(reply->ExtPageLength);
    reply->MsgContext = cpu_to_le32(reply->MsgContext);
    reply->IOCStatus = cpu_to_le16(reply->IOCStatus);
    reply->IOCLogInfo = cpu_to_le32(reply->IOCLogInfo);
}

void mptsas_fix_port_facts_endianness(MPIMsgPortFacts *req)
{
    req->MsgContext = le32_to_cpu(req->MsgContext);
}

void mptsas_fix_port_facts_reply_endianness(MPIMsgPortFactsReply *reply)
{
    reply->MsgContext = cpu_to_le32(reply->MsgContext);
    reply->IOCStatus = cpu_to_le16(reply->IOCStatus);
    reply->IOCLogInfo = cpu_to_le32(reply->IOCLogInfo);
    reply->MaxDevices = cpu_to_le16(reply->MaxDevices);
    reply->PortSCSIID = cpu_to_le16(reply->PortSCSIID);
    reply->ProtocolFlags = cpu_to_le16(reply->ProtocolFlags);
    reply->MaxPostedCmdBuffers = cpu_to_le16(reply->MaxPostedCmdBuffers);
    reply->MaxPersistentIDs = cpu_to_le16(reply->MaxPersistentIDs);
    reply->MaxLanBuckets = cpu_to_le16(reply->MaxLanBuckets);
}

void mptsas_fix_port_enable_endianness(MPIMsgPortEnable *req)
{
    req->MsgContext = le32_to_cpu(req->MsgContext);
}

void mptsas_fix_port_enable_reply_endianness(MPIMsgPortEnableReply *reply)
{
    reply->MsgContext = cpu_to_le32(reply->MsgContext);
    reply->IOCStatus = cpu_to_le16(reply->IOCStatus);
    reply->IOCLogInfo = cpu_to_le32(reply->IOCLogInfo);
}

void mptsas_fix_event_notification_endianness(MPIMsgEventNotify *req)
{
    req->MsgContext = le32_to_cpu(req->MsgContext);
}

void mptsas_fix_event_notification_reply_endianness(MPIMsgEventNotifyReply *reply)
{
    int length = reply->EventDataLength;
    int i;

    reply->EventDataLength = cpu_to_le16(reply->EventDataLength);
    reply->MsgContext = cpu_to_le32(reply->MsgContext);
    reply->IOCStatus = cpu_to_le16(reply->IOCStatus);
    reply->IOCLogInfo = cpu_to_le32(reply->IOCLogInfo);
    reply->Event = cpu_to_le32(reply->Event);
    reply->EventContext = cpu_to_le32(reply->EventContext);

    /* Really depends on the event kind.  This will do for now.  */
    for (i = 0; i < length; i++) {
        reply->Data[i] = cpu_to_le32(reply->Data[i]);
    }
}

