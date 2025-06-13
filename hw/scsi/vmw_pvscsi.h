/*
 * VMware PVSCSI header file
 *
 * Copyright (C) 2008-2009, VMware, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <https://www.gnu.org/licenses/>.
 *
 * Maintained by: Arvind Kumar <arvindkumar@vmware.com>
 *
 */

#ifndef VMW_PVSCSI_H
#define VMW_PVSCSI_H

#define VMW_PAGE_SIZE  (4096)
#define VMW_PAGE_SHIFT (12)

#define MASK(n)        ((1 << (n)) - 1)        /* make an n-bit mask */

/*
 * host adapter status/error codes
 */
enum HostBusAdapterStatus {
   BTSTAT_SUCCESS       = 0x00,  /* CCB complete normally with no errors */
   BTSTAT_LINKED_COMMAND_COMPLETED           = 0x0a,
   BTSTAT_LINKED_COMMAND_COMPLETED_WITH_FLAG = 0x0b,
   BTSTAT_DATA_UNDERRUN = 0x0c,
   BTSTAT_SELTIMEO      = 0x11,  /* SCSI selection timeout */
   BTSTAT_DATARUN       = 0x12,  /* data overrun/underrun */
   BTSTAT_BUSFREE       = 0x13,  /* unexpected bus free */
   BTSTAT_INVPHASE      = 0x14,  /* invalid bus phase or sequence */
                                 /* requested by target           */
   BTSTAT_LUNMISMATCH   = 0x17,  /* linked CCB has different LUN  */
                                 /* from first CCB                */
   BTSTAT_SENSFAILED    = 0x1b,  /* auto request sense failed */
   BTSTAT_TAGREJECT     = 0x1c,  /* SCSI II tagged queueing message */
                                 /* rejected by target              */
   BTSTAT_BADMSG        = 0x1d,  /* unsupported message received by */
                                 /* the host adapter                */
   BTSTAT_HAHARDWARE    = 0x20,  /* host adapter hardware failed */
   BTSTAT_NORESPONSE    = 0x21,  /* target did not respond to SCSI ATN, */
                                 /* sent a SCSI RST                     */
   BTSTAT_SENTRST       = 0x22,  /* host adapter asserted a SCSI RST */
   BTSTAT_RECVRST       = 0x23,  /* other SCSI devices asserted a SCSI RST */
   BTSTAT_DISCONNECT    = 0x24,  /* target device reconnected improperly */
                                 /* (w/o tag)                            */
   BTSTAT_BUSRESET      = 0x25,  /* host adapter issued BUS device reset */
   BTSTAT_ABORTQUEUE    = 0x26,  /* abort queue generated */
   BTSTAT_HASOFTWARE    = 0x27,  /* host adapter software error */
   BTSTAT_HATIMEOUT     = 0x30,  /* host adapter hardware timeout error */
   BTSTAT_SCSIPARITY    = 0x34,  /* SCSI parity error detected */
};

/*
 * Register offsets.
 *
 * These registers are accessible both via i/o space and mm i/o.
 */

enum PVSCSIRegOffset {
    PVSCSI_REG_OFFSET_COMMAND        =    0x0,
    PVSCSI_REG_OFFSET_COMMAND_DATA   =    0x4,
    PVSCSI_REG_OFFSET_COMMAND_STATUS =    0x8,
    PVSCSI_REG_OFFSET_LAST_STS_0     =  0x100,
    PVSCSI_REG_OFFSET_LAST_STS_1     =  0x104,
    PVSCSI_REG_OFFSET_LAST_STS_2     =  0x108,
    PVSCSI_REG_OFFSET_LAST_STS_3     =  0x10c,
    PVSCSI_REG_OFFSET_INTR_STATUS    = 0x100c,
    PVSCSI_REG_OFFSET_INTR_MASK      = 0x2010,
    PVSCSI_REG_OFFSET_KICK_NON_RW_IO = 0x3014,
    PVSCSI_REG_OFFSET_DEBUG          = 0x3018,
    PVSCSI_REG_OFFSET_KICK_RW_IO     = 0x4018,
};

