#ifndef IPOD_TOUCH_LCD_H
#define IPOD_TOUCH_LCD_H

#include <math.h>
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/arm/ipod_touch_multitouch.h"

#define TYPE_IPOD_TOUCH_LCD                "ipodtouch.lcd"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchLCDState, IPOD_TOUCH_LCD)

#define LCD_REFRESH_RATE_FREQUENCY 10

typedef struct IPodTouchLCDState
{
    SysBusDevice parent_obj;
    MemoryRegion *sysmem;
    MemoryRegion iomem;
    QemuConsole *con;
    IPodTouchMultitouchState *mt;
    int invalidate;
    MemoryRegionSection fbsection;
    qemu_irq irq;
    uint32_t lcd_con;

    uint32_t w1_display_resolution_info;
    uint32_t w1_framebuffer_base;
    uint32_t w1_hspan;
    uint32_t w1_display_depth_info;

    uint32_t unknown1;

    QEMUTimer *refresh_timer;
} IPodTouchLCDState;

#endif