/*
 * PCA9554 I/O port
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "hw/qdev-properties.h"
#include "hw/misc/pca9554.h"
#include "hw/misc/pca9554_regs.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "trace.h"
#include "qom/object.h"

struct PCA9554Class {
    /*< private >*/
    I2CSlaveClass parent_class;
    /*< public >*/
};
typedef struct PCA9554Class PCA9554Class;

DECLARE_CLASS_CHECKERS(PCA9554Class, PCA9554,
                       TYPE_PCA9554)

#define PCA9554_PIN_LOW  0x0
#define PCA9554_PIN_HIZ  0x1

static const char *pin_state[] = {"low", "high"};

static void pca9554_update_pin_input(PCA9554State *s)
{
    int i;
    uint8_t config = s->regs[PCA9554_CONFIG];
    uint8_t output = s->regs[PCA9554_OUTPUT];
    uint8_t internal_state = config | output;

    for (i = 0; i < PCA9554_PIN_COUNT; i++) {
        uint8_t bit_mask = 1 << i;
        uint8_t internal_pin_state = (internal_state >> i) & 0x1;
        uint8_t old_value = s->regs[PCA9554_INPUT] & bit_mask;
        uint8_t new_value;

        switch (internal_pin_state) {
        case PCA9554_PIN_LOW:
            s->regs[PCA9554_INPUT] &= ~bit_mask;
            break;
        case PCA9554_PIN_HIZ:
            /*
             * pullup sets it to a logical 1 unless
             * external device drives it low.
             */
            if (s->ext_state[i] == PCA9554_PIN_LOW) {
                s->regs[PCA9554_INPUT] &= ~bit_mask;
            } else {
                s->regs[PCA9554_INPUT] |=  bit_mask;
            }
            break;
        default:
            break;
        }

        /* update irq state only if pin state changed */
        new_value = s->regs[PCA9554_INPUT] & bit_mask;
        if (new_value != old_value) {
            if (new_value) {
                /* changed from 0 to 1 */
                qemu_set_irq(s->gpio_out[i], 1);
            } else {
                /* changed from 1 to 0 */
                qemu_set_irq(s->gpio_out[i], 0);
            }
        }
    }
}

static uint8_t pca9554_read(PCA9554State *s, uint8_t reg)
{
    switch (reg) {
    case PCA9554_INPUT:
        return s->regs[PCA9554_INPUT] ^ s->regs[PCA9554_POLARITY];
    case PCA9554_OUTPUT:
    case PCA9554_POLARITY:
    case PCA9554_CONFIG:
        return s->regs[reg];
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected read to register %d\n",
                      __func__, reg);
        return 0xFF;
    }
}

static void pca9554_write(PCA9554State *s, uint8_t reg, uint8_t data)
{
    switch (reg) {
    case PCA9554_OUTPUT:
    case PCA9554_CONFIG:
        s->regs[reg] = data;
        pca9554_update_pin_input(s);
        break;
    case PCA9554_POLARITY:
        s->regs[reg] = data;
        break;
    case PCA9554_INPUT:
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected write to register %d\n",
                      __func__, reg);
    }
}

static uint8_t pca9554_recv(I2CSlave *i2c)
{
    PCA9554State *s = PCA9554(i2c);
    uint8_t ret;

    ret = pca9554_read(s, s->pointer & 0x3);

    return ret;
}

static int pca9554_send(I2CSlave *i2c, uint8_t data)
{
    PCA9554State *s = PCA9554(i2c);

    /* First byte sent by is the register address */
    if (s->len == 0) {
        s->pointer = data;
        s->len++;
    } else {
        pca9554_write(s, s->pointer & 0x3, data);
    }

    return 0;
}

static int pca9554_event(I2CSlave *i2c, enum i2c_event event)
{
    PCA9554State *s = PCA9554(i2c);

    s->len = 0;
    return 0;
}

static void pca9554_get_pin(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    PCA9554State *s = PCA9554(obj);
    int pin, rc;
    uint8_t state;

    rc = sscanf(name, "pin%2d", &pin);
    if (rc != 1) {
        error_setg(errp, "%s: error reading %s", __func__, name);
        return;
    }
    if (pin < 0 || pin > PCA9554_PIN_COUNT) {
        error_setg(errp, "%s invalid pin %s", __func__, name);
        return;
    }

    state = pca9554_read(s, PCA9554_CONFIG);
    state |= pca9554_read(s, PCA9554_OUTPUT);
    state = (state >> pin) & 0x1;
    visit_type_str(v, name, (char **)&pin_state[state], errp);
}

