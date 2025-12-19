/*
 * ASPEED Serial GPIO Controller
 *
 * Copyright 2025 Google LLC.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/gpio/aspeed_sgpio.h"

/*
 *  For each set of gpios there are three sensitivity registers that control
 *  the interrupt trigger mode.
 *
 *  | 2 | 1 | 0 | trigger mode
 *  -----------------------------
 *  | 0 | 0 | 0 | falling-edge
 *  | 0 | 0 | 1 | rising-edge
 *  | 0 | 1 | 0 | level-low
 *  | 0 | 1 | 1 | level-high
 *  | 1 | X | X | dual-edge
 */

/* GPIO Interrupt Triggers */
#define ASPEED_FALLING_EDGE 0
#define ASPEED_RISING_EDGE  1
#define ASPEED_LEVEL_LOW    2
#define ASPEED_LEVEL_HIGH   3
#define ASPEED_DUAL_EDGE    4

static void aspeed_clear_irq(AspeedSGPIOState *s, int idx)
{
    uint32_t reg_index = idx / 32;
    uint32_t bit_index = idx % 32;
    uint32_t pending = extract32(s->int_regs[reg_index], bit_index, 1);

    assert(s->pending >= pending);

    /* No change to s->pending if pending is 0 */
    s->pending -= pending;

    /*
     * The write acknowledged the interrupt regardless of whether it
     * was pending or not. The post-condition is that it mustn't be
     * pending. Unconditionally clear the status bit.
     */
    s->int_regs[reg_index] = deposit32(s->int_regs[reg_index], bit_index, 1, 0);
}

static void aspeed_evaluate_irq(AspeedSGPIOState *s, int sgpio_prev_high,
                                int sgpio_curr_high, int idx)
{
    uint32_t ctrl = s->ctrl_regs[idx];
    uint32_t falling_edge = 0, rising_edge = 0;
    uint32_t int_trigger = SHARED_FIELD_EX32(ctrl, SGPIO_INT_TYPE);
    uint32_t int_enabled = SHARED_FIELD_EX32(ctrl, SGPIO_INT_EN);
    uint32_t reg_index = idx / 32;
    uint32_t bit_index = idx % 32;

    if (!int_enabled) {
        return;
    }

    /* Detect edges */
    if (sgpio_curr_high && !sgpio_prev_high) {
        rising_edge = 1;
    } else if (!sgpio_curr_high && sgpio_prev_high) {
        falling_edge = 1;
    }

    if (((int_trigger == ASPEED_FALLING_EDGE)  && falling_edge)   ||
        ((int_trigger == ASPEED_RISING_EDGE)  && rising_edge)     ||
        ((int_trigger == ASPEED_LEVEL_LOW)  && !sgpio_curr_high)  ||
        ((int_trigger == ASPEED_LEVEL_HIGH)  && sgpio_curr_high)  ||
        ((int_trigger >= ASPEED_DUAL_EDGE)  && (rising_edge || falling_edge)))
    {
        s->int_regs[reg_index] = deposit32(s->int_regs[reg_index],
                                              bit_index, 1, 1);
        /* Trigger the VIC IRQ */
        s->pending++;
    }
}

static void aspeed_sgpio_update(AspeedSGPIOState *s, uint32_t idx,
                                uint32_t value)
{
    uint32_t old = s->ctrl_regs[idx];
    uint32_t new = value;
    uint32_t diff = (old ^ new);
    if (diff) {
        /* If the interrupt clear bit is set */
        if (SHARED_FIELD_EX32(new, SGPIO_INT_STATUS)) {
            aspeed_clear_irq(s, idx);
            /* Clear the interrupt clear bit */
            new &= ~SGPIO_INT_STATUS_MASK;
        }

        /* Update the control register. */
        s->ctrl_regs[idx] = new;

        /* If the output value is changed */
        if (SHARED_FIELD_EX32(diff, SGPIO_SERIAL_OUT_VAL)) {
            /* ...trigger the line-state IRQ */
            qemu_set_irq(s->sgpios[idx], 1);
        }

        /* If the input value is changed */
        if (SHARED_FIELD_EX32(diff, SGPIO_SERIAL_IN_VAL)) {
            aspeed_evaluate_irq(s,
                            SHARED_FIELD_EX32(old, SGPIO_SERIAL_IN_VAL),
                            SHARED_FIELD_EX32(new, SGPIO_SERIAL_IN_VAL),
                            idx);
        }
    }
    qemu_set_irq(s->irq, !!(s->pending));
}

