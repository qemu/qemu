/*
 * Nuvoton NPCM7xx ADC Module
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
#ifndef NPCM7XX_ADC_H
#define NPCM7XX_ADC_H

#include "hw/clock.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"

#define NPCM7XX_ADC_NUM_INPUTS      8
/**
 * This value should not be changed unless write_adc_calibration function in
 * hw/arm/npcm7xx.c is also changed.
 */
#define NPCM7XX_ADC_NUM_CALIB       2

/**
 * struct NPCM7xxADCState - Analog to Digital Converter Module device state.
 * @parent: System bus device.
 * @iomem: Memory region through which registers are accessed.
 * @conv_timer: The timer counts down remaining cycles for the conversion.
 * @irq: GIC interrupt line to fire on expiration (if enabled).
 * @con: The Control Register.
 * @data: The Data Buffer.
 * @clock: The ADC Clock.
 * @adci: The input voltage in units of uV. 1uv = 1e-6V.
 * @vref: The external reference voltage.
 * @iref: The internal reference voltage, initialized at launch time.
 * @rv: The calibrated output values of 0.5V and 1.5V for the ADC.
 */
struct NPCM7xxADCState {
    SysBusDevice parent;

    MemoryRegion iomem;

    QEMUTimer    conv_timer;

    qemu_irq     irq;
    uint32_t     con;
    uint32_t     data;
    Clock       *clock;

    /* Voltages are in unit of uV. 1V = 1000000uV. */
    uint32_t     adci[NPCM7XX_ADC_NUM_INPUTS];
    uint32_t     vref;
    uint32_t     iref;

    uint16_t     calibration_r_values[NPCM7XX_ADC_NUM_CALIB];
};

#define TYPE_NPCM7XX_ADC "npcm7xx-adc"
OBJECT_DECLARE_SIMPLE_TYPE(NPCM7xxADCState, NPCM7XX_ADC)

#endif /* NPCM7XX_ADC_H */
