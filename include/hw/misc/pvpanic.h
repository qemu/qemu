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

#include "qom/object.h"

#define TYPE_PVPANIC_ISA_DEVICE "pvpanic"
#define TYPE_PVPANIC_PCI_DEVICE "pvpanic-pci"

#define PVPANIC_IOPORT_PROP "ioport"

/* The bit of supported pv event, TODO: include uapi header and remove this */
#define PVPANIC_F_PANICKED      0
#define PVPANIC_F_CRASHLOADED   1

/* The pv event value */
#define PVPANIC_PANICKED        (1 << PVPANIC_F_PANICKED)
#define PVPANIC_CRASHLOADED     (1 << PVPANIC_F_CRASHLOADED)

/*
 * PVPanicState for any device type
 */
typedef struct PVPanicState PVPanicState;
struct PVPanicState {
    MemoryRegion mr;
    uint8_t events;
};

void pvpanic_setup_io(PVPanicState *s, DeviceState *dev, unsigned size);

static inline uint16_t pvpanic_port(void)
{
    Object *o = object_resolve_path_type("", TYPE_PVPANIC_ISA_DEVICE, NULL);
    if (!o) {
        return 0;
    }
    return object_property_get_uint(o, PVPANIC_IOPORT_PROP, NULL);
}

#endif
