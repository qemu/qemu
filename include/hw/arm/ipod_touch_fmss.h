#ifndef IPOD_TOUCH_FMSS_H
#define IPOD_TOUCH_FMSS_H

#include <math.h>
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/irq.h"

#define TYPE_IPOD_TOUCH_FMSS                "ipodtouch.fmss"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchFMSSState, IPOD_TOUCH_FMSS)

#define FMSS__FMCTRL1       0x4
#define FMSS__CS_IRQ        0xC0C
#define FMSS__CS_BUF_RST_OK 0xC64

typedef struct IPodTouchFMSSState
{
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
} IPodTouchFMSSState;

#endif