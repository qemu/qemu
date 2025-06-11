/*
 * PCA9552 I2C LED blinker
 *
 *     https://www.nxp.com/docs/en/application-note/AN264.pdf
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 * Copyright (c) 2020 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "hw/qdev-properties.h"
#include "hw/gpio/pca9552.h"
#include "hw/gpio/pca9552_regs.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "trace.h"
#include "qom/object.h"

struct PCA955xClass {
    /*< private >*/
    I2CSlaveClass parent_class;
    /*< public >*/

    uint8_t pin_count;
    uint8_t max_reg;
};
typedef struct PCA955xClass PCA955xClass;

DECLARE_CLASS_CHECKERS(PCA955xClass, PCA955X,
                       TYPE_PCA955X)
/*
 * Note:  The LED_ON and LED_OFF configuration values for the PCA955X
 *        chips are the reverse of the PCA953X family of chips.
 */
#define PCA9552_LED_ON   0x0
#define PCA9552_LED_OFF  0x1
#define PCA9552_LED_PWM0 0x2
#define PCA9552_LED_PWM1 0x3
#define PCA9552_PIN_LOW  0x0
#define PCA9552_PIN_HIZ  0x1

static const char *led_state[] = {"on", "off", "pwm0", "pwm1"};

static uint8_t pca955x_pin_get_config(PCA955xState *s, int pin)
{
    uint8_t reg   = PCA9552_LS0 + (pin / 4);
    uint8_t shift = (pin % 4) << 1;

    return extract32(s->regs[reg], shift, 2);
}

/* Return INPUT status (bit #N belongs to GPIO #N) */
static uint16_t pca955x_pins_get_status(PCA955xState *s)
{
    return (s->regs[PCA9552_INPUT1] << 8) | s->regs[PCA9552_INPUT0];
}

static void pca955x_display_pins_status(PCA955xState *s,
                                        uint16_t previous_pins_status)
{
    PCA955xClass *k = PCA955X_GET_CLASS(s);
    uint16_t pins_status, pins_changed;
    int i;

    pins_status = pca955x_pins_get_status(s);
    pins_changed = previous_pins_status ^ pins_status;
    if (!pins_changed) {
        return;
    }
    if (trace_event_get_state_backends(TRACE_PCA955X_GPIO_STATUS)) {
        char buf[PCA955X_PIN_COUNT_MAX + 1];

        for (i = 0; i < k->pin_count; i++) {
            if (extract32(pins_status, i, 1)) {
                buf[i] = '*';
            } else {
                buf[i] = '.';
            }
        }
        buf[i] = '\0';
        trace_pca955x_gpio_status(s->description, buf);
    }
    if (trace_event_get_state_backends(TRACE_PCA955X_GPIO_CHANGE)) {
        for (i = 0; i < k->pin_count; i++) {
            if (extract32(pins_changed, i, 1)) {
                unsigned new_state = extract32(pins_status, i, 1);

                /*
                 * We display the state using the PCA logic ("active-high").
                 * This is not the state of the LED, which signal might be
                 * wired "active-low" on the board.
                 */
                trace_pca955x_gpio_change(s->description, i,
                                          !new_state, new_state);
            }
        }
    }
}

static void pca955x_update_pin_input(PCA955xState *s)
{
    PCA955xClass *k = PCA955X_GET_CLASS(s);
    int i;

    for (i = 0; i < k->pin_count; i++) {
        uint8_t input_reg = PCA9552_INPUT0 + (i / 8);
        uint8_t bit_mask = 1 << (i % 8);
        uint8_t config = pca955x_pin_get_config(s, i);
        uint8_t old_value = s->regs[input_reg] & bit_mask;
        uint8_t new_value;

        switch (config) {
        case PCA9552_LED_ON:
            /* Pin is set to 0V to turn on LED */
            s->regs[input_reg] &= ~bit_mask;
            break;
        case PCA9552_LED_OFF:
            /*
             * Pin is set to Hi-Z to turn off LED and
             * pullup sets it to a logical 1 unless
             * external device drives it low.
             */
            if (s->ext_state[i] == PCA9552_PIN_LOW) {
                s->regs[input_reg] &= ~bit_mask;
            } else {
                s->regs[input_reg] |=  bit_mask;
            }
            break;
        case PCA9552_LED_PWM0:
        case PCA9552_LED_PWM1:
            /* TODO */
        default:
            break;
        }

        /* update irq state only if pin state changed */
        new_value = s->regs[input_reg] & bit_mask;
        if (new_value != old_value) {
            qemu_set_irq(s->gpio_out[i], !!new_value);
        }
    }
}

