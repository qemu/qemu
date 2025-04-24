/*
 * Nuvoton NPCM7xx Timer Controller
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */
#ifndef NPCM7XX_TIMER_H
#define NPCM7XX_TIMER_H

#include "system/memory.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"

/* Each Timer Module (TIM) instance holds five 25 MHz timers. */
#define NPCM7XX_TIMERS_PER_CTRL (5)

/*
 * Number of registers in our device state structure. Don't change this without
 * incrementing the version_id in the vmstate.
 */
#define NPCM7XX_TIMER_NR_REGS (0x54 / sizeof(uint32_t))

/* The basic watchdog timer period is 2^14 clock cycles. */
#define NPCM7XX_WATCHDOG_BASETIME_SHIFT 14

#define NPCM7XX_WATCHDOG_RESET_GPIO_OUT "npcm7xx-clk-watchdog-reset-gpio-out"

typedef struct NPCM7xxTimerCtrlState NPCM7xxTimerCtrlState;

/**
 * struct NPCM7xxBaseTimer - Basic functionality that both regular timer and
 * watchdog timer use.
 * @qtimer: QEMU timer that notifies us on expiration.
 * @expires_ns: Absolute virtual expiration time.
 * @remaining_ns: Remaining time until expiration if timer is paused.
 */
typedef struct NPCM7xxBaseTimer {
    QEMUTimer   qtimer;
    int64_t     expires_ns;
    int64_t     remaining_ns;
} NPCM7xxBaseTimer;

/**
 * struct NPCM7xxTimer - Individual timer state.
 * @ctrl: The timer module that owns this timer.
 * @irq: GIC interrupt line to fire on expiration (if enabled).
 * @base_timer: The basic timer functionality for this timer.
 * @tcsr: The Timer Control and Status Register.
 * @ticr: The Timer Initial Count Register.
 */
typedef struct NPCM7xxTimer {
    NPCM7xxTimerCtrlState *ctrl;

    qemu_irq    irq;
    NPCM7xxBaseTimer base_timer;

    uint32_t    tcsr;
    uint32_t    ticr;
} NPCM7xxTimer;

/**
 * struct NPCM7xxWatchdogTimer - The watchdog timer state.
 * @ctrl: The timer module that owns this timer.
 * @irq: GIC interrupt line to fire on expiration (if enabled).
 * @reset_signal: The GPIO used to send a reset signal.
 * @base_timer: The basic timer functionality for this timer.
 * @wtcr: The Watchdog Timer Control Register.
 */
typedef struct NPCM7xxWatchdogTimer {
    NPCM7xxTimerCtrlState *ctrl;

    qemu_irq            irq;
    qemu_irq            reset_signal;
    NPCM7xxBaseTimer base_timer;

    uint32_t            wtcr;
} NPCM7xxWatchdogTimer;

/**
 * struct NPCM7xxTimerCtrlState - Timer Module device state.
 * @parent: System bus device.
 * @iomem: Memory region through which registers are accessed.
 * @index: The index of this timer module.
 * @tisr: The Timer Interrupt Status Register.
 * @timer: The five individual timers managed by this module.
 * @watchdog_timer: The watchdog timer managed by this module.
 */
struct NPCM7xxTimerCtrlState {
    SysBusDevice parent;

    MemoryRegion iomem;

    uint32_t    tisr;

    Clock       *clock;
    NPCM7xxTimer timer[NPCM7XX_TIMERS_PER_CTRL];
    NPCM7xxWatchdogTimer watchdog_timer;
};

#define TYPE_NPCM7XX_TIMER "npcm7xx-timer"
#define NPCM7XX_TIMER(obj)                                              \
    OBJECT_CHECK(NPCM7xxTimerCtrlState, (obj), TYPE_NPCM7XX_TIMER)

#endif /* NPCM7XX_TIMER_H */
