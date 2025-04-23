/*
 *  SCSI helpers
 *
 *  Copyright 2017 Red Hat, Inc.
 *
 *  Authors:
 *   Fam Zheng <famz@redhat.com>
 *   Paolo Bonzini <pbonzini@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "qemu/osdep.h"
#include "scsi/constants.h"
#include "scsi/utils.h"
#include "qemu/bswap.h"

uint32_t scsi_data_cdb_xfer(uint8_t *buf)
{
    if ((buf[0] >> 5) == 0 && buf[4] == 0) {
        return 256;
    } else {
        return scsi_cdb_xfer(buf);
    }
}

uint32_t scsi_cdb_xfer(uint8_t *buf)
{
    switch (buf[0] >> 5) {
    case 0:
        return buf[4];
    case 1:
    case 2:
        return lduw_be_p(&buf[7]);
    case 4:
        return ldl_be_p(&buf[10]) & 0xffffffffULL;
    case 5:
        return ldl_be_p(&buf[6]) & 0xffffffffULL;
    default:
        return -1;
    }
}

uint64_t scsi_cmd_lba(SCSICommand *cmd)
{
    uint8_t *buf = cmd->buf;
    uint64_t lba;

    switch (buf[0] >> 5) {
    case 0:
        lba = ldl_be_p(&buf[0]) & 0x1fffff;
        break;
    case 1:
    case 2:
    case 5:
        lba = ldl_be_p(&buf[2]) & 0xffffffffULL;
        break;
    case 4:
        lba = ldq_be_p(&buf[2]);
        break;
    default:
        lba = -1;

    }
    return lba;
}

int scsi_cdb_length(uint8_t *buf)
{
    int cdb_len;

    switch (buf[0] >> 5) {
    case 0:
        cdb_len = 6;
        break;
    case 1:
    case 2:
        cdb_len = 10;
        break;
    case 4:
        cdb_len = 16;
        break;
    case 5:
        cdb_len = 12;
        break;
    default:
        cdb_len = -1;
    }
    return cdb_len;
}

SCSISense scsi_parse_sense_buf(const uint8_t *in_buf, int in_len)
{
    bool fixed_in;
    SCSISense sense;

    assert(in_len > 0);
    fixed_in = (in_buf[0] & 2) == 0;
    if (fixed_in) {
        if (in_len < 14) {
            return SENSE_CODE(IO_ERROR);
        }
        sense.key = in_buf[2];
        sense.asc = in_buf[12];
        sense.ascq = in_buf[13];
    } else {
        if (in_len < 4) {
            return SENSE_CODE(IO_ERROR);
        }
        sense.key = in_buf[1];
        sense.asc = in_buf[2];
        sense.ascq = in_buf[3];
    }

    return sense;
}

int scsi_build_sense_buf(uint8_t *out_buf, size_t size, SCSISense sense,
                         bool fixed_sense)
{
    int len;
    uint8_t buf[SCSI_SENSE_LEN] = { 0 };

    if (fixed_sense) {
        buf[0] = 0x70;
        buf[2] = sense.key;
        buf[7] = 10;
        buf[12] = sense.asc;
        buf[13] = sense.ascq;
        len = 18;
    } else {
        buf[0] = 0x72;
        buf[1] = sense.key;
        buf[2] = sense.asc;
        buf[3] = sense.ascq;
        len = 8;
    }
    len = MIN(len, size);
    memcpy(out_buf, buf, len);
    return len;
}

int scsi_build_sense(uint8_t *buf, SCSISense sense)
{
    return scsi_build_sense_buf(buf, SCSI_SENSE_LEN, sense, true);
}

/*
 * Predefined sense codes
 */

/* No sense data available */
const struct SCSISense sense_code_NO_SENSE = {
    .key = NO_SENSE , .asc = 0x00 , .ascq = 0x00
};

/* LUN not ready, Manual intervention required */
const struct SCSISense sense_code_LUN_NOT_READY = {
    .key = NOT_READY, .asc = 0x04, .ascq = 0x03
};

