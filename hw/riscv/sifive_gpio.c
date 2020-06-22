/*
 * SiFive System-on-Chip general purpose input/output register definition
 *
 * Copyright 2019 AdaCore
 *
 * Base on nrf51_gpio.c:
 *
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/sifive_gpio.h"
#include "migration/vmstate.h"
#include "trace.h"

static void update_output_irq(SIFIVEGPIOState *s)
{
    uint32_t pending;
    uint32_t pin;

    pending = s->high_ip & s->high_ie;
    pending |= s->low_ip & s->low_ie;
    pending |= s->rise_ip & s->rise_ie;
    pending |= s->fall_ip & s->fall_ie;

    for (int i = 0; i < s->ngpio; i++) {
        pin = 1 << i;
        qemu_set_irq(s->irq[i], (pending & pin) != 0);
        trace_sifive_gpio_update_output_irq(i, (pending & pin) != 0);
    }
}

static void update_state(SIFIVEGPIOState *s)
{
    size_t i;
    bool prev_ival, in, in_mask, port, out_xor, pull, output_en, input_en,
        rise_ip, fall_ip, low_ip, high_ip, oval, actual_value, ival;

    for (i = 0; i < s->ngpio; i++) {

        prev_ival = extract32(s->value, i, 1);
        in        = extract32(s->in, i, 1);
        in_mask   = extract32(s->in_mask, i, 1);
        port      = extract32(s->port, i, 1);
        out_xor   = extract32(s->out_xor, i, 1);
        pull      = extract32(s->pue, i, 1);
        output_en = extract32(s->output_en, i, 1);
        input_en  = extract32(s->input_en, i, 1);
        rise_ip   = extract32(s->rise_ip, i, 1);
        fall_ip   = extract32(s->fall_ip, i, 1);
        low_ip    = extract32(s->low_ip, i, 1);
        high_ip   = extract32(s->high_ip, i, 1);

        /* Output value (IOF not supported) */
        oval = output_en && (port ^ out_xor);

        /* Pin both driven externally and internally */
        if (output_en && in_mask) {
            qemu_log_mask(LOG_GUEST_ERROR, "GPIO pin %zu short circuited\n", i);
        }

        if (in_mask) {
            /* The pin is driven by external device */
            actual_value = in;
        } else if (output_en) {
            /* The pin is driven by internal circuit */
            actual_value = oval;
        } else {
            /* Floating? Apply pull-up resistor */
            actual_value = pull;
        }

        if (output_en) {
            qemu_set_irq(s->output[i], actual_value);
        }

        /* Input value */
        ival = input_en && actual_value;

        /* Interrupts */
        high_ip = high_ip || ival;
        s->high_ip = deposit32(s->high_ip, i, 1, high_ip);

        low_ip = low_ip || !ival;
        s->low_ip = deposit32(s->low_ip,  i, 1, low_ip);

        rise_ip = rise_ip || (ival && !prev_ival);
        s->rise_ip = deposit32(s->rise_ip, i, 1, rise_ip);

        fall_ip = fall_ip || (!ival && prev_ival);
        s->fall_ip = deposit32(s->fall_ip, i, 1, fall_ip);

        /* Update value */
        s->value = deposit32(s->value, i, 1, ival);
    }
    update_output_irq(s);
}

