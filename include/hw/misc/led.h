/*
 * QEMU single LED device
 *
 * Copyright (C) 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MISC_LED_H
#define HW_MISC_LED_H

#include "qom/object.h"
#include "hw/qdev-core.h"

#define TYPE_LED "led"

/**
 * LEDColor: Color of a LED
 *
 * This set is restricted to physically available LED colors.
 *
 * LED colors from 'Table 1. Product performance of LUXEON Rebel Color
 * Line' of the 'DS68 LUXEON Rebel Color Line' datasheet available at:
 * https://www.lumileds.com/products/color-leds/luxeon-rebel-color/
 */
typedef enum {          /* Coarse wavelength range */
    LED_COLOR_VIOLET,   /* 425 nm */
    LED_COLOR_BLUE,     /* 475 nm */
    LED_COLOR_CYAN,     /* 500 nm */
    LED_COLOR_GREEN,    /* 535 nm */
    LED_COLOR_AMBER,    /* 590 nm */
    LED_COLOR_ORANGE,   /* 615 nm */
    LED_COLOR_RED,      /* 630 nm */
} LEDColor;

struct LEDState {
    /* Private */
    DeviceState parent_obj;
    /* Public */

    uint8_t intensity_percent;
    qemu_irq irq;

    /* Properties */
    char *description;
    char *color;
    /*
     * Determines whether a GPIO is using a positive (active-high)
     * logic (when used with GPIO, the intensity at reset is related
     * to the GPIO polarity).
     */
    bool gpio_active_high;
};
typedef struct LEDState LEDState;
DECLARE_INSTANCE_CHECKER(LEDState, LED, TYPE_LED)

/**
 * led_set_intensity: Set the intensity of a LED device
 * @s: the LED object
 * @intensity_percent: intensity as percentage in range 0 to 100.
 */
void led_set_intensity(LEDState *s, unsigned intensity_percent);

/**
 * led_get_intensity:
 * @s: the LED object
 *
 * Returns: The LED intensity as percentage in range 0 to 100.
 */
unsigned led_get_intensity(LEDState *s);

/**
 * led_set_state: Set the state of a LED device
 * @s: the LED object
 * @is_emitting: boolean indicating whether the LED is emitting
 *
 * This utility is meant for LED connected to GPIO.
 */
void led_set_state(LEDState *s, bool is_emitting);

/**
 * led_create_simple: Create and realize a LED device
 * @parentobj: the parent object
 * @gpio_polarity: GPIO polarity
 * @color: color of the LED
 * @description: description of the LED (optional)
 *
 * Create the device state structure, initialize it, and
 * drop the reference to it (the device is realized).
 *
 * Returns: The newly allocated and instantiated LED object.
 */
LEDState *led_create_simple(Object *parentobj,
                            GpioPolarity gpio_polarity,
                            LEDColor color,
                            const char *description);

#endif /* HW_MISC_LED_H */
