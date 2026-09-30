/*
 *  Allwinner GPIO Emulation
 *
 *  Copyright (C) 2026 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/gpio/allwinner-gpio.h"
#include "hw/gpio/allwinner-gpio-regs.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

#define REG_INDEX(offset)         (offset / sizeof(uint32_t))

#define AW_GPIO_SET(port) \
    static inline void allwinner_gpio_set_##port(void *opaque, \
                                                 int line, \
                                                 int level) { \
        allwinner_gpio_set(opaque, port, line, level); \
    }

static void allwinner_gpio_set(void *opaque, int port, int line, int level);

AW_GPIO_SET(GPIO_PA);
AW_GPIO_SET(GPIO_PB);
AW_GPIO_SET(GPIO_PC);
AW_GPIO_SET(GPIO_PD);
AW_GPIO_SET(GPIO_PE);
AW_GPIO_SET(GPIO_PF);
AW_GPIO_SET(GPIO_PG);
AW_GPIO_SET(GPIO_PH);
AW_GPIO_SET(GPIO_PI);

static void (*AW_GPIO_IN_HANDLER[AW_GPIO_PORTS_NUM]) (void *opaque,
                                                      int line,
                                                      int level) = {
    allwinner_gpio_set_GPIO_PA,
    allwinner_gpio_set_GPIO_PB,
    allwinner_gpio_set_GPIO_PC,
    allwinner_gpio_set_GPIO_PD,
    allwinner_gpio_set_GPIO_PE,
    allwinner_gpio_set_GPIO_PF,
    allwinner_gpio_set_GPIO_PG,
    allwinner_gpio_set_GPIO_PH,
    allwinner_gpio_set_GPIO_PI,
};

static char *allwinner_gpio_get_regname(unsigned offset)
{
    switch (offset) {
    case 0 ... GPIO_Pn_PUL1(GPIO_PI):
    {
        const char *regname = "?";
        switch (offset % PORT_STRIDE) {
        case CFG0:
            regname = "CFG0";
            break;
        case CFG1:
            regname = "CFG1";
            break;
        case CFG2:
            regname = "CFG2";
            break;
        case CFG3:
            regname = "CFG3";
            break;
        case DAT:
            regname = "DAT";
            break;
        case DRV0:
            regname = "DRV0";
            break;
        case DRV1:
            regname = "DRV1";
            break;
        case PUL0:
            regname = "PUL0";
            break;
        case PUL1:
            regname = "PUL1";
            break;
        }
        return g_strdup_printf("%s:%s",
                               portname(offset / PORT_STRIDE),
                               regname);
    }
    case GPIO_INT_CFG0:
        return g_strdup("INT_CFG0");
    case GPIO_INT_CFG1:
        return g_strdup("INT_CFG1");
    case GPIO_INT_CFG2:
        return g_strdup("INT_CFG2");
    case GPIO_INT_CFG3:
        return g_strdup("INT_CFG3");
    case GPIO_INT_CTL:
        return g_strdup("INT_CTL");
    case GPIO_INT_STA:
        return g_strdup("INT_STA");
    case GPIO_INT_DEB:
        return g_strdup("INT_DEB");
    default:
        return g_strdup("[?]");
    }
}

static inline bool gpio_is_output(AWPortMap *port, uint32_t pin)
{
    uint32_t cfg_n = pin / CFG_PINS_PER_REG;
    uint32_t pin_shift = (pin % CFG_PINS_PER_REG) * CFG_PIN_STRIDE;
    return (extract32(port->cfg[cfg_n], pin_shift, CFG_PIN_STRIDE - 1) ==
        CFG_OUTPUT_MASK);
}

static inline bool gpio_is_input(AWPortMap *port, uint32_t pin)
{
    uint32_t cfg_n = pin / CFG_PINS_PER_REG;
    uint32_t pin_shift = (pin % CFG_PINS_PER_REG) * CFG_PIN_STRIDE;
    return (extract32(port->cfg[cfg_n], pin_shift, CFG_PIN_STRIDE - 1) ==
        CFG_INPUT_MASK);
}

static inline int int_ctl_cfg(AWGPIOState *s, int irq_line)
{
    unsigned int_cfg_n = s->regs[REG_INDEX(GPIO_INT_CFG0) + irq_line / 8];
    return extract32(int_cfg_n,
                     (irq_line % INT_CFG_IRQ_PER_REG) * INT_CFG_IRQ_STRIDE,
                     INT_CFG_IRQ_STRIDE);
}

static inline void allwinner_gpio_update_int(AWGPIOState *s)
{
    qemu_set_irq(
        s->irq,
        !!(s->regs[REG_INDEX(GPIO_INT_CTL)] & s->regs[REG_INDEX(GPIO_INT_STA)])
    );
}

static void allwinner_gpio_set_int_line(AWGPIOState *s, int port,
                                        int line, AWGPIOLevel level)
{
    AWPortsOverlay *o = (AWPortsOverlay *)s->regs;
    int irq_line = irq_nr(port, line);
    bool raise_irq = false;
    unsigned input_val;
    int int_cfg;

    /* Check if pin can actually trigger an interrupt */
    if (irq_line == -1) {
        return;
    }

    /* if this signal isn't configured as an input signal, nothing to do */
    if (!gpio_is_input(&o->ports[port], line)) {
        return;
    }

    /* Get current input value */
    input_val = extract32(o->ports[port].dat, line, 1);
    int_cfg = int_ctl_cfg(s, irq_line);

    switch (int_cfg) {
    case AW_GPIO_IRQ_CFG_RISING_EDGE:
        if (!input_val && level) {
            raise_irq = true;
        }
        break;
    case AW_GPIO_IRQ_CFG_FALLING_EDGE:
        if (input_val && !level) {
            raise_irq = true;
        }
        break;
    case AW_GPIO_IRQ_CFG_HIGH_LEVEL:
        if (level) {
            raise_irq = true;
        }
        break;
    case AW_GPIO_IRQ_CFG_LOW_LEVEL:
        if (!level) {
            raise_irq = true;
        }
        break;
    case AW_GPIO_IRQ_CFG_BOTH_EDGE:
        if (input_val != level) {
            raise_irq = true;
        }
        break;
    default:
        /* Unexpected */
        break;
    }

    if (raise_irq) {
        s->regs[REG_INDEX(GPIO_INT_STA)] |= 1u << irq_line;
    }
}