static uint8_t pca955x_read(PCA955xState *s, uint8_t reg)
{
    switch (reg) {
    case PCA9552_INPUT0:
    case PCA9552_INPUT1:
    case PCA9552_PSC0:
    case PCA9552_PWM0:
    case PCA9552_PSC1:
    case PCA9552_PWM1:
    case PCA9552_LS0:
    case PCA9552_LS1:
    case PCA9552_LS2:
    case PCA9552_LS3:
        return s->regs[reg];
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected read to register %d\n",
                      __func__, reg);
        return 0xFF;
    }
}

static void pca955x_write(PCA955xState *s, uint8_t reg, uint8_t data)
{
    uint16_t pins_status;

    switch (reg) {
    case PCA9552_PSC0:
    case PCA9552_PWM0:
    case PCA9552_PSC1:
    case PCA9552_PWM1:
        s->regs[reg] = data;
        break;

    case PCA9552_LS0:
    case PCA9552_LS1:
    case PCA9552_LS2:
    case PCA9552_LS3:
        pins_status = pca955x_pins_get_status(s);
        s->regs[reg] = data;
        pca955x_update_pin_input(s);
        pca955x_display_pins_status(s, pins_status);
        break;

    case PCA9552_INPUT0:
    case PCA9552_INPUT1:
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected write to register %d\n",
                      __func__, reg);
    }
}

/*
 * When Auto-Increment is on, the register address is incremented
 * after each byte is sent to or received by the device. The index
 * rollovers to 0 when the maximum register address is reached.
 */
static void pca955x_autoinc(PCA955xState *s)
{
    PCA955xClass *k = PCA955X_GET_CLASS(s);

    if (s->pointer != 0xFF && s->pointer & PCA9552_AUTOINC) {
        uint8_t reg = s->pointer & 0xf;

        reg = (reg + 1) % (k->max_reg + 1);
        s->pointer = reg | PCA9552_AUTOINC;
    }
}

static uint8_t pca955x_recv(I2CSlave *i2c)
{
    PCA955xState *s = PCA955X(i2c);
    uint8_t ret;

    ret = pca955x_read(s, s->pointer & 0xf);

    /*
     * From the Specs:
     *
     *     Important Note: When a Read sequence is initiated and the
     *     AI bit is set to Logic Level 1, the Read Sequence MUST
     *     start by a register different from 0.
     *
     * I don't know what should be done in this case, so throw an
     * error.
     */
    if (s->pointer == PCA9552_AUTOINC) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Autoincrement read starting with register 0\n",
                      __func__);
    }

    pca955x_autoinc(s);

    return ret;
}

static int pca955x_send(I2CSlave *i2c, uint8_t data)
{
    PCA955xState *s = PCA955X(i2c);

    /* First byte sent by is the register address */
    if (s->len == 0) {
        s->pointer = data;
        s->len++;
    } else {
        pca955x_write(s, s->pointer & 0xf, data);

        pca955x_autoinc(s);
    }

    return 0;
}

static int pca955x_event(I2CSlave *i2c, enum i2c_event event)
{
    PCA955xState *s = PCA955X(i2c);

    s->len = 0;
    return 0;
}

static void pca955x_get_led(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    PCA955xClass *k = PCA955X_GET_CLASS(obj);
    PCA955xState *s = PCA955X(obj);
    int led, rc, reg;
    uint8_t state;

    rc = sscanf(name, "led%2d", &led);
    if (rc != 1) {
        error_setg(errp, "%s: error reading %s", __func__, name);
        return;
    }
    if (led < 0 || led > k->pin_count) {
        error_setg(errp, "%s invalid led %s", __func__, name);
        return;
    }
    /*
     * Get the LSx register as the qom interface should expose the device
     * state, not the modeled 'input line' behaviour which would come from
     * reading the INPUTx reg
     */
    reg = PCA9552_LS0 + led / 4;
    state = (pca955x_read(s, reg) >> ((led % 4) * 2)) & 0x3;
    visit_type_str(v, name, (char **)&led_state[state], errp);
}

/*
 * Return an LED selector register value based on an existing one, with
 * the appropriate 2-bit state value set for the given LED number (0-3).
 */
static inline uint8_t pca955x_ledsel(uint8_t oldval, int led_num, int state)
{
        return (oldval & (~(0x3 << (led_num << 1)))) |
                ((state & 0x3) << (led_num << 1));
}

static void pca955x_set_led(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    PCA955xClass *k = PCA955X_GET_CLASS(obj);
    PCA955xState *s = PCA955X(obj);
    int led, rc, reg, val;
    uint8_t state;
    char *state_str;

    if (!visit_type_str(v, name, &state_str, errp)) {
        return;
    }
    rc = sscanf(name, "led%2d", &led);
    if (rc != 1) {
        error_setg(errp, "%s: error reading %s", __func__, name);
        return;
    }
    if (led < 0 || led > k->pin_count) {
        error_setg(errp, "%s invalid led %s", __func__, name);
        return;
    }

    for (state = 0; state < ARRAY_SIZE(led_state); state++) {
        if (!strcmp(state_str, led_state[state])) {
            break;
        }
    }
    if (state >= ARRAY_SIZE(led_state)) {
        error_setg(errp, "%s invalid led state %s", __func__, state_str);
        return;
    }

    reg = PCA9552_LS0 + led / 4;
    val = pca955x_read(s, reg);
    val = pca955x_ledsel(val, led % 4, state);
    pca955x_write(s, reg, val);
}

