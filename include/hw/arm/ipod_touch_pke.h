#ifndef HW_ARM_IPOD_TOUCH_PKE_H
#define HW_ARM_IPOD_TOUCH_PKE_H

#include "qemu/osdep.h"
#include "hw/platform-bus.h"
#include "hw/hw.h"
#include "exec/hwaddr.h"
#include "exec/memory.h"

#define TYPE_IPOD_TOUCH_PKE                "ipodtouch.pke"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchPKEState, IPOD_TOUCH_PKE)

#define REG_PKE_START     0x8
#define REG_PKE_SEG_SIZE  0x14
#define REG_PKE_SWRESET   0x24
#define REG_PKE_SEG_START 0x800

typedef struct IPodTouchPKEState {
	SysBusDevice busdev;
    MemoryRegion iomem;
    uint8_t segments[1024];
    uint32_t seg_size_reg;
    uint32_t segment_size;
    uint8_t num_started;
} IPodTouchPKEState;

#endif