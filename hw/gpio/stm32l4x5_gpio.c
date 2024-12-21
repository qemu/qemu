/*
 * STM32L4x5 GPIO (General Purpose Input/Ouput)
 *
 * Copyright (c) 2024 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2024 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * The reference used is the STMicroElectronics RM0351 Reference manual
 * for STM32L4x5 and STM32L4x6 advanced Arm ® -based 32-bit MCUs.
 * https://www.st.com/en/microcontrollers-microprocessors/stm32l4x5/documentation.html
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/gpio/stm32l4x5_gpio.h"
#include "hw/irq.h"
#include "hw/clock.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "trace.h"

#define GPIO_MODER 0x00
#define GPIO_OTYPER 0x04
#define GPIO_OSPEEDR 0x08
#define GPIO_PUPDR 0x0C
#define GPIO_IDR 0x10
#define GPIO_ODR 0x14
#define GPIO_BSRR 0x18
#define GPIO_LCKR 0x1C
#define GPIO_AFRL 0x20
#define GPIO_AFRH 0x24
#define GPIO_BRR 0x28
#define GPIO_ASCR 0x2C

/* 0b11111111_11111111_00000000_00000000 */
#define RESERVED_BITS_MASK 0xFFFF0000

static void update_gpio_idr(Stm32l4x5GpioState *s);

static bool is_pull_up(Stm32l4x5GpioState *s, unsigned pin)
{
    return extract32(s->pupdr, 2 * pin, 2) == 1;
}

static bool is_pull_down(Stm32l4x5GpioState *s, unsigned pin)
{
    return extract32(s->pupdr, 2 * pin, 2) == 2;
}

static bool is_output(Stm32l4x5GpioState *s, unsigned pin)
{
    return extract32(s->moder, 2 * pin, 2) == 1;
}

static bool is_open_drain(Stm32l4x5GpioState *s, unsigned pin)
{
    return extract32(s->otyper, pin, 1) == 1;
}

static bool is_push_pull(Stm32l4x5GpioState *s, unsigned pin)
{
    return extract32(s->otyper, pin, 1) == 0;
}

static void stm32l4x5_gpio_reset_hold(Object *obj, ResetType type)
{
    Stm32l4x5GpioState *s = STM32L4X5_GPIO(obj);

    s->moder = s->moder_reset;
    s->otyper = 0x00000000;
    s->ospeedr = s->ospeedr_reset;
    s->pupdr = s->pupdr_reset;
    s->idr = 0x00000000;
    s->odr = 0x00000000;
    s->lckr = 0x00000000;
    s->afrl = 0x00000000;
    s->afrh = 0x00000000;
    s->ascr = 0x00000000;

    s->disconnected_pins = 0xFFFF;
    s->pins_connected_high = 0x0000;
    update_gpio_idr(s);
}

static void stm32l4x5_gpio_set(void *opaque, int line, int level)
{
    Stm32l4x5GpioState *s = opaque;
    /*
     * The pin isn't set if line is configured in output mode
     * except if level is 0 and the output is open-drain.
     * This way there will be no short-circuit prone situations.
     */
    if (is_output(s, line) && !(is_open_drain(s, line) && (level == 0))) {
        qemu_log_mask(LOG_GUEST_ERROR, "Line %d can't be driven externally\n",
                      line);
        return;
    }

    s->disconnected_pins &= ~(1 << line);
    if (level) {
        s->pins_connected_high |= (1 << line);
    } else {
        s->pins_connected_high &= ~(1 << line);
    }
    trace_stm32l4x5_gpio_pins(s->name, s->disconnected_pins,
                              s->pins_connected_high);
    update_gpio_idr(s);
}


