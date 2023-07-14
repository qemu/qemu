/*
 * NetBSD header file, copied from
 * http://gitorious.org/freebsd/freebsd/blobs/HEAD/sys/dev/mfi/mfireg.h
 */
/*-
 * Copyright (c) 2006 IronPort Systems
 * Copyright (c) 2007 LSI Corp.
 * Copyright (c) 2007 Rajesh Prabhakaran.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef SCSI_MFI_H
#define SCSI_MFI_H

/*
 * MegaRAID SAS MFI firmware definitions
 */

/*
 * Start with the register set.  All registers are 32 bits wide.
 * The usual Intel IOP style setup.
 */
#define MFI_IMSG0 0x10    /* Inbound message 0 */
#define MFI_IMSG1 0x14    /* Inbound message 1 */
#define MFI_OMSG0 0x18    /* Outbound message 0 */
#define MFI_OMSG1 0x1c    /* Outbound message 1 */
#define MFI_IDB   0x20    /* Inbound doorbell */
#define MFI_ISTS  0x24    /* Inbound interrupt status */
#define MFI_IMSK  0x28    /* Inbound interrupt mask */
#define MFI_ODB   0x2c    /* Outbound doorbell */
#define MFI_OSTS  0x30    /* Outbound interrupt status */
#define MFI_OMSK  0x34    /* Outbound interrupt mask */
#define MFI_IQP   0x40    /* Inbound queue port */
#define MFI_OQP   0x44    /* Outbound queue port */

/*
 * 1078 specific related register
 */
#define MFI_ODR0        0x9c            /* outbound doorbell register0 */
#define MFI_ODCR0       0xa0            /* outbound doorbell clear register0  */
#define MFI_OSP0        0xb0            /* outbound scratch pad0  */
#define MFI_OSP1        0xb4            /* outbound scratch pad1  */
#define MFI_IQPL        0xc0            /* Inbound queue port (low bytes)  */
#define MFI_IQPH        0xc4            /* Inbound queue port (high bytes)  */
#define MFI_DIAG        0xf8            /* Host diag */
#define MFI_SEQ         0xfc            /* Sequencer offset */
#define MFI_1078_EIM    0x80000004      /* 1078 enable interrupt mask  */
#define MFI_RMI         0x2             /* reply message interrupt  */
#define MFI_1078_RM     0x80000000      /* reply 1078 message interrupt  */
#define MFI_ODC         0x4             /* outbound doorbell change interrupt */

/*
 * gen2 specific changes
 */
#define MFI_GEN2_EIM    0x00000005      /* gen2 enable interrupt mask */
#define MFI_GEN2_RM     0x00000001      /* reply gen2 message interrupt */

/*
 * skinny specific changes
 */
#define MFI_SKINNY_IDB  0x00    /* Inbound doorbell is at 0x00 for skinny */
#define MFI_SKINNY_RM   0x00000001      /* reply skinny message interrupt */

/* Bits for MFI_OSTS */
#define MFI_OSTS_INTR_VALID     0x00000002

/*
 * Firmware state values.  Found in OMSG0 during initialization.
 */
#define MFI_FWSTATE_MASK                0xf0000000
#define MFI_FWSTATE_UNDEFINED           0x00000000
#define MFI_FWSTATE_BB_INIT             0x10000000
#define MFI_FWSTATE_FW_INIT             0x40000000
#define MFI_FWSTATE_WAIT_HANDSHAKE      0x60000000
#define MFI_FWSTATE_FW_INIT_2           0x70000000
#define MFI_FWSTATE_DEVICE_SCAN         0x80000000
#define MFI_FWSTATE_BOOT_MSG_PENDING    0x90000000
#define MFI_FWSTATE_FLUSH_CACHE         0xa0000000
#define MFI_FWSTATE_READY               0xb0000000
#define MFI_FWSTATE_OPERATIONAL         0xc0000000
#define MFI_FWSTATE_FAULT               0xf0000000
#define MFI_FWSTATE_MAXSGL_MASK         0x00ff0000
#define MFI_FWSTATE_MAXCMD_MASK         0x0000ffff
#define MFI_FWSTATE_MSIX_SUPPORTED      0x04000000
#define MFI_FWSTATE_HOSTMEMREQD_MASK    0x08000000

/*
 * Control bits to drive the card to ready state.  These go into the IDB
 * register.
 */
#define MFI_FWINIT_ABORT        0x00000001 /* Abort all pending commands */
#define MFI_FWINIT_READY        0x00000002 /* Move from operational to ready */
#define MFI_FWINIT_MFIMODE      0x00000004 /* unknown */
#define MFI_FWINIT_CLEAR_HANDSHAKE 0x00000008 /* Respond to WAIT_HANDSHAKE */
#define MFI_FWINIT_HOTPLUG      0x00000010
#define MFI_FWINIT_STOP_ADP     0x00000020 /* Move to operational, stop */
#define MFI_FWINIT_ADP_RESET    0x00000040 /* Reset ADP */

/*
 * Control bits for the DIAG register
 */
#define MFI_DIAG_WRITE_ENABLE 0x00000080
#define MFI_DIAG_RESET_ADP    0x00000004

/* MFI Commands */
typedef enum {
    MFI_CMD_INIT = 0x00,
    MFI_CMD_LD_READ,
    MFI_CMD_LD_WRITE,
    MFI_CMD_LD_SCSI_IO,
    MFI_CMD_PD_SCSI_IO,
    MFI_CMD_DCMD,
    MFI_CMD_ABORT,
    MFI_CMD_SMP,
    MFI_CMD_STP
} mfi_cmd_t;