static void pca9554_set_pin(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    PCA9554State *s = PCA9554(obj);
    int pin, rc, val;
    uint8_t state, mask;
    char *state_str;

    if (!visit_type_str(v, name, &state_str, errp)) {
        return;
    }
    rc = sscanf(name, "pin%2d", &pin);
    if (rc != 1) {
        error_setg(errp, "%s: error reading %s", __func__, name);
        return;
    }
    if (pin < 0 || pin > PCA9554_PIN_COUNT) {
        error_setg(errp, "%s invalid pin %s", __func__, name);
        return;
    }

    for (state = 0; state < ARRAY_SIZE(pin_state); state++) {
        if (!strcmp(state_str, pin_state[state])) {
            break;
        }
    }
    if (state >= ARRAY_SIZE(pin_state)) {
        error_setg(errp, "%s invalid pin state %s", __func__, state_str);
        return;
    }

    /* First, modify the output register bit */
    val = pca9554_read(s, PCA9554_OUTPUT);
    mask = 0x1 << pin;
    if (state == PCA9554_PIN_LOW) {
        val &= ~(mask);
    } else {
        val |= mask;
    }
    pca9554_write(s, PCA9554_OUTPUT, val);

    /* Then, clear the config register bit for output mode */
    val = pca9554_read(s, PCA9554_CONFIG);
    val &= ~mask;
    pca9554_write(s, PCA9554_CONFIG, val);
}

static const VMStateDescription pca9554_vmstate = {
    .name = "PCA9554",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(len, PCA9554State),
        VMSTATE_UINT8(pointer, PCA9554State),
        VMSTATE_UINT8_ARRAY(regs, PCA9554State, PCA9554_NR_REGS),
        VMSTATE_UINT8_ARRAY(ext_state, PCA9554State, PCA9554_PIN_COUNT),
        VMSTATE_I2C_SLAVE(i2c, PCA9554State),
        VMSTATE_END_OF_LIST()
    }
};

static void pca9554_reset(DeviceState *dev)
{
    PCA9554State *s = PCA9554(dev);

    s->regs[PCA9554_INPUT] = 0xFF;
    s->regs[PCA9554_OUTPUT] = 0xFF;
    s->regs[PCA9554_POLARITY] = 0x0; /* No pins are inverted */
    s->regs[PCA9554_CONFIG] = 0xFF; /* All pins are inputs */

    memset(s->ext_state, PCA9554_PIN_HIZ, PCA9554_PIN_COUNT);
    pca9554_update_pin_input(s);

    s->pointer = 0x0;
    s->len = 0;
}

static void pca9554_initfn(Object *obj)
{
    int pin;

    for (pin = 0; pin < PCA9554_PIN_COUNT; pin++) {
        char *name;

        name = g_strdup_printf("pin%d", pin);
        object_property_add(obj, name, "bool", pca9554_get_pin, pca9554_set_pin,
                            NULL, NULL);
        g_free(name);
    }
}

static void pca9554_set_ext_state(PCA9554State *s, int pin, int level)
{
    if (s->ext_state[pin] != level) {
        s->ext_state[pin] = level;
        pca9554_update_pin_input(s);
    }
}

static void pca9554_gpio_in_handler(void *opaque, int pin, int level)
{

    PCA9554State *s = PCA9554(opaque);

    assert((pin >= 0) && (pin < PCA9554_PIN_COUNT));
    pca9554_set_ext_state(s, pin, level);
}

static void pca9554_realize(DeviceState *dev, Error **errp)
{
    PCA9554State *s = PCA9554(dev);

    if (!s->description) {
        s->description = g_strdup("pca9554");
    }

    qdev_init_gpio_out(dev, s->gpio_out, PCA9554_PIN_COUNT);
    qdev_init_gpio_in(dev, pca9554_gpio_in_handler, PCA9554_PIN_COUNT);
}

static Property pca9554_properties[] = {
    DEFINE_PROP_STRING("description", PCA9554State, description),
    DEFINE_PROP_END_OF_LIST(),
};

static void pca9554_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = pca9554_event;
    k->recv = pca9554_recv;
    k->send = pca9554_send;
    dc->realize = pca9554_realize;
    dc->reset = pca9554_reset;
    dc->vmsd = &pca9554_vmstate;
    device_class_set_props(dc, pca9554_properties);
}

static const TypeInfo pca9554_info = {
    .name          = TYPE_PCA9554,
    .parent        = TYPE_I2C_SLAVE,
    .instance_init = pca9554_initfn,
    .instance_size = sizeof(PCA9554State),
    .class_init    = pca9554_class_init,
    .class_size    = sizeof(PCA9554Class),
    .abstract      = false,
};

static void pca9554_register_types(void)
{
    type_register_static(&pca9554_info);
}

type_init(pca9554_register_types)
