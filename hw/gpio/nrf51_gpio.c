/*
 * nRF51 System-on-Chip general purpose input/output register definition
 *
 * Reference Manual: http://infocenter.nordicsemi.com/pdf/nRF51_RM_v3.0.pdf
 * Product Spec: http://infocenter.nordicsemi.com/pdf/nRF51822_PS_v3.1.pdf
 *
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/gpio/nrf51_gpio.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "trace.h"

/*
 * Check if the output driver is connected to the direction switch
 * given the current configuration and logic level.
 * It is not differentiated between standard and "high"(-power) drive modes.
 */
static bool is_connected(uint32_t config, uint32_t level)
{
    bool state;
    uint32_t drive_config = extract32(config, 8, 3);

    switch (drive_config) {
    case 0 ... 3:
        state = true;
        break;
    case 4 ... 5:
        state = level != 0;
        break;
    case 6 ... 7:
        state = level == 0;
        break;
    default:
        g_assert_not_reached();
        break;
    }

    return state;
}

static int pull_value(uint32_t config)
{
    int pull = extract32(config, 2, 2);
    if (pull == NRF51_GPIO_PULLDOWN) {
        return 0;
    } else if (pull == NRF51_GPIO_PULLUP) {
        return 1;
    }
    return -1;
}

static void update_output_irq(NRF51GPIOState *s, size_t i,
                              bool connected, bool level)
{
    int64_t irq_level = connected ? level : -1;
    bool old_connected = extract32(s->old_out_connected, i, 1);
    bool old_level = extract32(s->old_out, i, 1);

    if ((old_connected != connected) || (old_level != level)) {
        qemu_set_irq(s->output[i], irq_level);
        trace_nrf51_gpio_update_output_irq(i, irq_level);
    }

    s->old_out = deposit32(s->old_out, i, 1, level);
    s->old_out_connected = deposit32(s->old_out_connected, i, 1, connected);
}

static void update_state(NRF51GPIOState *s)
{
    int pull;
    size_t i;
    bool connected_out, dir, connected_in, out, in, input;
    bool assert_detect = false;

    for (i = 0; i < NRF51_GPIO_PINS; i++) {
        pull = pull_value(s->cnf[i]);
        dir = extract32(s->cnf[i], 0, 1);
        connected_in = extract32(s->in_mask, i, 1);
        out = extract32(s->out, i, 1);
        in = extract32(s->in, i, 1);
        input = !extract32(s->cnf[i], 1, 1);
        connected_out = is_connected(s->cnf[i], out) && dir;

        if (!input) {
            if (pull >= 0) {
                /* Input buffer disconnected from external drives */
                s->in = deposit32(s->in, i, 1, pull);
            }
        } else {
            if (connected_out && connected_in && out != in) {
                /* Pin both driven externally and internally */
                qemu_log_mask(LOG_GUEST_ERROR,
                              "GPIO pin %zu short circuited\n", i);
            }
            if (connected_in) {
                uint32_t detect_config = extract32(s->cnf[i], 16, 2);
                if ((detect_config == 2) && (in == 1)) {
                    assert_detect = true;
                }
                if ((detect_config == 3) && (in == 0)) {
                    assert_detect = true;
                }
            } else {
                /*
                 * Floating input: the output stimulates IN if connected,
                 * otherwise pull-up/pull-down resistors put a value on both
                 * IN and OUT.
                 */
                if (pull >= 0 && !connected_out) {
                    connected_out = true;
                    out = pull;
                }
                if (connected_out) {
                    s->in = deposit32(s->in, i, 1, out);
                }
            }
        }
        update_output_irq(s, i, connected_out, out);
    }

    qemu_set_irq(s->detect, assert_detect);
}

/*
 * Direction is exposed in both the DIR register and the DIR bit
 * of each PINs CNF configuration register. Reflect bits for pins in DIR
 * to individual pin configuration registers.
 */
static void reflect_dir_bit_in_cnf(NRF51GPIOState *s)
{
    size_t i;

    uint32_t value = s->dir;

    for (i = 0; i < NRF51_GPIO_PINS; i++) {
        s->cnf[i] = (s->cnf[i] & ~(1UL)) | ((value >> i) & 0x01);
    }
}

