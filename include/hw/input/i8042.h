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

typedef struct KBDState {
    uint8_t write_cmd; /* if non zero, write data to port 60 is expected */
    uint8_t status;
    uint8_t mode;
    uint8_t outport;
    uint32_t migration_flags;
    uint32_t obsrc;
    bool outport_present;
    bool extended_state;
    bool extended_state_loaded;
    /* Bitmask of devices with data available.  */
    uint8_t pending;
    uint8_t obdata;
    uint8_t cbdata;
    uint8_t pending_tmp;
    void *kbd;
    void *mouse;
    QEMUTimer *throttle_timer;

    qemu_irq irq_kbd;
    qemu_irq irq_mouse;
    qemu_irq a20_out;
    hwaddr mask;
} KBDState;

#define TYPE_I8042 "i8042"
OBJECT_DECLARE_SIMPLE_TYPE(ISAKBDState, I8042)

struct ISAKBDState {
    ISADevice parent_obj;

    KBDState kbd;
    bool kbd_throttle;
    MemoryRegion io[2];
    uint8_t kbd_irq;
    uint8_t mouse_irq;
};

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
