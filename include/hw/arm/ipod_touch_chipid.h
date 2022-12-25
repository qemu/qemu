#ifndef HW_ARM_IPOD_TOUCH_CHIPID_H
#define HW_ARM_IPOD_TOUCH_CHIPID_H

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/sysbus.h"

#define TYPE_IPOD_TOUCH_CHIPID "ipodtouch.chipid"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchChipIDState, IPOD_TOUCH_CHIPID)

#define CHIPID_UNKNOWN1 0x4
#define CHIPID_INFO     0x8
#define CHIPID_UNKNOWN2 0xC
#define CHIPID_UNKNOWN3 0x10

typedef struct IPodTouchChipIDState {
    SysBusDevice busdev;
    MemoryRegion iomem;
} IPodTouchChipIDState;

#endif