#ifndef IPOD_TOUCH_CLOCK_H
#define IPOD_TOUCH_CLOCK_H

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/clock.h"

#define TYPE_IPOD_TOUCH_CLOCK                "ipodtouch.clock"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchClockState, IPOD_TOUCH_CLOCK)

#define CLOCK_CONFIG0 0x0
#define CLOCK_CONFIG1 0x4
#define CLOCK_CONFIG2 0x8
#define CLOCK_CONFIG3 0xC
#define CLOCK_CONFIG4 0x10
#define CLOCK_CONFIG5 0x14

#define CLOCK_PLL0CON 0x20
#define CLOCK_PLL1CON 0x24
#define CLOCK_PLL2CON 0x28
#define CLOCK_PLL3CON 0x2C
#define CLOCK_PLL0LCNT 0x30
#define CLOCK_PLL1LCNT 0x34
#define CLOCK_PLL2LCNT 0x38
#define CLOCK_PLL3LCNT 0x3C
#define CLOCK_PLLLOCK 0x40
#define CLOCK_PLLMODE 0x44

#define CLOCK_PWRCON0 0x48
#define CLOCK_PWRCON1 0x4C
#define CLOCK_PWRCON2 0x58
#define CLOCK_PWRCON3 0x68
#define CLOCK_PWRCON4 0x6C

typedef struct IPodTouchClockState
{
    SysBusDevice busdev;
    MemoryRegion iomem;
    uint32_t    config0;
    uint32_t    config1;
    uint32_t    config2;
    uint32_t    config3;
    uint32_t    config4;
    uint32_t    config5;

    uint32_t    pll0con;
    uint32_t    pll1con;
    uint32_t    pll2con;
    uint32_t    pll3con;
    uint32_t    pll0lcnt;
    uint32_t    pll1lcnt;
    uint32_t    pll2lcnt;
    uint32_t    pll3lcnt;
    uint32_t    pllmode;

    uint32_t    pwrcon0;
    uint32_t    pwrcon1;
    uint32_t    pwrcon2;
    uint32_t    pwrcon3;
    uint32_t    pwrcon4;

} IPodTouchClockState;

#endif