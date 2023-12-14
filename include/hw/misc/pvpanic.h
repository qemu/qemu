/*
 * QEMU simulated pvpanic device.
 *
 * Copyright Fujitsu, Corp. 2013
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *     Hu Tao <hutao@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_MISC_PVPANIC_H
#define HW_MISC_PVPANIC_H

#include "exec/memory.h"
#include "qom/object.h"

#define TYPE_PVPANIC_ISA_DEVICE "pvpanic"
#define TYPE_PVPANIC_PCI_DEVICE "pvpanic-pci"

#define PVPANIC_IOPORT_PROP "ioport"

/*
 * PVPanicState for any device type
 */
typedef struct PVPanicState PVPanicState;
struct PVPanicState {
    MemoryRegion mr;
    uint8_t events;
};

void pvpanic_setup_io(PVPanicState *s, DeviceState *dev, unsigned size);

#endif
