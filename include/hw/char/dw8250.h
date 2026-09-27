/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Synopsys DesignWare APB UART (DW 8250)
 *
 * Copyright (c) 2026 Kuan-Wei Chiu <visitorckw@gmail.com>
 */

#ifndef HW_CHAR_DW8250_H
#define HW_CHAR_DW8250_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "hw/char/serial-mm.h"

#define TYPE_DW8250 "dw8250"
OBJECT_DECLARE_SIMPLE_TYPE(DW8250State, DW8250)

struct DW8250State {
    SysBusDevice parent_obj;

    MemoryRegion container;
    MemoryRegion ext_iomem;
    SerialMM serial_mm;

    uint8_t regshift;
};

#endif
