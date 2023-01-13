/*
 * Nuvoton NPCM7xx PWM Module
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
#ifndef NPCM7XX_PWM_H
#define NPCM7XX_PWM_H

#include "hw/clock.h"
#include "hw/sysbus.h"
#include "hw/irq.h"

/* Each PWM module holds 4 PWM channels. */
#define NPCM7XX_PWM_PER_MODULE 4

/*
 * Number of registers in one pwm module. Don't change this without increasing
 * the version_id in vmstate.
 */
#define NPCM7XX_PWM_NR_REGS (0x54 / sizeof(uint32_t))

/*
 * The maximum duty values. Each duty unit represents 1/NPCM7XX_PWM_MAX_DUTY
 * cycles. For example, if NPCM7XX_PWM_MAX_DUTY=1,000,000 and a PWM has a duty
 * value of 100,000 the duty cycle for that PWM is 10%.
 */
#define NPCM7XX_PWM_MAX_DUTY 1000000

typedef struct NPCM7xxPWMState NPCM7xxPWMState;

/**
 * struct NPCM7xxPWM - The state of a single PWM channel.
 * @module: The PWM module that contains this channel.
 * @irq: GIC interrupt line to fire on expiration if enabled.
 * @running: Whether this PWM channel is generating output.
 * @inverted: Whether this PWM channel is inverted.
 * @index: The index of this PWM channel.
 * @cnr: The counter register.
 * @cmr: The comparator register.
 * @pdr: The data register.
 * @pwdr: The watchdog register.
 * @freq: The frequency of this PWM channel.
 * @duty: The duty cycle of this PWM channel. One unit represents
 *   1/NPCM7XX_MAX_DUTY cycles.
 */
typedef struct NPCM7xxPWM {
    NPCM7xxPWMState         *module;

    qemu_irq                irq;

    bool                    running;
    bool                    inverted;

    uint8_t                 index;
    uint32_t                cnr;
    uint32_t                cmr;
    uint32_t                pdr;
    uint32_t                pwdr;

    uint32_t                freq;
    uint32_t                duty;
} NPCM7xxPWM;

/**
 * struct NPCM7xxPWMState - Pulse Width Modulation device state.
 * @parent: System bus device.
 * @iomem: Memory region through which registers are accessed.
 * @clock: The PWM clock.
 * @pwm: The PWM channels owned by this module.
 * @duty_gpio_out: The duty cycle of each PWM channels as a output GPIO.
 * @ppr: The prescaler register.
 * @csr: The clock selector register.
 * @pcr: The control register.
 * @pier: The interrupt enable register.
 * @piir: The interrupt indication register.
 */
struct NPCM7xxPWMState {
    SysBusDevice parent;

    MemoryRegion iomem;

    Clock       *clock;
    NPCM7xxPWM  pwm[NPCM7XX_PWM_PER_MODULE];
    qemu_irq    duty_gpio_out[NPCM7XX_PWM_PER_MODULE];

    uint32_t    ppr;
    uint32_t    csr;
    uint32_t    pcr;
    uint32_t    pier;
    uint32_t    piir;
};

#define TYPE_NPCM7XX_PWM "npcm7xx-pwm"
OBJECT_DECLARE_SIMPLE_TYPE(NPCM7xxPWMState, NPCM7XX_PWM)

#endif /* NPCM7XX_PWM_H */
