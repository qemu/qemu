/*-
 * Based on FreeBSD sys/dev/mpt/mpilib headers.
 *
 * Copyright (c) 2000-2010, LSI Logic Corporation and its contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon including
 *    a substantially similar Disclaimer requirement for further binary
 *    redistribution.
 * 3. Neither the name of the LSI Logic Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF THE COPYRIGHT
 * OWNER OR CONTRIBUTOR IS ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef MPI_H
#define MPI_H

enum {
    MPI_FUNCTION_SCSI_IO_REQUEST                = 0x00,
    MPI_FUNCTION_SCSI_TASK_MGMT                 = 0x01,
    MPI_FUNCTION_IOC_INIT                       = 0x02,
    MPI_FUNCTION_IOC_FACTS                      = 0x03,
    MPI_FUNCTION_CONFIG                         = 0x04,
    MPI_FUNCTION_PORT_FACTS                     = 0x05,
    MPI_FUNCTION_PORT_ENABLE                    = 0x06,
    MPI_FUNCTION_EVENT_NOTIFICATION             = 0x07,
    MPI_FUNCTION_EVENT_ACK                      = 0x08,
    MPI_FUNCTION_FW_DOWNLOAD                    = 0x09,
    MPI_FUNCTION_TARGET_CMD_BUFFER_POST         = 0x0A,
    MPI_FUNCTION_TARGET_ASSIST                  = 0x0B,
    MPI_FUNCTION_TARGET_STATUS_SEND             = 0x0C,
    MPI_FUNCTION_TARGET_MODE_ABORT              = 0x0D,
    MPI_FUNCTION_FC_LINK_SRVC_BUF_POST          = 0x0E,
    MPI_FUNCTION_FC_LINK_SRVC_RSP               = 0x0F,
    MPI_FUNCTION_FC_EX_LINK_SRVC_SEND           = 0x10,
    MPI_FUNCTION_FC_ABORT                       = 0x11,
    MPI_FUNCTION_FW_UPLOAD                      = 0x12,
    MPI_FUNCTION_FC_COMMON_TRANSPORT_SEND       = 0x13,
    MPI_FUNCTION_FC_PRIMITIVE_SEND              = 0x14,

    MPI_FUNCTION_RAID_ACTION                    = 0x15,
    MPI_FUNCTION_RAID_SCSI_IO_PASSTHROUGH       = 0x16,

    MPI_FUNCTION_TOOLBOX                        = 0x17,

    MPI_FUNCTION_SCSI_ENCLOSURE_PROCESSOR       = 0x18,

    MPI_FUNCTION_MAILBOX                        = 0x19,

    MPI_FUNCTION_SMP_PASSTHROUGH                = 0x1A,
    MPI_FUNCTION_SAS_IO_UNIT_CONTROL            = 0x1B,
    MPI_FUNCTION_SATA_PASSTHROUGH               = 0x1C,

    MPI_FUNCTION_DIAG_BUFFER_POST               = 0x1D,
    MPI_FUNCTION_DIAG_RELEASE                   = 0x1E,

    MPI_FUNCTION_SCSI_IO_32                     = 0x1F,

    MPI_FUNCTION_LAN_SEND                       = 0x20,
    MPI_FUNCTION_LAN_RECEIVE                    = 0x21,
    MPI_FUNCTION_LAN_RESET                      = 0x22,

    MPI_FUNCTION_TARGET_ASSIST_EXTENDED         = 0x23,
    MPI_FUNCTION_TARGET_CMD_BUF_BASE_POST       = 0x24,
    MPI_FUNCTION_TARGET_CMD_BUF_LIST_POST       = 0x25,

    MPI_FUNCTION_INBAND_BUFFER_POST             = 0x28,
    MPI_FUNCTION_INBAND_SEND                    = 0x29,
    MPI_FUNCTION_INBAND_RSP                     = 0x2A,
    MPI_FUNCTION_INBAND_ABORT                   = 0x2B,

    MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET         = 0x40,
    MPI_FUNCTION_IO_UNIT_RESET                  = 0x41,
    MPI_FUNCTION_HANDSHAKE                      = 0x42,
    MPI_FUNCTION_REPLY_FRAME_REMOVAL            = 0x43,
    MPI_FUNCTION_HOST_PAGEBUF_ACCESS_CONTROL    = 0x44,
};

/****************************************************************************/
/*  Registers                                                               */
/****************************************************************************/

enum {
    MPI_IOC_STATE_RESET                 = 0x00000000,
    MPI_IOC_STATE_READY                 = 0x10000000,
    MPI_IOC_STATE_OPERATIONAL           = 0x20000000,
    MPI_IOC_STATE_FAULT                 = 0x40000000,

    MPI_DOORBELL_OFFSET                 = 0x00000000,
    MPI_DOORBELL_ACTIVE                 = 0x08000000, /* DoorbellUsed */
    MPI_DOORBELL_WHO_INIT_MASK          = 0x07000000,
    MPI_DOORBELL_WHO_INIT_SHIFT         = 24,
    MPI_DOORBELL_FUNCTION_MASK          = 0xFF000000,
    MPI_DOORBELL_FUNCTION_SHIFT         = 24,
    MPI_DOORBELL_ADD_DWORDS_MASK        = 0x00FF0000,
    MPI_DOORBELL_ADD_DWORDS_SHIFT       = 16,
    MPI_DOORBELL_DATA_MASK              = 0x0000FFFF,
    MPI_DOORBELL_FUNCTION_SPECIFIC_MASK = 0x0000FFFF,

    MPI_DB_HPBAC_VALUE_MASK             = 0x0000F000,
    MPI_DB_HPBAC_ENABLE_ACCESS          = 0x01,
    MPI_DB_HPBAC_DISABLE_ACCESS         = 0x02,
    MPI_DB_HPBAC_FREE_BUFFER            = 0x03,

    MPI_WRITE_SEQUENCE_OFFSET           = 0x00000004,
    MPI_WRSEQ_KEY_VALUE_MASK            = 0x0000000F,
    MPI_WRSEQ_1ST_KEY_VALUE             = 0x04,
    MPI_WRSEQ_2ND_KEY_VALUE             = 0x0B,
    MPI_WRSEQ_3RD_KEY_VALUE             = 0x02,
    MPI_WRSEQ_4TH_KEY_VALUE             = 0x07,
    MPI_WRSEQ_5TH_KEY_VALUE             = 0x0D,

    MPI_DIAGNOSTIC_OFFSET               = 0x00000008,
    MPI_DIAG_CLEAR_FLASH_BAD_SIG        = 0x00000400,
    MPI_DIAG_PREVENT_IOC_BOOT           = 0x00000200,
    MPI_DIAG_DRWE                       = 0x00000080,
    MPI_DIAG_FLASH_BAD_SIG              = 0x00000040,
    MPI_DIAG_RESET_HISTORY              = 0x00000020,
    MPI_DIAG_RW_ENABLE                  = 0x00000010,
    MPI_DIAG_RESET_ADAPTER              = 0x00000004,
    MPI_DIAG_DISABLE_ARM                = 0x00000002,
    MPI_DIAG_MEM_ENABLE                 = 0x00000001,

    MPI_TEST_BASE_ADDRESS_OFFSET        = 0x0000000C,

    MPI_DIAG_RW_DATA_OFFSET             = 0x00000010,

    MPI_DIAG_RW_ADDRESS_OFFSET          = 0x00000014,

    MPI_HOST_INTERRUPT_STATUS_OFFSET    = 0x00000030,
    MPI_HIS_IOP_DOORBELL_STATUS         = 0x80000000,
    MPI_HIS_REPLY_MESSAGE_INTERRUPT     = 0x00000008,
    MPI_HIS_DOORBELL_INTERRUPT          = 0x00000001,

    MPI_HOST_INTERRUPT_MASK_OFFSET      = 0x00000034,
    MPI_HIM_RIM                         = 0x00000008,
    MPI_HIM_DIM                         = 0x00000001,

    MPI_REQUEST_QUEUE_OFFSET            = 0x00000040,
    MPI_REQUEST_POST_FIFO_OFFSET        = 0x00000040,

    MPI_REPLY_QUEUE_OFFSET              = 0x00000044,
    MPI_REPLY_POST_FIFO_OFFSET          = 0x00000044,
    MPI_REPLY_FREE_FIFO_OFFSET          = 0x00000044,

