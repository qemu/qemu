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
#include "qemu/memalign.h"
#include "qapi/error.h"
#include "sysemu/memory_mapping.h"
#include "migration/vmstate.h"
#include "hw/qdev-core.h"
#include "hw/acpi/tpm.h"
#include "tpm_ppi.h"
#include "trace.h"

void tpm_ppi_reset(TPMPPI *tpmppi)
{
    if (tpmppi->buf[0x15a /* movv, docs/specs/tpm.rst */] & 0x1) {
        GuestPhysBlockList guest_phys_blocks;
        GuestPhysBlock *block;

        guest_phys_blocks_init(&guest_phys_blocks);
        guest_phys_blocks_append(&guest_phys_blocks);
        QTAILQ_FOREACH(block, &guest_phys_blocks.head, next) {
            hwaddr mr_offs = block->host_addr -
                             (uint8_t *)memory_region_get_ram_ptr(block->mr);

            trace_tpm_ppi_memset(block->host_addr,
                                 block->target_end - block->target_start);
            memset(block->host_addr, 0,
                   block->target_end - block->target_start);
            memory_region_set_dirty(block->mr, mr_offs,
                                    block->target_end - block->target_start);
        }
        guest_phys_blocks_free(&guest_phys_blocks);
    }
}

void tpm_ppi_init(TPMPPI *tpmppi, MemoryRegion *m,
                  hwaddr addr, Object *obj)
{
    tpmppi->buf = qemu_memalign(qemu_real_host_page_size,
                                HOST_PAGE_ALIGN(TPM_PPI_ADDR_SIZE));
    memory_region_init_ram_device_ptr(&tpmppi->ram, obj, "tpm-ppi",
                                      TPM_PPI_ADDR_SIZE, tpmppi->buf);
    vmstate_register_ram(&tpmppi->ram, DEVICE(obj));

    memory_region_add_subregion(m, addr, &tpmppi->ram);
}