static void update_gpio_idr(Stm32l4x5GpioState *s)
{
    uint32_t new_idr_mask = 0;
    uint32_t new_idr = s->odr;
    uint32_t old_idr = s->idr;
    int new_pin_state, old_pin_state;

    for (int i = 0; i < GPIO_NUM_PINS; i++) {
        if (is_output(s, i)) {
            if (is_push_pull(s, i)) {
                new_idr_mask |= (1 << i);
            } else if (!(s->odr & (1 << i))) {
                /* open-drain ODR 0 */
                new_idr_mask |= (1 << i);
            /* open-drain ODR 1 */
            } else if (!(s->disconnected_pins & (1 << i)) &&
                       !(s->pins_connected_high & (1 << i))) {
                /* open-drain ODR 1 with pin connected low */
                new_idr_mask |= (1 << i);
                new_idr &= ~(1 << i);
            /* open-drain ODR 1 with unactive pin */
            } else if (is_pull_up(s, i)) {
                new_idr_mask |= (1 << i);
            } else if (is_pull_down(s, i)) {
                new_idr_mask |= (1 << i);
                new_idr &= ~(1 << i);
            }
            /*
             * The only case left is for open-drain ODR 1
             * with unactive pin without pull-up or pull-down :
             * the value is floating.
             */
        /* input or analog mode with connected pin */
        } else if (!(s->disconnected_pins & (1 << i))) {
            if (s->pins_connected_high & (1 << i)) {
                /* pin high */
                new_idr_mask |= (1 << i);
                new_idr |= (1 << i);
            } else {
                /* pin low */
                new_idr_mask |= (1 << i);
                new_idr &= ~(1 << i);
            }
        /* input or analog mode with disconnected pin */
        } else {
            if (is_pull_up(s, i)) {
                /* pull-up */
                new_idr_mask |= (1 << i);
                new_idr |= (1 << i);
            } else if (is_pull_down(s, i)) {
                /* pull-down */
                new_idr_mask |= (1 << i);
                new_idr &= ~(1 << i);
            }
            /*
             * The only case left is for a disconnected pin
             * without pull-up or pull-down :
             * the value is floating.
             */
        }
    }

    s->idr = (old_idr & ~new_idr_mask) | (new_idr & new_idr_mask);
    trace_stm32l4x5_gpio_update_idr(s->name, old_idr, s->idr);

    for (int i = 0; i < GPIO_NUM_PINS; i++) {
        if (new_idr_mask & (1 << i)) {
            new_pin_state = (new_idr & (1 << i)) > 0;
            old_pin_state = (old_idr & (1 << i)) > 0;
            if (new_pin_state > old_pin_state) {
                qemu_irq_raise(s->pin[i]);
            } else if (new_pin_state < old_pin_state) {
                qemu_irq_lower(s->pin[i]);
            }
        }
    }
}

/*
 * Return mask of pins that are both configured in output
 * mode and externally driven (except pins in open-drain
 * mode externally set to 0).
 */
static uint32_t get_gpio_pinmask_to_disconnect(Stm32l4x5GpioState *s)
{
    uint32_t pins_to_disconnect = 0;
    for (int i = 0; i < GPIO_NUM_PINS; i++) {
        /* for each connected pin in output mode */
        if (!(s->disconnected_pins & (1 << i)) && is_output(s, i)) {
            /* if either push-pull or high level */
            if (is_push_pull(s, i) || s->pins_connected_high & (1 << i)) {
                pins_to_disconnect |= (1 << i);
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Line %d can't be driven externally\n",
                              i);
            }
        }
    }
    return pins_to_disconnect;
}

/*
 * Set field `disconnected_pins` and call `update_gpio_idr()`
 */
static void disconnect_gpio_pins(Stm32l4x5GpioState *s, uint16_t lines)
{
    s->disconnected_pins |= lines;
    trace_stm32l4x5_gpio_pins(s->name, s->disconnected_pins,
                              s->pins_connected_high);
    update_gpio_idr(s);
}

static void disconnected_pins_set(Object *obj, Visitor *v,
    const char *name, void *opaque, Error **errp)
{
    Stm32l4x5GpioState *s = STM32L4X5_GPIO(obj);
    uint16_t value;
    if (!visit_type_uint16(v, name, &value, errp)) {
        return;
    }
    disconnect_gpio_pins(s, value);
}