/* LUN not ready, Medium not present */
const struct SCSISense sense_code_NO_MEDIUM = {
    .key = NOT_READY, .asc = 0x3a, .ascq = 0x00
};

/* LUN not ready, medium removal prevented */
const struct SCSISense sense_code_NOT_READY_REMOVAL_PREVENTED = {
    .key = NOT_READY, .asc = 0x53, .ascq = 0x02
};

/* Hardware error, internal target failure */
const struct SCSISense sense_code_TARGET_FAILURE = {
    .key = HARDWARE_ERROR, .asc = 0x44, .ascq = 0x00
};

/* Illegal request, invalid command operation code */
const struct SCSISense sense_code_INVALID_OPCODE = {
    .key = ILLEGAL_REQUEST, .asc = 0x20, .ascq = 0x00
};

/* Illegal request, LBA out of range */
const struct SCSISense sense_code_LBA_OUT_OF_RANGE = {
    .key = ILLEGAL_REQUEST, .asc = 0x21, .ascq = 0x00
};

/* Illegal request, Invalid field in CDB */
const struct SCSISense sense_code_INVALID_FIELD = {
    .key = ILLEGAL_REQUEST, .asc = 0x24, .ascq = 0x00
};

/* Illegal request, Invalid field in parameter list */
const struct SCSISense sense_code_INVALID_PARAM = {
    .key = ILLEGAL_REQUEST, .asc = 0x26, .ascq = 0x00
};

/* Illegal request, Invalid value in parameter list */
const struct SCSISense sense_code_INVALID_PARAM_VALUE = {
    .key = ILLEGAL_REQUEST, .asc = 0x26, .ascq = 0x01
};

/* Illegal request, Parameter list length error */
const struct SCSISense sense_code_INVALID_PARAM_LEN = {
    .key = ILLEGAL_REQUEST, .asc = 0x1a, .ascq = 0x00
};

/* Illegal request, LUN not supported */
const struct SCSISense sense_code_LUN_NOT_SUPPORTED = {
    .key = ILLEGAL_REQUEST, .asc = 0x25, .ascq = 0x00
};

/* Illegal request, Saving parameters not supported */
const struct SCSISense sense_code_SAVING_PARAMS_NOT_SUPPORTED = {
    .key = ILLEGAL_REQUEST, .asc = 0x39, .ascq = 0x00
};

/* Illegal request, Incompatible medium installed */
const struct SCSISense sense_code_INCOMPATIBLE_FORMAT = {
    .key = ILLEGAL_REQUEST, .asc = 0x30, .ascq = 0x00
};

/* Illegal request, medium removal prevented */
const struct SCSISense sense_code_ILLEGAL_REQ_REMOVAL_PREVENTED = {
    .key = ILLEGAL_REQUEST, .asc = 0x53, .ascq = 0x02
};

/* Illegal request, Invalid Transfer Tag */
const struct SCSISense sense_code_INVALID_TAG = {
    .key = ILLEGAL_REQUEST, .asc = 0x4b, .ascq = 0x01
};

/* Command aborted, I/O process terminated */
const struct SCSISense sense_code_IO_ERROR = {
    .key = ABORTED_COMMAND, .asc = 0x00, .ascq = 0x06
};

/* Command aborted, I_T Nexus loss occurred */
const struct SCSISense sense_code_I_T_NEXUS_LOSS = {
    .key = ABORTED_COMMAND, .asc = 0x29, .ascq = 0x07
};

/* Command aborted, Logical Unit failure */
const struct SCSISense sense_code_LUN_FAILURE = {
    .key = ABORTED_COMMAND, .asc = 0x3e, .ascq = 0x01
};

/* Command aborted, Overlapped Commands Attempted */
const struct SCSISense sense_code_OVERLAPPED_COMMANDS = {
    .key = ABORTED_COMMAND, .asc = 0x4e, .ascq = 0x00
};

/* Command aborted, LUN Communication Failure */
const struct SCSISense sense_code_LUN_COMM_FAILURE = {
    .key = ABORTED_COMMAND, .asc = 0x08, .ascq = 0x00
};

