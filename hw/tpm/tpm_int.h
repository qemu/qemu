/*
 * TPM configuration
 *
 * Copyright (C) 2011-2013 IBM Corporation
 *
 * Authors:
 *  Stefan Berger    <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef TPM_TPM_INT_H
#define TPM_TPM_INT_H

#include "exec/memory.h"
#include "tpm_tis.h"

/* overall state of the TPM interface */
struct TPMState {
    ISADevice busdev;
    MemoryRegion mmio;

    union {
        TPMTISEmuState tis;
    } s;

    uint8_t     locty_number;
    TPMLocality *locty_data;

    char *backend;
    TPMBackend *be_driver;
};

#define TPM(obj) OBJECT_CHECK(TPMState, (obj), TYPE_TPM_TIS)

#define TPM_STANDARD_CMDLINE_OPTS \
    { \
        .name = "type", \
        .type = QEMU_OPT_STRING, \
        .help = "Type of TPM backend", \
    }

struct tpm_req_hdr {
    uint16_t tag;
    uint32_t len;
    uint32_t ordinal;
} QEMU_PACKED;

struct tpm_resp_hdr {
    uint16_t tag;
    uint32_t len;
    uint32_t errcode;
} QEMU_PACKED;

#define TPM_TAG_RQU_COMMAND       0xc1
#define TPM_TAG_RQU_AUTH1_COMMAND 0xc2
#define TPM_TAG_RQU_AUTH2_COMMAND 0xc3

#define TPM_TAG_RSP_COMMAND       0xc4
#define TPM_TAG_RSP_AUTH1_COMMAND 0xc5
#define TPM_TAG_RSP_AUTH2_COMMAND 0xc6

#define TPM_FAIL                  9

#define TPM_ORD_GetTicks          0xf1

#endif /* TPM_TPM_INT_H */
