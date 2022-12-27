#ifndef HW_ARM_IPOD_TOUCH_PKE_H
#define HW_ARM_IPOD_TOUCH_PKE_H

#include "qemu/osdep.h"
#include "hw/platform-bus.h"
#include "hw/hw.h"
#include "exec/hwaddr.h"
#include "exec/memory.h"

#define TYPE_IPOD_TOUCH_PKE                "ipodtouch.pke"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchPKEState, IPOD_TOUCH_PKE)

typedef struct IPodTouchPKEState {
	SysBusDevice busdev;
    MemoryRegion iomem;
    uint8_t pmod_result[256];
} IPodTouchPKEState;

#endif