/* Command aborted, LUN does not respond to selection */
const struct SCSISense sense_code_LUN_NOT_RESPONDING = {
    .key = ABORTED_COMMAND, .asc = 0x05, .ascq = 0x00
};

/* Command aborted, Command Timeout during processing */
const struct SCSISense sense_code_COMMAND_TIMEOUT = {
    .key = ABORTED_COMMAND, .asc = 0x2e, .ascq = 0x02
};

/* Command aborted, Commands cleared by device server */
const struct SCSISense sense_code_COMMAND_ABORTED = {
    .key = ABORTED_COMMAND, .asc = 0x2f, .ascq = 0x02
};

/* Medium Error, Unrecovered read error */
const struct SCSISense sense_code_READ_ERROR = {
    .key = MEDIUM_ERROR, .asc = 0x11, .ascq = 0x00
};

/* Not ready, Cause not reportable */
const struct SCSISense sense_code_NOT_READY = {
    .key = NOT_READY, .asc = 0x04, .ascq = 0x00
};

/* Unit attention, Capacity data has changed */
const struct SCSISense sense_code_CAPACITY_CHANGED = {
    .key = UNIT_ATTENTION, .asc = 0x2a, .ascq = 0x09
};

/* Unit attention, Power on, reset or bus device reset occurred */
const struct SCSISense sense_code_RESET = {
    .key = UNIT_ATTENTION, .asc = 0x29, .ascq = 0x00
};

/* Unit attention, SCSI bus reset */
const struct SCSISense sense_code_SCSI_BUS_RESET = {
    .key = UNIT_ATTENTION, .asc = 0x29, .ascq = 0x02
};

/* Unit attention, No medium */
const struct SCSISense sense_code_UNIT_ATTENTION_NO_MEDIUM = {
    .key = UNIT_ATTENTION, .asc = 0x3a, .ascq = 0x00
};

/* Unit attention, Medium may have changed */
const struct SCSISense sense_code_MEDIUM_CHANGED = {
    .key = UNIT_ATTENTION, .asc = 0x28, .ascq = 0x00
};

/* Unit attention, Reported LUNs data has changed */
const struct SCSISense sense_code_REPORTED_LUNS_CHANGED = {
    .key = UNIT_ATTENTION, .asc = 0x3f, .ascq = 0x0e
};

/* Unit attention, Device internal reset */
const struct SCSISense sense_code_DEVICE_INTERNAL_RESET = {
    .key = UNIT_ATTENTION, .asc = 0x29, .ascq = 0x04
};

/* Data Protection, Write Protected */
const struct SCSISense sense_code_WRITE_PROTECTED = {
    .key = DATA_PROTECT, .asc = 0x27, .ascq = 0x00
};

/* Data Protection, Space Allocation Failed Write Protect */
const struct SCSISense sense_code_SPACE_ALLOC_FAILED = {
    .key = DATA_PROTECT, .asc = 0x27, .ascq = 0x07
};

/*
 * scsi_convert_sense
 *
 * Convert between fixed and descriptor sense buffers
 */
int scsi_convert_sense(uint8_t *in_buf, int in_len,
                       uint8_t *buf, int len, bool fixed)
{
    SCSISense sense;
    bool fixed_in;

    if (in_len == 0) {
        return scsi_build_sense_buf(buf, len, SENSE_CODE(NO_SENSE), fixed);
    }

    fixed_in = (in_buf[0] & 2) == 0;
    if (fixed == fixed_in) {
        memcpy(buf, in_buf, MIN(len, in_len));
        return MIN(len, in_len);
    } else {
        sense = scsi_parse_sense_buf(in_buf, in_len);
        return scsi_build_sense_buf(buf, len, sense, fixed);
    }
}

