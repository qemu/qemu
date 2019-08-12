/*
 * QEMU PS/2 Controller
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef HW_INPUT_I8042_H
#define HW_INPUT_I8042_H

#include "hw/isa/isa.h"

#define TYPE_I8042 "i8042"

#define I8042_A20_LINE "a20"

void i8042_mm_init(qemu_irq kbd_irq, qemu_irq mouse_irq,
                   MemoryRegion *region, ram_addr_t size,
                   hwaddr mask);
void i8042_isa_mouse_fake_event(void *opaque);
void i8042_setup_a20_line(ISADevice *dev, qemu_irq a20_out);

#endif /* HW_INPUT_I8042_H */
