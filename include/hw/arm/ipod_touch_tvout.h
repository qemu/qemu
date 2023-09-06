#ifndef IPOD_TOUCH_TVOUT_H
#define IPOD_TOUCH_TVOUT_H

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/irq.h"

#define SDO_IRQ 0x280
#define SDO_IRQMASK 0x284

#define MXR_STATUS 0x0
#define MXR_CFG 0x4

#define TYPE_IPOD_TOUCH_TVOUT                "ipodtouch.tvout"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchTVOutState, IPOD_TOUCH_TVOUT)

typedef struct IPodTouchTVOutState {
    SysBusDevice parent_obj;

    MemoryRegion mixer1_iomem;
    MemoryRegion mixer2_iomem;
    MemoryRegion sdo_iomem;
    qemu_irq irq;

    uint32_t mixer1_status;
    uint32_t mixer1_cfg;

    uint32_t mixer2_status;
    uint32_t mixer2_cfg;

    uint32_t sdo_irq;
    uint32_t sdo_irq_mask;
} IPodTouchTVOutState;

#endif