static void port_set_all_int_lines(AWGPIOState *s, int port)
{
    AWPortsOverlay *o = (AWPortsOverlay *)s->regs;

    for (int i = 0; i < AW_PINS_PER_PORT[port]; i++) {
        AWGPIOLevel aw_level = extract32(o->ports[port].dat, i, 1);
        allwinner_gpio_set_int_line(s, port, i, aw_level);
    }

    allwinner_gpio_update_int(s);
}

static void allwinner_set_all_int_lines(AWGPIOState *s)
{
    for (int i = 0; i < AW_GPIO_PORTS_NUM; i++) {
        port_set_all_int_lines(s, i);
    }
}

static void allwinner_gpio_set(void *opaque, int port, int line, int level)
{
    AWGPIOState *s = AW_GPIO(opaque);
    AWPortsOverlay *o = (AWPortsOverlay *)s->regs;
    AWGPIOLevel aw_level = level ? AW_GPIO_LEVEL_HIGH : AW_GPIO_LEVEL_LOW;

    if (gpio_is_input(&o->ports[port], line)) {

        trace_allwinner_gpio_set(portname(port), line, aw_level);

        allwinner_gpio_set_int_line(s, port, line, aw_level);
        o->ports[port].dat = deposit32(o->ports[port].dat, line, 1, aw_level);
    }
    allwinner_gpio_update_int(s);
}


static inline void port_update_output_lines(AWGPIOState *s, uint32_t port)
{
    AWPortsOverlay *o = (AWPortsOverlay *)s->regs;

    for (int pin = 0; pin < AW_PINS_PER_PORT[port]; pin++) {
        if (gpio_is_output(&o->ports[port], pin)) {
            qemu_set_irq(s->output[port][pin],
                         !!(o->ports[port].dat & BIT_MASK(pin)));
            trace_allwinner_gpio_out_pin(
                portname(port), pin,
                !!(o->ports[port].dat & BIT_MASK(pin)));
         }
        else {
            qemu_irq_lower(s->output[port][pin]);
        }
    }
}

static uint64_t allwinner_gpio_read(void *opaque, hwaddr offset, unsigned size)
{
    AWGPIOState *s = AW_GPIO(opaque);
    uint32_t reg_value = 0;
    g_autofree char *regname = allwinner_gpio_get_regname(offset);

    switch (offset) {
    case 0 ... GPIO_Pn_PUL1(GPIO_PI):
    case GPIO_INT_CFG0:
    case GPIO_INT_CFG1:
    case GPIO_INT_CFG2:
    case GPIO_INT_CFG3:
    case GPIO_INT_CTL:
    case GPIO_INT_STA:
    case GPIO_INT_DEB:
        reg_value = s->regs[REG_INDEX(offset)];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_AW_GPIO, __func__, offset);
        break;
    }

    trace_allwinner_gpio_read(regname, reg_value);

    return reg_value;
}