    MPI_HI_PRI_REQUEST_QUEUE_OFFSET     = 0x00000048,
};

#define MPI_ADDRESS_REPLY_A_BIT          0x80000000

/****************************************************************************/
/*  Scatter/gather elements                                                 */
/****************************************************************************/

typedef struct MPISGEntry {
    uint32_t                FlagsLength;
    union
    {
        uint32_t            Address32;
        uint64_t            Address64;
    } u;
} QEMU_PACKED MPISGEntry;

/* Flags field bit definitions */

enum {
    MPI_SGE_FLAGS_LAST_ELEMENT              = 0x80000000,
    MPI_SGE_FLAGS_END_OF_BUFFER             = 0x40000000,
    MPI_SGE_FLAGS_ELEMENT_TYPE_MASK         = 0x30000000,
    MPI_SGE_FLAGS_LOCAL_ADDRESS             = 0x08000000,
    MPI_SGE_FLAGS_DIRECTION                 = 0x04000000,
    MPI_SGE_FLAGS_64_BIT_ADDRESSING         = 0x02000000,
    MPI_SGE_FLAGS_END_OF_LIST               = 0x01000000,

    MPI_SGE_LENGTH_MASK                     = 0x00FFFFFF,
    MPI_SGE_CHAIN_LENGTH_MASK               = 0x0000FFFF,

    MPI_SGE_FLAGS_TRANSACTION_ELEMENT       = 0x00000000,
    MPI_SGE_FLAGS_SIMPLE_ELEMENT            = 0x10000000,
    MPI_SGE_FLAGS_CHAIN_ELEMENT             = 0x30000000,

    /* Direction */

    MPI_SGE_FLAGS_IOC_TO_HOST               = 0x00000000,
    MPI_SGE_FLAGS_HOST_TO_IOC               = 0x04000000,

    MPI_SGE_CHAIN_OFFSET_MASK               = 0x00FF0000,
};

#define MPI_SGE_CHAIN_OFFSET_SHIFT 16

/****************************************************************************/
/* Standard message request header for all request messages                 */
/****************************************************************************/

typedef struct MPIRequestHeader {
    uint8_t                 Reserved[2];      /* function specific */
    uint8_t                 ChainOffset;
    uint8_t                 Function;
    uint8_t                 Reserved1[3];     /* function specific */
    uint8_t                 MsgFlags;
    uint32_t                MsgContext;
} QEMU_PACKED MPIRequestHeader;


typedef struct MPIDefaultReply {
    uint8_t                 Reserved[2];      /* function specific */
    uint8_t                 MsgLength;
    uint8_t                 Function;
    uint8_t                 Reserved1[3];     /* function specific */
    uint8_t                 MsgFlags;
    uint32_t                MsgContext;
    uint8_t                 Reserved2[2];     /* function specific */
    uint16_t                IOCStatus;
    uint32_t                IOCLogInfo;
} QEMU_PACKED MPIDefaultReply;

/* MsgFlags definition for all replies */

#define MPI_MSGFLAGS_CONTINUATION_REPLY         (0x80)

enum {

    /************************************************************************/
    /*  Common IOCStatus values for all replies                             */
    /************************************************************************/

    MPI_IOCSTATUS_SUCCESS                   = 0x0000,
    MPI_IOCSTATUS_INVALID_FUNCTION          = 0x0001,
    MPI_IOCSTATUS_BUSY                      = 0x0002,
    MPI_IOCSTATUS_INVALID_SGL               = 0x0003,
    MPI_IOCSTATUS_INTERNAL_ERROR            = 0x0004,
    MPI_IOCSTATUS_RESERVED                  = 0x0005,
    MPI_IOCSTATUS_INSUFFICIENT_RESOURCES    = 0x0006,
    MPI_IOCSTATUS_INVALID_FIELD             = 0x0007,
    MPI_IOCSTATUS_INVALID_STATE             = 0x0008,
    MPI_IOCSTATUS_OP_STATE_NOT_SUPPORTED    = 0x0009,

    /************************************************************************/
    /*  Config IOCStatus values                                             */
    /************************************************************************/

    MPI_IOCSTATUS_CONFIG_INVALID_ACTION     = 0x0020,
    MPI_IOCSTATUS_CONFIG_INVALID_TYPE       = 0x0021,
    MPI_IOCSTATUS_CONFIG_INVALID_PAGE       = 0x0022,
    MPI_IOCSTATUS_CONFIG_INVALID_DATA       = 0x0023,
    MPI_IOCSTATUS_CONFIG_NO_DEFAULTS        = 0x0024,
    MPI_IOCSTATUS_CONFIG_CANT_COMMIT        = 0x0025,

    /************************************************************************/
    /*  SCSIIO Reply = SPI & FCP, initiator values                           */
    /************************************************************************/

    MPI_IOCSTATUS_SCSI_RECOVERED_ERROR      = 0x0040,
    MPI_IOCSTATUS_SCSI_INVALID_BUS          = 0x0041,
    MPI_IOCSTATUS_SCSI_INVALID_TARGETID     = 0x0042,
    MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE     = 0x0043,
    MPI_IOCSTATUS_SCSI_DATA_OVERRUN         = 0x0044,
    MPI_IOCSTATUS_SCSI_DATA_UNDERRUN        = 0x0045,
    MPI_IOCSTATUS_SCSI_IO_DATA_ERROR        = 0x0046,
    MPI_IOCSTATUS_SCSI_PROTOCOL_ERROR       = 0x0047,
    MPI_IOCSTATUS_SCSI_TASK_TERMINATED      = 0x0048,
    MPI_IOCSTATUS_SCSI_RESIDUAL_MISMATCH    = 0x0049,
    MPI_IOCSTATUS_SCSI_TASK_MGMT_FAILED     = 0x004A,
    MPI_IOCSTATUS_SCSI_IOC_TERMINATED       = 0x004B,
    MPI_IOCSTATUS_SCSI_EXT_TERMINATED       = 0x004C,

    /************************************************************************/
    /*  For use by SCSI Initiator and SCSI Target end-to-end data protection*/
    /************************************************************************/

    MPI_IOCSTATUS_EEDP_GUARD_ERROR          = 0x004D,
    MPI_IOCSTATUS_EEDP_REF_TAG_ERROR        = 0x004E,
    MPI_IOCSTATUS_EEDP_APP_TAG_ERROR        = 0x004F,

    /************************************************************************/
    /*  SCSI Target values                                                  */
    /************************************************************************/

    MPI_IOCSTATUS_TARGET_PRIORITY_IO         = 0x0060,
    MPI_IOCSTATUS_TARGET_INVALID_PORT        = 0x0061,
    MPI_IOCSTATUS_TARGET_INVALID_IO_INDEX    = 0x0062,
    MPI_IOCSTATUS_TARGET_ABORTED             = 0x0063,
    MPI_IOCSTATUS_TARGET_NO_CONN_RETRYABLE   = 0x0064,
    MPI_IOCSTATUS_TARGET_NO_CONNECTION       = 0x0065,
    MPI_IOCSTATUS_TARGET_XFER_COUNT_MISMATCH = 0x006A,
    MPI_IOCSTATUS_TARGET_STS_DATA_NOT_SENT   = 0x006B,
    MPI_IOCSTATUS_TARGET_DATA_OFFSET_ERROR   = 0x006D,
    MPI_IOCSTATUS_TARGET_TOO_MUCH_WRITE_DATA = 0x006E,
    MPI_IOCSTATUS_TARGET_IU_TOO_SHORT        = 0x006F,
    MPI_IOCSTATUS_TARGET_ACK_NAK_TIMEOUT     = 0x0070,
    MPI_IOCSTATUS_TARGET_NAK_RECEIVED        = 0x0071,

    /************************************************************************/
    /*  Fibre Channel Direct Access values                                  */
    /************************************************************************/

    MPI_IOCSTATUS_FC_ABORTED                = 0x0066,
    MPI_IOCSTATUS_FC_RX_ID_INVALID          = 0x0067,
    MPI_IOCSTATUS_FC_DID_INVALID            = 0x0068,
    MPI_IOCSTATUS_FC_NODE_LOGGED_OUT        = 0x0069,
    MPI_IOCSTATUS_FC_EXCHANGE_CANCELED      = 0x006C,

