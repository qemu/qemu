#ifndef IPOD_TOUCH_BLOCK_DEVICE_H
#define IPOD_TOUCH_BLOCK_DEVICE_H

#include <math.h>
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"

#define TYPE_IPOD_TOUCH_BLOCK_DEVICE              "ipodtouch.blockdevice"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchBlockDeviceState, IPOD_TOUCH_BLOCK_DEVICE)

#define BYTES_PER_BLOCK 4096

typedef struct IPodTouchBlockDeviceState
{
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t block_num_reg;
    uint32_t num_blocks_reg;
    uint32_t out_addr_reg;

    uint8_t *block_buffer;
} IPodTouchBlockDeviceState;

#endif