/*
 * Virtual h/w commands.
 */

enum PVSCSICommands {
    PVSCSI_CMD_FIRST             = 0, /* has to be first */

    PVSCSI_CMD_ADAPTER_RESET     = 1,
    PVSCSI_CMD_ISSUE_SCSI        = 2,
    PVSCSI_CMD_SETUP_RINGS       = 3,
    PVSCSI_CMD_RESET_BUS         = 4,
    PVSCSI_CMD_RESET_DEVICE      = 5,
    PVSCSI_CMD_ABORT_CMD         = 6,
    PVSCSI_CMD_CONFIG            = 7,
    PVSCSI_CMD_SETUP_MSG_RING    = 8,
    PVSCSI_CMD_DEVICE_UNPLUG     = 9,

    PVSCSI_CMD_LAST              = 10  /* has to be last */
};

#define PVSCSI_COMMAND_PROCESSING_SUCCEEDED   (0)
#define PVSCSI_COMMAND_PROCESSING_FAILED     (-1)
#define PVSCSI_COMMAND_NOT_ENOUGH_DATA       (-2)

/*
 * Command descriptor for PVSCSI_CMD_RESET_DEVICE --
 */

struct PVSCSICmdDescResetDevice {
    uint32_t    target;
    uint8_t     lun[8];
} QEMU_PACKED;

typedef struct PVSCSICmdDescResetDevice PVSCSICmdDescResetDevice;

/*
 * Command descriptor for PVSCSI_CMD_ABORT_CMD --
 *
 * - currently does not support specifying the LUN.
 * - pad should be 0.
 */

struct PVSCSICmdDescAbortCmd {
    uint64_t    context;
    uint32_t    target;
    uint32_t    pad;
} QEMU_PACKED;

typedef struct PVSCSICmdDescAbortCmd PVSCSICmdDescAbortCmd;

/*
 * Command descriptor for PVSCSI_CMD_SETUP_RINGS --
 *
 * Notes:
 * - reqRingNumPages and cmpRingNumPages need to be power of two.
 * - reqRingNumPages and cmpRingNumPages need to be different from 0,
 * - reqRingNumPages and cmpRingNumPages need to be inferior to
 *   PVSCSI_SETUP_RINGS_MAX_NUM_PAGES.
 */

#define PVSCSI_SETUP_RINGS_MAX_NUM_PAGES        32
struct PVSCSICmdDescSetupRings {
    uint32_t    reqRingNumPages;
    uint32_t    cmpRingNumPages;
    uint64_t    ringsStatePPN;
    uint64_t    reqRingPPNs[PVSCSI_SETUP_RINGS_MAX_NUM_PAGES];
    uint64_t    cmpRingPPNs[PVSCSI_SETUP_RINGS_MAX_NUM_PAGES];
} QEMU_PACKED;

typedef struct PVSCSICmdDescSetupRings PVSCSICmdDescSetupRings;

/*
 * Command descriptor for PVSCSI_CMD_SETUP_MSG_RING --
 *
 * Notes:
 * - this command was not supported in the initial revision of the h/w
 *   interface. Before using it, you need to check that it is supported by
 *   writing PVSCSI_CMD_SETUP_MSG_RING to the 'command' register, then
 *   immediately after read the 'command status' register:
 *       * a value of -1 means that the cmd is NOT supported,
 *       * a value != -1 means that the cmd IS supported.
 *   If it's supported the 'command status' register should return:
 *      sizeof(PVSCSICmdDescSetupMsgRing) / sizeof(uint32_t).
 * - this command should be issued _after_ the usual SETUP_RINGS so that the
 *   RingsState page is already setup. If not, the command is a nop.
 * - numPages needs to be a power of two,
 * - numPages needs to be different from 0,
 * - pad should be zero.
 */