/* Direct commands */
typedef enum {
    MFI_DCMD_CTRL_MFI_HOST_MEM_ALLOC =  0x0100e100,
    MFI_DCMD_CTRL_GET_INFO =            0x01010000,
    MFI_DCMD_CTRL_GET_PROPERTIES =      0x01020100,
    MFI_DCMD_CTRL_SET_PROPERTIES =      0x01020200,
    MFI_DCMD_CTRL_ALARM =               0x01030000,
    MFI_DCMD_CTRL_ALARM_GET =           0x01030100,
    MFI_DCMD_CTRL_ALARM_ENABLE =        0x01030200,
    MFI_DCMD_CTRL_ALARM_DISABLE =       0x01030300,
    MFI_DCMD_CTRL_ALARM_SILENCE =       0x01030400,
    MFI_DCMD_CTRL_ALARM_TEST =          0x01030500,
    MFI_DCMD_CTRL_EVENT_GETINFO =       0x01040100,
    MFI_DCMD_CTRL_EVENT_CLEAR =         0x01040200,
    MFI_DCMD_CTRL_EVENT_GET =           0x01040300,
    MFI_DCMD_CTRL_EVENT_COUNT =         0x01040400,
    MFI_DCMD_CTRL_EVENT_WAIT =          0x01040500,
    MFI_DCMD_CTRL_SHUTDOWN =            0x01050000,
    MFI_DCMD_HIBERNATE_STANDBY =        0x01060000,
    MFI_DCMD_CTRL_GET_TIME =            0x01080101,
    MFI_DCMD_CTRL_SET_TIME =            0x01080102,
    MFI_DCMD_CTRL_BIOS_DATA_GET =       0x010c0100,
    MFI_DCMD_CTRL_BIOS_DATA_SET =       0x010c0200,
    MFI_DCMD_CTRL_FACTORY_DEFAULTS =    0x010d0000,
    MFI_DCMD_CTRL_MFC_DEFAULTS_GET =    0x010e0201,
    MFI_DCMD_CTRL_MFC_DEFAULTS_SET =    0x010e0202,
    MFI_DCMD_CTRL_CACHE_FLUSH =         0x01101000,
    MFI_DCMD_PD_GET_LIST =              0x02010000,
    MFI_DCMD_PD_LIST_QUERY =            0x02010100,
    MFI_DCMD_PD_GET_INFO =              0x02020000,
    MFI_DCMD_PD_STATE_SET =             0x02030100,
    MFI_DCMD_PD_REBUILD =               0x02040100,
    MFI_DCMD_PD_BLINK =                 0x02070100,
    MFI_DCMD_PD_UNBLINK =               0x02070200,
    MFI_DCMD_LD_GET_LIST =              0x03010000,
    MFI_DCMD_LD_LIST_QUERY =            0x03010100,
    MFI_DCMD_LD_GET_INFO =              0x03020000,
    MFI_DCMD_LD_GET_PROP =              0x03030000,
    MFI_DCMD_LD_SET_PROP =              0x03040000,
    MFI_DCMD_LD_DELETE =                0x03090000,
    MFI_DCMD_CFG_READ =                 0x04010000,
    MFI_DCMD_CFG_ADD =                  0x04020000,
    MFI_DCMD_CFG_CLEAR =                0x04030000,
    MFI_DCMD_CFG_FOREIGN_READ =         0x04060100,
    MFI_DCMD_CFG_FOREIGN_IMPORT =       0x04060400,
    MFI_DCMD_BBU_STATUS =               0x05010000,
    MFI_DCMD_BBU_CAPACITY_INFO =        0x05020000,
    MFI_DCMD_BBU_DESIGN_INFO =          0x05030000,
    MFI_DCMD_BBU_PROP_GET =             0x05050100,
    MFI_DCMD_CLUSTER =                  0x08000000,
    MFI_DCMD_CLUSTER_RESET_ALL =        0x08010100,
    MFI_DCMD_CLUSTER_RESET_LD =         0x08010200
} mfi_dcmd_t;

/* Modifiers for MFI_DCMD_CTRL_FLUSHCACHE */
#define MFI_FLUSHCACHE_CTRL     0x01
#define MFI_FLUSHCACHE_DISK     0x02

/* Modifiers for MFI_DCMD_CTRL_SHUTDOWN */
#define MFI_SHUTDOWN_SPINDOWN   0x01

/*
 * MFI Frame flags
 */
typedef enum {
    MFI_FRAME_DONT_POST_IN_REPLY_QUEUE =        0x0001,
    MFI_FRAME_SGL64 =                           0x0002,
    MFI_FRAME_SENSE64 =                         0x0004,
    MFI_FRAME_DIR_WRITE =                       0x0008,
    MFI_FRAME_DIR_READ =                        0x0010,
    MFI_FRAME_IEEE_SGL =                        0x0020,
} mfi_frame_flags;