    /************************************************************************/
    /*  LAN values                                                          */
    /************************************************************************/

    MPI_IOCSTATUS_LAN_DEVICE_NOT_FOUND      = 0x0080,
    MPI_IOCSTATUS_LAN_DEVICE_FAILURE        = 0x0081,
    MPI_IOCSTATUS_LAN_TRANSMIT_ERROR        = 0x0082,
    MPI_IOCSTATUS_LAN_TRANSMIT_ABORTED      = 0x0083,
    MPI_IOCSTATUS_LAN_RECEIVE_ERROR         = 0x0084,
    MPI_IOCSTATUS_LAN_RECEIVE_ABORTED       = 0x0085,
    MPI_IOCSTATUS_LAN_PARTIAL_PACKET        = 0x0086,
    MPI_IOCSTATUS_LAN_CANCELED              = 0x0087,

    /************************************************************************/
    /*  Serial Attached SCSI values                                         */
    /************************************************************************/

    MPI_IOCSTATUS_SAS_SMP_REQUEST_FAILED    = 0x0090,
    MPI_IOCSTATUS_SAS_SMP_DATA_OVERRUN      = 0x0091,

    /************************************************************************/
    /*  Inband values                                                       */
    /************************************************************************/

    MPI_IOCSTATUS_INBAND_ABORTED            = 0x0098,
    MPI_IOCSTATUS_INBAND_NO_CONNECTION      = 0x0099,

    /************************************************************************/
    /*  Diagnostic Tools values                                             */
    /************************************************************************/

    MPI_IOCSTATUS_DIAGNOSTIC_RELEASED       = 0x00A0,

    /************************************************************************/
    /*  IOCStatus flag to indicate that log info is available               */
    /************************************************************************/

    MPI_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE   = 0x8000,
    MPI_IOCSTATUS_MASK                      = 0x7FFF,

    /************************************************************************/
    /*  LogInfo Types                                                       */
    /************************************************************************/

    MPI_IOCLOGINFO_TYPE_MASK                = 0xF0000000,
    MPI_IOCLOGINFO_TYPE_SHIFT               = 28,
    MPI_IOCLOGINFO_TYPE_NONE                = 0x0,
    MPI_IOCLOGINFO_TYPE_SCSI                = 0x1,
    MPI_IOCLOGINFO_TYPE_FC                  = 0x2,
    MPI_IOCLOGINFO_TYPE_SAS                 = 0x3,
    MPI_IOCLOGINFO_TYPE_ISCSI               = 0x4,
    MPI_IOCLOGINFO_LOG_DATA_MASK            = 0x0FFFFFFF,
};

/****************************************************************************/
/*  SCSI IO messages and associated structures                              */
/****************************************************************************/

typedef struct MPIMsgSCSIIORequest {
    uint8_t                 TargetID;           /* 00h */
    uint8_t                 Bus;                /* 01h */
    uint8_t                 ChainOffset;        /* 02h */
    uint8_t                 Function;           /* 03h */
    uint8_t                 CDBLength;          /* 04h */
    uint8_t                 SenseBufferLength;  /* 05h */
    uint8_t                 Reserved;           /* 06h */
    uint8_t                 MsgFlags;           /* 07h */
    uint32_t                MsgContext;         /* 08h */
    uint8_t                 LUN[8];             /* 0Ch */
    uint32_t                Control;            /* 14h */
    uint8_t                 CDB[16];            /* 18h */
    uint32_t                DataLength;         /* 28h */
    uint32_t                SenseBufferLowAddr; /* 2Ch */
} QEMU_PACKED MPIMsgSCSIIORequest;

/* SCSI IO MsgFlags bits */

#define MPI_SCSIIO_MSGFLGS_SENSE_WIDTH              (0x01)
#define MPI_SCSIIO_MSGFLGS_SENSE_WIDTH_32           (0x00)
#define MPI_SCSIIO_MSGFLGS_SENSE_WIDTH_64           (0x01)

#define MPI_SCSIIO_MSGFLGS_SENSE_LOCATION           (0x02)
#define MPI_SCSIIO_MSGFLGS_SENSE_LOC_HOST           (0x00)
#define MPI_SCSIIO_MSGFLGS_SENSE_LOC_IOC            (0x02)

#define MPI_SCSIIO_MSGFLGS_CMD_DETERMINES_DATA_DIR  (0x04)

/* SCSI IO LUN fields */

#define MPI_SCSIIO_LUN_FIRST_LEVEL_ADDRESSING   (0x0000FFFF)
#define MPI_SCSIIO_LUN_SECOND_LEVEL_ADDRESSING  (0xFFFF0000)
#define MPI_SCSIIO_LUN_THIRD_LEVEL_ADDRESSING   (0x0000FFFF)
#define MPI_SCSIIO_LUN_FOURTH_LEVEL_ADDRESSING  (0xFFFF0000)
#define MPI_SCSIIO_LUN_LEVEL_1_WORD             (0xFF00)
#define MPI_SCSIIO_LUN_LEVEL_1_DWORD            (0x0000FF00)

/* SCSI IO Control bits */

#define MPI_SCSIIO_CONTROL_DATADIRECTION_MASK   (0x03000000)
#define MPI_SCSIIO_CONTROL_NODATATRANSFER       (0x00000000)
#define MPI_SCSIIO_CONTROL_WRITE                (0x01000000)
#define MPI_SCSIIO_CONTROL_READ                 (0x02000000)

#define MPI_SCSIIO_CONTROL_ADDCDBLEN_MASK       (0x3C000000)
#define MPI_SCSIIO_CONTROL_ADDCDBLEN_SHIFT      (26)

#define MPI_SCSIIO_CONTROL_TASKATTRIBUTE_MASK   (0x00000700)
#define MPI_SCSIIO_CONTROL_SIMPLEQ              (0x00000000)
#define MPI_SCSIIO_CONTROL_HEADOFQ              (0x00000100)
#define MPI_SCSIIO_CONTROL_ORDEREDQ             (0x00000200)
#define MPI_SCSIIO_CONTROL_ACAQ                 (0x00000400)
#define MPI_SCSIIO_CONTROL_UNTAGGED             (0x00000500)
#define MPI_SCSIIO_CONTROL_NO_DISCONNECT        (0x00000700)

#define MPI_SCSIIO_CONTROL_TASKMANAGE_MASK      (0x00FF0000)
#define MPI_SCSIIO_CONTROL_OBSOLETE             (0x00800000)
#define MPI_SCSIIO_CONTROL_CLEAR_ACA_RSV        (0x00400000)
#define MPI_SCSIIO_CONTROL_TARGET_RESET         (0x00200000)
#define MPI_SCSIIO_CONTROL_LUN_RESET_RSV        (0x00100000)
#define MPI_SCSIIO_CONTROL_RESERVED             (0x00080000)
#define MPI_SCSIIO_CONTROL_CLR_TASK_SET_RSV     (0x00040000)
#define MPI_SCSIIO_CONTROL_ABORT_TASK_SET       (0x00020000)
#define MPI_SCSIIO_CONTROL_RESERVED2            (0x00010000)

/* SCSI IO reply structure */
typedef struct MPIMsgSCSIIOReply
{
    uint8_t                 TargetID;           /* 00h */
    uint8_t                 Bus;                /* 01h */
    uint8_t                 MsgLength;          /* 02h */
    uint8_t                 Function;           /* 03h */
    uint8_t                 CDBLength;          /* 04h */
    uint8_t                 SenseBufferLength;  /* 05h */
    uint8_t                 Reserved;           /* 06h */
    uint8_t                 MsgFlags;           /* 07h */
    uint32_t                MsgContext;         /* 08h */
    uint8_t                 SCSIStatus;         /* 0Ch */
    uint8_t                 SCSIState;          /* 0Dh */
    uint16_t                IOCStatus;          /* 0Eh */
    uint32_t                IOCLogInfo;         /* 10h */
    uint32_t                TransferCount;      /* 14h */
    uint32_t                SenseCount;         /* 18h */
    uint32_t                ResponseInfo;       /* 1Ch */
    uint16_t                TaskTag;            /* 20h */
    uint16_t                Reserved1;          /* 22h */
} QEMU_PACKED MPIMsgSCSIIOReply;