static uint64_t sifive_gpio_read(void *opaque, hwaddr offset, unsigned int size)
{
    SIFIVEGPIOState *s = SIFIVE_GPIO(opaque);
    uint64_t r = 0;

    switch (offset) {
    case SIFIVE_GPIO_REG_VALUE:
        r = s->value;
        break;

    case SIFIVE_GPIO_REG_INPUT_EN:
        r = s->input_en;
        break;

    case SIFIVE_GPIO_REG_OUTPUT_EN:
        r = s->output_en;
        break;

    case SIFIVE_GPIO_REG_PORT:
        r = s->port;
        break;

    case SIFIVE_GPIO_REG_PUE:
        r = s->pue;
        break;

    case SIFIVE_GPIO_REG_DS:
        r = s->ds;
        break;

    case SIFIVE_GPIO_REG_RISE_IE:
        r = s->rise_ie;
        break;

    case SIFIVE_GPIO_REG_RISE_IP:
        r = s->rise_ip;
        break;

    case SIFIVE_GPIO_REG_FALL_IE:
        r = s->fall_ie;
        break;

    case SIFIVE_GPIO_REG_FALL_IP:
        r = s->fall_ip;
        break;

    case SIFIVE_GPIO_REG_HIGH_IE:
        r = s->high_ie;
        break;

    case SIFIVE_GPIO_REG_HIGH_IP:
        r = s->high_ip;
        break;

    case SIFIVE_GPIO_REG_LOW_IE:
        r = s->low_ie;
        break;

    case SIFIVE_GPIO_REG_LOW_IP:
        r = s->low_ip;
        break;

    case SIFIVE_GPIO_REG_IOF_EN:
        r = s->iof_en;
        break;

    case SIFIVE_GPIO_REG_IOF_SEL:
        r = s->iof_sel;
        break;

    case SIFIVE_GPIO_REG_OUT_XOR:
        r = s->out_xor;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    trace_sifive_gpio_read(offset, r);

    return r;
}

static void sifive_gpio_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned int size)
{
    SIFIVEGPIOState *s = SIFIVE_GPIO(opaque);

    trace_sifive_gpio_write(offset, value);

    switch (offset) {

    case SIFIVE_GPIO_REG_INPUT_EN:
        s->input_en = value;
        break;

    case SIFIVE_GPIO_REG_OUTPUT_EN:
        s->output_en = value;
        break;

    case SIFIVE_GPIO_REG_PORT:
        s->port = value;
        break;

    case SIFIVE_GPIO_REG_PUE:
        s->pue = value;
        break;

    case SIFIVE_GPIO_REG_DS:
        s->ds = value;
        break;

    case SIFIVE_GPIO_REG_RISE_IE:
        s->rise_ie = value;
        break;

    case SIFIVE_GPIO_REG_RISE_IP:
         /* Write 1 to clear */
        s->rise_ip &= ~value;
        break;

    case SIFIVE_GPIO_REG_FALL_IE:
        s->fall_ie = value;
        break;

    case SIFIVE_GPIO_REG_FALL_IP:
         /* Write 1 to clear */
        s->fall_ip &= ~value;
        break;

    case SIFIVE_GPIO_REG_HIGH_IE:
        s->high_ie = value;
        break;

    case SIFIVE_GPIO_REG_HIGH_IP:
         /* Write 1 to clear */
        s->high_ip &= ~value;
        break;

    case SIFIVE_GPIO_REG_LOW_IE:
        s->low_ie = value;
        break;

    case SIFIVE_GPIO_REG_LOW_IP:
         /* Write 1 to clear */
        s->low_ip &= ~value;
        break;

    case SIFIVE_GPIO_REG_IOF_EN:
        s->iof_en = value;
        break;

    case SIFIVE_GPIO_REG_IOF_SEL:
        s->iof_sel = value;
        break;

    case SIFIVE_GPIO_REG_OUT_XOR:
        s->out_xor = value;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    update_state(s);
}

static const MemoryRegionOps gpio_ops = {
    .read =  sifive_gpio_read,
    .write = sifive_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void sifive_gpio_set(void *opaque, int line, int value)
{
    SIFIVEGPIOState *s = SIFIVE_GPIO(opaque);

    trace_sifive_gpio_set(line, value);

    assert(line >= 0 && line < SIFIVE_GPIO_PINS);

    s->in_mask = deposit32(s->in_mask, line, 1, value >= 0);
    if (value >= 0) {
        s->in = deposit32(s->in, line, 1, value != 0);
    }

    update_state(s);
}

static void sifive_gpio_reset(DeviceState *dev)
{
    SIFIVEGPIOState *s = SIFIVE_GPIO(dev);

    s->value = 0;
    s->input_en = 0;
    s->output_en = 0;
    s->port = 0;
    s->pue = 0;
    s->ds = 0;
    s->rise_ie = 0;
    s->rise_ip = 0;
    s->fall_ie = 0;
    s->fall_ip = 0;
    s->high_ie = 0;
    s->high_ip = 0;
    s->low_ie = 0;
    s->low_ip = 0;
    s->iof_en = 0;
    s->iof_sel = 0;
    s->out_xor = 0;
    s->in = 0;
    s->in_mask = 0;
}

static const VMStateDescription vmstate_sifive_gpio = {
    .name = TYPE_SIFIVE_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(value,     SIFIVEGPIOState),
        VMSTATE_UINT32(input_en,  SIFIVEGPIOState),
        VMSTATE_UINT32(output_en, SIFIVEGPIOState),
        VMSTATE_UINT32(port,      SIFIVEGPIOState),
        VMSTATE_UINT32(pue,       SIFIVEGPIOState),
        VMSTATE_UINT32(rise_ie,   SIFIVEGPIOState),
        VMSTATE_UINT32(rise_ip,   SIFIVEGPIOState),
        VMSTATE_UINT32(fall_ie,   SIFIVEGPIOState),
        VMSTATE_UINT32(fall_ip,   SIFIVEGPIOState),
        VMSTATE_UINT32(high_ie,   SIFIVEGPIOState),
        VMSTATE_UINT32(high_ip,   SIFIVEGPIOState),
        VMSTATE_UINT32(low_ie,    SIFIVEGPIOState),
        VMSTATE_UINT32(low_ip,    SIFIVEGPIOState),
        VMSTATE_UINT32(iof_en,    SIFIVEGPIOState),
        VMSTATE_UINT32(iof_sel,   SIFIVEGPIOState),
        VMSTATE_UINT32(out_xor,   SIFIVEGPIOState),
        VMSTATE_UINT32(in,        SIFIVEGPIOState),
        VMSTATE_UINT32(in_mask,   SIFIVEGPIOState),
        VMSTATE_END_OF_LIST()
    }
};

static Property sifive_gpio_properties[] = {
    DEFINE_PROP_UINT32("ngpio", SIFIVEGPIOState, ngpio, SIFIVE_GPIO_PINS),
    DEFINE_PROP_END_OF_LIST(),
};

static void sifive_gpio_realize(DeviceState *dev, Error **errp)
{
    SIFIVEGPIOState *s = SIFIVE_GPIO(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &gpio_ops, s,
            TYPE_SIFIVE_GPIO, SIFIVE_GPIO_SIZE);

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);

    for (int i = 0; i < s->ngpio; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq[i]);
    }

    qdev_init_gpio_in(DEVICE(s), sifive_gpio_set, s->ngpio);
    qdev_init_gpio_out(DEVICE(s), s->output, s->ngpio);
}

static void sifive_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, sifive_gpio_properties);
    dc->vmsd = &vmstate_sifive_gpio;
    dc->realize = sifive_gpio_realize;
    dc->reset = sifive_gpio_reset;
    dc->desc = "SiFive GPIO";
}

static const TypeInfo sifive_gpio_info = {
    .name = TYPE_SIFIVE_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SIFIVEGPIOState),
    .class_init = sifive_gpio_class_init
};

static void sifive_gpio_register_types(void)
{
    type_register_static(&sifive_gpio_info);
}

type_init(sifive_gpio_register_types)
