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

/*
 * The reference used for the DM163 is the following :
 * http://www.siti.com.tw/product/spec/LED/DM163.pdf
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/display/dm163.h"
#include "ui/console.h"
#include "trace.h"

#define LED_SQUARE_SIZE 100
/* Number of frames a row stays visible after being turned off. */
#define ROW_PERSISTENCE 3
#define TURNED_OFF_ROW (COLOR_BUFFER_SIZE - 1)

static const VMStateDescription vmstate_dm163 = {
    .name = TYPE_DM163,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64_ARRAY(bank0_shift_register, DM163State, 3),
        VMSTATE_UINT64_ARRAY(bank1_shift_register, DM163State, 3),
        VMSTATE_UINT16_ARRAY(latched_outputs, DM163State, DM163_NUM_LEDS),
        VMSTATE_UINT16_ARRAY(outputs, DM163State, DM163_NUM_LEDS),
        VMSTATE_UINT8(dck, DM163State),
        VMSTATE_UINT8(en_b, DM163State),
        VMSTATE_UINT8(lat_b, DM163State),
        VMSTATE_UINT8(rst_b, DM163State),
        VMSTATE_UINT8(selbk, DM163State),
        VMSTATE_UINT8(sin, DM163State),
        VMSTATE_UINT8(activated_rows, DM163State),
        VMSTATE_UINT32_2DARRAY(buffer, DM163State, COLOR_BUFFER_SIZE,
                               RGB_MATRIX_NUM_COLS),
        VMSTATE_UINT8(last_buffer_idx, DM163State),
        VMSTATE_UINT8_ARRAY(buffer_idx_of_row, DM163State, RGB_MATRIX_NUM_ROWS),
        VMSTATE_UINT8_ARRAY(row_persistence_delay, DM163State,
                            RGB_MATRIX_NUM_ROWS),
        VMSTATE_END_OF_LIST()
    }
};

static void dm163_reset_hold(Object *obj, ResetType type)
{
    DM163State *s = DM163(obj);

    s->sin = 0;
    s->dck = 0;
    s->rst_b = 0;
    /* Ensuring the first falling edge of lat_b isn't missed */
    s->lat_b = 1;
    s->selbk = 0;
    s->en_b = 0;
    /* Reset stops the PWM, not the shift and latched registers. */
    memset(s->outputs, 0, sizeof(s->outputs));

    s->activated_rows = 0;
    s->redraw = 0;
    trace_dm163_redraw(s->redraw);
    for (unsigned i = 0; i < COLOR_BUFFER_SIZE; i++) {
        memset(s->buffer[i], 0, sizeof(s->buffer[0]));
    }
    s->last_buffer_idx = 0;
    memset(s->buffer_idx_of_row, TURNED_OFF_ROW, sizeof(s->buffer_idx_of_row));
    memset(s->row_persistence_delay, 0, sizeof(s->row_persistence_delay));
}

static void dm163_dck_gpio_handler(void *opaque, int line, int new_state)
{
    DM163State *s = opaque;

    if (new_state && !s->dck) {
        /*
         * On raising dck, sample selbk to get the bank to use, and
         * sample sin for the bit to enter into the bank shift buffer.
         */
        uint64_t *sb =
            s->selbk ? s->bank1_shift_register : s->bank0_shift_register;
        /* Output the outgoing bit on sout */
        const bool sout = (s->selbk ? sb[2] & MAKE_64BIT_MASK(63, 1) :
                           sb[2] & MAKE_64BIT_MASK(15, 1)) != 0;
        qemu_set_irq(s->sout, sout);
        /* Enter sin into the shift buffer */
        sb[2] = (sb[2] << 1) | ((sb[1] >> 63) & 1);
        sb[1] = (sb[1] << 1) | ((sb[0] >> 63) & 1);
        sb[0] = (sb[0] << 1) | s->sin;
    }

    s->dck = new_state;
    trace_dm163_dck(new_state);
}

