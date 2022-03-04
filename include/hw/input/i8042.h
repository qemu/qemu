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
#include "qom/object.h"

#define TYPE_I8042 "i8042"
OBJECT_DECLARE_SIMPLE_TYPE(ISAKBDState, I8042)

#define I8042_A20_LINE "a20"


void i8042_mm_init(qemu_irq kbd_irq, qemu_irq mouse_irq,
                   MemoryRegion *region, ram_addr_t size,
                   hwaddr mask);
void i8042_isa_mouse_fake_event(ISAKBDState *isa);
void i8042_setup_a20_line(ISADevice *dev, qemu_irq a20_out);

static inline bool i8042_present(void)
{
    bool amb = false;
    return object_resolve_path_type("", TYPE_I8042, &amb) || amb;
}

/*
 * ACPI v2, Table 5-10 - Fixed ACPI Description Table Boot Architecture
 * Flags, bit offset 1 - 8042.
 */
static inline uint16_t iapc_boot_arch_8042(void)
{
    return i8042_present() ? 0x1 << 1 : 0x0 ;
}

#endif /* HW_INPUT_I8042_H */