static uint64_t nrf51_gpio_read(void *opaque, hwaddr offset, unsigned int size)
{
    NRF51GPIOState *s = NRF51_GPIO(opaque);
    uint64_t r = 0;
    size_t idx;

    switch (offset) {
    case NRF51_GPIO_REG_OUT ... NRF51_GPIO_REG_OUTCLR:
        r = s->out;
        break;

    case NRF51_GPIO_REG_IN:
        r = s->in;
        break;

    case NRF51_GPIO_REG_DIR ... NRF51_GPIO_REG_DIRCLR:
        r = s->dir;
        break;

    case NRF51_GPIO_REG_CNF_START ... NRF51_GPIO_REG_CNF_END:
        idx = (offset - NRF51_GPIO_REG_CNF_START) / 4;
        r = s->cnf[idx];
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    trace_nrf51_gpio_read(offset, r);

    return r;
}

static void nrf51_gpio_write(void *opaque, hwaddr offset,
                       uint64_t value, unsigned int size)
{
    NRF51GPIOState *s = NRF51_GPIO(opaque);
    size_t idx;

    trace_nrf51_gpio_write(offset, value);

    switch (offset) {
    case NRF51_GPIO_REG_OUT:
        s->out = value;
        break;

    case NRF51_GPIO_REG_OUTSET:
        s->out |= value;
        break;

    case NRF51_GPIO_REG_OUTCLR:
        s->out &= ~value;
        break;

    case NRF51_GPIO_REG_DIR:
        s->dir = value;
        reflect_dir_bit_in_cnf(s);
        break;

    case NRF51_GPIO_REG_DIRSET:
        s->dir |= value;
        reflect_dir_bit_in_cnf(s);
        break;

    case NRF51_GPIO_REG_DIRCLR:
        s->dir &= ~value;
        reflect_dir_bit_in_cnf(s);
        break;

    case NRF51_GPIO_REG_CNF_START ... NRF51_GPIO_REG_CNF_END:
        idx = (offset - NRF51_GPIO_REG_CNF_START) / 4;
        s->cnf[idx] = value;
        /*
         * direction is exposed in both the DIR register and the DIR bit
         * of each PINs CNF configuration register.
         */
        s->dir = (s->dir & ~(1UL << idx)) | ((value & 0x01) << idx);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    update_state(s);
}

static const MemoryRegionOps gpio_ops = {
    .read =  nrf51_gpio_read,
    .write = nrf51_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void nrf51_gpio_set(void *opaque, int line, int value)
{
    NRF51GPIOState *s = NRF51_GPIO(opaque);

    trace_nrf51_gpio_set(line, value);

    assert(line >= 0 && line < NRF51_GPIO_PINS);

    s->in_mask = deposit32(s->in_mask, line, 1, value >= 0);
    if (value >= 0) {
        s->in = deposit32(s->in, line, 1, value != 0);
    }

    update_state(s);
}

static void nrf51_gpio_reset(DeviceState *dev)
{
    NRF51GPIOState *s = NRF51_GPIO(dev);
    size_t i;

    s->out = 0;
    s->old_out = 0;
    s->old_out_connected = 0;
    s->in = 0;
    s->in_mask = 0;
    s->dir = 0;

    for (i = 0; i < NRF51_GPIO_PINS; i++) {
        s->cnf[i] = 0x00000002;
    }
}

static const VMStateDescription vmstate_nrf51_gpio = {
    .name = TYPE_NRF51_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(out, NRF51GPIOState),
        VMSTATE_UINT32(in, NRF51GPIOState),
        VMSTATE_UINT32(in_mask, NRF51GPIOState),
        VMSTATE_UINT32(dir, NRF51GPIOState),
        VMSTATE_UINT32_ARRAY(cnf, NRF51GPIOState, NRF51_GPIO_PINS),
        VMSTATE_UINT32(old_out, NRF51GPIOState),
        VMSTATE_UINT32(old_out_connected, NRF51GPIOState),
        VMSTATE_END_OF_LIST()
    }
};

static void nrf51_gpio_init(Object *obj)
{
    NRF51GPIOState *s = NRF51_GPIO(obj);

    memory_region_init_io(&s->mmio, obj, &gpio_ops, s,
            TYPE_NRF51_GPIO, NRF51_GPIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    qdev_init_gpio_in(DEVICE(s), nrf51_gpio_set, NRF51_GPIO_PINS);
    qdev_init_gpio_out(DEVICE(s), s->output, NRF51_GPIO_PINS);
    qdev_init_gpio_out_named(DEVICE(s), &s->detect, "detect", 1);
}

static void nrf51_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_nrf51_gpio;
    dc->reset = nrf51_gpio_reset;
    dc->desc = "nRF51 GPIO";
}

static const TypeInfo nrf51_gpio_info = {
    .name = TYPE_NRF51_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NRF51GPIOState),
    .instance_init = nrf51_gpio_init,
    .class_init = nrf51_gpio_class_init
};

static void nrf51_gpio_register_types(void)
{
    type_register_static(&nrf51_gpio_info);
}

type_init(nrf51_gpio_register_types)
