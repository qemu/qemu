/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Goldfish TTY
 *
 * (c) 2020 Laurent Vivier <laurent@vivier.eu>
 *
 */

#ifndef HW_CHAR_GOLDFISH_TTY_H
#define HW_CHAR_GOLDFISH_TTY_H

#include "qemu/fifo8.h"
#include "chardev/char-fe.h"
#include "hw/sysbus.h"

#define TYPE_GOLDFISH_TTY "goldfish_tty"
OBJECT_DECLARE_SIMPLE_TYPE(GoldfishTTYState, GOLDFISH_TTY)

#define GOLFISH_TTY_BUFFER_SIZE 128

struct GoldfishTTYState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    CharBackend chr;

    uint32_t data_len;
    uint64_t data_ptr;
    bool int_enabled;

    Fifo8 rx_fifo;
};

#endif
