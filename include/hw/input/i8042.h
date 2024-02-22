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
#include "hw/sysbus.h"
#include "hw/input/ps2.h"
#include "qom/object.h"

#define I8042_KBD_IRQ      0
#define I8042_MOUSE_IRQ    1

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
    PS2KbdState ps2kbd;
    PS2MouseState ps2mouse;
    QEMUTimer *throttle_timer;

    qemu_irq irqs[2];
    qemu_irq a20_out;
    hwaddr mask;
} KBDState;

/*
 * QEMU interface:
 * + Named GPIO input "ps2-kbd-input-irq": set to 1 if the downstream PS2
 *   keyboard device has asserted its irq
 * + Named GPIO input "ps2-mouse-input-irq": set to 1 if the downstream PS2
 *   mouse device has asserted its irq
 * + Named GPIO output "a20": A20 line for x86 PCs
 * + Unnamed GPIO output 0-1: i8042 output irqs for keyboard (0) or mouse (1)
 */

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

/*
 * QEMU interface:
 * + sysbus MMIO region 0: MemoryRegion defining the command/status/data
 *   registers (access determined by mask property and access type)
 * + Named GPIO input "ps2-kbd-input-irq": set to 1 if the downstream PS2
 *   keyboard device has asserted its irq
 * + Named GPIO input "ps2-mouse-input-irq": set to 1 if the downstream PS2
 *   mouse device has asserted its irq
 * + Unnamed GPIO output 0-1: i8042 output irqs for keyboard (0) or mouse (1)
 */

#define TYPE_I8042_MMIO "i8042-mmio"
OBJECT_DECLARE_SIMPLE_TYPE(MMIOKBDState, I8042_MMIO)

struct MMIOKBDState {
    SysBusDevice parent_obj;

    KBDState kbd;
    uint32_t size;
    MemoryRegion region;
};

#define I8042_A20_LINE "a20"


void i8042_isa_mouse_fake_event(ISAKBDState *isa);

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
