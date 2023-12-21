/*
 * QEMU single LED device
 *
 * Copyright (C) 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/misc/led.h"
#include "trace.h"

#define LED_INTENSITY_PERCENT_MAX   100

static const char * const led_color_name[] = {
    [LED_COLOR_VIOLET]  = "violet",
    [LED_COLOR_BLUE]    = "blue",
    [LED_COLOR_CYAN]    = "cyan",
    [LED_COLOR_GREEN]   = "green",
    [LED_COLOR_YELLOW]  = "yellow",
    [LED_COLOR_AMBER]   = "amber",
    [LED_COLOR_ORANGE]  = "orange",
    [LED_COLOR_RED]     = "red",
};

static bool led_color_name_is_valid(const char *color_name)
{
    for (size_t i = 0; i < ARRAY_SIZE(led_color_name); i++) {
        if (strcmp(color_name, led_color_name[i]) == 0) {
            return true;
        }
    }
    return false;
}

void led_set_intensity(LEDState *s, unsigned intensity_percent)
{
    if (intensity_percent > LED_INTENSITY_PERCENT_MAX) {
        intensity_percent = LED_INTENSITY_PERCENT_MAX;
    }
    trace_led_set_intensity(s->description, s->color, intensity_percent);
    if (intensity_percent != s->intensity_percent) {
        trace_led_change_intensity(s->description, s->color,
                                   s->intensity_percent, intensity_percent);
    }
    s->intensity_percent = intensity_percent;
}

unsigned led_get_intensity(LEDState *s)
{
    return s->intensity_percent;
}

void led_set_state(LEDState *s, bool is_emitting)
{
    led_set_intensity(s, is_emitting ? LED_INTENSITY_PERCENT_MAX : 0);
}

static void led_set_state_gpio_handler(void *opaque, int line, int new_state)
{
    LEDState *s = LED(opaque);

    assert(line == 0);
    led_set_state(s, !!new_state == s->gpio_active_high);
}

static void led_reset(DeviceState *dev)
{
    LEDState *s = LED(dev);

    led_set_state(s, s->gpio_active_high);
}

static const VMStateDescription vmstate_led = {
    .name = TYPE_LED,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(intensity_percent, LEDState),
        VMSTATE_END_OF_LIST()
    }
};

static void led_realize(DeviceState *dev, Error **errp)
{
    LEDState *s = LED(dev);

    if (s->color == NULL) {
        error_setg(errp, "property 'color' not specified");
        return;
    } else if (!led_color_name_is_valid(s->color)) {
        error_setg(errp, "property 'color' invalid or not supported");
        return;
    }
    if (s->description == NULL) {
        s->description = g_strdup("n/a");
    }

    qdev_init_gpio_in(DEVICE(s), led_set_state_gpio_handler, 1);
}

static Property led_properties[] = {
    DEFINE_PROP_STRING("color", LEDState, color),
    DEFINE_PROP_STRING("description", LEDState, description),
    DEFINE_PROP_BOOL("gpio-active-high", LEDState, gpio_active_high, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void led_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "LED";
    dc->vmsd = &vmstate_led;
    dc->reset = led_reset;
    dc->realize = led_realize;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    device_class_set_props(dc, led_properties);
}

static const TypeInfo led_info = {
    .name = TYPE_LED,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(LEDState),
    .class_init = led_class_init
};

static void led_register_types(void)
{
    type_register_static(&led_info);
}

type_init(led_register_types)

LEDState *led_create_simple(Object *parentobj,
                            GpioPolarity gpio_polarity,
                            LEDColor color,
                            const char *description)
{
    g_autofree char *name = NULL;
    DeviceState *dev;

    dev = qdev_new(TYPE_LED);
    qdev_prop_set_bit(dev, "gpio-active-high",
                      gpio_polarity == GPIO_POLARITY_ACTIVE_HIGH);
    qdev_prop_set_string(dev, "color", led_color_name[color]);
    if (!description) {
        static unsigned undescribed_led_id;
        name = g_strdup_printf("undescribed-led-#%u", undescribed_led_id++);
    } else {
        qdev_prop_set_string(dev, "description", description);
        name = g_ascii_strdown(description, -1);
        name = g_strdelimit(name, " #", '-');
    }
    object_property_add_child(parentobj, name, OBJECT(dev));
    qdev_realize_and_unref(dev, NULL, &error_fatal);

    return LED(dev);
}
