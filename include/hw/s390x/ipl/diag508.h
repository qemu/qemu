/*
 * S/390 DIAGNOSE 508 definitions and structures
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Collin Walling <walling@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef S390X_DIAG508_H
#define S390X_DIAG508_H

#define DIAG_508_SUBC_QUERY_SUBC    0x0000
#define DIAG_508_SUBC_SIG_VERIF     0x8000

#define DIAG_508_RC_OK              0x0001
#define DIAG_508_RC_NO_CERTS        0x0102
#define DIAG_508_RC_INVAL_COMP_DATA 0x0202
#define DIAG_508_RC_INVAL_PKCS7_SIG 0x0302
#define DIAG_508_RC_FAIL_VERIF      0x0402
#define DIAG_508_RC_INVAL_LEN       0x0502

/*
 * Maximum component and signature sizes for current secure boot implementation
 * Not architecturally defined and may need to revisit if increased
 */
#define DIAG_508_MAX_COMP_LEN      0x10000000
#define DIAG_508_MAX_SIG_LEN       4096

struct Diag508SigVerifBlock {
    uint32_t length;
    uint8_t reserved0[3];
    uint8_t version;
    uint32_t reserved[2];
    uint8_t cert_store_index;
    uint8_t reserved1[7];
    uint64_t cert_len;
    uint64_t comp_len;
    uint64_t comp_addr;
    uint64_t sig_len;
    uint64_t sig_addr;
};
typedef struct Diag508SigVerifBlock Diag508SigVerifBlock;

#endif