static void disconnected_pins_get(Object *obj, Visitor *v,
    const char *name, void *opaque, Error **errp)
{
    visit_type_uint16(v, name, (uint16_t *)opaque, errp);
}

static void clock_freq_get(Object *obj, Visitor *v,
    const char *name, void *opaque, Error **errp)
{
    Stm32l4x5GpioState *s = STM32L4X5_GPIO(obj);
    uint32_t clock_freq_hz = clock_get_hz(s->clk);
    visit_type_uint32(v, name, &clock_freq_hz, errp);
}

static void stm32l4x5_gpio_write(void *opaque, hwaddr addr,
                                 uint64_t val64, unsigned int size)
{
    Stm32l4x5GpioState *s = opaque;

    uint32_t value = val64;
    trace_stm32l4x5_gpio_write(s->name, addr, val64);

    switch (addr) {
    case GPIO_MODER:
        s->moder = value;
        disconnect_gpio_pins(s, get_gpio_pinmask_to_disconnect(s));
        qemu_log_mask(LOG_UNIMP,
                      "%s: Analog and AF modes aren't supported\n\
                       Analog and AF mode behave like input mode\n",
                      __func__);
        return;
    case GPIO_OTYPER:
        s->otyper = value & ~RESERVED_BITS_MASK;
        disconnect_gpio_pins(s, get_gpio_pinmask_to_disconnect(s));
        return;
    case GPIO_OSPEEDR:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Changing I/O output speed isn't supported\n\
                       I/O speed is already maximal\n",
                      __func__);
        s->ospeedr = value;
        return;
    case GPIO_PUPDR:
        s->pupdr = value;
        update_gpio_idr(s);
        return;
    case GPIO_IDR:
        qemu_log_mask(LOG_UNIMP,
                      "%s: GPIO->IDR is read-only\n",
                      __func__);
        return;
    case GPIO_ODR:
        s->odr = value & ~RESERVED_BITS_MASK;
        update_gpio_idr(s);
        return;
    case GPIO_BSRR: {
        uint32_t bits_to_reset = (value & RESERVED_BITS_MASK) >> GPIO_NUM_PINS;
        uint32_t bits_to_set = value & ~RESERVED_BITS_MASK;
        /* If both BSx and BRx are set, BSx has priority.*/
        s->odr &= ~bits_to_reset;
        s->odr |= bits_to_set;
        update_gpio_idr(s);
        return;
    }
    case GPIO_LCKR:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Locking port bits configuration isn't supported\n",
                      __func__);
        s->lckr = value & ~RESERVED_BITS_MASK;
        return;
    case GPIO_AFRL:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Alternate functions aren't supported\n",
                      __func__);
        s->afrl = value;
        return;
    case GPIO_AFRH:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Alternate functions aren't supported\n",
                      __func__);
        s->afrh = value;
        return;
    case GPIO_BRR: {
        uint32_t bits_to_reset = value & ~RESERVED_BITS_MASK;
        s->odr &= ~bits_to_reset;
        update_gpio_idr(s);
        return;
    }
    case GPIO_ASCR:
        qemu_log_mask(LOG_UNIMP,
                      "%s: ADC function isn't supported\n",
                      __func__);
        s->ascr = value & ~RESERVED_BITS_MASK;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, addr);
    }
}

static uint64_t stm32l4x5_gpio_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    Stm32l4x5GpioState *s = opaque;

    trace_stm32l4x5_gpio_read(s->name, addr);

    switch (addr) {
    case GPIO_MODER:
        return s->moder;
    case GPIO_OTYPER:
        return s->otyper;
    case GPIO_OSPEEDR:
        return s->ospeedr;
    case GPIO_PUPDR:
        return s->pupdr;
    case GPIO_IDR:
        return s->idr;
    case GPIO_ODR:
        return s->odr;
    case GPIO_BSRR:
        return 0;
    case GPIO_LCKR:
        return s->lckr;
    case GPIO_AFRL:
        return s->afrl;
    case GPIO_AFRH:
        return s->afrh;
    case GPIO_BRR:
        return 0;
    case GPIO_ASCR:
        return s->ascr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }
}

