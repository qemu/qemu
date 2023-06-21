/*
 * QEMU q800 logic GLUE (General Logic Unit)
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

#ifndef HW_Q800_GLUE_H
#define HW_Q800_GLUE_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"

#define TYPE_GLUE "q800-glue"
OBJECT_DECLARE_SIMPLE_TYPE(GLUEState, GLUE)

struct GLUEState {
    SysBusDevice parent_obj;

    M68kCPU *cpu;
    uint8_t ipr;
    uint8_t auxmode;
    qemu_irq irqs[1];
    QEMUTimer *nmi_release;
};

#define GLUE_IRQ_IN_VIA1       0
#define GLUE_IRQ_IN_VIA2       1
#define GLUE_IRQ_IN_SONIC      2
#define GLUE_IRQ_IN_ESCC       3
#define GLUE_IRQ_IN_NMI        4

#define GLUE_IRQ_NUBUS_9       0

#endif
