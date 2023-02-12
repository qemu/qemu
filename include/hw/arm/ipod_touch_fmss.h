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
#define FMSS_TARGET_ADDR    0xD08
#define FMSS_CMD            0xD0C

typedef struct IPodTouchFMSSState
{
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t reg_cs_irq_bit;
    uint32_t reg_target_addr;
} IPodTouchFMSSState;

#endif