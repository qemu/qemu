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
#define REWIND                0x01
#define REQUEST_SENSE         0x03
#define FORMAT_UNIT           0x04
#define READ_BLOCK_LIMITS     0x05
#define REASSIGN_BLOCKS       0x07
#define READ_6                0x08
#define WRITE_6               0x0a
#define SET_CAPACITY          0x0b
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
#define READ_CAPACITY_10      0x25
#define READ_10               0x28
#define WRITE_10              0x2a
#define SEEK_10               0x2b
#define LOCATE_10             0x2b
#define WRITE_VERIFY_10       0x2e
#define VERIFY_10             0x2f
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
#define READ_LONG_10          0x3e
#define WRITE_LONG_10         0x3f
#define CHANGE_DEFINITION     0x40
#define WRITE_SAME_10         0x41
#define UNMAP                 0x42
#define READ_TOC              0x43
#define REPORT_DENSITY_SUPPORT 0x44
#define GET_CONFIGURATION     0x46
#define GET_EVENT_STATUS_NOTIFICATION 0x4a
#define LOG_SELECT            0x4c
#define LOG_SENSE             0x4d
#define RESERVE_TRACK         0x53
#define MODE_SELECT_10        0x55
#define RESERVE_10            0x56
#define RELEASE_10            0x57
#define MODE_SENSE_10         0x5a
#define SEND_CUE_SHEET        0x5d
#define PERSISTENT_RESERVE_IN 0x5e
#define PERSISTENT_RESERVE_OUT 0x5f
#define VARLENGTH_CDB         0x7f
#define WRITE_FILEMARKS_16    0x80
#define ALLOW_OVERWRITE       0x82
#define EXTENDED_COPY         0x83
#define ATA_PASSTHROUGH       0x85
#define ACCESS_CONTROL_IN     0x86
#define ACCESS_CONTROL_OUT    0x87
#define READ_16               0x88
#define COMPARE_AND_WRITE     0x89
#define WRITE_16              0x8a
#define WRITE_VERIFY_16       0x8e
#define VERIFY_16             0x8f
#define PRE_FETCH_16          0x90
#define SPACE_16              0x91
#define SYNCHRONIZE_CACHE_16  0x91
#define LOCATE_16             0x92
#define WRITE_SAME_16         0x93
#define ERASE_16              0x93
#define SERVICE_ACTION_IN_16  0x9e
#define WRITE_LONG_16         0x9f
#define REPORT_LUNS           0xa0
#define BLANK                 0xa1
#define MAINTENANCE_IN        0xa3
#define MAINTENANCE_OUT       0xa4
#define MOVE_MEDIUM           0xa5
#define LOAD_UNLOAD           0xa6
#define SET_READ_AHEAD        0xa7
#define READ_12               0xa8
#define WRITE_12              0xaa
#define SERVICE_ACTION_IN_12  0xab
#define ERASE_12              0xac
#define READ_DVD_STRUCTURE    0xad
#define WRITE_VERIFY_12       0xae
#define VERIFY_12             0xaf
#define SEARCH_HIGH_12        0xb0
#define SEARCH_EQUAL_12       0xb1
#define SEARCH_LOW_12         0xb2
#define READ_ELEMENT_STATUS   0xb8
#define SEND_VOLUME_TAG       0xb6
#define READ_DEFECT_DATA_12   0xb7
#define SET_CD_SPEED          0xbb
#define MECHANISM_STATUS      0xbd
#define READ_CD               0xbe
#define SEND_DVD_STRUCTURE    0xbf

/*
 * SERVICE ACTION IN subcodes
 */
#define SAI_READ_CAPACITY_16  0x10

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
#define TYPE_PRINTER        0x02
#define TYPE_PROCESSOR      0x03    /* HP scanners use this */
#define TYPE_WORM           0x04    /* Treated as ROM by our system */
#define TYPE_ROM            0x05
#define TYPE_SCANNER        0x06
#define TYPE_MOD            0x07    /* Magneto-optical disk -
				     * - treated as TYPE_DISK */
#define TYPE_MEDIUM_CHANGER 0x08
#define TYPE_STORAGE_ARRAY  0x0c    /* Storage array device */
#define TYPE_ENCLOSURE      0x0d    /* Enclosure Services Device */
#define TYPE_RBC            0x0e    /* Simplified Direct-Access Device */
#define TYPE_OSD            0x11    /* Object-storage Device */
#define TYPE_WLUN           0x1e    /* Well known LUN */
#define TYPE_NOT_PRESENT    0x1f
#define TYPE_INACTIVE       0x20
#define TYPE_NO_LUN         0x7f

