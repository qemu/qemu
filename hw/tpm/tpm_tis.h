/*
 * tpm_tis.h - QEMU's TPM TIS interface emulator
 *
 * Copyright (C) 2006, 2010-2013 IBM Corporation
 *
 * Authors:
 *  Stefan Berger <stefanb@us.ibm.com>
 *  David Safford <safford@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Implementation of the TIS interface according to specs found at
 * http://www.trustedcomputinggroup.org
 *
 */
#ifndef TPM_TPM_TIS_H
#define TPM_TPM_TIS_H

#include "hw/isa/isa.h"
#include "hw/acpi/tpm.h"
#include "qemu-common.h"

#define TPM_TIS_NUM_LOCALITIES      5     /* per spec */
#define TPM_TIS_LOCALITY_SHIFT      12
#define TPM_TIS_NO_LOCALITY         0xff

#define TPM_TIS_IS_VALID_LOCTY(x)   ((x) < TPM_TIS_NUM_LOCALITIES)

#define TPM_TIS_BUFFER_MAX          4096

typedef enum {
    TPM_TIS_STATE_IDLE = 0,
    TPM_TIS_STATE_READY,
    TPM_TIS_STATE_COMPLETION,
    TPM_TIS_STATE_EXECUTION,
    TPM_TIS_STATE_RECEPTION,
} TPMTISState;

/* locality data  -- all fields are persisted */
typedef struct TPMLocality {
    TPMTISState state;
    uint8_t access;
    uint8_t sts;
    uint32_t inte;
    uint32_t ints;

    uint16_t w_offset;
    uint16_t r_offset;
    TPMSizedBuffer w_buffer;
    TPMSizedBuffer r_buffer;
} TPMLocality;

typedef struct TPMTISEmuState {
    QEMUBH *bh;
    uint32_t offset;
    uint8_t buf[TPM_TIS_BUFFER_MAX];

    uint8_t active_locty;
    uint8_t aborting_locty;
    uint8_t next_locty;

    TPMLocality loc[TPM_TIS_NUM_LOCALITIES];

    qemu_irq irq;
    uint32_t irq_num;
} TPMTISEmuState;

#endif /* TPM_TPM_TIS_H */