/* SCSI IO Reply SCSIStatus values (SAM-2 status codes) */

#define MPI_SCSI_STATUS_SUCCESS                 (0x00)
#define MPI_SCSI_STATUS_CHECK_CONDITION         (0x02)
#define MPI_SCSI_STATUS_CONDITION_MET           (0x04)
#define MPI_SCSI_STATUS_BUSY                    (0x08)
#define MPI_SCSI_STATUS_INTERMEDIATE            (0x10)
#define MPI_SCSI_STATUS_INTERMEDIATE_CONDMET    (0x14)
#define MPI_SCSI_STATUS_RESERVATION_CONFLICT    (0x18)
#define MPI_SCSI_STATUS_COMMAND_TERMINATED      (0x22)
#define MPI_SCSI_STATUS_TASK_SET_FULL           (0x28)
#define MPI_SCSI_STATUS_ACA_ACTIVE              (0x30)

#define MPI_SCSI_STATUS_FCPEXT_DEVICE_LOGGED_OUT    (0x80)
#define MPI_SCSI_STATUS_FCPEXT_NO_LINK              (0x81)
#define MPI_SCSI_STATUS_FCPEXT_UNASSIGNED           (0x82)


/* SCSI IO Reply SCSIState values */

#define MPI_SCSI_STATE_AUTOSENSE_VALID          (0x01)
#define MPI_SCSI_STATE_AUTOSENSE_FAILED         (0x02)
#define MPI_SCSI_STATE_NO_SCSI_STATUS           (0x04)
#define MPI_SCSI_STATE_TERMINATED               (0x08)
#define MPI_SCSI_STATE_RESPONSE_INFO_VALID      (0x10)
#define MPI_SCSI_STATE_QUEUE_TAG_REJECTED       (0x20)

/* SCSI IO Reply ResponseInfo values */
/* (FCP-1 RSP_CODE values and SPI-3 Packetized Failure codes) */

#define MPI_SCSI_RSP_INFO_FUNCTION_COMPLETE     (0x00000000)
#define MPI_SCSI_RSP_INFO_FCP_BURST_LEN_ERROR   (0x01000000)
#define MPI_SCSI_RSP_INFO_CMND_FIELDS_INVALID   (0x02000000)
#define MPI_SCSI_RSP_INFO_FCP_DATA_RO_ERROR     (0x03000000)
#define MPI_SCSI_RSP_INFO_TASK_MGMT_UNSUPPORTED (0x04000000)
#define MPI_SCSI_RSP_INFO_TASK_MGMT_FAILED      (0x05000000)
#define MPI_SCSI_RSP_INFO_SPI_LQ_INVALID_TYPE   (0x06000000)

#define MPI_SCSI_TASKTAG_UNKNOWN                (0xFFFF)


/****************************************************************************/
/*  SCSI Task Management messages                                           */
/****************************************************************************/

typedef struct MPIMsgSCSITaskMgmt {
    uint8_t                 TargetID;           /* 00h */
    uint8_t                 Bus;                /* 01h */
    uint8_t                 ChainOffset;        /* 02h */
    uint8_t                 Function;           /* 03h */
    uint8_t                 Reserved;           /* 04h */
    uint8_t                 TaskType;           /* 05h */
    uint8_t                 Reserved1;          /* 06h */
    uint8_t                 MsgFlags;           /* 07h */
    uint32_t                MsgContext;         /* 08h */
    uint8_t                 LUN[8];             /* 0Ch */
    uint32_t                Reserved2[7];       /* 14h */
    uint32_t                TaskMsgContext;     /* 30h */
} QEMU_PACKED MPIMsgSCSITaskMgmt;

enum {
    /* TaskType values */

    MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK            = 0x01,
    MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET         = 0x02,
    MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET          = 0x03,
    MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS             = 0x04,
    MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET    = 0x05,
    MPI_SCSITASKMGMT_TASKTYPE_CLEAR_TASK_SET        = 0x06,
    MPI_SCSITASKMGMT_TASKTYPE_QUERY_TASK            = 0x07,
    MPI_SCSITASKMGMT_TASKTYPE_CLR_ACA               = 0x08,

    /* MsgFlags bits */

    MPI_SCSITASKMGMT_MSGFLAGS_DO_NOT_SEND_TASK_IU   = 0x01,

    MPI_SCSITASKMGMT_MSGFLAGS_TARGET_RESET_OPTION   = 0x00,
    MPI_SCSITASKMGMT_MSGFLAGS_LIP_RESET_OPTION      = 0x02,
    MPI_SCSITASKMGMT_MSGFLAGS_LIPRESET_RESET_OPTION = 0x04,

    MPI_SCSITASKMGMT_MSGFLAGS_SOFT_RESET_OPTION     = 0x08,
};

/* SCSI Task Management Reply */
typedef struct MPIMsgSCSITaskMgmtReply {
    uint8_t                 TargetID;           /* 00h */
    uint8_t                 Bus;                /* 01h */
    uint8_t                 MsgLength;          /* 02h */
    uint8_t                 Function;           /* 03h */
    uint8_t                 ResponseCode;       /* 04h */
    uint8_t                 TaskType;           /* 05h */
    uint8_t                 Reserved1;          /* 06h */
    uint8_t                 MsgFlags;           /* 07h */
    uint32_t                MsgContext;         /* 08h */
    uint8_t                 Reserved2[2];       /* 0Ch */
    uint16_t                IOCStatus;          /* 0Eh */
    uint32_t                IOCLogInfo;         /* 10h */
    uint32_t                TerminationCount;   /* 14h */
} QEMU_PACKED MPIMsgSCSITaskMgmtReply;

/* ResponseCode values */
enum {
    MPI_SCSITASKMGMT_RSP_TM_COMPLETE                = 0x00,
    MPI_SCSITASKMGMT_RSP_INVALID_FRAME              = 0x02,
    MPI_SCSITASKMGMT_RSP_TM_NOT_SUPPORTED           = 0x04,
    MPI_SCSITASKMGMT_RSP_TM_FAILED                  = 0x05,
    MPI_SCSITASKMGMT_RSP_TM_SUCCEEDED               = 0x08,
    MPI_SCSITASKMGMT_RSP_TM_INVALID_LUN             = 0x09,
    MPI_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC           = 0x80,
};

/****************************************************************************/
/*  IOCInit message                                                         */
/****************************************************************************/

typedef struct MPIMsgIOCInit {
    uint8_t                 WhoInit;                    /* 00h */
    uint8_t                 Reserved;                   /* 01h */
    uint8_t                 ChainOffset;                /* 02h */
    uint8_t                 Function;                   /* 03h */
    uint8_t                 Flags;                      /* 04h */
    uint8_t                 MaxDevices;                 /* 05h */
    uint8_t                 MaxBuses;                   /* 06h */
    uint8_t                 MsgFlags;                   /* 07h */
    uint32_t                MsgContext;                 /* 08h */
    uint16_t                ReplyFrameSize;             /* 0Ch */
    uint8_t                 Reserved1[2];               /* 0Eh */
    uint32_t                HostMfaHighAddr;            /* 10h */
    uint32_t                SenseBufferHighAddr;        /* 14h */
    uint32_t                ReplyFifoHostSignalingAddr; /* 18h */
    MPISGEntry              HostPageBufferSGE;          /* 1Ch */
    uint16_t                MsgVersion;                 /* 28h */
    uint16_t                HeaderVersion;              /* 2Ah */
} QEMU_PACKED MPIMsgIOCInit;

enum {
    /* WhoInit values */

    MPI_WHOINIT_NO_ONE                              = 0x00,
    MPI_WHOINIT_SYSTEM_BIOS                         = 0x01,
    MPI_WHOINIT_ROM_BIOS                            = 0x02,
    MPI_WHOINIT_PCI_PEER                            = 0x03,
    MPI_WHOINIT_HOST_DRIVER                         = 0x04,
    MPI_WHOINIT_MANUFACTURER                        = 0x05,

    /* Flags values */

    MPI_IOCINIT_FLAGS_HOST_PAGE_BUFFER_PERSISTENT   = 0x04,
    MPI_IOCINIT_FLAGS_REPLY_FIFO_HOST_SIGNAL        = 0x02,
    MPI_IOCINIT_FLAGS_DISCARD_FW_IMAGE              = 0x01,

