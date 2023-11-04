#ifndef IPOD_TOUCH_GPIO_H
#define IPOD_TOUCH_GPIO_H

#include <math.h>
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"

#define TYPE_IPOD_TOUCH_GPIO                "ipodtouch.gpio"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchGPIOState, IPOD_TOUCH_GPIO)

#define GPIO_BUTTON_POWER 0xC02
#define GPIO_BUTTON_HOME  0xC01

#define GPIO_BUTTON_POWER_IRQ 0x7A
#define GPIO_BUTTON_HOME_IRQ  0x79

#define NUM_GPIO_PINS 0x20

typedef struct IPodTouchGPIOState
{
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t gpio_state;
} IPodTouchGPIOState;

#endif