static bool scsi_sense_is_guest_recoverable(int key, int asc, int ascq)
{
    switch (key) {
    case NO_SENSE:
    case RECOVERED_ERROR:
    case UNIT_ATTENTION:
    case ABORTED_COMMAND:
        return true;
    case NOT_READY:
    case ILLEGAL_REQUEST:
    case DATA_PROTECT:
        /* Parse ASCQ */
        break;
    default:
        return false;
    }

    switch ((asc << 8) | ascq) {
    case 0x1a00: /* PARAMETER LIST LENGTH ERROR */
    case 0x2000: /* INVALID OPERATION CODE */
    case 0x2400: /* INVALID FIELD IN CDB */
    case 0x2500: /* LOGICAL UNIT NOT SUPPORTED */
    case 0x2600: /* INVALID FIELD IN PARAMETER LIST */

    case 0x2104: /* UNALIGNED WRITE COMMAND */
    case 0x2105: /* WRITE BOUNDARY VIOLATION */
    case 0x2106: /* ATTEMPT TO READ INVALID DATA */
    case 0x550e: /* INSUFFICIENT ZONE RESOURCES */

    case 0x0401: /* NOT READY, IN PROGRESS OF BECOMING READY */
    case 0x0402: /* NOT READY, INITIALIZING COMMAND REQUIRED */
        return true;
    default:
        return false;
    }
}

int scsi_sense_to_errno(int key, int asc, int ascq)
{
    switch (key) {
    case NO_SENSE:
    case RECOVERED_ERROR:
    case UNIT_ATTENTION:
        return EAGAIN;
    case ABORTED_COMMAND: /* COMMAND ABORTED */
        return ECANCELED;
    case NOT_READY:
    case ILLEGAL_REQUEST:
    case DATA_PROTECT:
        /* Parse ASCQ */
        break;
    default:
        return EIO;
    }
    switch ((asc << 8) | ascq) {
    case 0x1a00: /* PARAMETER LIST LENGTH ERROR */
    case 0x2000: /* INVALID OPERATION CODE */
    case 0x2400: /* INVALID FIELD IN CDB */
    case 0x2600: /* INVALID FIELD IN PARAMETER LIST */
        return EINVAL;
    case 0x2100: /* LBA OUT OF RANGE */
    case 0x2707: /* SPACE ALLOC FAILED */
        return ENOSPC;
    case 0x2500: /* LOGICAL UNIT NOT SUPPORTED */
        return ENOTSUP;
    case 0x3a00: /* MEDIUM NOT PRESENT */
    case 0x3a01: /* MEDIUM NOT PRESENT TRAY CLOSED */
    case 0x3a02: /* MEDIUM NOT PRESENT TRAY OPEN */
        return ENOMEDIUM;
    case 0x2700: /* WRITE PROTECTED */
        return EACCES;
    case 0x0401: /* NOT READY, IN PROGRESS OF BECOMING READY */
        return EINPROGRESS;
    case 0x0402: /* NOT READY, INITIALIZING COMMAND REQUIRED */
        return ENOTCONN;
    default:
        return EIO;
    }
}

int scsi_sense_buf_to_errno(const uint8_t *in_buf, size_t in_len)
{
    SCSISense sense;
    if (in_len < 1) {
        return EIO;
    }

    sense = scsi_parse_sense_buf(in_buf, in_len);
    return scsi_sense_to_errno(sense.key, sense.asc, sense.ascq);
}

bool scsi_sense_buf_is_guest_recoverable(const uint8_t *in_buf, size_t in_len)
{
    SCSISense sense;
    if (in_len < 1) {
        return false;
    }

    sense = scsi_parse_sense_buf(in_buf, in_len);
    return scsi_sense_is_guest_recoverable(sense.key, sense.asc, sense.ascq);
}