static void dm163_propagate_outputs(DM163State *s)
{
    s->last_buffer_idx = (s->last_buffer_idx + 1) % RGB_MATRIX_NUM_ROWS;
    /* Values are output when reset is high and enable is low. */
    if (s->rst_b && !s->en_b) {
        memcpy(s->outputs, s->latched_outputs, sizeof(s->outputs));
    } else {
        memset(s->outputs, 0, sizeof(s->outputs));
    }
    for (unsigned x = 0; x < RGB_MATRIX_NUM_COLS; x++) {
        /* Grouping the 3 RGB channels in a pixel value */
        const uint16_t b = extract16(s->outputs[3 * x + 0], 6, 8);
        const uint16_t g = extract16(s->outputs[3 * x + 1], 6, 8);
        const uint16_t r = extract16(s->outputs[3 * x + 2], 6, 8);
        uint32_t rgba = 0;

        trace_dm163_channels(3 * x + 2, r);
        trace_dm163_channels(3 * x + 1, g);
        trace_dm163_channels(3 * x + 0, b);

        rgba = deposit32(rgba,  0, 8, r);
        rgba = deposit32(rgba,  8, 8, g);
        rgba = deposit32(rgba, 16, 8, b);

        /* Led values are sent from the last one to the first one */
        s->buffer[s->last_buffer_idx][RGB_MATRIX_NUM_COLS - x - 1] = rgba;
    }
    for (unsigned row = 0; row < RGB_MATRIX_NUM_ROWS; row++) {
        if (s->activated_rows & (1 << row)) {
            s->buffer_idx_of_row[row] = s->last_buffer_idx;
            s->redraw |= (1 << row);
            trace_dm163_redraw(s->redraw);
        }
    }
}

static void dm163_en_b_gpio_handler(void *opaque, int line, int new_state)
{
    DM163State *s = opaque;

    s->en_b = new_state;
    dm163_propagate_outputs(s);
    trace_dm163_en_b(new_state);
}

static uint8_t dm163_bank0(const DM163State *s, uint8_t led)
{
    /*
     * Bank 0 uses 6 bits per led, so a value may be stored accross
     * two uint64_t entries.
     */
    const uint8_t low_bit = 6 * led;
    const uint8_t low_word = low_bit / 64;
    const uint8_t high_word = (low_bit + 5) / 64;
    const uint8_t low_shift = low_bit % 64;

    if (low_word == high_word) {
        /* Simple case: the value belongs to one entry. */
        return extract64(s->bank0_shift_register[low_word], low_shift, 6);
    }

    const uint8_t nb_bits_in_low_word = 64 - low_shift;
    const uint8_t nb_bits_in_high_word = 6 - nb_bits_in_low_word;

    const uint64_t bits_in_low_word = \
        extract64(s->bank0_shift_register[low_word], low_shift,
                  nb_bits_in_low_word);
    const uint64_t bits_in_high_word = \
        extract64(s->bank0_shift_register[high_word], 0,
                  nb_bits_in_high_word);
    uint8_t val = 0;

    val = deposit32(val, 0, nb_bits_in_low_word, bits_in_low_word);
    val = deposit32(val, nb_bits_in_low_word, nb_bits_in_high_word,
                    bits_in_high_word);

    return val;
}

static uint8_t dm163_bank1(const DM163State *s, uint8_t led)
{
    const uint64_t entry = s->bank1_shift_register[led / RGB_MATRIX_NUM_COLS];
    return extract64(entry, 8 * (led % RGB_MATRIX_NUM_COLS), 8);
}

static void dm163_lat_b_gpio_handler(void *opaque, int line, int new_state)
{
    DM163State *s = opaque;

    if (s->lat_b && !new_state) {
        for (int led = 0; led < DM163_NUM_LEDS; led++) {
            s->latched_outputs[led] = dm163_bank0(s, led) * dm163_bank1(s, led);
        }
        dm163_propagate_outputs(s);
    }

    s->lat_b = new_state;
    trace_dm163_lat_b(new_state);
}

static void dm163_rst_b_gpio_handler(void *opaque, int line, int new_state)
{
    DM163State *s = opaque;

    s->rst_b = new_state;
    dm163_propagate_outputs(s);
    trace_dm163_rst_b(new_state);
}

static void dm163_selbk_gpio_handler(void *opaque, int line, int new_state)
{
    DM163State *s = opaque;

    s->selbk = new_state;
    trace_dm163_selbk(new_state);
}

static void dm163_sin_gpio_handler(void *opaque, int line, int new_state)
{
    DM163State *s = opaque;

    s->sin = new_state;
    trace_dm163_sin(new_state);
}