static void allwinner_port_write(AWGPIOState *s, hwaddr offset, uint64_t value)
{
    AWPortsOverlay *o = (AWPortsOverlay *)s->regs;
    uint32_t port = offset / PORT_STRIDE;
    uint32_t reg = offset % PORT_STRIDE;

    switch (reg) {
    case CFG0:
        o->ports[port].cfg[0] =
            value & GPIO_CFG0_PINS_MASK(AW_PINS_PER_PORT[port]);
        break;
    case CFG1:
        o->ports[port].cfg[1] =
            value & GPIO_CFG1_PINS_MASK(AW_PINS_PER_PORT[port]);
        break;
    case CFG2:
        o->ports[port].cfg[2] =
            value & GPIO_CFG2_PINS_MASK(AW_PINS_PER_PORT[port]);
        break;
    case CFG3:
        o->ports[port].cfg[3] =
            value & GPIO_CFG3_PINS_MASK(AW_PINS_PER_PORT[port]);
        break;
    case DAT:
        o->ports[port].dat =
            value & GPIO_DAT_PINS_MASK(AW_PINS_PER_PORT[port]);
        break;
    case DRV0:
        o->ports[port].drv[0] =
            value & GPIO_DRV0_PINS_MASK(AW_PINS_PER_PORT[port]);
        break;
    case DRV1:
        o->ports[port].drv[1] =
            value & GPIO_DRV1_PINS_MASK(AW_PINS_PER_PORT[port]);
        break;
    case PUL0:
        o->ports[port].pul[0] =
            value & GPIO_PUL0_PINS_MASK(AW_PINS_PER_PORT[port]);
        break;
    case PUL1:
        o->ports[port].pul[1] =
            value & GPIO_PUL1_PINS_MASK(AW_PINS_PER_PORT[port]);
        break;
    }
    port_update_output_lines(s, port);
    port_set_all_int_lines(s, port);
}

static void allwinner_gpio_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    AWGPIOState *s = AW_GPIO(opaque);
    g_autofree char *regname = allwinner_gpio_get_regname(offset);

    trace_allwinner_gpio_write(regname, value);

    switch (offset) {
    case 0 ... GPIO_Pn_PUL1(GPIO_PI):
        allwinner_port_write(s, offset, value);
        break;
    case GPIO_INT_CFG0:
    case GPIO_INT_CFG1:
    case GPIO_INT_CFG2:
    case GPIO_INT_CFG3:
    case GPIO_INT_CTL:
    case GPIO_INT_DEB:
        s->regs[REG_INDEX(offset)] = value;
        /* update interrupts */
        allwinner_set_all_int_lines(s);
        break;
    case GPIO_INT_STA:
        /* W1C */
        s->regs[REG_INDEX(offset)] &= ~value;
        allwinner_set_all_int_lines(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_AW_GPIO, __func__, offset);
        break;
    }
}

static const MemoryRegionOps allwinner_gpio_ops = {
    .read = allwinner_gpio_read,
    .write = allwinner_gpio_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const VMStateDescription vmstate_allwinner_gpio = {
    .name = TYPE_AW_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AWGPIOState, AW_GPIO_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_gpio_reset(DeviceState *dev)
{
    AWGPIOState *s = AW_GPIO(dev);
    AWPortsOverlay *o = (AWPortsOverlay *)s->regs;

    memset(s->regs, 0, AW_GPIO_IOSIZE);

    for (int port = 0; port < AW_GPIO_PORTS_NUM; port++) {
        o->ports[port].drv[0] = aw_gpio_port_reset[port].drv[0];
        o->ports[port].drv[1] = aw_gpio_port_reset[port].drv[1];
        o->ports[port].pul[0] = aw_gpio_port_reset[port].pul[0];
        o->ports[port].pul[1] = aw_gpio_port_reset[port].pul[1];
    }
}

static void allwinner_gpio_realize(DeviceState *dev, Error **errp)
{
    AWGPIOState *s = AW_GPIO(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_gpio_ops, s,
                          TYPE_AW_GPIO, AW_GPIO_IOSIZE);

    for (int port = 0; port < AW_GPIO_PORTS_NUM; port++) {
        g_autofree char *in_name = portname_in(port);
        g_autofree char *out_name = portname_out(port);
        qdev_init_gpio_in_named(dev, AW_GPIO_IN_HANDLER[port],
                                in_name, AW_PINS_PER_PORT[port]);
        qdev_init_gpio_out_named(dev, s->output[port],
                                 out_name, AW_PINS_PER_PORT[port]);
    }
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void allwinner_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = allwinner_gpio_realize;
    device_class_set_legacy_reset(dc, allwinner_gpio_reset);
    dc->vmsd = &vmstate_allwinner_gpio;
    dc->desc = "Allwinner GPIO controller";
}

static const TypeInfo allwinner_gpio_info = {
    .name = TYPE_AW_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AWGPIOState),
    .class_init = allwinner_gpio_class_init,
};

static void allwinner_gpio_register_types(void)
{
    type_register_static(&allwinner_gpio_info);
}

type_init(allwinner_gpio_register_types)