/* Mode page codes for mode sense/set */
#define MODE_PAGE_R_W_ERROR                   0x01
#define MODE_PAGE_HD_GEOMETRY                 0x04
#define MODE_PAGE_FLEXIBLE_DISK_GEOMETRY      0x05
#define MODE_PAGE_CACHING                     0x08
#define MODE_PAGE_AUDIO_CTL                   0x0e
#define MODE_PAGE_POWER                       0x1a
#define MODE_PAGE_FAULT_FAIL                  0x1c
#define MODE_PAGE_TO_PROTECT                  0x1d
#define MODE_PAGE_CAPABILITIES                0x2a
#define MODE_PAGE_ALLS                        0x3f
/* Not in Mt. Fuji, but in ATAPI 2.6 -- depricated now in favor
 * of MODE_PAGE_SENSE_POWER */
#define MODE_PAGE_CDROM                       0x0d

/* Event notification classes for GET EVENT STATUS NOTIFICATION */
#define GESN_NO_EVENTS                0
#define GESN_OPERATIONAL_CHANGE       1
#define GESN_POWER_MANAGEMENT         2
#define GESN_EXTERNAL_REQUEST         3
#define GESN_MEDIA                    4
#define GESN_MULTIPLE_HOSTS           5
#define GESN_DEVICE_BUSY              6

/* Event codes for MEDIA event status notification */
#define MEC_NO_CHANGE                 0
#define MEC_EJECT_REQUESTED           1
#define MEC_NEW_MEDIA                 2
#define MEC_MEDIA_REMOVAL             3 /* only for media changers */
#define MEC_MEDIA_CHANGED             4 /* only for media changers */
#define MEC_BG_FORMAT_COMPLETED       5 /* MRW or DVD+RW b/g format completed */
#define MEC_BG_FORMAT_RESTARTED       6 /* MRW or DVD+RW b/g format restarted */

#define MS_TRAY_OPEN                  1
#define MS_MEDIA_PRESENT              2

/*
 * Based on values from <linux/cdrom.h> but extending CD_MINS
 * to the maximum common size allowed by the Orange's Book ATIP
 *
 * 90 and 99 min CDs are also available but using them as the
 * upper limit reduces the effectiveness of the heuristic to
 * detect DVDs burned to less than 25% of their maximum capacity
 */

/* Some generally useful CD-ROM information */
#define CD_MINS                       80 /* max. minutes per CD */
#define CD_SECS                       60 /* seconds per minute */
#define CD_FRAMES                     75 /* frames per second */
#define CD_FRAMESIZE                2048 /* bytes per frame, "cooked" mode */
#define CD_MAX_BYTES       (CD_MINS * CD_SECS * CD_FRAMES * CD_FRAMESIZE)
#define CD_MAX_SECTORS     (CD_MAX_BYTES / 512)

/*
 * The MMC values are not IDE specific and might need to be moved
 * to a common header if they are also needed for the SCSI emulation
 */

/* Profile list from MMC-6 revision 1 table 91 */
#define MMC_PROFILE_NONE                0x0000
#define MMC_PROFILE_CD_ROM              0x0008
#define MMC_PROFILE_CD_R                0x0009
#define MMC_PROFILE_CD_RW               0x000A
#define MMC_PROFILE_DVD_ROM             0x0010
#define MMC_PROFILE_DVD_R_SR            0x0011
#define MMC_PROFILE_DVD_RAM             0x0012
#define MMC_PROFILE_DVD_RW_RO           0x0013
#define MMC_PROFILE_DVD_RW_SR           0x0014
#define MMC_PROFILE_DVD_R_DL_SR         0x0015
#define MMC_PROFILE_DVD_R_DL_JR         0x0016
#define MMC_PROFILE_DVD_RW_DL           0x0017
#define MMC_PROFILE_DVD_DDR             0x0018
#define MMC_PROFILE_DVD_PLUS_RW         0x001A
#define MMC_PROFILE_DVD_PLUS_R          0x001B
#define MMC_PROFILE_DVD_PLUS_RW_DL      0x002A
#define MMC_PROFILE_DVD_PLUS_R_DL       0x002B
#define MMC_PROFILE_BD_ROM              0x0040
#define MMC_PROFILE_BD_R_SRM            0x0041
#define MMC_PROFILE_BD_R_RRM            0x0042
#define MMC_PROFILE_BD_RE               0x0043
#define MMC_PROFILE_HDDVD_ROM           0x0050
#define MMC_PROFILE_HDDVD_R             0x0051
#define MMC_PROFILE_HDDVD_RAM           0x0052
#define MMC_PROFILE_HDDVD_RW            0x0053
#define MMC_PROFILE_HDDVD_R_DL          0x0058
#define MMC_PROFILE_HDDVD_RW_DL         0x005A
#define MMC_PROFILE_INVALID             0xFFFF