/* MFI Status codes */
typedef enum {
    MFI_STAT_OK =                       0x00,
    MFI_STAT_INVALID_CMD,
    MFI_STAT_INVALID_DCMD,
    MFI_STAT_INVALID_PARAMETER,
    MFI_STAT_INVALID_SEQUENCE_NUMBER,
    MFI_STAT_ABORT_NOT_POSSIBLE,
    MFI_STAT_APP_HOST_CODE_NOT_FOUND,
    MFI_STAT_APP_IN_USE,
    MFI_STAT_APP_NOT_INITIALIZED,
    MFI_STAT_ARRAY_INDEX_INVALID,
    MFI_STAT_ARRAY_ROW_NOT_EMPTY,
    MFI_STAT_CONFIG_RESOURCE_CONFLICT,
    MFI_STAT_DEVICE_NOT_FOUND,
    MFI_STAT_DRIVE_TOO_SMALL,
    MFI_STAT_FLASH_ALLOC_FAIL,
    MFI_STAT_FLASH_BUSY,
    MFI_STAT_FLASH_ERROR =              0x10,
    MFI_STAT_FLASH_IMAGE_BAD,
    MFI_STAT_FLASH_IMAGE_INCOMPLETE,
    MFI_STAT_FLASH_NOT_OPEN,
    MFI_STAT_FLASH_NOT_STARTED,
    MFI_STAT_FLUSH_FAILED,
    MFI_STAT_HOST_CODE_NOT_FOUNT,
    MFI_STAT_LD_CC_IN_PROGRESS,
    MFI_STAT_LD_INIT_IN_PROGRESS,
    MFI_STAT_LD_LBA_OUT_OF_RANGE,
    MFI_STAT_LD_MAX_CONFIGURED,
    MFI_STAT_LD_NOT_OPTIMAL,
    MFI_STAT_LD_RBLD_IN_PROGRESS,
    MFI_STAT_LD_RECON_IN_PROGRESS,
    MFI_STAT_LD_WRONG_RAID_LEVEL,
    MFI_STAT_MAX_SPARES_EXCEEDED,
    MFI_STAT_MEMORY_NOT_AVAILABLE =     0x20,
    MFI_STAT_MFC_HW_ERROR,
    MFI_STAT_NO_HW_PRESENT,
    MFI_STAT_NOT_FOUND,
    MFI_STAT_NOT_IN_ENCL,
    MFI_STAT_PD_CLEAR_IN_PROGRESS,
    MFI_STAT_PD_TYPE_WRONG,
    MFI_STAT_PR_DISABLED,
    MFI_STAT_ROW_INDEX_INVALID,
    MFI_STAT_SAS_CONFIG_INVALID_ACTION,
    MFI_STAT_SAS_CONFIG_INVALID_DATA,
    MFI_STAT_SAS_CONFIG_INVALID_PAGE,
    MFI_STAT_SAS_CONFIG_INVALID_TYPE,
    MFI_STAT_SCSI_DONE_WITH_ERROR,
    MFI_STAT_SCSI_IO_FAILED,
    MFI_STAT_SCSI_RESERVATION_CONFLICT,
    MFI_STAT_SHUTDOWN_FAILED =          0x30,
    MFI_STAT_TIME_NOT_SET,
    MFI_STAT_WRONG_STATE,
    MFI_STAT_LD_OFFLINE,
    MFI_STAT_PEER_NOTIFICATION_REJECTED,
    MFI_STAT_PEER_NOTIFICATION_FAILED,
    MFI_STAT_RESERVATION_IN_PROGRESS,
    MFI_STAT_I2C_ERRORS_DETECTED,
    MFI_STAT_PCI_ERRORS_DETECTED,
    MFI_STAT_DIAG_FAILED,
    MFI_STAT_BOOT_MSG_PENDING,
    MFI_STAT_FOREIGN_CONFIG_INCOMPLETE,
    MFI_STAT_INVALID_SGL,
    MFI_STAT_UNSUPPORTED_HW,
    MFI_STAT_CC_SCHEDULE_DISABLED,
    MFI_STAT_PD_COPYBACK_IN_PROGRESS,
    MFI_STAT_MULTIPLE_PDS_IN_ARRAY =    0x40,
    MFI_STAT_FW_DOWNLOAD_ERROR,
    MFI_STAT_FEATURE_SECURITY_NOT_ENABLED,
    MFI_STAT_LOCK_KEY_ALREADY_EXISTS,
    MFI_STAT_LOCK_KEY_BACKUP_NOT_ALLOWED,
    MFI_STAT_LOCK_KEY_VERIFY_NOT_ALLOWED,
    MFI_STAT_LOCK_KEY_VERIFY_FAILED,
    MFI_STAT_LOCK_KEY_REKEY_NOT_ALLOWED,
    MFI_STAT_LOCK_KEY_INVALID,
    MFI_STAT_LOCK_KEY_ESCROW_INVALID,
    MFI_STAT_LOCK_KEY_BACKUP_REQUIRED,
    MFI_STAT_SECURE_LD_EXISTS,
    MFI_STAT_LD_SECURE_NOT_ALLOWED,
    MFI_STAT_REPROVISION_NOT_ALLOWED,
    MFI_STAT_PD_SECURITY_TYPE_WRONG,
    MFI_STAT_LD_ENCRYPTION_TYPE_INVALID,
    MFI_STAT_CONFIG_FDE_NON_FDE_MIX_NOT_ALLOWED = 0x50,
    MFI_STAT_CONFIG_LD_ENCRYPTION_TYPE_MIX_NOT_ALLOWED,
    MFI_STAT_SECRET_KEY_NOT_ALLOWED,
    MFI_STAT_PD_HW_ERRORS_DETECTED,
    MFI_STAT_LD_CACHE_PINNED,
    MFI_STAT_POWER_STATE_SET_IN_PROGRESS,
    MFI_STAT_POWER_STATE_SET_BUSY,
    MFI_STAT_POWER_STATE_WRONG,
    MFI_STAT_PR_NO_AVAILABLE_PD_FOUND,
    MFI_STAT_CTRL_RESET_REQUIRED,
    MFI_STAT_LOCK_KEY_EKM_NO_BOOT_AGENT,
    MFI_STAT_SNAP_NO_SPACE,
    MFI_STAT_SNAP_PARTIAL_FAILURE,
    MFI_STAT_UPGRADE_KEY_INCOMPATIBLE,
    MFI_STAT_PFK_INCOMPATIBLE,
    MFI_STAT_PD_MAX_UNCONFIGURED,
    MFI_STAT_IO_METRICS_DISABLED =      0x60,
    MFI_STAT_AEC_NOT_STOPPED,
    MFI_STAT_PI_TYPE_WRONG,
    MFI_STAT_LD_PD_PI_INCOMPATIBLE,
    MFI_STAT_PI_NOT_ENABLED,
    MFI_STAT_LD_BLOCK_SIZE_MISMATCH,
    MFI_STAT_INVALID_STATUS =           0xFF
} mfi_status_t;

/* Event classes */
typedef enum {
    MFI_EVT_CLASS_DEBUG =      -2,
    MFI_EVT_CLASS_PROGRESS =   -1,
    MFI_EVT_CLASS_INFO =        0,
    MFI_EVT_CLASS_WARNING =     1,
    MFI_EVT_CLASS_CRITICAL =    2,
    MFI_EVT_CLASS_FATAL =       3,
    MFI_EVT_CLASS_DEAD =        4
} mfi_evt_class_t;

/* Event locales */
typedef enum {
    MFI_EVT_LOCALE_LD =         0x0001,
    MFI_EVT_LOCALE_PD =         0x0002,
    MFI_EVT_LOCALE_ENCL =       0x0004,
    MFI_EVT_LOCALE_BBU =        0x0008,
    MFI_EVT_LOCALE_SAS =        0x0010,
    MFI_EVT_LOCALE_CTRL =       0x0020,
    MFI_EVT_LOCALE_CONFIG =     0x0040,
    MFI_EVT_LOCALE_CLUSTER =    0x0080,
    MFI_EVT_LOCALE_ALL =        0xffff
} mfi_evt_locale_t;

