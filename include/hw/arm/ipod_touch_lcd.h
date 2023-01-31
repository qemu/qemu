#ifndef IPOD_TOUCH_LCD_H
#define IPOD_TOUCH_LCD_H

#include <math.h>
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/irq.h"

#define TYPE_IPOD_TOUCH_LCD                "ipodtouch.lcd"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchLCDState, IPOD_TOUCH_LCD)

typedef struct IPodTouchLCDState
{
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
} IPodTouchLCDState;

#endif