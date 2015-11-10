/*
 * SCSI definitions for s390 machine loader for qemu
 *
 * Copyright 2015 IBM Corp.
 * Author: Eugene "jno" Dvurechenski <jno@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef SCSI_H
#define SCSI_H

#include "s390-ccw.h"

#define SCSI_DEFAULT_CDB_SIZE                   32
#define SCSI_DEFAULT_SENSE_SIZE                 96

#define CDB_STATUS_GOOD                         0
#define CDB_STATUS_CHECK_CONDITION              0x02U
#define CDB_STATUS_VALID(status)    (((status) & ~0x3eU) == 0)

#define SCSI_SENSE_CODE_MASK                    0x7fU
#define SCSI_SENSE_KEY_MASK                     0x0fU
#define SCSI_SENSE_KEY_NO_SENSE                 0
#define SCSI_SENSE_KEY_UNIT_ATTENTION           6

union ScsiLun {
    uint64_t v64;        /* numeric shortcut                             */
    uint8_t  v8[8];      /* generic 8 bytes representation               */
    uint16_t v16[4];     /* 4-level big-endian LUN as specified by SAM-2 */
};
typedef union ScsiLun ScsiLun;

struct ScsiSense70 {
    uint8_t b0;         /* b0 & 7f = resp code (0x70 or 0x71)   */
    uint8_t b1, b2;     /* b2 & 0f = sense key                  */
    uint8_t u1[1 * 4 + 1 + 1 * 4];   /* b7 = N - 7                  */
    uint8_t additional_sense_code;              /* b12          */
    uint8_t additional_sense_code_qualifier;    /* b13          */
    uint8_t u2[1 + 3 + 0];           /* up to N (<=252) bytes       */
} __attribute__((packed));
typedef struct ScsiSense70 ScsiSense70;

/* don't confuse with virtio-scsi response/status fields! */

static inline uint8_t scsi_sense_response(const void *p)
{
    return ((const ScsiSense70 *)p)->b0 & SCSI_SENSE_CODE_MASK;
}

static inline uint8_t scsi_sense_key(const void *p)
{
    return ((const ScsiSense70 *)p)->b2 & SCSI_SENSE_KEY_MASK;
}

#define SCSI_INQ_RDT_CDROM                      0x05

struct ScsiInquiryStd {
    uint8_t peripheral_qdt; /* b0, use (b0 & 0x1f) to get SCSI_INQ_RDT  */
    uint8_t b1;             /* Removable Media Bit = b1 & 0x80          */
    uint8_t spc_version;    /* b2                                       */
    uint8_t b3;             /* b3 & 0x0f == resp_data_fmt == 2, must!   */
    uint8_t u1[1 + 1 + 1 + 1 + 8];  /* b4..b15 unused, b4 = (N - 1)     */
    char prod_id[16];       /* "QEMU CD-ROM" is here                    */
    uint8_t u2[4            /* b32..b35 unused, mandatory               */
              + 8 + 12 + 1 + 1 + 8 * 2 + 22  /* b36..95 unused, optional*/
              + 0];          /* b96..bN unused, vendor specific          */
    /* byte N                                                           */
}  __attribute__((packed));
typedef struct ScsiInquiryStd ScsiInquiryStd;

struct ScsiCdbInquiry {
    uint8_t command;     /* b0, == 0x12         */
    uint8_t b1;          /* b1, |= 0x01 (evpd)  */
    uint8_t b2;          /* b2; if evpd==1      */
    uint16_t alloc_len;  /* b3, b4              */
    uint8_t control;     /* b5                  */
}  __attribute__((packed));
typedef struct ScsiCdbInquiry ScsiCdbInquiry;

struct ScsiCdbRead10 {
    uint8_t command;    /* =0x28    */
    uint8_t b1;
    uint32_t lba;
    uint8_t b6;
    uint16_t xfer_length;
    uint8_t control;
}  __attribute__((packed));
typedef struct ScsiCdbRead10 ScsiCdbRead10;

struct ScsiCdbTestUnitReady {
    uint8_t command;    /* =0x00    */
    uint8_t b1_b4[4];
    uint8_t control;
} __attribute__((packed));
typedef struct ScsiCdbTestUnitReady ScsiCdbTestUnitReady;

struct ScsiCdbReportLuns {
    uint8_t command;        /* =0xa0        */
    uint8_t b1;
    uint8_t select_report;  /* =0x02, "all" */
    uint8_t b3_b5[3];
    uint32_t alloc_len;
    uint8_t b10;
    uint8_t control;
} __attribute__((packed));
typedef struct ScsiCdbReportLuns ScsiCdbReportLuns;

struct ScsiLunReport {
    uint32_t lun_list_len;
    uint32_t b4_b7;
    ScsiLun lun[1];   /* space for at least 1 lun must be allocated */
} __attribute__((packed));
typedef struct ScsiLunReport ScsiLunReport;

struct ScsiCdbReadCapacity16 {
    uint8_t command;        /* =0x9e = "service action in 16"       */
    uint8_t service_action; /* 5 bits, =0x10 = "read capacity 16"   */
    uint64_t b2_b9;
    uint32_t alloc_len;
    uint8_t b14;
    uint8_t control;
} __attribute__((packed));
typedef struct ScsiCdbReadCapacity16 ScsiCdbReadCapacity16;

struct ScsiReadCapacity16Data {
    uint64_t ret_lba;             /* get it, 0..7     */
    uint32_t lb_len;              /* bytes, 8..11     */
    uint8_t u1[2 + 1 * 2 + 16];   /* b12..b31, unused */
} __attribute__((packed));
typedef struct ScsiReadCapacity16Data ScsiReadCapacity16Data;

static inline ScsiLun make_lun(uint16_t channel, uint16_t target, uint32_t lun)
{
    ScsiLun r = { .v64 = 0 };

    /* See QEMU code to choose the way to handle LUNs.
     *
     * So, a valid LUN must have (always channel #0):
     *  lun[0] == 1
     *  lun[1] - target, any value
     *  lun[2] == 0 or (LUN, MSB, 0x40 set, 0x80 clear)
     *  lun[3] - LUN, LSB, any value
     */
    r.v8[0] = 1;
    r.v8[1] = target & 0xffU;
    r.v8[2] = (lun >> 8) & 0x3fU;
    if (r.v8[2]) {
        r.v8[2] |= 0x40;
    }
    r.v8[3] = lun & 0xffU;

    return r;
}

static inline const char *scsi_cdb_status_msg(uint8_t status)
{
    static char err_msg[] = "STATUS=XX";
    uint8_t v = status & 0x3eU;

    fill_hex_val(err_msg + 7, &v, 1);
    return err_msg;
}

static inline const char *scsi_cdb_asc_msg(const void *s)
{
    static char err_msg[] = "RSPN=XX KEY=XX CODE=XX QLFR=XX";
    const ScsiSense70 *p = s;
    uint8_t sr = scsi_sense_response(s);
    uint8_t sk = scsi_sense_key(s);
    uint8_t ac = p->additional_sense_code;
    uint8_t cq = p->additional_sense_code_qualifier;

    fill_hex_val(err_msg + 5, &sr, 1);
    fill_hex_val(err_msg + 12, &sk, 1);
    fill_hex_val(err_msg + 20, &ac, 1);
    fill_hex_val(err_msg + 28, &cq, 1);

    return err_msg;
}

#endif /* SCSI_H */