/* Event args */
typedef enum {
    MR_EVT_ARGS_NONE =          0x00,
    MR_EVT_ARGS_CDB_SENSE,
    MR_EVT_ARGS_LD,
    MR_EVT_ARGS_LD_COUNT,
    MR_EVT_ARGS_LD_LBA,
    MR_EVT_ARGS_LD_OWNER,
    MR_EVT_ARGS_LD_LBA_PD_LBA,
    MR_EVT_ARGS_LD_PROG,
    MR_EVT_ARGS_LD_STATE,
    MR_EVT_ARGS_LD_STRIP,
    MR_EVT_ARGS_PD,
    MR_EVT_ARGS_PD_ERR,
    MR_EVT_ARGS_PD_LBA,
    MR_EVT_ARGS_PD_LBA_LD,
    MR_EVT_ARGS_PD_PROG,
    MR_EVT_ARGS_PD_STATE,
    MR_EVT_ARGS_PCI,
    MR_EVT_ARGS_RATE,
    MR_EVT_ARGS_STR,
    MR_EVT_ARGS_TIME,
    MR_EVT_ARGS_ECC,
    MR_EVT_ARGS_LD_PROP,
    MR_EVT_ARGS_PD_SPARE,
    MR_EVT_ARGS_PD_INDEX,
    MR_EVT_ARGS_DIAG_PASS,
    MR_EVT_ARGS_DIAG_FAIL,
    MR_EVT_ARGS_PD_LBA_LBA,
    MR_EVT_ARGS_PORT_PHY,
    MR_EVT_ARGS_PD_MISSING,
    MR_EVT_ARGS_PD_ADDRESS,
    MR_EVT_ARGS_BITMAP,
    MR_EVT_ARGS_CONNECTOR,
    MR_EVT_ARGS_PD_PD,
    MR_EVT_ARGS_PD_FRU,
    MR_EVT_ARGS_PD_PATHINFO,
    MR_EVT_ARGS_PD_POWER_STATE,
    MR_EVT_ARGS_GENERIC,
} mfi_evt_args;

/* Event codes */
#define MR_EVT_CFG_CLEARED                          0x0004
#define MR_EVT_CTRL_SHUTDOWN                        0x002a
#define MR_EVT_LD_STATE_CHANGE                      0x0051
#define MR_EVT_PD_INSERTED                          0x005b
#define MR_EVT_PD_REMOVED                           0x0070
#define MR_EVT_PD_STATE_CHANGED                     0x0072
#define MR_EVT_LD_CREATED                           0x008a
#define MR_EVT_LD_DELETED                           0x008b
#define MR_EVT_FOREIGN_CFG_IMPORTED                 0x00db
#define MR_EVT_LD_OFFLINE                           0x00fc
#define MR_EVT_CTRL_HOST_BUS_SCAN_REQUESTED         0x0152

typedef enum {
    MR_LD_CACHE_WRITE_BACK =            0x01,
    MR_LD_CACHE_WRITE_ADAPTIVE =        0x02,
    MR_LD_CACHE_READ_AHEAD =            0x04,
    MR_LD_CACHE_READ_ADAPTIVE =         0x08,
    MR_LD_CACHE_WRITE_CACHE_BAD_BBU =   0x10,
    MR_LD_CACHE_ALLOW_WRITE_CACHE =     0x20,
    MR_LD_CACHE_ALLOW_READ_CACHE =      0x40
} mfi_ld_cache;

typedef enum {
    MR_PD_CACHE_UNCHANGED  =    0,
    MR_PD_CACHE_ENABLE =        1,
    MR_PD_CACHE_DISABLE =       2
} mfi_pd_cache;

typedef enum {
    MR_PD_QUERY_TYPE_ALL =              0,
    MR_PD_QUERY_TYPE_STATE =            1,
    MR_PD_QUERY_TYPE_POWER_STATE =      2,
    MR_PD_QUERY_TYPE_MEDIA_TYPE =       3,
    MR_PD_QUERY_TYPE_SPEED =            4,
    MR_PD_QUERY_TYPE_EXPOSED_TO_HOST =  5, /*query for system drives */
} mfi_pd_query_type;

typedef enum {
    MR_LD_QUERY_TYPE_ALL =              0,
    MR_LD_QUERY_TYPE_EXPOSED_TO_HOST =  1,
    MR_LD_QUERY_TYPE_USED_TGT_IDS =     2,
    MR_LD_QUERY_TYPE_CLUSTER_ACCESS =   3,
    MR_LD_QUERY_TYPE_CLUSTER_LOCALE =   4,
} mfi_ld_query_type;

/*
 * Other propertities and definitions
 */
#define MFI_MAX_PD_CHANNELS     2
#define MFI_MAX_LD_CHANNELS     2
#define MFI_MAX_CHANNELS        (MFI_MAX_PD_CHANNELS + MFI_MAX_LD_CHANNELS)
#define MFI_MAX_CHANNEL_DEVS  128
#define MFI_DEFAULT_ID         -1
#define MFI_MAX_LUN             8
#define MFI_MAX_LD             64

#define MFI_FRAME_SIZE         64
#define MFI_MBOX_SIZE          12

/* Firmware flashing can take 40s */
#define MFI_POLL_TIMEOUT_SECS  50

/* Allow for speedier math calculations */
#define MFI_SECTOR_LEN        512

/* Scatter Gather elements */
struct mfi_sg32 {
    uint32_t addr;
    uint32_t len;
} QEMU_PACKED;

struct mfi_sg64 {
    uint64_t addr;
    uint32_t len;
} QEMU_PACKED;

struct mfi_sg_skinny {
    uint64_t addr;
    uint32_t len;
    uint32_t flag;
} QEMU_PACKED;

union mfi_sgl {
    struct mfi_sg32 sg32[1];
    struct mfi_sg64 sg64[1];
    struct mfi_sg_skinny sg_skinny[1];
} QEMU_PACKED;

/* Message frames.  All messages have a common header */
struct mfi_frame_header {
    uint8_t frame_cmd;
    uint8_t sense_len;
    uint8_t cmd_status;
    uint8_t scsi_status;
    uint8_t target_id;
    uint8_t lun_id;
    uint8_t cdb_len;
    uint8_t sge_count;
    uint64_t context;
    uint16_t flags;
    uint16_t timeout;
    uint32_t data_len;
} QEMU_PACKED;

struct mfi_init_frame {
    struct mfi_frame_header header;
    uint32_t qinfo_new_addr_lo;
    uint32_t qinfo_new_addr_hi;
    uint32_t qinfo_old_addr_lo;
    uint32_t qinfo_old_addr_hi;
    uint32_t reserved[6];
};

#define MFI_IO_FRAME_SIZE 40
struct mfi_io_frame {
    struct mfi_frame_header header;
    uint32_t sense_addr_lo;
    uint32_t sense_addr_hi;
    uint32_t lba_lo;
    uint32_t lba_hi;
    union mfi_sgl sgl;
} QEMU_PACKED;

#define MFI_PASS_FRAME_SIZE 48
struct mfi_pass_frame {
    struct mfi_frame_header header;
    uint32_t sense_addr_lo;
    uint32_t sense_addr_hi;
    uint8_t cdb[16];
    union mfi_sgl sgl;
} QEMU_PACKED;

