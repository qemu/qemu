/* Copyright (C) 1998, 1999 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/

/*
 * This header file contains public constants and structures used by
 * the scsi code for linux.
 */

/*
 *      SCSI opcodes
 */

#define TEST_UNIT_READY       0x00
#define REQUEST_SENSE         0x03
#define FORMAT_UNIT           0x04
#define READ_BLOCK_LIMITS     0x05
#define REASSIGN_BLOCKS       0x07
#define READ_6                0x08
#define WRITE_6               0x0a
#define SEEK_6                0x0b
#define READ_REVERSE          0x0f
#define WRITE_FILEMARKS       0x10
#define SPACE                 0x11
#define INQUIRY               0x12
#define RECOVER_BUFFERED_DATA 0x14
#define MODE_SELECT           0x15
#define RESERVE               0x16
#define RELEASE               0x17
#define COPY                  0x18
#define ERASE                 0x19
#define MODE_SENSE            0x1a
#define START_STOP            0x1b
#define RECEIVE_DIAGNOSTIC    0x1c
#define SEND_DIAGNOSTIC       0x1d
#define ALLOW_MEDIUM_REMOVAL  0x1e

#define READ_CAPACITY         0x25
#define READ_10               0x28
#define WRITE_10              0x2a
#define SEEK_10               0x2b
#define WRITE_VERIFY          0x2e
#define VERIFY                0x2f
#define SEARCH_HIGH           0x30
#define SEARCH_EQUAL          0x31
#define SEARCH_LOW            0x32
#define SET_LIMITS            0x33
#define PRE_FETCH             0x34
#define READ_POSITION         0x34
#define SYNCHRONIZE_CACHE     0x35
#define LOCK_UNLOCK_CACHE     0x36
#define READ_DEFECT_DATA      0x37
#define MEDIUM_SCAN           0x38
#define COMPARE               0x39
#define COPY_VERIFY           0x3a
#define WRITE_BUFFER          0x3b
#define READ_BUFFER           0x3c
#define UPDATE_BLOCK          0x3d
#define READ_LONG             0x3e
#define WRITE_LONG            0x3f
#define CHANGE_DEFINITION     0x40
#define WRITE_SAME            0x41
#define READ_TOC              0x43
#define LOG_SELECT            0x4c
#define LOG_SENSE             0x4d
#define MODE_SELECT_10        0x55
#define RESERVE_10            0x56
#define RELEASE_10            0x57
#define MODE_SENSE_10         0x5a
#define PERSISTENT_RESERVE_IN 0x5e
#define PERSISTENT_RESERVE_OUT 0x5f
#define WRITE_SAME_16         0x93
#define MAINTENANCE_IN        0xa3
#define MAINTENANCE_OUT       0xa4
#define MOVE_MEDIUM           0xa5
#define READ_12               0xa8
#define WRITE_12              0xaa
#define WRITE_VERIFY_12       0xae
#define SEARCH_HIGH_12        0xb0
#define SEARCH_EQUAL_12       0xb1
#define SEARCH_LOW_12         0xb2
#define READ_ELEMENT_STATUS   0xb8
#define SEND_VOLUME_TAG       0xb6
#define WRITE_LONG_2          0xea

/* from hw/scsi-generic.c */
#define REWIND 0x01
#define REPORT_DENSITY_SUPPORT 0x44
#define GET_CONFIGURATION 0x46
#define READ_16 0x88
#define WRITE_16 0x8a
#define WRITE_VERIFY_16 0x8e
#define SERVICE_ACTION_IN 0x9e
#define REPORT_LUNS 0xa0
#define LOAD_UNLOAD 0xa6
#define SET_CD_SPEED 0xbb
#define BLANK 0xa1

/*
 *  SAM Status codes
 */

#define GOOD                 0x00
#define CHECK_CONDITION      0x02
#define CONDITION_GOOD       0x04
#define BUSY                 0x08
#define INTERMEDIATE_GOOD    0x10
#define INTERMEDIATE_C_GOOD  0x14
#define RESERVATION_CONFLICT 0x18
#define COMMAND_TERMINATED   0x22
#define TASK_SET_FULL        0x28
#define ACA_ACTIVE           0x30
#define TASK_ABORTED         0x40

#define STATUS_MASK          0x3e

/*
 *  SENSE KEYS
 */

#define NO_SENSE            0x00
#define RECOVERED_ERROR     0x01
#define NOT_READY           0x02
#define MEDIUM_ERROR        0x03
#define HARDWARE_ERROR      0x04
#define ILLEGAL_REQUEST     0x05
#define UNIT_ATTENTION      0x06
#define DATA_PROTECT        0x07
#define BLANK_CHECK         0x08
#define COPY_ABORTED        0x0a
#define ABORTED_COMMAND     0x0b
#define VOLUME_OVERFLOW     0x0d
#define MISCOMPARE          0x0e


/*
 *  DEVICE TYPES
 */

#define TYPE_DISK           0x00
#define TYPE_TAPE           0x01
#define TYPE_PROCESSOR      0x03    /* HP scanners use this */
#define TYPE_WORM           0x04    /* Treated as ROM by our system */
#define TYPE_ROM            0x05
#define TYPE_SCANNER        0x06
#define TYPE_MOD            0x07    /* Magneto-optical disk -
				     * - treated as TYPE_DISK */
#define TYPE_MEDIUM_CHANGER 0x08
#define TYPE_ENCLOSURE	    0x0d    /* Enclosure Services Device */
#define TYPE_NO_LUN         0x7f

