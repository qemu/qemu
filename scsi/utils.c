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
#include "block/scsi.h"
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
        break;
    case 1:
    case 2:
        return lduw_be_p(&buf[7]);
        break;
    case 4:
        return ldl_be_p(&buf[10]) & 0xffffffffULL;
        break;
    case 5:
        return ldl_be_p(&buf[6]) & 0xffffffffULL;
        break;
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

/* Unit attention, Capacity data has changed */
const struct SCSISense sense_code_CAPACITY_CHANGED = {
    .key = UNIT_ATTENTION, .asc = 0x2a, .ascq = 0x09
};

/* Unit attention, Power on, reset or bus device reset occurred */
const struct SCSISense sense_code_RESET = {
    .key = UNIT_ATTENTION, .asc = 0x29, .ascq = 0x00
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
    bool fixed_in;
    SCSISense sense;
    if (!fixed && len < 8) {
        return 0;
    }

    if (in_len == 0) {
        sense.key = NO_SENSE;
        sense.asc = 0;
        sense.ascq = 0;
    } else {
        fixed_in = (in_buf[0] & 2) == 0;

        if (fixed == fixed_in) {
            memcpy(buf, in_buf, MIN(len, in_len));
            return MIN(len, in_len);
        }

        if (fixed_in) {
            sense.key = in_buf[2];
            sense.asc = in_buf[12];
            sense.ascq = in_buf[13];
        } else {
            sense.key = in_buf[1];
            sense.asc = in_buf[2];
            sense.ascq = in_buf[3];
        }
    }

    memset(buf, 0, len);
    if (fixed) {
        /* Return fixed format sense buffer */
        buf[0] = 0x70;
        buf[2] = sense.key;
        buf[7] = 10;
        buf[12] = sense.asc;
        buf[13] = sense.ascq;
        return MIN(len, SCSI_SENSE_LEN);
    } else {
        /* Return descriptor format sense buffer */
        buf[0] = 0x72;
        buf[1] = sense.key;
        buf[2] = sense.asc;
        buf[3] = sense.ascq;
        return 8;
    }
}

int scsi_sense_to_errno(int key, int asc, int ascq)
{
    switch (key) {
    case 0x00: /* NO SENSE */
    case 0x01: /* RECOVERED ERROR */
    case 0x06: /* UNIT ATTENTION */
        /* These sense keys are not errors */
        return 0;
    case 0x0b: /* COMMAND ABORTED */
        return ECANCELED;
    case 0x02: /* NOT READY */
    case 0x05: /* ILLEGAL REQUEST */
    case 0x07: /* DATA PROTECTION */
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
        return EAGAIN;
    case 0x0402: /* NOT READY, INITIALIZING COMMAND REQUIRED */
        return ENOTCONN;
    default:
        return EIO;
    }
}

int scsi_sense_buf_to_errno(const uint8_t *sense, size_t sense_size)
{
    int key, asc, ascq;
    if (sense_size < 1) {
        return EIO;
    }
    switch (sense[0]) {
    case 0x70: /* Fixed format sense data. */
        if (sense_size < 14) {
            return EIO;
        }
        key = sense[2] & 0xF;
        asc = sense[12];
        ascq = sense[13];
        break;
    case 0x72: /* Descriptor format sense data. */
        if (sense_size < 4) {
            return EIO;
        }
        key = sense[1] & 0xF;
        asc = sense[2];
        ascq = sense[3];
        break;
    default:
        return EIO;
        break;
    }
    return scsi_sense_to_errno(key, asc, ascq);
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