    /* MsgVersion */

    MPI_IOCINIT_MSGVERSION_MAJOR_MASK               = 0xFF00,
    MPI_IOCINIT_MSGVERSION_MAJOR_SHIFT              = 8,
    MPI_IOCINIT_MSGVERSION_MINOR_MASK               = 0x00FF,
    MPI_IOCINIT_MSGVERSION_MINOR_SHIFT              = 0,

    /* HeaderVersion */

    MPI_IOCINIT_HEADERVERSION_UNIT_MASK             = 0xFF00,
    MPI_IOCINIT_HEADERVERSION_UNIT_SHIFT            = 8,
    MPI_IOCINIT_HEADERVERSION_DEV_MASK              = 0x00FF,
    MPI_IOCINIT_HEADERVERSION_DEV_SHIFT             = 0,
};

typedef struct MPIMsgIOCInitReply {
    uint8_t                 WhoInit;                    /* 00h */
    uint8_t                 Reserved;                   /* 01h */
    uint8_t                 MsgLength;                  /* 02h */
    uint8_t                 Function;                   /* 03h */
    uint8_t                 Flags;                      /* 04h */
    uint8_t                 MaxDevices;                 /* 05h */
    uint8_t                 MaxBuses;                   /* 06h */
    uint8_t                 MsgFlags;                   /* 07h */
    uint32_t                MsgContext;                 /* 08h */
    uint16_t                Reserved2;                  /* 0Ch */
    uint16_t                IOCStatus;                  /* 0Eh */
    uint32_t                IOCLogInfo;                 /* 10h */
} QEMU_PACKED MPIMsgIOCInitReply;



/****************************************************************************/
/*  IOC Facts message                                                       */
/****************************************************************************/

typedef struct MPIMsgIOCFacts {
    uint8_t                 Reserved[2];                /* 00h */
    uint8_t                 ChainOffset;                /* 01h */
    uint8_t                 Function;                   /* 02h */
    uint8_t                 Reserved1[3];               /* 03h */
    uint8_t                 MsgFlags;                   /* 04h */
    uint32_t                MsgContext;                 /* 08h */
} QEMU_PACKED MPIMsgIOCFacts;

/* IOC Facts Reply */
typedef struct MPIMsgIOCFactsReply {
    uint16_t                MsgVersion;                 /* 00h */
    uint8_t                 MsgLength;                  /* 02h */
    uint8_t                 Function;                   /* 03h */
    uint16_t                HeaderVersion;              /* 04h */
    uint8_t                 IOCNumber;                  /* 06h */
    uint8_t                 MsgFlags;                   /* 07h */
    uint32_t                MsgContext;                 /* 08h */
    uint16_t                IOCExceptions;              /* 0Ch */
    uint16_t                IOCStatus;                  /* 0Eh */
    uint32_t                IOCLogInfo;                 /* 10h */
    uint8_t                 MaxChainDepth;              /* 14h */
    uint8_t                 WhoInit;                    /* 15h */
    uint8_t                 BlockSize;                  /* 16h */
    uint8_t                 Flags;                      /* 17h */
    uint16_t                ReplyQueueDepth;            /* 18h */
    uint16_t                RequestFrameSize;           /* 1Ah */
    uint16_t                Reserved_0101_FWVersion;    /* 1Ch */ /* obsolete 16-bit FWVersion */
    uint16_t                ProductID;                  /* 1Eh */
    uint32_t                CurrentHostMfaHighAddr;     /* 20h */
    uint16_t                GlobalCredits;              /* 24h */
    uint8_t                 NumberOfPorts;              /* 26h */
    uint8_t                 EventState;                 /* 27h */
    uint32_t                CurrentSenseBufferHighAddr; /* 28h */
    uint16_t                CurReplyFrameSize;          /* 2Ch */
    uint8_t                 MaxDevices;                 /* 2Eh */
    uint8_t                 MaxBuses;                   /* 2Fh */
    uint32_t                FWImageSize;                /* 30h */
    uint32_t                IOCCapabilities;            /* 34h */
    uint8_t                 FWVersionDev;               /* 38h */
    uint8_t                 FWVersionUnit;              /* 39h */
    uint8_t                 FWVersionMinor;             /* 3ah */
    uint8_t                 FWVersionMajor;             /* 3bh */
    uint16_t                HighPriorityQueueDepth;     /* 3Ch */
    uint16_t                Reserved2;                  /* 3Eh */
    MPISGEntry              HostPageBufferSGE;          /* 40h */
    uint32_t                ReplyFifoHostSignalingAddr; /* 4Ch */
} QEMU_PACKED MPIMsgIOCFactsReply;

enum {
    MPI_IOCFACTS_MSGVERSION_MAJOR_MASK              = 0xFF00,
    MPI_IOCFACTS_MSGVERSION_MAJOR_SHIFT             = 8,
    MPI_IOCFACTS_MSGVERSION_MINOR_MASK              = 0x00FF,
    MPI_IOCFACTS_MSGVERSION_MINOR_SHIFT             = 0,

    MPI_IOCFACTS_HDRVERSION_UNIT_MASK               = 0xFF00,
    MPI_IOCFACTS_HDRVERSION_UNIT_SHIFT              = 8,
    MPI_IOCFACTS_HDRVERSION_DEV_MASK                = 0x00FF,
    MPI_IOCFACTS_HDRVERSION_DEV_SHIFT               = 0,

    MPI_IOCFACTS_EXCEPT_CONFIG_CHECKSUM_FAIL        = 0x0001,
    MPI_IOCFACTS_EXCEPT_RAID_CONFIG_INVALID         = 0x0002,
    MPI_IOCFACTS_EXCEPT_FW_CHECKSUM_FAIL            = 0x0004,
    MPI_IOCFACTS_EXCEPT_PERSISTENT_TABLE_FULL       = 0x0008,
    MPI_IOCFACTS_EXCEPT_METADATA_UNSUPPORTED        = 0x0010,

    MPI_IOCFACTS_FLAGS_FW_DOWNLOAD_BOOT             = 0x01,
    MPI_IOCFACTS_FLAGS_REPLY_FIFO_HOST_SIGNAL       = 0x02,
    MPI_IOCFACTS_FLAGS_HOST_PAGE_BUFFER_PERSISTENT  = 0x04,

    MPI_IOCFACTS_EVENTSTATE_DISABLED                = 0x00,
    MPI_IOCFACTS_EVENTSTATE_ENABLED                 = 0x01,

    MPI_IOCFACTS_CAPABILITY_HIGH_PRI_Q              = 0x00000001,
    MPI_IOCFACTS_CAPABILITY_REPLY_HOST_SIGNAL       = 0x00000002,
    MPI_IOCFACTS_CAPABILITY_QUEUE_FULL_HANDLING     = 0x00000004,
    MPI_IOCFACTS_CAPABILITY_DIAG_TRACE_BUFFER       = 0x00000008,
    MPI_IOCFACTS_CAPABILITY_SNAPSHOT_BUFFER         = 0x00000010,
    MPI_IOCFACTS_CAPABILITY_EXTENDED_BUFFER         = 0x00000020,
    MPI_IOCFACTS_CAPABILITY_EEDP                    = 0x00000040,
    MPI_IOCFACTS_CAPABILITY_BIDIRECTIONAL           = 0x00000080,
    MPI_IOCFACTS_CAPABILITY_MULTICAST               = 0x00000100,
    MPI_IOCFACTS_CAPABILITY_SCSIIO32                = 0x00000200,
    MPI_IOCFACTS_CAPABILITY_NO_SCSIIO16             = 0x00000400,
    MPI_IOCFACTS_CAPABILITY_TLR                     = 0x00000800,
};

/****************************************************************************/
/*  Port Facts message and Reply                                            */
/****************************************************************************/

typedef struct MPIMsgPortFacts {
     uint8_t                Reserved[2];                /* 00h */
     uint8_t                ChainOffset;                /* 02h */
     uint8_t                Function;                   /* 03h */
     uint8_t                Reserved1[2];               /* 04h */
     uint8_t                PortNumber;                 /* 06h */
     uint8_t                MsgFlags;                   /* 07h */
     uint32_t               MsgContext;                 /* 08h */
} QEMU_PACKED MPIMsgPortFacts;