const char *scsi_command_name(uint8_t cmd)
{
    static const char *names[] = {
        [ TEST_UNIT_READY          ] = "TEST_UNIT_READY",
        [ REWIND                   ] = "REWIND",
        [ REQUEST_SENSE            ] = "REQUEST_SENSE",
        [ FORMAT_UNIT              ] = "FORMAT_UNIT",
        [ READ_BLOCK_LIMITS        ] = "READ_BLOCK_LIMITS",
        [ REASSIGN_BLOCKS          ] = "REASSIGN_BLOCKS/INITIALIZE ELEMENT STATUS",
        /* LOAD_UNLOAD and INITIALIZE_ELEMENT_STATUS use the same operation code */
        [ READ_6                   ] = "READ_6",
        [ WRITE_6                  ] = "WRITE_6",
        [ SET_CAPACITY             ] = "SET_CAPACITY",
        [ READ_REVERSE             ] = "READ_REVERSE",
        [ WRITE_FILEMARKS          ] = "WRITE_FILEMARKS",
        [ SPACE                    ] = "SPACE",
        [ INQUIRY                  ] = "INQUIRY",
        [ RECOVER_BUFFERED_DATA    ] = "RECOVER_BUFFERED_DATA",
        [ MAINTENANCE_IN           ] = "MAINTENANCE_IN",
        [ MAINTENANCE_OUT          ] = "MAINTENANCE_OUT",
        [ MODE_SELECT              ] = "MODE_SELECT",
        [ RESERVE                  ] = "RESERVE",
        [ RELEASE                  ] = "RELEASE",
        [ COPY                     ] = "COPY",
        [ ERASE                    ] = "ERASE",
        [ MODE_SENSE               ] = "MODE_SENSE",
        [ START_STOP               ] = "START_STOP/LOAD_UNLOAD",
        /* LOAD_UNLOAD and START_STOP use the same operation code */
        [ RECEIVE_DIAGNOSTIC       ] = "RECEIVE_DIAGNOSTIC",
        [ SEND_DIAGNOSTIC          ] = "SEND_DIAGNOSTIC",
        [ ALLOW_MEDIUM_REMOVAL     ] = "ALLOW_MEDIUM_REMOVAL",
        [ READ_CAPACITY_10         ] = "READ_CAPACITY_10",
        [ READ_10                  ] = "READ_10",
        [ WRITE_10                 ] = "WRITE_10",
        [ SEEK_10                  ] = "SEEK_10/POSITION_TO_ELEMENT",
        /* SEEK_10 and POSITION_TO_ELEMENT use the same operation code */
        [ WRITE_VERIFY_10          ] = "WRITE_VERIFY_10",
        [ VERIFY_10                ] = "VERIFY_10",
        [ SEARCH_HIGH              ] = "SEARCH_HIGH",
        [ SEARCH_EQUAL             ] = "SEARCH_EQUAL",
        [ SEARCH_LOW               ] = "SEARCH_LOW",
        [ SET_LIMITS               ] = "SET_LIMITS",
        [ PRE_FETCH                ] = "PRE_FETCH/READ_POSITION",
        /* READ_POSITION and PRE_FETCH use the same operation code */
        [ SYNCHRONIZE_CACHE        ] = "SYNCHRONIZE_CACHE",
        [ LOCK_UNLOCK_CACHE        ] = "LOCK_UNLOCK_CACHE",
        [ READ_DEFECT_DATA         ] = "READ_DEFECT_DATA/INITIALIZE_ELEMENT_STATUS_WITH_RANGE",
        /* READ_DEFECT_DATA and INITIALIZE_ELEMENT_STATUS_WITH_RANGE use the same operation code */
        [ MEDIUM_SCAN              ] = "MEDIUM_SCAN",
        [ COMPARE                  ] = "COMPARE",
        [ COPY_VERIFY              ] = "COPY_VERIFY",
        [ WRITE_BUFFER             ] = "WRITE_BUFFER",
        [ READ_BUFFER              ] = "READ_BUFFER",
        [ UPDATE_BLOCK             ] = "UPDATE_BLOCK",
        [ READ_LONG_10             ] = "READ_LONG_10",
        [ WRITE_LONG_10            ] = "WRITE_LONG_10",
        [ CHANGE_DEFINITION        ] = "CHANGE_DEFINITION",
        [ WRITE_SAME_10            ] = "WRITE_SAME_10",
        [ UNMAP                    ] = "UNMAP",
        [ READ_TOC                 ] = "READ_TOC",
        [ REPORT_DENSITY_SUPPORT   ] = "REPORT_DENSITY_SUPPORT",
        [ SANITIZE                 ] = "SANITIZE",
        [ GET_CONFIGURATION        ] = "GET_CONFIGURATION",
        [ LOG_SELECT               ] = "LOG_SELECT",
        [ LOG_SENSE                ] = "LOG_SENSE",
        [ MODE_SELECT_10           ] = "MODE_SELECT_10",
        [ RESERVE_10               ] = "RESERVE_10",
        [ RELEASE_10               ] = "RELEASE_10",
        [ MODE_SENSE_10            ] = "MODE_SENSE_10",
        [ PERSISTENT_RESERVE_IN    ] = "PERSISTENT_RESERVE_IN",
        [ PERSISTENT_RESERVE_OUT   ] = "PERSISTENT_RESERVE_OUT",
        [ WRITE_FILEMARKS_16       ] = "WRITE_FILEMARKS_16",
        [ EXTENDED_COPY            ] = "EXTENDED_COPY",
        [ ATA_PASSTHROUGH_16       ] = "ATA_PASSTHROUGH_16",
        [ ACCESS_CONTROL_IN        ] = "ACCESS_CONTROL_IN",
        [ ACCESS_CONTROL_OUT       ] = "ACCESS_CONTROL_OUT",
        [ READ_16                  ] = "READ_16",
        [ COMPARE_AND_WRITE        ] = "COMPARE_AND_WRITE",
        [ WRITE_16                 ] = "WRITE_16",
        [ WRITE_VERIFY_16          ] = "WRITE_VERIFY_16",
        [ VERIFY_16                ] = "VERIFY_16",
        [ PRE_FETCH_16             ] = "PRE_FETCH_16",
        [ SYNCHRONIZE_CACHE_16     ] = "SPACE_16/SYNCHRONIZE_CACHE_16",
        /* SPACE_16 and SYNCHRONIZE_CACHE_16 use the same operation code */
        [ LOCATE_16                ] = "LOCATE_16",
        [ WRITE_SAME_16            ] = "ERASE_16/WRITE_SAME_16",
        /* ERASE_16 and WRITE_SAME_16 use the same operation code */
        [ SERVICE_ACTION_IN_16     ] = "SERVICE_ACTION_IN_16",
        [ WRITE_LONG_16            ] = "WRITE_LONG_16",
        [ REPORT_LUNS              ] = "REPORT_LUNS",
        [ ATA_PASSTHROUGH_12       ] = "BLANK/ATA_PASSTHROUGH_12",
        [ MOVE_MEDIUM              ] = "MOVE_MEDIUM",
        [ EXCHANGE_MEDIUM          ] = "EXCHANGE MEDIUM",
        [ READ_12                  ] = "READ_12",
        [ WRITE_12                 ] = "WRITE_12",
        [ ERASE_12                 ] = "ERASE_12/GET_PERFORMANCE",
        /* ERASE_12 and GET_PERFORMANCE use the same operation code */
        [ SERVICE_ACTION_IN_12     ] = "SERVICE_ACTION_IN_12",
        [ WRITE_VERIFY_12          ] = "WRITE_VERIFY_12",
        [ VERIFY_12                ] = "VERIFY_12",
        [ SEARCH_HIGH_12           ] = "SEARCH_HIGH_12",
        [ SEARCH_EQUAL_12          ] = "SEARCH_EQUAL_12",
        [ SEARCH_LOW_12            ] = "SEARCH_LOW_12",
        [ READ_ELEMENT_STATUS      ] = "READ_ELEMENT_STATUS",
        [ SEND_VOLUME_TAG          ] = "SEND_VOLUME_TAG/SET_STREAMING",
        /* SEND_VOLUME_TAG and SET_STREAMING use the same operation code */
        [ READ_CD                  ] = "READ_CD",
        [ READ_DEFECT_DATA_12      ] = "READ_DEFECT_DATA_12",
        [ READ_DVD_STRUCTURE       ] = "READ_DVD_STRUCTURE",
        [ RESERVE_TRACK            ] = "RESERVE_TRACK",
        [ SEND_CUE_SHEET           ] = "SEND_CUE_SHEET",
        [ SEND_DVD_STRUCTURE       ] = "SEND_DVD_STRUCTURE",
        [ SET_CD_SPEED             ] = "SET_CD_SPEED",
        [ SET_READ_AHEAD           ] = "SET_READ_AHEAD",
        [ ALLOW_OVERWRITE          ] = "ALLOW_OVERWRITE",
        [ MECHANISM_STATUS         ] = "MECHANISM_STATUS",
        [ GET_EVENT_STATUS_NOTIFICATION ] = "GET_EVENT_STATUS_NOTIFICATION",
        [ READ_DISC_INFORMATION    ] = "READ_DISC_INFORMATION",
    };

    if (cmd >= ARRAY_SIZE(names) || names[cmd] == NULL) {
        return "*UNKNOWN*";
    }
    return names[cmd];
}

