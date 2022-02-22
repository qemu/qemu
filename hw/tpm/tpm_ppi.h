/*
 * TPM Physical Presence Interface
 *
 * Copyright (C) 2018 IBM Corporation
 *
 * Authors:
 *  Stefan Berger    <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef TPM_TPM_PPI_H
#define TPM_TPM_PPI_H

#include "exec/memory.h"

typedef struct TPMPPI {
    MemoryRegion ram;
    uint8_t *buf;
} TPMPPI;

/**
 * tpm_ppi_init:
 * @tpmppi: a TPMPPI
 * @m: the address-space / MemoryRegion to use
 * @addr: the address of the PPI region
 * @obj: the owner object
 *
 * Register the TPM PPI memory region at @addr on the given address
 * space for the object @obj.
 **/
void tpm_ppi_init(TPMPPI *tpmppi, MemoryRegion *m,
                  hwaddr addr, Object *obj);

/**
 * tpm_ppi_reset:
 * @tpmppi: a TPMPPI
 *
 * Function to call on machine reset. It will check if the "Memory
 * overwrite" variable is set, and perform a memory clear on volatile
 * memory if requested.
 **/
void tpm_ppi_reset(TPMPPI *tpmppi);

#endif /* TPM_TPM_PPI_H */