static const VMStateDescription pca9552_vmstate = {
    .name = "PCA9552",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(len, PCA955xState),
        VMSTATE_UINT8(pointer, PCA955xState),
        VMSTATE_UINT8_ARRAY(regs, PCA955xState, PCA955X_NR_REGS),
        VMSTATE_UINT8_ARRAY(ext_state, PCA955xState, PCA955X_PIN_COUNT_MAX),
        VMSTATE_I2C_SLAVE(i2c, PCA955xState),
        VMSTATE_END_OF_LIST()
    }
};

static void pca9552_reset(DeviceState *dev)
{
    PCA955xState *s = PCA955X(dev);

    s->regs[PCA9552_PSC0] = 0xFF;
    s->regs[PCA9552_PWM0] = 0x80;
    s->regs[PCA9552_PSC1] = 0xFF;
    s->regs[PCA9552_PWM1] = 0x80;
    s->regs[PCA9552_LS0] = 0x55; /* all OFF */
    s->regs[PCA9552_LS1] = 0x55;
    s->regs[PCA9552_LS2] = 0x55;
    s->regs[PCA9552_LS3] = 0x55;

    memset(s->ext_state, PCA9552_PIN_HIZ, PCA955X_PIN_COUNT_MAX);
    pca955x_update_pin_input(s);

    s->pointer = 0xFF;
    s->len = 0;
}

static void pca955x_initfn(Object *obj)
{
    PCA955xClass *k = PCA955X_GET_CLASS(obj);
    int led;

    assert(k->pin_count <= PCA955X_PIN_COUNT_MAX);
    for (led = 0; led < k->pin_count; led++) {
        char *name;

        name = g_strdup_printf("led%d", led);
        object_property_add(obj, name, "bool", pca955x_get_led, pca955x_set_led,
                            NULL, NULL);
        g_free(name);
    }
}

static void pca955x_set_ext_state(PCA955xState *s, int pin, int level)
{
    if (s->ext_state[pin] != level) {
        uint16_t pins_status = pca955x_pins_get_status(s);
        s->ext_state[pin] = level;
        pca955x_update_pin_input(s);
        pca955x_display_pins_status(s, pins_status);
    }
}

static void pca955x_gpio_in_handler(void *opaque, int pin, int level)
{

    PCA955xState *s = PCA955X(opaque);
    PCA955xClass *k = PCA955X_GET_CLASS(s);

    assert((pin >= 0) && (pin < k->pin_count));
    pca955x_set_ext_state(s, pin, level);
}

static void pca955x_realize(DeviceState *dev, Error **errp)
{
    PCA955xClass *k = PCA955X_GET_CLASS(dev);
    PCA955xState *s = PCA955X(dev);

    if (!s->description) {
        s->description = g_strdup("pca-unspecified");
    }

    qdev_init_gpio_out(dev, s->gpio_out, k->pin_count);
    qdev_init_gpio_in(dev, pca955x_gpio_in_handler, k->pin_count);
}

static const Property pca955x_properties[] = {
    DEFINE_PROP_STRING("description", PCA955xState, description),
};

static void pca955x_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = pca955x_event;
    k->recv = pca955x_recv;
    k->send = pca955x_send;
    dc->realize = pca955x_realize;
    device_class_set_props(dc, pca955x_properties);
}

static const TypeInfo pca955x_info = {
    .name          = TYPE_PCA955X,
    .parent        = TYPE_I2C_SLAVE,
    .instance_init = pca955x_initfn,
    .instance_size = sizeof(PCA955xState),
    .class_init    = pca955x_class_init,
    .class_size    = sizeof(PCA955xClass),
    .abstract      = true,
};

static void pca9552_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCA955xClass *pc = PCA955X_CLASS(oc);

    device_class_set_legacy_reset(dc, pca9552_reset);
    dc->vmsd = &pca9552_vmstate;
    pc->max_reg = PCA9552_LS3;
    pc->pin_count = 16;
}

static const TypeInfo pca9552_info = {
    .name          = TYPE_PCA9552,
    .parent        = TYPE_PCA955X,
    .class_init    = pca9552_class_init,
};

static void pca955x_register_types(void)
{
    type_register_static(&pca955x_info);
    type_register_static(&pca9552_info);
}

type_init(pca955x_register_types)
