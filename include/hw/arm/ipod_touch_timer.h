#ifndef IPOD_TOUCH_TIMER_H
#define IPOD_TOUCH_TIMER_H

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/clock.h"

#define TYPE_IPOD_TOUCH_TIMER                "ipodtouch.timer"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchTimerState, IPOD_TOUCH_TIMER)

#define TIMER_IRQSTAT 0x10000
#define TIMER_IRQLATCH 0xF8
#define TIMER_TICKSHIGH 0x80
#define TIMER_TICKSLOW 0x84
#define TIMER_STATE_START 1
#define TIMER_STATE_STOP 0
#define TIMER_STATE_MANUALUPDATE 2
#define NUM_TIMERS 7
#define TIMER_4 0xA0
#define TIMER_CONFIG 0 
#define TIMER_STATE 0x4
#define TIMER_COUNT_BUFFER 0x8
#define TIMER_COUNT_BUFFER2 0xC

typedef struct IPodTouchTimerState
{
    SysBusDevice busdev;
    MemoryRegion iomem;
    uint32_t    ticks_high;
    uint32_t    ticks_low;
    uint32_t    status;
    uint32_t    config;
    uint32_t    bcount1;
    uint32_t    bcount2;
    uint32_t    prescaler;
    uint32_t    irqstat;
    QEMUTimer *st_timer;
    Clock *sysclk;
    uint32_t bcreload;
    uint32_t freq_out;
    uint64_t tick_interval;
    uint64_t last_tick;
    uint64_t next_planned_tick;
    uint64_t base_time;
    qemu_irq    irq;

} IPodTouchTimerState;

#endif