int scsi_sense_from_errno(int errno_value, SCSISense *sense)
{
    switch (errno_value) {
    case 0:
        return GOOD;
    case EDOM:
        return TASK_SET_FULL;
#if ENODEV != ENOMEDIUM
    case ENODEV:
        /*
         * Some of the BSDs have ENODEV and ENOMEDIUM as synonyms.  For
         * everyone else, give a more severe sense code for ENODEV.
         */
#endif
#ifdef CONFIG_LINUX
        /* These errno mapping are specific to Linux.  For more information:
         * - scsi_check_sense and scsi_decide_disposition in drivers/scsi/scsi_error.c
         * - scsi_result_to_blk_status in drivers/scsi/scsi_lib.c
         * - blk_errors[] in block/blk-core.c
         */
    case EREMOTEIO:
        *sense = SENSE_CODE(TARGET_FAILURE);
        return CHECK_CONDITION;
    case EBADE:
        return RESERVATION_CONFLICT;
    case ENODATA:
        *sense = SENSE_CODE(READ_ERROR);
        return CHECK_CONDITION;
#endif
    case ENOMEDIUM:
        *sense = SENSE_CODE(NO_MEDIUM);
        return CHECK_CONDITION;
    case ENOMEM:
        *sense = SENSE_CODE(TARGET_FAILURE);
        return CHECK_CONDITION;
    case EINVAL:
        *sense = SENSE_CODE(INVALID_FIELD);
        return CHECK_CONDITION;
    case ENOSPC:
        *sense = SENSE_CODE(SPACE_ALLOC_FAILED);
        return CHECK_CONDITION;
    default:
        *sense = SENSE_CODE(IO_ERROR);
        return CHECK_CONDITION;
    }
}

