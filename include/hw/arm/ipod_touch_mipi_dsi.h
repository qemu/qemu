#ifndef IPOD_TOUCH_MIPI_DSI_H
#define IPOD_TOUCH_MIPI_DSI_H

#include <math.h>
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/irq.h"

#define TYPE_IPOD_TOUCH_MIPI_DSI                "ipodtouch.mipidsi"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchMIPIDSIState, IPOD_TOUCH_MIPI_DSI)

typedef struct IPodTouchMIPIDSIState
{
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
} IPodTouchMIPIDSIState;

#endif