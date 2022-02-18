/*
 * PC DIMM device
 *
 * Copyright ProfitBricks GmbH 2012
 * Copyright (C) 2013-2014 Red Hat Inc
 *
 * Authors:
 *  Vasilis Liaskovitis <vasilis.liaskovitis@profitbricks.com>
 *  Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_PC_DIMM_H
#define QEMU_PC_DIMM_H

#include "exec/memory.h"
#include "hw/qdev-core.h"
#include "qom/object.h"

#define TYPE_PC_DIMM "pc-dimm"
OBJECT_DECLARE_TYPE(PCDIMMDevice, PCDIMMDeviceClass,
                    PC_DIMM)

#define PC_DIMM_ADDR_PROP "addr"
#define PC_DIMM_SLOT_PROP "slot"
#define PC_DIMM_NODE_PROP "node"
#define PC_DIMM_SIZE_PROP "size"
#define PC_DIMM_MEMDEV_PROP "memdev"

#define PC_DIMM_UNASSIGNED_SLOT -1

/**
 * PCDIMMDevice:
 * @addr: starting guest physical address, where @PCDIMMDevice is mapped.
 *         Default value: 0, means that address is auto-allocated.
 * @node: numa node to which @PCDIMMDevice is attached.
 * @slot: slot number into which @PCDIMMDevice is plugged in.
 *        Default value: -1, means that slot is auto-allocated.
 * @hostmem: host memory backend providing memory for @PCDIMMDevice
 */
struct PCDIMMDevice {
    /* private */
    DeviceState parent_obj;

    /* public */
    uint64_t addr;
    uint32_t node;
    int32_t slot;
    HostMemoryBackend *hostmem;
};

/**
 * PCDIMMDeviceClass:
 * @realize: called after common dimm is realized so that the dimm based
 * devices get the chance to do specified operations.
 */
struct PCDIMMDeviceClass {
    /* private */
    DeviceClass parent_class;

    /* public */
    void (*realize)(PCDIMMDevice *dimm, Error **errp);
    void (*unrealize)(PCDIMMDevice *dimm);
};

void pc_dimm_pre_plug(PCDIMMDevice *dimm, MachineState *machine,
                      const uint64_t *legacy_align, Error **errp);
void pc_dimm_plug(PCDIMMDevice *dimm, MachineState *machine);
void pc_dimm_unplug(PCDIMMDevice *dimm, MachineState *machine);
#endif