static uint64_t aspeed_sgpio_2700_read_int_status_reg(AspeedSGPIOState *s,
                                                      uint32_t reg)
{
    uint32_t idx = reg - R_SGPIO_INT_STATUS_0;
    if (idx >= ASPEED_SGPIO_MAX_INT) {
        qemu_log_mask(LOG_GUEST_ERROR,
                         "%s: interrupt status index: %d, out of bounds\n",
                         __func__, idx);
        return 0;
    }
    return s->int_regs[idx];
}

static uint64_t aspeed_sgpio_2700_read_control_reg(AspeedSGPIOState *s,
                                uint32_t reg)
{
    AspeedSGPIOClass *agc = ASPEED_SGPIO_GET_CLASS(s);
    uint32_t idx = reg - R_SGPIO_0_CONTROL;
    if (idx >= agc->nr_sgpio_pin_pairs) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: pin index: %d, out of bounds\n",
                      __func__, idx);
        return 0;
    }
    return s->ctrl_regs[idx];
}

static void aspeed_sgpio_2700_write_control_reg(AspeedSGPIOState *s,
                                uint32_t reg, uint64_t data)
{
    AspeedSGPIOClass *agc = ASPEED_SGPIO_GET_CLASS(s);
    uint32_t idx = reg - R_SGPIO_0_CONTROL;
    if (idx >= agc->nr_sgpio_pin_pairs) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: pin index: %d, out of bounds\n",
                      __func__, idx);
        return;
    }
    aspeed_sgpio_update(s, idx, data);
}

static uint64_t aspeed_sgpio_2700_read(void *opaque, hwaddr offset,
                                uint32_t size)
{
    AspeedSGPIOState *s = ASPEED_SGPIO(opaque);
    uint64_t value = 0;
    uint64_t reg;

    reg = offset >> 2;

    switch (reg) {
    case R_SGPIO_INT_STATUS_0 ... R_SGPIO_INT_STATUS_7:
        value = aspeed_sgpio_2700_read_int_status_reg(s, reg);
        break;
    case R_SGPIO_0_CONTROL ... R_SGPIO_255_CONTROL:
        value = aspeed_sgpio_2700_read_control_reg(s, reg);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no getter for offset 0x%"
                      HWADDR_PRIx"\n", __func__, offset);
        return 0;
    }

    return value;
}

static void aspeed_sgpio_2700_write(void *opaque, hwaddr offset, uint64_t data,
                                uint32_t size)
{
    AspeedSGPIOState *s = ASPEED_SGPIO(opaque);
    uint64_t reg;

    reg = offset >> 2;

    switch (reg) {
    case R_SGPIO_0_CONTROL ... R_SGPIO_255_CONTROL:
        aspeed_sgpio_2700_write_control_reg(s, reg, data);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no setter for offset 0x%"
                      HWADDR_PRIx"\n", __func__, offset);
        return;
    }
}

static bool aspeed_sgpio_get_pin_level(AspeedSGPIOState *s, int pin)
{
    uint32_t value = s->ctrl_regs[pin >> 1];
    bool is_input = !(pin % 2);
    uint32_t bit_mask = 0;

    if (is_input) {
        bit_mask = SGPIO_SERIAL_IN_VAL_MASK;
    } else {
        bit_mask = SGPIO_SERIAL_OUT_VAL_MASK;
    }

    return value & bit_mask;
}