#define PVSCSI_SETUP_MSG_RING_MAX_NUM_PAGES  16

struct PVSCSICmdDescSetupMsgRing {
    uint32_t    numPages;
    uint32_t    pad;
    uint64_t    ringPPNs[PVSCSI_SETUP_MSG_RING_MAX_NUM_PAGES];
} QEMU_PACKED;

typedef struct PVSCSICmdDescSetupMsgRing PVSCSICmdDescSetupMsgRing;

enum PVSCSIMsgType {
    PVSCSI_MSG_DEV_ADDED          = 0,
    PVSCSI_MSG_DEV_REMOVED        = 1,
    PVSCSI_MSG_LAST               = 2,
};

/*
 * Msg descriptor.
 *
 * sizeof(struct PVSCSIRingMsgDesc) == 128.
 *
 * - type is of type enum PVSCSIMsgType.
 * - the content of args depend on the type of event being delivered.
 */

struct PVSCSIRingMsgDesc {
    uint32_t    type;
    uint32_t    args[31];
} QEMU_PACKED;

typedef struct PVSCSIRingMsgDesc PVSCSIRingMsgDesc;

struct PVSCSIMsgDescDevStatusChanged {
    uint32_t    type;  /* PVSCSI_MSG_DEV _ADDED / _REMOVED */
    uint32_t    bus;
    uint32_t    target;
    uint8_t     lun[8];
    uint32_t    pad[27];
} QEMU_PACKED;

typedef struct PVSCSIMsgDescDevStatusChanged PVSCSIMsgDescDevStatusChanged;

/*
 * Rings state.
 *
 * - the fields:
 *    . msgProdIdx,
 *    . msgConsIdx,
 *    . msgNumEntriesLog2,
 *   .. are only used once the SETUP_MSG_RING cmd has been issued.
 * - 'pad' helps to ensure that the msg related fields are on their own
 *   cache-line.
 */

struct PVSCSIRingsState {
    uint32_t    reqProdIdx;
    uint32_t    reqConsIdx;
    uint32_t    reqNumEntriesLog2;

    uint32_t    cmpProdIdx;
    uint32_t    cmpConsIdx;
    uint32_t    cmpNumEntriesLog2;

    uint8_t     pad[104];

    uint32_t    msgProdIdx;
    uint32_t    msgConsIdx;
    uint32_t    msgNumEntriesLog2;
} QEMU_PACKED;

typedef struct PVSCSIRingsState PVSCSIRingsState;

/*
 * Request descriptor.
 *
 * sizeof(RingReqDesc) = 128
 *
 * - context: is a unique identifier of a command. It could normally be any
 *   64bit value, however we currently store it in the serialNumber variable
 *   of struct SCSI_Command, so we have the following restrictions due to the
 *   way this field is handled in the vmkernel storage stack:
 *    * this value can't be 0,
 *    * the upper 32bit need to be 0 since serialNumber is as a uint32_t.
 *   Currently tracked as PR 292060.
 * - dataLen: contains the total number of bytes that need to be transferred.
 * - dataAddr:
 *   * if PVSCSI_FLAG_CMD_WITH_SG_LIST is set: dataAddr is the PA of the first
 *     s/g table segment, each s/g segment is entirely contained on a single
 *     page of physical memory,
 *   * if PVSCSI_FLAG_CMD_WITH_SG_LIST is NOT set, then dataAddr is the PA of
 *     the buffer used for the DMA transfer,
 * - flags:
 *   * PVSCSI_FLAG_CMD_WITH_SG_LIST: see dataAddr above,
 *   * PVSCSI_FLAG_CMD_DIR_NONE: no DMA involved,
 *   * PVSCSI_FLAG_CMD_DIR_TOHOST: transfer from device to main memory,
 *   * PVSCSI_FLAG_CMD_DIR_TODEVICE: transfer from main memory to device,
 *   * PVSCSI_FLAG_CMD_OUT_OF_BAND_CDB: reserved to handle CDBs larger than
 *     16bytes. To be specified.
 * - vcpuHint: vcpuId of the processor that will be most likely waiting for the
 *   completion of the i/o. For guest OSes that use lowest priority message
 *   delivery mode (such as windows), we use this "hint" to deliver the
 *   completion action to the proper vcpu. For now, we can use the vcpuId of
 *   the processor that initiated the i/o as a likely candidate for the vcpu
 *   that will be waiting for the completion..
 * - bus should be 0: we currently only support bus 0 for now.
 * - unused should be zero'd.
 */

