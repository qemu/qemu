/*
 * tpm_ppi.c - TPM Physical Presence Interface
 *
 * Copyright (C) 2018 IBM Corporation
 *
 * Authors:
 *  Stefan Berger <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "cpu.h"
#include "sysemu/memory_mapping.h"
#include "migration/vmstate.h"
#include "tpm_ppi.h"

void tpm_ppi_init(TPMPPI *tpmppi, struct MemoryRegion *m,
                  hwaddr addr, Object *obj)
{
    tpmppi->buf = g_malloc0(HOST_PAGE_ALIGN(TPM_PPI_ADDR_SIZE));
    memory_region_init_ram_device_ptr(&tpmppi->ram, obj, "tpm-ppi",
                                      TPM_PPI_ADDR_SIZE, tpmppi->buf);
    vmstate_register_ram(&tpmppi->ram, DEVICE(obj));

    memory_region_add_subregion(m, addr, &tpmppi->ram);
}