static void aspeed_sgpio_set_pin_level(AspeedSGPIOState *s, int pin, bool level)
{
    uint32_t value = s->ctrl_regs[pin >> 1];
    bool is_input = !(pin % 2);
    uint32_t bit_mask = 0;

    if (is_input) {
        bit_mask = SGPIO_SERIAL_IN_VAL_MASK;
    } else {
        bit_mask = SGPIO_SERIAL_OUT_VAL_MASK;
    }

    if (level) {
        value |= bit_mask;
    } else {
        value &= ~bit_mask;
    }
    aspeed_sgpio_update(s, pin >> 1, value);
}

static void aspeed_sgpio_get_pin(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    bool level = true;
    int pin = 0xfff;
    AspeedSGPIOState *s = ASPEED_SGPIO(obj);

    if (sscanf(name, "sgpio%03d", &pin) != 1) {
        error_setg(errp, "%s: error reading %s", __func__, name);
        return;
    }
    level = aspeed_sgpio_get_pin_level(s, pin);
    visit_type_bool(v, name, &level, errp);
}

static void aspeed_sgpio_set_pin(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    bool level;
    int pin = 0xfff;
    AspeedSGPIOState *s = ASPEED_SGPIO(obj);

    if (!visit_type_bool(v, name, &level, errp)) {
        return;
    }
    if (sscanf(name, "sgpio%03d", &pin) != 1) {
        error_setg(errp, "%s: error reading %s", __func__, name);
        return;
    }
    aspeed_sgpio_set_pin_level(s, pin, level);
}

static const MemoryRegionOps aspeed_sgpio_2700_ops = {
    .read       = aspeed_sgpio_2700_read,
    .write      = aspeed_sgpio_2700_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void aspeed_sgpio_realize(DeviceState *dev, Error **errp)
{
    AspeedSGPIOState *s = ASPEED_SGPIO(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedSGPIOClass *agc = ASPEED_SGPIO_GET_CLASS(s);

    /* Interrupt parent line */
    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->iomem, OBJECT(s), agc->reg_ops, s,
                          TYPE_ASPEED_SGPIO, agc->mem_size);

    sysbus_init_mmio(sbd, &s->iomem);
}

static void aspeed_sgpio_init(Object *obj)
{
    for (int i = 0; i < ASPEED_SGPIO_MAX_PIN_PAIR * 2; i++) {
        g_autofree char *name = g_strdup_printf("sgpio%03d", i);
        object_property_add(obj, name, "bool", aspeed_sgpio_get_pin,
                            aspeed_sgpio_set_pin, NULL, NULL);
    }
}

static void aspeed_sgpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_sgpio_realize;
    dc->desc = "Aspeed SGPIO Controller";
}

static void aspeed_sgpio_2700_class_init(ObjectClass *klass, const void *data)
{
    AspeedSGPIOClass *agc = ASPEED_SGPIO_CLASS(klass);
    agc->nr_sgpio_pin_pairs = ASPEED_SGPIO_MAX_PIN_PAIR;
    agc->mem_size = 0x1000;
    agc->reg_ops = &aspeed_sgpio_2700_ops;
}

static const TypeInfo aspeed_sgpio_info = {
    .name           = TYPE_ASPEED_SGPIO,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(AspeedSGPIOState),
    .class_size     = sizeof(AspeedSGPIOClass),
    .class_init     = aspeed_sgpio_class_init,
    .abstract       = true,
};

static const TypeInfo aspeed_sgpio_ast2700_info = {
    .name           = TYPE_ASPEED_SGPIO "-ast2700",
    .parent         = TYPE_ASPEED_SGPIO,
    .class_init     = aspeed_sgpio_2700_class_init,
    .instance_init  = aspeed_sgpio_init,
};

static void aspeed_sgpio_register_types(void)
{
    type_register_static(&aspeed_sgpio_info);
    type_register_static(&aspeed_sgpio_ast2700_info);
}

type_init(aspeed_sgpio_register_types);