#define MFI_DCMD_FRAME_SIZE 40
struct mfi_dcmd_frame {
    struct mfi_frame_header header;
    uint32_t opcode;
    uint8_t mbox[MFI_MBOX_SIZE];
    union mfi_sgl sgl;
} QEMU_PACKED;

struct mfi_abort_frame {
    struct mfi_frame_header header;
    uint64_t abort_context;
    uint32_t abort_mfi_addr_lo;
    uint32_t abort_mfi_addr_hi;
    uint32_t reserved1[6];
} QEMU_PACKED;

struct mfi_smp_frame {
    struct mfi_frame_header header;
    uint64_t sas_addr;
    union {
        struct mfi_sg32 sg32[2];
        struct mfi_sg64 sg64[2];
    } sgl;
} QEMU_PACKED;

struct mfi_stp_frame {
    struct mfi_frame_header header;
    uint16_t fis[10];
    uint32_t stp_flags;
    union {
        struct mfi_sg32 sg32[2];
        struct mfi_sg64 sg64[2];
    } sgl;
} QEMU_PACKED;

union mfi_frame {
    struct mfi_frame_header header;
    struct mfi_init_frame init;
    struct mfi_io_frame io;
    struct mfi_pass_frame pass;
    struct mfi_dcmd_frame dcmd;
    struct mfi_abort_frame abort;
    struct mfi_smp_frame smp;
    struct mfi_stp_frame stp;
    uint64_t raw[8];
    uint8_t bytes[MFI_FRAME_SIZE];
};

#define MFI_SENSE_LEN 128
struct mfi_sense {
    uint8_t     data[MFI_SENSE_LEN];
};

#define MFI_QUEUE_FLAG_CONTEXT64 0x00000002

/* The queue init structure that is passed with the init message */
struct mfi_init_qinfo {
    uint32_t flags;
    uint32_t rq_entries;
    uint32_t rq_addr_lo;
    uint32_t rq_addr_hi;
    uint32_t pi_addr_lo;
    uint32_t pi_addr_hi;
    uint32_t ci_addr_lo;
    uint32_t ci_addr_hi;
} QEMU_PACKED;

/* Controller properties */
struct mfi_ctrl_props {
    uint16_t seq_num;
    uint16_t pred_fail_poll_interval;
    uint16_t intr_throttle_cnt;
    uint16_t intr_throttle_timeout;
    uint8_t rebuild_rate;
    uint8_t patrol_read_rate;
    uint8_t bgi_rate;
    uint8_t cc_rate;
    uint8_t recon_rate;
    uint8_t cache_flush_interval;
    uint8_t spinup_drv_cnt;
    uint8_t spinup_delay;
    uint8_t cluster_enable;
    uint8_t coercion_mode;
    uint8_t alarm_enable;
    uint8_t disable_auto_rebuild;
    uint8_t disable_battery_warn;
    uint8_t ecc_bucket_size;
    uint16_t ecc_bucket_leak_rate;
    uint8_t restore_hotspare_on_insertion;
    uint8_t expose_encl_devices;
    uint8_t maintainPdFailHistory;
    uint8_t disallowHostRequestReordering;
    uint8_t abortCCOnError;
    uint8_t loadBalanceMode;
    uint8_t disableAutoDetectBackplane;
    uint8_t snapVDSpace;
    uint32_t OnOffProperties;
/* set TRUE to disable copyBack (0=copyback enabled) */
#define MFI_CTRL_PROP_CopyBackDisabled           (1 << 0)
#define MFI_CTRL_PROP_SMARTerEnabled             (1 << 1)
#define MFI_CTRL_PROP_PRCorrectUnconfiguredAreas (1 << 2)
#define MFI_CTRL_PROP_UseFdeOnly                 (1 << 3)
#define MFI_CTRL_PROP_DisableNCQ                 (1 << 4)
#define MFI_CTRL_PROP_SSDSMARTerEnabled          (1 << 5)
#define MFI_CTRL_PROP_SSDPatrolReadEnabled       (1 << 6)
#define MFI_CTRL_PROP_EnableSpinDownUnconfigured (1 << 7)
#define MFI_CTRL_PROP_AutoEnhancedImport         (1 << 8)
#define MFI_CTRL_PROP_EnableSecretKeyControl     (1 << 9)
#define MFI_CTRL_PROP_DisableOnlineCtrlReset     (1 << 10)
#define MFI_CTRL_PROP_AllowBootWithPinnedCache   (1 << 11)
#define MFI_CTRL_PROP_DisableSpinDownHS          (1 << 12)
#define MFI_CTRL_PROP_EnableJBOD                 (1 << 13)

    uint8_t autoSnapVDSpace; /* % of source LD to be
                              * reserved for auto snapshot
                              * in snapshot repository, for
                              * metadata and user data
                              * 1=5%, 2=10%, 3=15% and so on
                              */
    uint8_t viewSpace;       /* snapshot writable VIEWs
                              * capacity as a % of source LD
                              * capacity. 0=READ only
                              * 1=5%, 2=10%, 3=15% and so on
                              */
    uint16_t spinDownTime;    /* # of idle minutes before device
                               * is spun down (0=use FW defaults)
                               */
    uint8_t reserved[24];
} QEMU_PACKED;

/* PCI information about the card. */
struct mfi_info_pci {
    uint16_t vendor;
    uint16_t device;
    uint16_t subvendor;
    uint16_t subdevice;
    uint8_t reserved[24];
} QEMU_PACKED;

/* Host (front end) interface information */
struct mfi_info_host {
    uint8_t type;
#define MFI_INFO_HOST_PCIX      0x01
#define MFI_INFO_HOST_PCIE      0x02
#define MFI_INFO_HOST_ISCSI     0x04
#define MFI_INFO_HOST_SAS3G     0x08
    uint8_t reserved[6];
    uint8_t port_count;
    uint64_t port_addr[8];
} QEMU_PACKED;

/* Device (back end) interface information */
struct mfi_info_device {
    uint8_t type;
#define MFI_INFO_DEV_SPI        0x01
#define MFI_INFO_DEV_SAS3G      0x02
#define MFI_INFO_DEV_SATA1      0x04
#define MFI_INFO_DEV_SATA3G     0x08
#define MFI_INFO_DEV_PCIE       0x10
    uint8_t reserved[6];
    uint8_t port_count;
    uint64_t port_addr[8];
} QEMU_PACKED;

/* Firmware component information */
struct mfi_info_component {
    char name[8];
    char version[32];
    char build_date[16];
    char build_time[16];
} QEMU_PACKED;

