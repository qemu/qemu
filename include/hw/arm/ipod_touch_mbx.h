#ifndef HW_ARM_IPOD_TOUCH_MBX_H
#define HW_ARM_IPOD_TOUCH_MBX_H

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/sysbus.h"

#define TYPE_IPOD_TOUCH_MBX "ipodtouch.mbx"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchMBXState, IPOD_TOUCH_MBX)

typedef struct IPodTouchMBXState {
    SysBusDevice busdev;
    MemoryRegion iomem;
} IPodTouchMBXState;

#endif