static const MemoryRegionOps stm32l4x5_gpio_ops = {
    .read = stm32l4x5_gpio_read,
    .write = stm32l4x5_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void stm32l4x5_gpio_init(Object *obj)
{
    Stm32l4x5GpioState *s = STM32L4X5_GPIO(obj);

    memory_region_init_io(&s->mmio, obj, &stm32l4x5_gpio_ops, s,
                          TYPE_STM32L4X5_GPIO, 0x400);

    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    qdev_init_gpio_out(DEVICE(obj), s->pin, GPIO_NUM_PINS);
    qdev_init_gpio_in(DEVICE(obj), stm32l4x5_gpio_set, GPIO_NUM_PINS);

    s->clk = qdev_init_clock_in(DEVICE(s), "clk", NULL, s, 0);

    object_property_add(obj, "disconnected-pins", "uint16",
                        disconnected_pins_get, disconnected_pins_set,
                        NULL, &s->disconnected_pins);
    object_property_add(obj, "clock-freq-hz", "uint32",
                        clock_freq_get, NULL, NULL, NULL);
}

static void stm32l4x5_gpio_realize(DeviceState *dev, Error **errp)
{
    Stm32l4x5GpioState *s = STM32L4X5_GPIO(dev);
    if (!clock_has_source(s->clk)) {
        error_setg(errp, "GPIO: clk input must be connected");
        return;
    }
}

static const VMStateDescription vmstate_stm32l4x5_gpio = {
    .name = TYPE_STM32L4X5_GPIO,
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]){
        VMSTATE_UINT32(moder, Stm32l4x5GpioState),
        VMSTATE_UINT32(otyper, Stm32l4x5GpioState),
        VMSTATE_UINT32(ospeedr, Stm32l4x5GpioState),
        VMSTATE_UINT32(pupdr, Stm32l4x5GpioState),
        VMSTATE_UINT32(idr, Stm32l4x5GpioState),
        VMSTATE_UINT32(odr, Stm32l4x5GpioState),
        VMSTATE_UINT32(lckr, Stm32l4x5GpioState),
        VMSTATE_UINT32(afrl, Stm32l4x5GpioState),
        VMSTATE_UINT32(afrh, Stm32l4x5GpioState),
        VMSTATE_UINT32(ascr, Stm32l4x5GpioState),
        VMSTATE_UINT16(disconnected_pins, Stm32l4x5GpioState),
        VMSTATE_UINT16(pins_connected_high, Stm32l4x5GpioState),
        VMSTATE_CLOCK(clk, Stm32l4x5GpioState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property stm32l4x5_gpio_properties[] = {
    DEFINE_PROP_STRING("name", Stm32l4x5GpioState, name),
    DEFINE_PROP_UINT32("mode-reset", Stm32l4x5GpioState, moder_reset, 0),
    DEFINE_PROP_UINT32("ospeed-reset", Stm32l4x5GpioState, ospeedr_reset, 0),
    DEFINE_PROP_UINT32("pupd-reset", Stm32l4x5GpioState, pupdr_reset, 0),
};

static void stm32l4x5_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    device_class_set_props(dc, stm32l4x5_gpio_properties);
    dc->vmsd = &vmstate_stm32l4x5_gpio;
    dc->realize = stm32l4x5_gpio_realize;
    rc->phases.hold = stm32l4x5_gpio_reset_hold;
}

static const TypeInfo stm32l4x5_gpio_types[] = {
    {
        .name = TYPE_STM32L4X5_GPIO,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Stm32l4x5GpioState),
        .instance_init = stm32l4x5_gpio_init,
        .class_init = stm32l4x5_gpio_class_init,
    },
};

DEFINE_TYPES(stm32l4x5_gpio_types)