/* Controller default settings */
struct mfi_defaults {
    uint64_t sas_addr;
    uint8_t phy_polarity;
    uint8_t background_rate;
    uint8_t stripe_size;
    uint8_t flush_time;
    uint8_t write_back;
    uint8_t read_ahead;
    uint8_t cache_when_bbu_bad;
    uint8_t cached_io;
    uint8_t smart_mode;
    uint8_t alarm_disable;
    uint8_t coercion;
    uint8_t zrc_config;
    uint8_t dirty_led_shows_drive_activity;
    uint8_t bios_continue_on_error;
    uint8_t spindown_mode;
    uint8_t allowed_device_types;
    uint8_t allow_mix_in_enclosure;
    uint8_t allow_mix_in_ld;
    uint8_t allow_sata_in_cluster;
    uint8_t max_chained_enclosures;
    uint8_t disable_ctrl_r;
    uint8_t enable_web_bios;
    uint8_t phy_polarity_split;
    uint8_t direct_pd_mapping;
    uint8_t bios_enumerate_lds;
    uint8_t restored_hot_spare_on_insertion;
    uint8_t expose_enclosure_devices;
    uint8_t maintain_pd_fail_history;
    uint8_t disable_puncture;
    uint8_t zero_based_enumeration;
    uint8_t disable_preboot_cli;
    uint8_t show_drive_led_on_activity;
    uint8_t cluster_disable;
    uint8_t sas_disable;
    uint8_t auto_detect_backplane;
    uint8_t fde_only;
    uint8_t delay_during_post;
    uint8_t resv[19];
} QEMU_PACKED;

/* Controller default settings */
struct mfi_bios_data {
    uint16_t boot_target_id;
    uint8_t do_not_int_13;
    uint8_t continue_on_error;
    uint8_t verbose;
    uint8_t geometry;
    uint8_t expose_all_drives;
    uint8_t reserved[56];
    uint8_t check_sum;
} QEMU_PACKED;

/* SAS (?) controller info, returned from MFI_DCMD_CTRL_GETINFO. */
struct mfi_ctrl_info {
    struct mfi_info_pci pci;
    struct mfi_info_host host;
    struct mfi_info_device device;

    /* Firmware components that are present and active. */
    uint32_t image_check_word;
    uint32_t image_component_count;
    struct mfi_info_component image_component[8];

    /* Firmware components that have been flashed but are inactive */
    uint32_t pending_image_component_count;
    struct mfi_info_component pending_image_component[8];

    uint8_t max_arms;
    uint8_t max_spans;
    uint8_t max_arrays;
    uint8_t max_lds;
    char product_name[80];
    char serial_number[32];
    uint32_t hw_present;
#define MFI_INFO_HW_BBU         0x01
#define MFI_INFO_HW_ALARM       0x02
#define MFI_INFO_HW_NVRAM       0x04
#define MFI_INFO_HW_UART        0x08
#define MFI_INFO_HW_MEM         0x10
#define MFI_INFO_HW_FLASH       0x20
    uint32_t current_fw_time;
    uint16_t max_cmds;
    uint16_t max_sg_elements;
    uint32_t max_request_size;
    uint16_t lds_present;
    uint16_t lds_degraded;
    uint16_t lds_offline;
    uint16_t pd_present;
    uint16_t pd_disks_present;
    uint16_t pd_disks_pred_failure;
    uint16_t pd_disks_failed;
    uint16_t nvram_size;
    uint16_t memory_size;
    uint16_t flash_size;
    uint16_t ram_correctable_errors;
    uint16_t ram_uncorrectable_errors;
    uint8_t cluster_allowed;
    uint8_t cluster_active;
    uint16_t max_strips_per_io;

    uint32_t raid_levels;
#define MFI_INFO_RAID_0         0x01
#define MFI_INFO_RAID_1         0x02
#define MFI_INFO_RAID_5         0x04
#define MFI_INFO_RAID_1E        0x08
#define MFI_INFO_RAID_6         0x10

    uint32_t adapter_ops;
#define MFI_INFO_AOPS_RBLD_RATE         0x0001
#define MFI_INFO_AOPS_CC_RATE           0x0002
#define MFI_INFO_AOPS_BGI_RATE          0x0004
#define MFI_INFO_AOPS_RECON_RATE        0x0008
#define MFI_INFO_AOPS_PATROL_RATE       0x0010
#define MFI_INFO_AOPS_ALARM_CONTROL     0x0020
#define MFI_INFO_AOPS_CLUSTER_SUPPORTED 0x0040
#define MFI_INFO_AOPS_BBU               0x0080
#define MFI_INFO_AOPS_SPANNING_ALLOWED  0x0100
#define MFI_INFO_AOPS_DEDICATED_SPARES  0x0200
#define MFI_INFO_AOPS_REVERTIBLE_SPARES 0x0400
#define MFI_INFO_AOPS_FOREIGN_IMPORT    0x0800
#define MFI_INFO_AOPS_SELF_DIAGNOSTIC   0x1000
#define MFI_INFO_AOPS_MIXED_ARRAY       0x2000
#define MFI_INFO_AOPS_GLOBAL_SPARES     0x4000

    uint32_t ld_ops;
#define MFI_INFO_LDOPS_READ_POLICY      0x01
#define MFI_INFO_LDOPS_WRITE_POLICY     0x02
#define MFI_INFO_LDOPS_IO_POLICY        0x04
#define MFI_INFO_LDOPS_ACCESS_POLICY    0x08
#define MFI_INFO_LDOPS_DISK_CACHE_POLICY 0x10

    struct {
        uint8_t min;
        uint8_t max;
        uint8_t reserved[2];
    } QEMU_PACKED stripe_sz_ops;

    uint32_t pd_ops;
#define MFI_INFO_PDOPS_FORCE_ONLINE     0x01
#define MFI_INFO_PDOPS_FORCE_OFFLINE    0x02
#define MFI_INFO_PDOPS_FORCE_REBUILD    0x04

    uint32_t pd_mix_support;
#define MFI_INFO_PDMIX_SAS              0x01
#define MFI_INFO_PDMIX_SATA             0x02
#define MFI_INFO_PDMIX_ENCL             0x04
#define MFI_INFO_PDMIX_LD               0x08
#define MFI_INFO_PDMIX_SATA_CLUSTER     0x10