typedef struct MPIMsgPortFactsReply {
     uint16_t               Reserved;                   /* 00h */
     uint8_t                MsgLength;                  /* 02h */
     uint8_t                Function;                   /* 03h */
     uint16_t               Reserved1;                  /* 04h */
     uint8_t                PortNumber;                 /* 06h */
     uint8_t                MsgFlags;                   /* 07h */
     uint32_t               MsgContext;                 /* 08h */
     uint16_t               Reserved2;                  /* 0Ch */
     uint16_t               IOCStatus;                  /* 0Eh */
     uint32_t               IOCLogInfo;                 /* 10h */
     uint8_t                Reserved3;                  /* 14h */
     uint8_t                PortType;                   /* 15h */
     uint16_t               MaxDevices;                 /* 16h */
     uint16_t               PortSCSIID;                 /* 18h */
     uint16_t               ProtocolFlags;              /* 1Ah */
     uint16_t               MaxPostedCmdBuffers;        /* 1Ch */
     uint16_t               MaxPersistentIDs;           /* 1Eh */
     uint16_t               MaxLanBuckets;              /* 20h */
     uint8_t                MaxInitiators;              /* 22h */
     uint8_t                Reserved4;                  /* 23h */
     uint32_t               Reserved5;                  /* 24h */
} QEMU_PACKED MPIMsgPortFactsReply;


enum {
    /* PortTypes values */
    MPI_PORTFACTS_PORTTYPE_INACTIVE         = 0x00,
    MPI_PORTFACTS_PORTTYPE_SCSI             = 0x01,
    MPI_PORTFACTS_PORTTYPE_FC               = 0x10,
    MPI_PORTFACTS_PORTTYPE_ISCSI            = 0x20,
    MPI_PORTFACTS_PORTTYPE_SAS              = 0x30,

    /* ProtocolFlags values */
    MPI_PORTFACTS_PROTOCOL_LOGBUSADDR       = 0x01,
    MPI_PORTFACTS_PROTOCOL_LAN              = 0x02,
    MPI_PORTFACTS_PROTOCOL_TARGET           = 0x04,
    MPI_PORTFACTS_PROTOCOL_INITIATOR        = 0x08,
};


/****************************************************************************/
/*  Port Enable Message                                                     */
/****************************************************************************/

typedef struct MPIMsgPortEnable {
    uint8_t                 Reserved[2];                /* 00h */
    uint8_t                 ChainOffset;                /* 02h */
    uint8_t                 Function;                   /* 03h */
    uint8_t                 Reserved1[2];               /* 04h */
    uint8_t                 PortNumber;                 /* 06h */
    uint8_t                 MsgFlags;                   /* 07h */
    uint32_t                MsgContext;                 /* 08h */
} QEMU_PACKED MPIMsgPortEnable;

typedef struct MPIMsgPortEnableReply {
    uint8_t                 Reserved[2];                /* 00h */
    uint8_t                 MsgLength;                  /* 02h */
    uint8_t                 Function;                   /* 03h */
    uint8_t                 Reserved1[2];               /* 04h */
    uint8_t                 PortNumber;                 /* 05h */
    uint8_t                 MsgFlags;                   /* 07h */
    uint32_t                MsgContext;                 /* 08h */
    uint16_t                Reserved2;                  /* 0Ch */
    uint16_t                IOCStatus;                  /* 0Eh */
    uint32_t                IOCLogInfo;                 /* 10h */
} QEMU_PACKED MPIMsgPortEnableReply;

/****************************************************************************/
/*  Event Notification messages                                             */
/****************************************************************************/

typedef struct MPIMsgEventNotify {
    uint8_t                 Switch;                     /* 00h */
    uint8_t                 Reserved;                   /* 01h */
    uint8_t                 ChainOffset;                /* 02h */
    uint8_t                 Function;                   /* 03h */
    uint8_t                 Reserved1[3];               /* 04h */
    uint8_t                 MsgFlags;                   /* 07h */
    uint32_t                MsgContext;                 /* 08h */
} QEMU_PACKED MPIMsgEventNotify;

/* Event Notification Reply */

typedef struct MPIMsgEventNotifyReply {
     uint16_t               EventDataLength;            /* 00h */
     uint8_t                MsgLength;                  /* 02h */
     uint8_t                Function;                   /* 03h */
     uint8_t                Reserved1[2];               /* 04h */
     uint8_t                AckRequired;                /* 06h */
     uint8_t                MsgFlags;                   /* 07h */
     uint32_t               MsgContext;                 /* 08h */
     uint8_t                Reserved2[2];               /* 0Ch */
     uint16_t               IOCStatus;                  /* 0Eh */
     uint32_t               IOCLogInfo;                 /* 10h */
     uint32_t               Event;                      /* 14h */
     uint32_t               EventContext;               /* 18h */
     uint32_t               Data[1];                    /* 1Ch */
} QEMU_PACKED MPIMsgEventNotifyReply;

/* Event Acknowledge */

typedef struct MPIMsgEventAck {
    uint8_t                 Reserved[2];                /* 00h */
    uint8_t                 ChainOffset;                /* 02h */
    uint8_t                 Function;                   /* 03h */
    uint8_t                 Reserved1[3];               /* 04h */
    uint8_t                 MsgFlags;                   /* 07h */
    uint32_t                MsgContext;                 /* 08h */
    uint32_t                Event;                      /* 0Ch */
    uint32_t                EventContext;               /* 10h */
} QEMU_PACKED MPIMsgEventAck;

typedef struct MPIMsgEventAckReply {
    uint8_t                 Reserved[2];                /* 00h */
    uint8_t                 MsgLength;                  /* 02h */
    uint8_t                 Function;                   /* 03h */
    uint8_t                 Reserved1[3];               /* 04h */
    uint8_t                 MsgFlags;                   /* 07h */
    uint32_t                MsgContext;                 /* 08h */
    uint16_t                Reserved2;                  /* 0Ch */
    uint16_t                IOCStatus;                  /* 0Eh */
    uint32_t                IOCLogInfo;                 /* 10h */
} QEMU_PACKED MPIMsgEventAckReply;

enum {
    /* Switch */

    MPI_EVENT_NOTIFICATION_SWITCH_OFF   = 0x00,
    MPI_EVENT_NOTIFICATION_SWITCH_ON    = 0x01,

    /* Event */

    MPI_EVENT_NONE                          = 0x00000000,
    MPI_EVENT_LOG_DATA                      = 0x00000001,
    MPI_EVENT_STATE_CHANGE                  = 0x00000002,
    MPI_EVENT_UNIT_ATTENTION                = 0x00000003,
    MPI_EVENT_IOC_BUS_RESET                 = 0x00000004,
    MPI_EVENT_EXT_BUS_RESET                 = 0x00000005,
    MPI_EVENT_RESCAN                        = 0x00000006,
    MPI_EVENT_LINK_STATUS_CHANGE            = 0x00000007,
    MPI_EVENT_LOOP_STATE_CHANGE             = 0x00000008,
    MPI_EVENT_LOGOUT                        = 0x00000009,
    MPI_EVENT_EVENT_CHANGE                  = 0x0000000A,
    MPI_EVENT_INTEGRATED_RAID               = 0x0000000B,
    MPI_EVENT_SCSI_DEVICE_STATUS_CHANGE     = 0x0000000C,
    MPI_EVENT_ON_BUS_TIMER_EXPIRED          = 0x0000000D,
    MPI_EVENT_QUEUE_FULL                    = 0x0000000E,
    MPI_EVENT_SAS_DEVICE_STATUS_CHANGE      = 0x0000000F,
    MPI_EVENT_SAS_SES                       = 0x00000010,
    MPI_EVENT_PERSISTENT_TABLE_FULL         = 0x00000011,
    MPI_EVENT_SAS_PHY_LINK_STATUS           = 0x00000012,
    MPI_EVENT_SAS_DISCOVERY_ERROR           = 0x00000013,
    MPI_EVENT_IR_RESYNC_UPDATE              = 0x00000014,
    MPI_EVENT_IR2                           = 0x00000015,
    MPI_EVENT_SAS_DISCOVERY                 = 0x00000016,
    MPI_EVENT_SAS_BROADCAST_PRIMITIVE       = 0x00000017,
    MPI_EVENT_SAS_INIT_DEVICE_STATUS_CHANGE = 0x00000018,
    MPI_EVENT_SAS_INIT_TABLE_OVERFLOW       = 0x00000019,
    MPI_EVENT_SAS_SMP_ERROR                 = 0x0000001A,
    MPI_EVENT_SAS_EXPANDER_STATUS_CHANGE    = 0x0000001B,
    MPI_EVENT_LOG_ENTRY_ADDED               = 0x00000021,