int scsi_sense_from_host_status(uint8_t host_status,
                                SCSISense *sense)
{
    switch (host_status) {
    case SCSI_HOST_NO_LUN:
        *sense = SENSE_CODE(LUN_NOT_RESPONDING);
        return CHECK_CONDITION;
    case SCSI_HOST_BUSY:
        return BUSY;
    case SCSI_HOST_TIME_OUT:
        *sense = SENSE_CODE(COMMAND_TIMEOUT);
        return CHECK_CONDITION;
    case SCSI_HOST_BAD_RESPONSE:
        *sense = SENSE_CODE(LUN_COMM_FAILURE);
        return CHECK_CONDITION;
    case SCSI_HOST_ABORTED:
        *sense = SENSE_CODE(COMMAND_ABORTED);
        return CHECK_CONDITION;
    case SCSI_HOST_RESET:
        *sense = SENSE_CODE(RESET);
        return CHECK_CONDITION;
    case SCSI_HOST_TRANSPORT_DISRUPTED:
        *sense = SENSE_CODE(I_T_NEXUS_LOSS);
        return CHECK_CONDITION;
    case SCSI_HOST_TARGET_FAILURE:
        *sense = SENSE_CODE(TARGET_FAILURE);
        return CHECK_CONDITION;
    case SCSI_HOST_RESERVATION_ERROR:
        return RESERVATION_CONFLICT;
    case SCSI_HOST_ALLOCATION_FAILURE:
        *sense = SENSE_CODE(SPACE_ALLOC_FAILED);
        return CHECK_CONDITION;
    case SCSI_HOST_MEDIUM_ERROR:
        *sense = SENSE_CODE(READ_ERROR);
        return CHECK_CONDITION;
    }
    return GOOD;
}
