/*
 * S/390 DIAGNOSE 320 definitions and structures
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Zhuoying Cai <zycai@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef S390X_DIAG320_H
#define S390X_DIAG320_H

#define DIAG_320_SUBC_QUERY_ISM     0
#define DIAG_320_SUBC_QUERY_VCSI    1

#define DIAG_320_RC_OK              0x0001
#define DIAG_320_RC_NOT_SUPPORTED   0x0102
#define DIAG_320_RC_INVAL_VCSSB_LEN 0x0202

#define DIAG_320_ISM_QUERY_SUBCODES 0x80000000
#define DIAG_320_ISM_QUERY_VCSI     0x40000000

#define VCSSB_NO_VC     4
#define VCSSB_LEN_VALID   128

#define CERT_NAME_MAX_LEN  64

struct VCStorageSizeBlock {
    uint32_t length;
    uint8_t reserved0[3];
    uint8_t version;
    uint32_t reserved1[6];
    uint16_t total_vc_ct;
    uint16_t max_vc_ct;
    uint32_t reserved3[11];
    uint32_t max_single_vcb_len;
    uint32_t total_vcb_len;
    uint32_t reserved4[10];
};
typedef struct VCStorageSizeBlock VCStorageSizeBlock;

struct VCEntryHeader {
    uint32_t len;
    uint8_t flags;
    uint8_t key_type;
    uint16_t cert_idx;
    uint8_t name[CERT_NAME_MAX_LEN];
    uint8_t format;
    uint8_t reserved0;
    uint16_t keyid_len;
    uint8_t reserved1;
    uint8_t hash_type;
    uint16_t hash_len;
    uint32_t reserved2;
    uint32_t cert_len;
    uint32_t reserved3[2];
    uint16_t hash_offset;
    uint16_t cert_offset;
    uint32_t reserved4[7];
};
typedef struct VCEntryHeader VCEntryHeader;

struct VCBlockHeader {
    uint32_t in_len;
    uint32_t reserved0;
    uint16_t first_vc_index;
    uint16_t last_vc_index;
    uint32_t reserved1[5];
    uint32_t out_len;
    uint8_t reserved2[4];
    uint16_t stored_ct;
    uint16_t remain_ct;
    uint32_t reserved3[5];
};
typedef struct VCBlockHeader VCBlockHeader;

#endif
