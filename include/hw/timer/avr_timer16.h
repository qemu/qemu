/*
 * AVR 16-bit timer
 *
 * Copyright (c) 2018 University of Kent
 * Author: Ed Robbins
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

/*
 * Driver for 16 bit timers on 8 bit AVR devices.
 * Note:
 * On ATmega640/V-1280/V-1281/V-2560/V-2561/V timers 1, 3, 4 and 5 are 16 bit
 */

#ifndef HW_TIMER_AVR_TIMER16_H
#define HW_TIMER_AVR_TIMER16_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "hw/hw.h"
#include "qom/object.h"

enum NextInterrupt {
    OVERFLOW,
    COMPA,
    COMPB,
    COMPC,
    CAPT
};

#define TYPE_AVR_TIMER16 "avr-timer16"
OBJECT_DECLARE_SIMPLE_TYPE(AVRTimer16State, AVR_TIMER16)

struct AVRTimer16State {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;
    MemoryRegion imsk_iomem;
    MemoryRegion ifr_iomem;
    QEMUTimer *timer;
    qemu_irq capt_irq;
    qemu_irq compa_irq;
    qemu_irq compb_irq;
    qemu_irq compc_irq;
    qemu_irq ovf_irq;

    bool enabled;

    /* registers */
    uint8_t cra;
    uint8_t crb;
    uint8_t crc;
    uint8_t cntl;
    uint8_t cnth;
    uint8_t icrl;
    uint8_t icrh;
    uint8_t ocral;
    uint8_t ocrah;
    uint8_t ocrbl;
    uint8_t ocrbh;
    uint8_t ocrcl;
    uint8_t ocrch;
    /*
     * Reads and writes to CNT and ICR utilise a bizarre temporary
     * register, which we emulate
     */
    uint8_t rtmp;
    uint8_t imsk;
    uint8_t ifr;

    uint8_t id;
    uint64_t cpu_freq_hz;
    uint64_t freq_hz;
    uint64_t period_ns;
    uint64_t reset_time_ns;
    enum NextInterrupt next_interrupt;
};

#endif /* HW_TIMER_AVR_TIMER16_H */
