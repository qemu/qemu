#ifndef HW_ARM_IPOD_TOUCH_UNKNOWN1_H
#define HW_ARM_IPOD_TOUCH_UNKNOWN1_H

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/sysbus.h"

#define TYPE_IPOD_TOUCH_UNKNOWN1 "ipodtouch.unknown1"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchUnknown1State, IPOD_TOUCH_UNKNOWN1)

typedef struct IPodTouchUnknown1State {
    SysBusDevice busdev;
    MemoryRegion iomem;
} IPodTouchUnknown1State;

#endif