    /* AckRequired field values */

    MPI_EVENT_NOTIFICATION_ACK_NOT_REQUIRED = 0x00,
    MPI_EVENT_NOTIFICATION_ACK_REQUIRED     = 0x01,
};

/****************************************************************************
*   Config Request Message
****************************************************************************/

typedef struct MPIMsgConfig {
    uint8_t                 Action;                     /* 00h */
    uint8_t                 Reserved;                   /* 01h */
    uint8_t                 ChainOffset;                /* 02h */
    uint8_t                 Function;                   /* 03h */
    uint16_t                ExtPageLength;              /* 04h */
    uint8_t                 ExtPageType;                /* 06h */
    uint8_t                 MsgFlags;                   /* 07h */
    uint32_t                MsgContext;                 /* 08h */
    uint8_t                 Reserved2[8];               /* 0Ch */
    uint8_t                 PageVersion;                /* 14h */
    uint8_t                 PageLength;                 /* 15h */
    uint8_t                 PageNumber;                 /* 16h */
    uint8_t                 PageType;                   /* 17h */
    uint32_t                PageAddress;                /* 18h */
    MPISGEntry              PageBufferSGE;              /* 1Ch */
} QEMU_PACKED MPIMsgConfig;

/* Action field values */

enum {
    MPI_CONFIG_ACTION_PAGE_HEADER               = 0x00,
    MPI_CONFIG_ACTION_PAGE_READ_CURRENT         = 0x01,
    MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT        = 0x02,
    MPI_CONFIG_ACTION_PAGE_DEFAULT              = 0x03,
    MPI_CONFIG_ACTION_PAGE_WRITE_NVRAM          = 0x04,
    MPI_CONFIG_ACTION_PAGE_READ_DEFAULT         = 0x05,
    MPI_CONFIG_ACTION_PAGE_READ_NVRAM           = 0x06,
};


/* Config Reply Message */
typedef struct MPIMsgConfigReply {
    uint8_t                 Action;                     /* 00h */
    uint8_t                 Reserved;                   /* 01h */
    uint8_t                 MsgLength;                  /* 02h */
    uint8_t                 Function;                   /* 03h */
    uint16_t                ExtPageLength;              /* 04h */
    uint8_t                 ExtPageType;                /* 06h */
    uint8_t                 MsgFlags;                   /* 07h */
    uint32_t                MsgContext;                 /* 08h */
    uint8_t                 Reserved2[2];               /* 0Ch */
    uint16_t                IOCStatus;                  /* 0Eh */
    uint32_t                IOCLogInfo;                 /* 10h */
    uint8_t                 PageVersion;                /* 14h */
    uint8_t                 PageLength;                 /* 15h */
    uint8_t                 PageNumber;                 /* 16h */
    uint8_t                 PageType;                   /* 17h */
} QEMU_PACKED MPIMsgConfigReply;

enum {
    /* PageAddress field values */
    MPI_CONFIG_PAGEATTR_READ_ONLY               = 0x00,
    MPI_CONFIG_PAGEATTR_CHANGEABLE              = 0x10,
    MPI_CONFIG_PAGEATTR_PERSISTENT              = 0x20,
    MPI_CONFIG_PAGEATTR_RO_PERSISTENT           = 0x30,
    MPI_CONFIG_PAGEATTR_MASK                    = 0xF0,

    MPI_CONFIG_PAGETYPE_IO_UNIT                 = 0x00,
    MPI_CONFIG_PAGETYPE_IOC                     = 0x01,
    MPI_CONFIG_PAGETYPE_BIOS                    = 0x02,
    MPI_CONFIG_PAGETYPE_SCSI_PORT               = 0x03,
    MPI_CONFIG_PAGETYPE_SCSI_DEVICE             = 0x04,
    MPI_CONFIG_PAGETYPE_FC_PORT                 = 0x05,
    MPI_CONFIG_PAGETYPE_FC_DEVICE               = 0x06,
    MPI_CONFIG_PAGETYPE_LAN                     = 0x07,
    MPI_CONFIG_PAGETYPE_RAID_VOLUME             = 0x08,
    MPI_CONFIG_PAGETYPE_MANUFACTURING           = 0x09,
    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK           = 0x0A,
    MPI_CONFIG_PAGETYPE_INBAND                  = 0x0B,
    MPI_CONFIG_PAGETYPE_EXTENDED                = 0x0F,
    MPI_CONFIG_PAGETYPE_MASK                    = 0x0F,

    MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT          = 0x10,
    MPI_CONFIG_EXTPAGETYPE_SAS_EXPANDER         = 0x11,
    MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE           = 0x12,
    MPI_CONFIG_EXTPAGETYPE_SAS_PHY              = 0x13,
    MPI_CONFIG_EXTPAGETYPE_LOG                  = 0x14,
    MPI_CONFIG_EXTPAGETYPE_ENCLOSURE            = 0x15,

    MPI_SCSI_PORT_PGAD_PORT_MASK                = 0x000000FF,

    MPI_SCSI_DEVICE_FORM_MASK                   = 0xF0000000,
    MPI_SCSI_DEVICE_FORM_BUS_TID                = 0x00000000,
    MPI_SCSI_DEVICE_TARGET_ID_MASK              = 0x000000FF,
    MPI_SCSI_DEVICE_TARGET_ID_SHIFT             = 0,
    MPI_SCSI_DEVICE_BUS_MASK                    = 0x0000FF00,
    MPI_SCSI_DEVICE_BUS_SHIFT                   = 8,
    MPI_SCSI_DEVICE_FORM_TARGET_MODE            = 0x10000000,
    MPI_SCSI_DEVICE_TM_RESPOND_ID_MASK          = 0x000000FF,
    MPI_SCSI_DEVICE_TM_RESPOND_ID_SHIFT         = 0,
    MPI_SCSI_DEVICE_TM_BUS_MASK                 = 0x0000FF00,
    MPI_SCSI_DEVICE_TM_BUS_SHIFT                = 8,
    MPI_SCSI_DEVICE_TM_INIT_ID_MASK             = 0x00FF0000,
    MPI_SCSI_DEVICE_TM_INIT_ID_SHIFT            = 16,

    MPI_FC_PORT_PGAD_PORT_MASK                  = 0xF0000000,
    MPI_FC_PORT_PGAD_PORT_SHIFT                 = 28,
    MPI_FC_PORT_PGAD_FORM_MASK                  = 0x0F000000,
    MPI_FC_PORT_PGAD_FORM_INDEX                 = 0x01000000,
    MPI_FC_PORT_PGAD_INDEX_MASK                 = 0x0000FFFF,
    MPI_FC_PORT_PGAD_INDEX_SHIFT                = 0,

    MPI_FC_DEVICE_PGAD_PORT_MASK                = 0xF0000000,
    MPI_FC_DEVICE_PGAD_PORT_SHIFT               = 28,
    MPI_FC_DEVICE_PGAD_FORM_MASK                = 0x0F000000,
    MPI_FC_DEVICE_PGAD_FORM_NEXT_DID            = 0x00000000,
    MPI_FC_DEVICE_PGAD_ND_PORT_MASK             = 0xF0000000,
    MPI_FC_DEVICE_PGAD_ND_PORT_SHIFT            = 28,
    MPI_FC_DEVICE_PGAD_ND_DID_MASK              = 0x00FFFFFF,
    MPI_FC_DEVICE_PGAD_ND_DID_SHIFT             = 0,
    MPI_FC_DEVICE_PGAD_FORM_BUS_TID             = 0x01000000,
    MPI_FC_DEVICE_PGAD_BT_BUS_MASK              = 0x0000FF00,
    MPI_FC_DEVICE_PGAD_BT_BUS_SHIFT             = 8,
    MPI_FC_DEVICE_PGAD_BT_TID_MASK              = 0x000000FF,
    MPI_FC_DEVICE_PGAD_BT_TID_SHIFT             = 0,

