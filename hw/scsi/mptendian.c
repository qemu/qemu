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
 * version 2 of the License, or (at your option) any later version.
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
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "sysemu/dma.h"
#include "sysemu/block-backend.h"
#include "hw/pci/msi.h"
#include "qemu/iov.h"
#include "hw/scsi/scsi.h"
#include "block/scsi.h"
#include "trace.h"

#include "mptsas.h"
#include "mpi.h"

static void mptsas_fix_sgentry_endianness(MPISGEntry *sge)
{
    le32_to_cpus(&sge->FlagsLength);
    if (sge->FlagsLength & MPI_SGE_FLAGS_64_BIT_ADDRESSING) {
       le64_to_cpus(&sge->u.Address64);
    } else {
       le32_to_cpus(&sge->u.Address32);
    }
}

static void mptsas_fix_sgentry_endianness_reply(MPISGEntry *sge)
{
    if (sge->FlagsLength & MPI_SGE_FLAGS_64_BIT_ADDRESSING) {
       cpu_to_le64s(&sge->u.Address64);
    } else {
       cpu_to_le32s(&sge->u.Address32);
    }
    cpu_to_le32s(&sge->FlagsLength);
}

void mptsas_fix_scsi_io_endianness(MPIMsgSCSIIORequest *req)
{
    le32_to_cpus(&req->MsgContext);
    le32_to_cpus(&req->Control);
    le32_to_cpus(&req->DataLength);
    le32_to_cpus(&req->SenseBufferLowAddr);
}

void mptsas_fix_scsi_io_reply_endianness(MPIMsgSCSIIOReply *reply)
{
    cpu_to_le32s(&reply->MsgContext);
    cpu_to_le16s(&reply->IOCStatus);
    cpu_to_le32s(&reply->IOCLogInfo);
    cpu_to_le32s(&reply->TransferCount);
    cpu_to_le32s(&reply->SenseCount);
    cpu_to_le32s(&reply->ResponseInfo);
    cpu_to_le16s(&reply->TaskTag);
}

void mptsas_fix_scsi_task_mgmt_endianness(MPIMsgSCSITaskMgmt *req)
{
    le32_to_cpus(&req->MsgContext);
    le32_to_cpus(&req->TaskMsgContext);
}

void mptsas_fix_scsi_task_mgmt_reply_endianness(MPIMsgSCSITaskMgmtReply *reply)
{
    cpu_to_le32s(&reply->MsgContext);
    cpu_to_le16s(&reply->IOCStatus);
    cpu_to_le32s(&reply->IOCLogInfo);
    cpu_to_le32s(&reply->TerminationCount);
}

void mptsas_fix_ioc_init_endianness(MPIMsgIOCInit *req)
{
    le32_to_cpus(&req->MsgContext);
    le16_to_cpus(&req->ReplyFrameSize);
    le32_to_cpus(&req->HostMfaHighAddr);
    le32_to_cpus(&req->SenseBufferHighAddr);
    le32_to_cpus(&req->ReplyFifoHostSignalingAddr);
    mptsas_fix_sgentry_endianness(&req->HostPageBufferSGE);
    le16_to_cpus(&req->MsgVersion);
    le16_to_cpus(&req->HeaderVersion);
}

void mptsas_fix_ioc_init_reply_endianness(MPIMsgIOCInitReply *reply)
{
    cpu_to_le32s(&reply->MsgContext);
    cpu_to_le16s(&reply->IOCStatus);
    cpu_to_le32s(&reply->IOCLogInfo);
}

void mptsas_fix_ioc_facts_endianness(MPIMsgIOCFacts *req)
{
    le32_to_cpus(&req->MsgContext);
}

void mptsas_fix_ioc_facts_reply_endianness(MPIMsgIOCFactsReply *reply)
{
    cpu_to_le16s(&reply->MsgVersion);
    cpu_to_le16s(&reply->HeaderVersion);
    cpu_to_le32s(&reply->MsgContext);
    cpu_to_le16s(&reply->IOCExceptions);
    cpu_to_le16s(&reply->IOCStatus);
    cpu_to_le32s(&reply->IOCLogInfo);
    cpu_to_le16s(&reply->ReplyQueueDepth);
    cpu_to_le16s(&reply->RequestFrameSize);
    cpu_to_le16s(&reply->ProductID);
    cpu_to_le32s(&reply->CurrentHostMfaHighAddr);
    cpu_to_le16s(&reply->GlobalCredits);
    cpu_to_le32s(&reply->CurrentSenseBufferHighAddr);
    cpu_to_le16s(&reply->CurReplyFrameSize);
    cpu_to_le32s(&reply->FWImageSize);
    cpu_to_le32s(&reply->IOCCapabilities);
    cpu_to_le16s(&reply->HighPriorityQueueDepth);
    mptsas_fix_sgentry_endianness_reply(&reply->HostPageBufferSGE);
    cpu_to_le32s(&reply->ReplyFifoHostSignalingAddr);
}

void mptsas_fix_config_endianness(MPIMsgConfig *req)
{
    le16_to_cpus(&req->ExtPageLength);
    le32_to_cpus(&req->MsgContext);
    le32_to_cpus(&req->PageAddress);
    mptsas_fix_sgentry_endianness(&req->PageBufferSGE);
}

void mptsas_fix_config_reply_endianness(MPIMsgConfigReply *reply)
{
    cpu_to_le16s(&reply->ExtPageLength);
    cpu_to_le32s(&reply->MsgContext);
    cpu_to_le16s(&reply->IOCStatus);
    cpu_to_le32s(&reply->IOCLogInfo);
}

void mptsas_fix_port_facts_endianness(MPIMsgPortFacts *req)
{
    le32_to_cpus(&req->MsgContext);
}

void mptsas_fix_port_facts_reply_endianness(MPIMsgPortFactsReply *reply)
{
    cpu_to_le32s(&reply->MsgContext);
    cpu_to_le16s(&reply->IOCStatus);
    cpu_to_le32s(&reply->IOCLogInfo);
    cpu_to_le16s(&reply->MaxDevices);
    cpu_to_le16s(&reply->PortSCSIID);
    cpu_to_le16s(&reply->ProtocolFlags);
    cpu_to_le16s(&reply->MaxPostedCmdBuffers);
    cpu_to_le16s(&reply->MaxPersistentIDs);
    cpu_to_le16s(&reply->MaxLanBuckets);
}

void mptsas_fix_port_enable_endianness(MPIMsgPortEnable *req)
{
    le32_to_cpus(&req->MsgContext);
}

void mptsas_fix_port_enable_reply_endianness(MPIMsgPortEnableReply *reply)
{
    cpu_to_le32s(&reply->MsgContext);
    cpu_to_le16s(&reply->IOCStatus);
    cpu_to_le32s(&reply->IOCLogInfo);
}

void mptsas_fix_event_notification_endianness(MPIMsgEventNotify *req)
{
    le32_to_cpus(&req->MsgContext);
}

void mptsas_fix_event_notification_reply_endianness(MPIMsgEventNotifyReply *reply)
{
    int length = reply->EventDataLength;
    int i;

    cpu_to_le16s(&reply->EventDataLength);
    cpu_to_le32s(&reply->MsgContext);
    cpu_to_le16s(&reply->IOCStatus);
    cpu_to_le32s(&reply->IOCLogInfo);
    cpu_to_le32s(&reply->Event);
    cpu_to_le32s(&reply->EventContext);

    /* Really depends on the event kind.  This will do for now.  */
    for (i = 0; i < length; i++) {
        cpu_to_le32s(&reply->Data[i]);
    }
}

