/*
 * QEMU PC APM controller Emulation
 * This is split out from acpi.c
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/isa/apm.h"
#include "hw/pci/pci.h"
#include "migration/vmstate.h"
#include "trace.h"


/* fixed I/O location */
#define APM_STS_IOPORT  0xb3

static void apm_ioport_writeb(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    APMState *apm = opaque;
    addr &= 1;

    trace_apm_io_write(addr, val);
    if (addr == 0) {
        apm->apmc = val;

        if (apm->callback) {
            (apm->callback)(val, apm->arg);
        }
    } else {
        apm->apms = val;
    }
}

static uint64_t apm_ioport_readb(void *opaque, hwaddr addr, unsigned size)
{
    APMState *apm = opaque;
    uint32_t val;

    addr &= 1;
    if (addr == 0) {
        val = apm->apmc;
    } else {
        val = apm->apms;
    }
    trace_apm_io_read(addr, val);

    return val;
}

const VMStateDescription vmstate_apm = {
    .name = "APM State",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(apmc, APMState),
        VMSTATE_UINT8(apms, APMState),
        VMSTATE_END_OF_LIST()
    }
};

static const MemoryRegionOps apm_ops = {
    .read = apm_ioport_readb,
    .write = apm_ioport_writeb,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

void apm_init(PCIDevice *dev, APMState *apm, apm_ctrl_changed_t callback,
              void *arg)
{
    apm->callback = callback;
    apm->arg = arg;

    /* ioport 0xb2, 0xb3 */
    memory_region_init_io(&apm->io, OBJECT(dev), &apm_ops, apm, "apm-io", 2);
    memory_region_add_subregion(pci_address_space_io(dev), APM_CNT_IOPORT,
                                &apm->io);
}
