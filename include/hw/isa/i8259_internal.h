/*
 * QEMU 8259 - internal interfaces
 *
 * Copyright (c) 2011 Jan Kiszka, Siemens AG
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

#ifndef QEMU_I8259_INTERNAL_H
#define QEMU_I8259_INTERNAL_H

#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/isa/isa.h"

typedef struct PICCommonState PICCommonState;

#define TYPE_PIC_COMMON "pic-common"
#define PIC_COMMON(obj) \
     OBJECT_CHECK(PICCommonState, (obj), TYPE_PIC_COMMON)
#define PIC_COMMON_CLASS(klass) \
     OBJECT_CLASS_CHECK(PICCommonClass, (klass), TYPE_PIC_COMMON)
#define PIC_COMMON_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PICCommonClass, (obj), TYPE_PIC_COMMON)

typedef struct PICCommonClass
{
    ISADeviceClass parent_class;

    void (*pre_save)(PICCommonState *s);
    void (*post_load)(PICCommonState *s);
} PICCommonClass;

struct PICCommonState {
    ISADevice parent_obj;

    uint8_t last_irr; /* edge detection */
    uint8_t irr; /* interrupt request register */
    uint8_t imr; /* interrupt mask register */
    uint8_t isr; /* interrupt service register */
    uint8_t priority_add; /* highest irq priority */
    uint8_t irq_base;
    uint8_t read_reg_select;
    uint8_t poll;
    uint8_t special_mask;
    uint8_t init_state;
    uint8_t auto_eoi;
    uint8_t rotate_on_auto_eoi;
    uint8_t special_fully_nested_mode;
    uint8_t init4; /* true if 4 byte init */
    uint8_t single_mode; /* true if slave pic is not initialized */
    uint8_t elcr; /* PIIX edge/trigger selection*/
    uint8_t elcr_mask;
    qemu_irq int_out[1];
    uint32_t master; /* reflects /SP input pin */
    uint32_t iobase;
    uint32_t elcr_addr;
    MemoryRegion base_io;
    MemoryRegion elcr_io;
};

void pic_reset_common(PICCommonState *s);

ISADevice *i8259_init_chip(const char *name, ISABus *bus, bool master);


#endif /* !QEMU_I8259_INTERNAL_H */