#define PVSCSI_FLAG_CMD_WITH_SG_LIST        (1 << 0)
#define PVSCSI_FLAG_CMD_OUT_OF_BAND_CDB     (1 << 1)
#define PVSCSI_FLAG_CMD_DIR_NONE            (1 << 2)
#define PVSCSI_FLAG_CMD_DIR_TOHOST          (1 << 3)
#define PVSCSI_FLAG_CMD_DIR_TODEVICE        (1 << 4)

#define PVSCSI_KNOWN_FLAGS \
  (PVSCSI_FLAG_CMD_WITH_SG_LIST     | \
   PVSCSI_FLAG_CMD_OUT_OF_BAND_CDB  | \
   PVSCSI_FLAG_CMD_DIR_NONE         | \
   PVSCSI_FLAG_CMD_DIR_TOHOST       | \
   PVSCSI_FLAG_CMD_DIR_TODEVICE)

struct PVSCSIRingReqDesc {
    uint64_t    context;
    uint64_t    dataAddr;
    uint64_t    dataLen;
    uint64_t    senseAddr;
    uint32_t    senseLen;
    uint32_t    flags;
    uint8_t     cdb[16];
    uint8_t     cdbLen;
    uint8_t     lun[8];
    uint8_t     tag;
    uint8_t     bus;
    uint8_t     target;
    uint8_t     vcpuHint;
    uint8_t     unused[59];
} QEMU_PACKED;

typedef struct PVSCSIRingReqDesc PVSCSIRingReqDesc;

/*
 * Scatter-gather list management.
 *
 * As described above, when PVSCSI_FLAG_CMD_WITH_SG_LIST is set in the
 * RingReqDesc.flags, then RingReqDesc.dataAddr is the PA of the first s/g
 * table segment.
 *
 * - each segment of the s/g table contain a succession of struct
 *   PVSCSISGElement.
 * - each segment is entirely contained on a single physical page of memory.
 * - a "chain" s/g element has the flag PVSCSI_SGE_FLAG_CHAIN_ELEMENT set in
 *   PVSCSISGElement.flags and in this case:
 *     * addr is the PA of the next s/g segment,
 *     * length is undefined, assumed to be 0.
 */

struct PVSCSISGElement {
    uint64_t    addr;
    uint32_t    length;
    uint32_t    flags;
} QEMU_PACKED;

typedef struct PVSCSISGElement PVSCSISGElement;

/*
 * Completion descriptor.
 *
 * sizeof(RingCmpDesc) = 32
 *
 * - context: identifier of the command. The same thing that was specified
 *   under "context" as part of struct RingReqDesc at initiation time,
 * - dataLen: number of bytes transferred for the actual i/o operation,
 * - senseLen: number of bytes written into the sense buffer,
 * - hostStatus: adapter status,
 * - scsiStatus: device status,
 * - pad should be zero.
 */

struct PVSCSIRingCmpDesc {
    uint64_t    context;
    uint64_t    dataLen;
    uint32_t    senseLen;
    uint16_t    hostStatus;
    uint16_t    scsiStatus;
    uint32_t    pad[2];
} QEMU_PACKED;