    MPI_PHYSDISK_PGAD_PHYSDISKNUM_MASK          = 0x000000FF,
    MPI_PHYSDISK_PGAD_PHYSDISKNUM_SHIFT         = 0,

    MPI_SAS_EXPAND_PGAD_FORM_MASK             = 0xF0000000,
    MPI_SAS_EXPAND_PGAD_FORM_SHIFT            = 28,
    MPI_SAS_EXPAND_PGAD_FORM_GET_NEXT_HANDLE  = 0x00000000,
    MPI_SAS_EXPAND_PGAD_FORM_HANDLE_PHY_NUM   = 0x00000001,
    MPI_SAS_EXPAND_PGAD_FORM_HANDLE           = 0x00000002,
    MPI_SAS_EXPAND_PGAD_GNH_MASK_HANDLE       = 0x0000FFFF,
    MPI_SAS_EXPAND_PGAD_GNH_SHIFT_HANDLE      = 0,
    MPI_SAS_EXPAND_PGAD_HPN_MASK_PHY          = 0x00FF0000,
    MPI_SAS_EXPAND_PGAD_HPN_SHIFT_PHY         = 16,
    MPI_SAS_EXPAND_PGAD_HPN_MASK_HANDLE       = 0x0000FFFF,
    MPI_SAS_EXPAND_PGAD_HPN_SHIFT_HANDLE      = 0,
    MPI_SAS_EXPAND_PGAD_H_MASK_HANDLE         = 0x0000FFFF,
    MPI_SAS_EXPAND_PGAD_H_SHIFT_HANDLE        = 0,

    MPI_SAS_DEVICE_PGAD_FORM_MASK               = 0xF0000000,
    MPI_SAS_DEVICE_PGAD_FORM_SHIFT              = 28,
    MPI_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE    = 0x00000000,
    MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID      = 0x00000001,
    MPI_SAS_DEVICE_PGAD_FORM_HANDLE             = 0x00000002,
    MPI_SAS_DEVICE_PGAD_GNH_HANDLE_MASK         = 0x0000FFFF,
    MPI_SAS_DEVICE_PGAD_GNH_HANDLE_SHIFT        = 0,
    MPI_SAS_DEVICE_PGAD_BT_BUS_MASK             = 0x0000FF00,
    MPI_SAS_DEVICE_PGAD_BT_BUS_SHIFT            = 8,
    MPI_SAS_DEVICE_PGAD_BT_TID_MASK             = 0x000000FF,
    MPI_SAS_DEVICE_PGAD_BT_TID_SHIFT            = 0,
    MPI_SAS_DEVICE_PGAD_H_HANDLE_MASK           = 0x0000FFFF,
    MPI_SAS_DEVICE_PGAD_H_HANDLE_SHIFT          = 0,

    MPI_SAS_PHY_PGAD_FORM_MASK                  = 0xF0000000,
    MPI_SAS_PHY_PGAD_FORM_SHIFT                 = 28,
    MPI_SAS_PHY_PGAD_FORM_PHY_NUMBER            = 0x0,
    MPI_SAS_PHY_PGAD_FORM_PHY_TBL_INDEX         = 0x1,
    MPI_SAS_PHY_PGAD_PHY_NUMBER_MASK            = 0x000000FF,
    MPI_SAS_PHY_PGAD_PHY_NUMBER_SHIFT           = 0,
    MPI_SAS_PHY_PGAD_PHY_TBL_INDEX_MASK         = 0x0000FFFF,
    MPI_SAS_PHY_PGAD_PHY_TBL_INDEX_SHIFT        = 0,

    MPI_SAS_ENCLOS_PGAD_FORM_MASK               = 0xF0000000,
    MPI_SAS_ENCLOS_PGAD_FORM_SHIFT              = 28,
    MPI_SAS_ENCLOS_PGAD_FORM_GET_NEXT_HANDLE    = 0x00000000,
    MPI_SAS_ENCLOS_PGAD_FORM_HANDLE             = 0x00000001,
    MPI_SAS_ENCLOS_PGAD_GNH_HANDLE_MASK         = 0x0000FFFF,
    MPI_SAS_ENCLOS_PGAD_GNH_HANDLE_SHIFT        = 0,
    MPI_SAS_ENCLOS_PGAD_H_HANDLE_MASK           = 0x0000FFFF,
    MPI_SAS_ENCLOS_PGAD_H_HANDLE_SHIFT          = 0,
};

/* Too many structs and definitions... see mptconfig.c for the few
 * that are used.
 */

/****************************************************************************/
/*  Firmware Upload message and associated structures                       */
/****************************************************************************/

enum {
    /* defines for using the ProductId field */
    MPI_FW_HEADER_PID_TYPE_MASK                     = 0xF000,
    MPI_FW_HEADER_PID_TYPE_SCSI                     = 0x0000,
    MPI_FW_HEADER_PID_TYPE_FC                       = 0x1000,
    MPI_FW_HEADER_PID_TYPE_SAS                      = 0x2000,

    MPI_FW_HEADER_PID_PROD_MASK                     = 0x0F00,
    MPI_FW_HEADER_PID_PROD_INITIATOR_SCSI           = 0x0100,
    MPI_FW_HEADER_PID_PROD_TARGET_INITIATOR_SCSI    = 0x0200,
    MPI_FW_HEADER_PID_PROD_TARGET_SCSI              = 0x0300,
    MPI_FW_HEADER_PID_PROD_IM_SCSI                  = 0x0400,
    MPI_FW_HEADER_PID_PROD_IS_SCSI                  = 0x0500,
    MPI_FW_HEADER_PID_PROD_CTX_SCSI                 = 0x0600,
    MPI_FW_HEADER_PID_PROD_IR_SCSI                  = 0x0700,

    MPI_FW_HEADER_PID_FAMILY_MASK                   = 0x00FF,

    /* SCSI */
    MPI_FW_HEADER_PID_FAMILY_1030A0_SCSI            = 0x0001,
    MPI_FW_HEADER_PID_FAMILY_1030B0_SCSI            = 0x0002,
    MPI_FW_HEADER_PID_FAMILY_1030B1_SCSI            = 0x0003,
    MPI_FW_HEADER_PID_FAMILY_1030C0_SCSI            = 0x0004,
    MPI_FW_HEADER_PID_FAMILY_1020A0_SCSI            = 0x0005,
    MPI_FW_HEADER_PID_FAMILY_1020B0_SCSI            = 0x0006,
    MPI_FW_HEADER_PID_FAMILY_1020B1_SCSI            = 0x0007,
    MPI_FW_HEADER_PID_FAMILY_1020C0_SCSI            = 0x0008,
    MPI_FW_HEADER_PID_FAMILY_1035A0_SCSI            = 0x0009,
    MPI_FW_HEADER_PID_FAMILY_1035B0_SCSI            = 0x000A,
    MPI_FW_HEADER_PID_FAMILY_1030TA0_SCSI           = 0x000B,
    MPI_FW_HEADER_PID_FAMILY_1020TA0_SCSI           = 0x000C,

    /* Fibre Channel */
    MPI_FW_HEADER_PID_FAMILY_909_FC                 = 0x0000,
    MPI_FW_HEADER_PID_FAMILY_919_FC                 = 0x0001, /* 919 and 929     */
    MPI_FW_HEADER_PID_FAMILY_919X_FC                = 0x0002, /* 919X and 929X   */
    MPI_FW_HEADER_PID_FAMILY_919XL_FC               = 0x0003, /* 919XL and 929XL */
    MPI_FW_HEADER_PID_FAMILY_939X_FC                = 0x0004, /* 939X and 949X   */
    MPI_FW_HEADER_PID_FAMILY_959_FC                 = 0x0005,
    MPI_FW_HEADER_PID_FAMILY_949E_FC                = 0x0006,

    /* SAS */
    MPI_FW_HEADER_PID_FAMILY_1064_SAS               = 0x0001,
    MPI_FW_HEADER_PID_FAMILY_1068_SAS               = 0x0002,
    MPI_FW_HEADER_PID_FAMILY_1078_SAS               = 0x0003,
    MPI_FW_HEADER_PID_FAMILY_106xE_SAS              = 0x0004, /* 1068E, 1066E, and 1064E */
};

#endif