    uint8_t ecc_bucket_count;
    uint8_t reserved2[11];
    struct mfi_ctrl_props properties;
    char package_version[0x60];
    uint8_t pad[0x800 - 0x6a0];
} QEMU_PACKED;

/* keep track of an event. */
union mfi_evt {
    struct {
        uint16_t locale;
        uint8_t reserved;
        int8_t class;
    } members;
    uint32_t word;
} QEMU_PACKED;

/* event log state. */
struct mfi_evt_log_state {
    uint32_t newest_seq_num;
    uint32_t oldest_seq_num;
    uint32_t clear_seq_num;
    uint32_t shutdown_seq_num;
    uint32_t boot_seq_num;
} QEMU_PACKED;

struct mfi_progress {
    uint16_t progress;
    uint16_t elapsed_seconds;
} QEMU_PACKED;

struct mfi_evt_ld {
    uint16_t target_id;
    uint8_t ld_index;
    uint8_t reserved;
} QEMU_PACKED;

struct mfi_evt_pd {
    uint16_t device_id;
    uint8_t enclosure_index;
    uint8_t slot_number;
} QEMU_PACKED;

/* event detail, returned from MFI_DCMD_CTRL_EVENT_WAIT. */
struct mfi_evt_detail {
    uint32_t seq;
    uint32_t time;
    uint32_t code;
    union mfi_evt class;
    uint8_t arg_type;
    uint8_t reserved1[15];

    union {
        struct {
            struct mfi_evt_pd pd;
            uint8_t cdb_len;
            uint8_t sense_len;
            uint8_t reserved[2];
            uint8_t cdb[16];
            uint8_t sense[64];
        } cdb_sense;

        struct mfi_evt_ld ld;

        struct {
            struct mfi_evt_ld ld;
            uint64_t count;
        } ld_count;

        struct {
            uint64_t lba;
            struct mfi_evt_ld ld;
        } ld_lba;

        struct {
            struct mfi_evt_ld ld;
            uint32_t pre_owner;
            uint32_t new_owner;
        } ld_owner;

        struct {
            uint64_t ld_lba;
            uint64_t pd_lba;
            struct mfi_evt_ld ld;
            struct mfi_evt_pd pd;
        } ld_lba_pd_lba;

        struct {
            struct mfi_evt_ld ld;
            struct mfi_progress prog;
        } ld_prog;

        struct {
            struct mfi_evt_ld ld;
            uint32_t prev_state;
            uint32_t new_state;
        } ld_state;

        struct {
            uint64_t strip;
            struct mfi_evt_ld ld;
        } ld_strip;

        struct mfi_evt_pd pd;

        struct {
            struct mfi_evt_pd pd;
            uint32_t err;
        } pd_err;

        struct {
            uint64_t lba;
            struct mfi_evt_pd pd;
        } pd_lba;

        struct {
            uint64_t lba;
            struct mfi_evt_pd pd;
            struct mfi_evt_ld ld;
        } pd_lba_ld;

        struct {
            struct mfi_evt_pd pd;
            struct mfi_progress prog;
        } pd_prog;

        struct {
            struct mfi_evt_pd ld;
            uint32_t prev_state;
            uint32_t new_state;
        } pd_state;

        struct {
            uint16_t venderId;
            uint16_t deviceId;
            uint16_t subVenderId;
            uint16_t subDeviceId;
        } pci;

        uint32_t rate;

        char str[96];

        struct {
            uint32_t rtc;
            uint16_t elapsedSeconds;
        } time;

        struct {
            uint32_t ecar;
            uint32_t elog;
            char str[64];
        } ecc;

        uint8_t b[96];
        uint16_t s[48];
        uint32_t w[24];
        uint64_t d[12];
    } args;

    char description[128];
} QEMU_PACKED;

struct mfi_evt_list {
    uint32_t count;
    uint32_t reserved;
    struct mfi_evt_detail event[1];
} QEMU_PACKED;

union mfi_pd_ref {
    struct {
        uint16_t device_id;
        uint16_t seq_num;
    } v;
    uint32_t ref;
} QEMU_PACKED;

union mfi_pd_ddf_type {
    struct {
        uint16_t pd_type;
#define MFI_PD_DDF_TYPE_FORCED_PD_GUID (1 << 0)
#define MFI_PD_DDF_TYPE_IN_VD          (1 << 1)
#define MFI_PD_DDF_TYPE_IS_GLOBAL_SPARE (1 << 2)
#define MFI_PD_DDF_TYPE_IS_SPARE        (1 << 3)
#define MFI_PD_DDF_TYPE_IS_FOREIGN      (1 << 4)
#define MFI_PD_DDF_TYPE_INTF_SPI        (1 << 12)
#define MFI_PD_DDF_TYPE_INTF_SAS        (1 << 13)
#define MFI_PD_DDF_TYPE_INTF_SATA1      (1 << 14)
#define MFI_PD_DDF_TYPE_INTF_SATA3G     (1 << 15)
        uint16_t reserved;
    } ddf;
    struct {
        uint32_t reserved;
    } non_disk;
    uint32_t type;
} QEMU_PACKED;

struct mfi_pd_progress {
    uint32_t active;
#define PD_PROGRESS_ACTIVE_REBUILD (1 << 0)
#define PD_PROGRESS_ACTIVE_PATROL  (1 << 1)
#define PD_PROGRESS_ACTIVE_CLEAR   (1 << 2)
    struct mfi_progress rbld;
    struct mfi_progress patrol;
    struct mfi_progress clear;
    struct mfi_progress reserved[4];
} QEMU_PACKED;

struct mfi_pd_info {
    union mfi_pd_ref ref;
    uint8_t inquiry_data[96];
    uint8_t vpd_page83[64];
    uint8_t not_supported;
    uint8_t scsi_dev_type;
    uint8_t connected_port_bitmap;
    uint8_t device_speed;
    uint32_t media_err_count;
    uint32_t other_err_count;
    uint32_t pred_fail_count;
    uint32_t last_pred_fail_event_seq_num;
    uint16_t fw_state;
    uint8_t disable_for_removal;
    uint8_t link_speed;
    union mfi_pd_ddf_type state;
    struct {
        uint8_t count;
        uint8_t is_path_broken;
        uint8_t reserved[6];
        uint64_t sas_addr[4];
    } path_info;
    uint64_t raw_size;
    uint64_t non_coerced_size;
    uint64_t coerced_size;
    uint16_t encl_device_id;
    uint8_t encl_index;
    uint8_t slot_number;
    struct mfi_pd_progress prog_info;
    uint8_t bad_block_table_full;
    uint8_t unusable_in_current_config;
    uint8_t vpd_page83_ext[64];
    uint8_t reserved[512-358];
} QEMU_PACKED;