static void dm163_rows_gpio_handler(void *opaque, int line, int new_state)
{
    DM163State *s = opaque;

    if (new_state) {
        s->activated_rows |= (1 << line);
        s->buffer_idx_of_row[line] = s->last_buffer_idx;
        s->redraw |= (1 << line);
        trace_dm163_redraw(s->redraw);
    } else {
        s->activated_rows &= ~(1 << line);
        s->row_persistence_delay[line] = ROW_PERSISTENCE;
    }
    trace_dm163_activated_rows(s->activated_rows);
}

static void dm163_invalidate_display(void *opaque)
{
    DM163State *s = (DM163State *)opaque;
    s->redraw = 0xFF;
    trace_dm163_redraw(s->redraw);
}

static void update_row_persistence_delay(DM163State *s, unsigned row)
{
    if (s->row_persistence_delay[row]) {
        s->row_persistence_delay[row]--;
    } else {
        /*
         * If the ROW_PERSISTENCE delay is up,
         * the row is turned off.
         */
        s->buffer_idx_of_row[row] = TURNED_OFF_ROW;
        s->redraw |= (1 << row);
        trace_dm163_redraw(s->redraw);
    }
}

static uint32_t *update_display_of_row(DM163State *s, uint32_t *dest,
                                       unsigned row)
{
    for (unsigned _ = 0; _ < LED_SQUARE_SIZE; _++) {
        for (int x = RGB_MATRIX_NUM_COLS * LED_SQUARE_SIZE - 1; x >= 0; x--) {
            /* UI layer guarantees that there's 32 bits per pixel (Mar 2024) */
            *dest++ = s->buffer[s->buffer_idx_of_row[row]][x / LED_SQUARE_SIZE];
        }
    }

    dpy_gfx_update(s->console, 0, LED_SQUARE_SIZE * row,
                    RGB_MATRIX_NUM_COLS * LED_SQUARE_SIZE, LED_SQUARE_SIZE);
    s->redraw &= ~(1 << row);
    trace_dm163_redraw(s->redraw);

    return dest;
}

static void dm163_update_display(void *opaque)
{
    DM163State *s = (DM163State *)opaque;
    DisplaySurface *surface = qemu_console_surface(s->console);
    uint32_t *dest;

    dest = surface_data(surface);
    for (unsigned row = 0; row < RGB_MATRIX_NUM_ROWS; row++) {
        update_row_persistence_delay(s, row);
        if (!extract8(s->redraw, row, 1)) {
            dest += LED_SQUARE_SIZE * LED_SQUARE_SIZE * RGB_MATRIX_NUM_COLS;
            continue;
        }
        dest = update_display_of_row(s, dest, row);
    }
}

static const GraphicHwOps dm163_ops = {
    .invalidate  = dm163_invalidate_display,
    .gfx_update  = dm163_update_display,
};

static void dm163_realize(DeviceState *dev, Error **errp)
{
    DM163State *s = DM163(dev);

    qdev_init_gpio_in(dev, dm163_rows_gpio_handler, RGB_MATRIX_NUM_ROWS);
    qdev_init_gpio_in(dev, dm163_sin_gpio_handler, 1);
    qdev_init_gpio_in(dev, dm163_dck_gpio_handler, 1);
    qdev_init_gpio_in(dev, dm163_rst_b_gpio_handler, 1);
    qdev_init_gpio_in(dev, dm163_lat_b_gpio_handler, 1);
    qdev_init_gpio_in(dev, dm163_selbk_gpio_handler, 1);
    qdev_init_gpio_in(dev, dm163_en_b_gpio_handler, 1);
    qdev_init_gpio_out_named(dev, &s->sout, "sout", 1);

    s->console = graphic_console_init(dev, 0, &dm163_ops, s);
    qemu_console_resize(s->console, RGB_MATRIX_NUM_COLS * LED_SQUARE_SIZE,
                        RGB_MATRIX_NUM_ROWS * LED_SQUARE_SIZE);
}

static void dm163_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "DM163 8x3-channel constant current LED driver";
    dc->vmsd = &vmstate_dm163;
    dc->realize = dm163_realize;
    rc->phases.hold = dm163_reset_hold;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo dm163_types[] = {
    {
        .name = TYPE_DM163,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(DM163State),
        .class_init = dm163_class_init
    }
};

DEFINE_TYPES(dm163_types)
