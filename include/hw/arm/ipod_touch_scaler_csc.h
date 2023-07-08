#ifndef HW_ARM_IPOD_TOUCH_SCALER_CSC_H
#define HW_ARM_IPOD_TOUCH_SCALER_CSC_H

#include "qemu/osdep.h"
#include "hw/platform-bus.h"
#include "hw/hw.h"
#include "exec/hwaddr.h"
#include "exec/memory.h"

#define TYPE_IPOD_TOUCH_SCALER_CSC                "ipodtouch.scalercsc"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchScalerCSCState, IPOD_TOUCH_SCALER_CSC)


typedef struct IPodTouchScalerCSCState {
	SysBusDevice busdev;
    MemoryRegion iomem;
} IPodTouchScalerCSCState;

#endif