/*
 * QEMU DM163 8x3-channel constant current led driver
 * driving columns of associated 8x8 RGB matrix.
 *
 * Copyright (C) 2024 Samuel Tardieu <sam@rfc1149.net>
 * Copyright (C) 2024 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (C) 2024 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_DISPLAY_DM163_H
#define HW_DISPLAY_DM163_H

#include "qom/object.h"
#include "hw/qdev-core.h"

#define TYPE_DM163 "dm163"
OBJECT_DECLARE_SIMPLE_TYPE(DM163State, DM163);

#define RGB_MATRIX_NUM_ROWS 8
#define RGB_MATRIX_NUM_COLS 8
#define DM163_NUM_LEDS (RGB_MATRIX_NUM_COLS * 3)
/* The last row is filled with 0 (turned off row) */
#define COLOR_BUFFER_SIZE (RGB_MATRIX_NUM_ROWS + 1)

typedef struct DM163State {
    DeviceState parent_obj;

    /* DM163 driver */
    uint64_t bank0_shift_register[3];
    uint64_t bank1_shift_register[3];
    uint16_t latched_outputs[DM163_NUM_LEDS];
    uint16_t outputs[DM163_NUM_LEDS];
    qemu_irq sout;

    uint8_t sin;
    uint8_t dck;
    uint8_t rst_b;
    uint8_t lat_b;
    uint8_t selbk;
    uint8_t en_b;

    /* IM120417002 colors shield */
    uint8_t activated_rows;

    /* 8x8 RGB matrix */
    QemuConsole *console;
    uint8_t redraw;
    /* Rows currently being displayed on the matrix. */
    /* The last row is filled with 0 (turned off row) */
    uint32_t buffer[COLOR_BUFFER_SIZE][RGB_MATRIX_NUM_COLS];
    uint8_t last_buffer_idx;
    uint8_t buffer_idx_of_row[RGB_MATRIX_NUM_ROWS];
    /* Used to simulate retinal persistence of rows */
    uint8_t row_persistence_delay[RGB_MATRIX_NUM_ROWS];
} DM163State;

#endif /* HW_DISPLAY_DM163_H */