struct mfi_pd_address {
    uint16_t device_id;
    uint16_t encl_device_id;
    uint8_t encl_index;
    uint8_t slot_number;
    uint8_t scsi_dev_type;
    uint8_t connect_port_bitmap;
    uint64_t sas_addr[2];
} QEMU_PACKED;

#define MFI_MAX_SYS_PDS 240
struct mfi_pd_list {
    uint32_t size;
    uint32_t count;
    struct mfi_pd_address addr[MFI_MAX_SYS_PDS];
} QEMU_PACKED;

union mfi_ld_ref {
    struct {
        uint8_t target_id;
        uint8_t reserved;
        uint16_t seq;
    } v;
    uint32_t ref;
} QEMU_PACKED;

struct mfi_ld_list {
    uint32_t ld_count;
    uint32_t reserved1;
    struct {
        union mfi_ld_ref ld;
        uint8_t state;
        uint8_t reserved2[3];
        uint64_t size;
    } ld_list[MFI_MAX_LD];
} QEMU_PACKED;

struct mfi_ld_targetid_list {
    uint32_t size;
    uint32_t ld_count;
    uint8_t pad[3];
    uint8_t targetid[MFI_MAX_LD];
} QEMU_PACKED;

enum mfi_ld_access {
    MFI_LD_ACCESS_RW =          0,
    MFI_LD_ACCSSS_RO =          2,
    MFI_LD_ACCESS_BLOCKED =     3,
};
#define MFI_LD_ACCESS_MASK      3

enum mfi_ld_state {
    MFI_LD_STATE_OFFLINE =              0,
    MFI_LD_STATE_PARTIALLY_DEGRADED =   1,
    MFI_LD_STATE_DEGRADED =             2,
    MFI_LD_STATE_OPTIMAL =              3
};

enum mfi_syspd_state {
    MFI_PD_STATE_UNCONFIGURED_GOOD =    0x00,
    MFI_PD_STATE_UNCONFIGURED_BAD =     0x01,
    MFI_PD_STATE_HOT_SPARE =            0x02,
    MFI_PD_STATE_OFFLINE =              0x10,
    MFI_PD_STATE_FAILED =               0x11,
    MFI_PD_STATE_REBUILD =              0x14,
    MFI_PD_STATE_ONLINE =               0x18,
    MFI_PD_STATE_COPYBACK =             0x20,
    MFI_PD_STATE_SYSTEM =               0x40
};

struct mfi_ld_props {
    union mfi_ld_ref ld;
    char name[16];
    uint8_t default_cache_policy;
    uint8_t access_policy;
    uint8_t disk_cache_policy;
    uint8_t current_cache_policy;
    uint8_t no_bgi;
    uint8_t reserved[7];
} QEMU_PACKED;

struct mfi_ld_params {
    uint8_t primary_raid_level;
    uint8_t raid_level_qualifier;
    uint8_t secondary_raid_level;
    uint8_t stripe_size;
    uint8_t num_drives;
    uint8_t span_depth;
    uint8_t state;
    uint8_t init_state;
    uint8_t is_consistent;
    uint8_t reserved[23];
} QEMU_PACKED;

struct mfi_ld_progress {
    uint32_t            active;
#define MFI_LD_PROGRESS_CC      (1<<0)
#define MFI_LD_PROGRESS_BGI     (1<<1)
#define MFI_LD_PROGRESS_FGI     (1<<2)
#define MFI_LD_PORGRESS_RECON   (1<<3)
    struct mfi_progress cc;
    struct mfi_progress bgi;
    struct mfi_progress fgi;
    struct mfi_progress recon;
    struct mfi_progress reserved[4];
} QEMU_PACKED;

struct mfi_span {
    uint64_t start_block;
    uint64_t num_blocks;
    uint16_t array_ref;
    uint8_t reserved[6];
} QEMU_PACKED;

#define MFI_MAX_SPAN_DEPTH      8
struct mfi_ld_config {
    struct mfi_ld_props properties;
    struct mfi_ld_params params;
    struct mfi_span span[MFI_MAX_SPAN_DEPTH];
} QEMU_PACKED;

struct mfi_ld_info {
    struct mfi_ld_config ld_config;
    uint64_t size;
    struct mfi_ld_progress progress;
    uint16_t cluster_owner;
    uint8_t reconstruct_active;
    uint8_t reserved1[1];
    uint8_t vpd_page83[64];
    uint8_t reserved2[16];
} QEMU_PACKED;

union mfi_spare_type {
    uint8_t flags;
#define MFI_SPARE_IS_DEDICATED (1 << 0)
#define MFI_SPARE_IS_REVERTABLE (1 << 1)
#define MFI_SPARE_IS_ENCL_AFFINITY (1 << 2)
    uint8_t type;
} QEMU_PACKED;

#define MFI_MAX_ARRAYS 16
struct mfi_spare {
    union mfi_pd_ref ref;
    union mfi_spare_type spare_type;
    uint8_t reserved[2];
    uint8_t array_count;
    uint16_t array_refd[MFI_MAX_ARRAYS];
} QEMU_PACKED;

#define MFI_MAX_ROW_SIZE 32
struct mfi_array {
    uint64_t size;
    uint8_t num_drives;
    uint8_t reserved;
    uint16_t array_ref;
    uint8_t pad[20];
    struct {
        union mfi_pd_ref ref;
        uint16_t fw_state; /* enum mfi_syspd_state */
        struct {
            uint8_t pd;
            uint8_t slot;
        } encl;
    } pd[MFI_MAX_ROW_SIZE];
} QEMU_PACKED;

struct mfi_config_data {
    uint32_t size;
    uint16_t array_count;
    uint16_t array_size;
    uint16_t log_drv_count;
    uint16_t log_drv_size;
    uint16_t spares_count;
    uint16_t spares_size;
    uint8_t reserved[16];
    /*
      struct mfi_array  array[];
      struct mfi_ld_config ld[];
      struct mfi_spare  spare[];
    */
} QEMU_PACKED;

#define MFI_SCSI_MAX_TARGETS  128
#define MFI_SCSI_MAX_LUNS       8
#define MFI_SCSI_INITIATOR_ID 255
#define MFI_SCSI_MAX_CMDS       8
#define MFI_SCSI_MAX_CDB_LEN   16

#endif /* SCSI_MFI_H */