typedef struct PVSCSIRingCmpDesc PVSCSIRingCmpDesc;

/*
 * Interrupt status / IRQ bits.
 */

#define PVSCSI_INTR_CMPL_0                 (1 << 0)
#define PVSCSI_INTR_CMPL_1                 (1 << 1)
#define PVSCSI_INTR_CMPL_MASK              MASK(2)

#define PVSCSI_INTR_MSG_0                  (1 << 2)
#define PVSCSI_INTR_MSG_1                  (1 << 3)
#define PVSCSI_INTR_MSG_MASK               (MASK(2) << 2)

#define PVSCSI_INTR_ALL_SUPPORTED          MASK(4)

/*
 * Number of MSI-X vectors supported.
 */
#define PVSCSI_MAX_INTRS        24

/*
 * Enumeration of supported MSI-X vectors
 */
#define PVSCSI_VECTOR_COMPLETION   0

/*
 * Misc constants for the rings.
 */

#define PVSCSI_MAX_NUM_PAGES_REQ_RING   PVSCSI_SETUP_RINGS_MAX_NUM_PAGES
#define PVSCSI_MAX_NUM_PAGES_CMP_RING   PVSCSI_SETUP_RINGS_MAX_NUM_PAGES
#define PVSCSI_MAX_NUM_PAGES_MSG_RING   PVSCSI_SETUP_MSG_RING_MAX_NUM_PAGES

#define PVSCSI_MAX_NUM_REQ_ENTRIES_PER_PAGE \
                (VMW_PAGE_SIZE / sizeof(struct PVSCSIRingReqDesc))

#define PVSCSI_MAX_NUM_CMP_ENTRIES_PER_PAGE \
                (VMW_PAGE_SIZE / sizeof(PVSCSIRingCmpDesc))

#define PVSCSI_MAX_NUM_MSG_ENTRIES_PER_PAGE \
                (VMW_PAGE_SIZE / sizeof(PVSCSIRingMsgDesc))

#define PVSCSI_MAX_REQ_QUEUE_DEPTH \
    (PVSCSI_MAX_NUM_PAGES_REQ_RING * PVSCSI_MAX_NUM_REQ_ENTRIES_PER_PAGE)

#define PVSCSI_MEM_SPACE_COMMAND_NUM_PAGES     1
#define PVSCSI_MEM_SPACE_INTR_STATUS_NUM_PAGES 1
#define PVSCSI_MEM_SPACE_MISC_NUM_PAGES        2
#define PVSCSI_MEM_SPACE_KICK_IO_NUM_PAGES     2
#define PVSCSI_MEM_SPACE_MSIX_NUM_PAGES        2

enum PVSCSIMemSpace {
    PVSCSI_MEM_SPACE_COMMAND_PAGE       = 0,
    PVSCSI_MEM_SPACE_INTR_STATUS_PAGE   = 1,
    PVSCSI_MEM_SPACE_MISC_PAGE          = 2,
    PVSCSI_MEM_SPACE_KICK_IO_PAGE       = 4,
    PVSCSI_MEM_SPACE_MSIX_TABLE_PAGE    = 6,
    PVSCSI_MEM_SPACE_MSIX_PBA_PAGE      = 7,
};

#define PVSCSI_MEM_SPACE_NUM_PAGES \
    (PVSCSI_MEM_SPACE_COMMAND_NUM_PAGES +       \
     PVSCSI_MEM_SPACE_INTR_STATUS_NUM_PAGES +   \
     PVSCSI_MEM_SPACE_MISC_NUM_PAGES +          \
     PVSCSI_MEM_SPACE_KICK_IO_NUM_PAGES +       \
     PVSCSI_MEM_SPACE_MSIX_NUM_PAGES)

#define PVSCSI_MEM_SPACE_SIZE    (PVSCSI_MEM_SPACE_NUM_PAGES * VMW_PAGE_SIZE)

#endif /* VMW_PVSCSI_H */
