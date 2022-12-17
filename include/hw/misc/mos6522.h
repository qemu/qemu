/*
 * QEMU MOS6522 VIA emulation
 *
 * Copyright (c) 2004-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
 * Copyright (c) 2018 Mark Cave-Ayland
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef MOS6522_H
#define MOS6522_H

#include "exec/memory.h"
#include "hw/sysbus.h"
#include "hw/input/adb.h"
#include "qom/object.h"

#define MOS6522_NUM_REGS 16

/* Bits in ACR */
#define SR_CTRL            0x1c    /* Shift register control bits */
#define SR_EXT             0x0c    /* Shift on external clock */
#define SR_OUT             0x10    /* Shift out if 1 */

/* Bits in IFR and IER */
#define IER_SET            0x80    /* set bits in IER */
#define IER_CLR            0       /* clear bits in IER */

#define CA2_INT_BIT        0
#define CA1_INT_BIT        1
#define SR_INT_BIT         2       /* Shift register full/empty */
#define CB2_INT_BIT        3
#define CB1_INT_BIT        4
#define T2_INT_BIT         5       /* Timer 2 interrupt */
#define T1_INT_BIT         6       /* Timer 1 interrupt */

#define CA2_INT            BIT(CA2_INT_BIT)
#define CA1_INT            BIT(CA1_INT_BIT)
#define SR_INT             BIT(SR_INT_BIT)
#define CB2_INT            BIT(CB2_INT_BIT)
#define CB1_INT            BIT(CB1_INT_BIT)
#define T2_INT             BIT(T2_INT_BIT)
#define T1_INT             BIT(T1_INT_BIT)

#define VIA_NUM_INTS       5

/* Bits in ACR */
#define T1MODE             0xc0    /* Timer 1 mode */
#define T1MODE_CONT        0x40    /*  continuous interrupts */

/* Bits in PCR */
#define CB2_CTRL_MASK      0xe0
#define CB2_CTRL_SHIFT     5
#define CB1_CTRL_MASK      0x10
#define CB1_CTRL_SHIFT     4
#define CA2_CTRL_MASK      0x0e
#define CA2_CTRL_SHIFT     1
#define CA1_CTRL_MASK      0x1
#define CA1_CTRL_SHIFT     0

#define C2_POS             0x2
#define C2_IND             0x1

#define C1_POS             0x1

/* VIA registers */
#define VIA_REG_B       0x00
#define VIA_REG_A       0x01
#define VIA_REG_DIRB    0x02
#define VIA_REG_DIRA    0x03
#define VIA_REG_T1CL    0x04
#define VIA_REG_T1CH    0x05
#define VIA_REG_T1LL    0x06
#define VIA_REG_T1LH    0x07
#define VIA_REG_T2CL    0x08
#define VIA_REG_T2CH    0x09
#define VIA_REG_SR      0x0a
#define VIA_REG_ACR     0x0b
#define VIA_REG_PCR     0x0c
#define VIA_REG_IFR     0x0d
#define VIA_REG_IER     0x0e
#define VIA_REG_ANH     0x0f

/**
 * MOS6522Timer:
 * @counter_value: counter value at load time
 */
typedef struct MOS6522Timer {
    int index;
    uint16_t latch;
    uint16_t counter_value;
    int64_t load_time;
    int64_t next_irq_time;
    uint64_t frequency;
    QEMUTimer *timer;
} MOS6522Timer;

/**
 * MOS6522State:
 * @b: B-side data
 * @a: A-side data
 * @dirb: B-side direction (1=output)
 * @dira: A-side direction (1=output)
 * @sr: Shift register
 * @acr: Auxiliary control register
 * @pcr: Peripheral control register
 * @ifr: Interrupt flag register
 * @ier: Interrupt enable register
 * @anh: A-side data, no handshake
 * @last_b: last value of B register
 * @last_acr: last value of ACR register
 */
struct MOS6522State {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mem;
    /* VIA registers */
    uint8_t b;
    uint8_t a;
    uint8_t dirb;
    uint8_t dira;
    uint8_t sr;
    uint8_t acr;
    uint8_t pcr;
    uint8_t ifr;
    uint8_t ier;

    MOS6522Timer timers[2];
    uint64_t frequency;

    qemu_irq irq;
    uint8_t last_irq_levels;
};

#define TYPE_MOS6522 "mos6522"
OBJECT_DECLARE_TYPE(MOS6522State, MOS6522DeviceClass, MOS6522)

struct MOS6522DeviceClass {
    DeviceClass parent_class;

    ResettablePhases parent_phases;
    void (*portB_write)(MOS6522State *dev);
    void (*portA_write)(MOS6522State *dev);
    /* These are used to influence the CUDA MacOS timebase calibration */
    uint64_t (*get_timer1_counter_value)(MOS6522State *dev, MOS6522Timer *ti);
    uint64_t (*get_timer2_counter_value)(MOS6522State *dev, MOS6522Timer *ti);
    uint64_t (*get_timer1_load_time)(MOS6522State *dev, MOS6522Timer *ti);
    uint64_t (*get_timer2_load_time)(MOS6522State *dev, MOS6522Timer *ti);
};


extern const VMStateDescription vmstate_mos6522;

uint64_t mos6522_read(void *opaque, hwaddr addr, unsigned size);
void mos6522_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

void hmp_info_via(Monitor *mon, const QDict *qdict);

#endif /* MOS6522_H */
