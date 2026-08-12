/*
 * Cadence GPIO emulation.
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/gpio/cadence_gpio.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "trace.h"

static void cdns_gpio_update_irq(CadenceGPIOState *s)
{
    qemu_set_irq(s->irq, s->isr ? 1 : 0);
}

static void cdns_gpio_update_isr_with_inpvr(CadenceGPIOState *s, uint32_t new)
{
    uint32_t new_isr = 0;
    uint32_t any_edges, rising_edges, falling_edges, deassert_mask;

    /*
     * If ITR is set, this is level triggered:
     * set corresponding ISR bits when IVR matches new inpvr value.
     */
    new_isr |= s->itr & ~(s->ivr ^ new);

    /*
     * If ITR is not set, this is edge-triggered:
     * If IOAR bit is set, trigger on any edge;
     * otherwise trigger on rising edge if IVR is set,
     * trigger on falling edge if IVR bit is 0.
     */
    any_edges = s->ioar & (s->inpvr ^ new);
    rising_edges = s->ivr & ~s->inpvr & new;
    falling_edges = ~s->ivr & s->inpvr & ~new;
    new_isr |= ~s->itr & (any_edges | rising_edges | falling_edges);

    /*
     * In bypass mode or if this isn't an input pin, the corresponding ISR
     * bit is forced to zero.
     */
    deassert_mask = s->bmr | ~s->dmr | s->imr;

    new_isr &= ~deassert_mask;
    s->isr = new_isr;

    cdns_gpio_update_irq(s);
}

static void cdns_gpio_update_isr(CadenceGPIOState *s)
{
    cdns_gpio_update_isr_with_inpvr(s, s->inpvr);
}

static void cdns_gpio_set(void *opaque, int line, int level)
{
    CadenceGPIOState *s = CADENCE_GPIO(opaque);
    uint32_t new_inpvr = deposit32(s->inpvr, line, 1, level ? 1 : 0);

    trace_cdns_gpio_set(DEVICE(s)->canonical_path, line, level);

    cdns_gpio_update_isr_with_inpvr(s, new_inpvr);

    /* Sync INPVR with new value */
    s->inpvr = new_inpvr;
}

static inline void cdns_gpio_update_output_irq(CadenceGPIOState *s)
{
    uint32_t is_output = ~s->bmr & ~s->dmr & s->oer;

    for (int i = 0; i < CDNS_GPIO_NUM; i++) {
        if (extract32(is_output, i, 1)) {
            /* Forward the output value to corresponding irq */
            qemu_set_irq(s->output[i], extract32(s->ovr, i, 1));
        }
    }
}

static uint64_t cdns_gpio_read(void *opaque, hwaddr offset, unsigned size)
{
    CadenceGPIOState *s = CADENCE_GPIO(opaque);
    uint32_t reg_value = 0x0;

    switch (offset) {
    case CDNS_GPIO_BYPASS_MODE:
        reg_value = s->bmr;
        break;

    case CDNS_GPIO_DIRECTION_MODE:
        reg_value = s->dmr;
        break;

    case CDNS_GPIO_OUTPUT_EN:
        reg_value = s->oer;
        break;

    case CDNS_GPIO_OUTPUT_VALUE:
        reg_value = s->ovr;
        break;

    case CDNS_GPIO_INPUT_VALUE:
        reg_value = s->inpvr;
        break;

    case CDNS_GPIO_IRQ_MASK:
        reg_value = s->imr;
        break;

    case CDNS_GPIO_IRQ_STATUS:
        reg_value = s->isr;
        break;

    case CDNS_GPIO_IRQ_TYPE:
        reg_value = s->itr;
        break;

    case CDNS_GPIO_IRQ_VALUE:
        reg_value = s->ivr;
        break;

    case CDNS_GPIO_IRQ_ANY_EDGE:
        reg_value = s->ioar;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_CADENCE_GPIO, __func__, offset);
        break;
    }

    trace_cdns_gpio_read(DEVICE(s)->canonical_path, offset, reg_value);

    return reg_value;
}

static void cdns_gpio_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    CadenceGPIOState *s = CADENCE_GPIO(opaque);

    trace_cdns_gpio_write(DEVICE(s)->canonical_path, offset, value);

    switch (offset) {
    case CDNS_GPIO_BYPASS_MODE:
        s->bmr = value;
        cdns_gpio_update_output_irq(s);
        cdns_gpio_update_isr(s);
        break;

    case CDNS_GPIO_DIRECTION_MODE:
        s->dmr = value;
        cdns_gpio_update_output_irq(s);
        cdns_gpio_update_isr(s);
        break;

    case CDNS_GPIO_OUTPUT_EN:
        s->oer = value;
        cdns_gpio_update_output_irq(s);
        break;

    case CDNS_GPIO_OUTPUT_VALUE:
        s->ovr = value;
        cdns_gpio_update_output_irq(s);
        break;

    case CDNS_GPIO_IRQ_EN:
        s->imr &= ~value;
        cdns_gpio_update_isr(s);
        break;

    case CDNS_GPIO_IRQ_DIS:
        s->imr |= value;
        cdns_gpio_update_isr(s);
        break;

    case CDNS_GPIO_IRQ_TYPE:
        s->itr = value;
        break;

    case CDNS_GPIO_IRQ_VALUE:
        s->ivr = value;
        break;

    case CDNS_GPIO_IRQ_ANY_EDGE:
        s->ioar = value;
        break;

    case CDNS_GPIO_INPUT_VALUE:
    case CDNS_GPIO_IRQ_MASK:
    case CDNS_GPIO_IRQ_STATUS:
        /* Read-Only */
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_CADENCE_GPIO, __func__, offset);
        break;
    }
}

static const MemoryRegionOps cdns_gpio_ops = {
    .read = cdns_gpio_read,
    .write = cdns_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static const VMStateDescription vmstate_cdns_gpio = {
    .name = TYPE_CADENCE_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(bmr, CadenceGPIOState),
        VMSTATE_UINT32(dmr, CadenceGPIOState),
        VMSTATE_UINT32(oer, CadenceGPIOState),
        VMSTATE_UINT32(ovr, CadenceGPIOState),
        VMSTATE_UINT32(inpvr, CadenceGPIOState),
        VMSTATE_UINT32(imr, CadenceGPIOState),
        VMSTATE_UINT32(isr, CadenceGPIOState),
        VMSTATE_UINT32(itr, CadenceGPIOState),
        VMSTATE_UINT32(ivr, CadenceGPIOState),
        VMSTATE_UINT32(ioar, CadenceGPIOState),
        VMSTATE_END_OF_LIST()
    }
};

static void cdns_gpio_reset(DeviceState *dev)
{
    CadenceGPIOState *s = CADENCE_GPIO(dev);

    s->bmr      = 0;
    s->dmr      = 0;
    s->oer      = 0;
    s->ovr      = 0;
    s->inpvr    = 0;
    s->imr      = 0xffffffff;
    s->isr      = 0;
    s->itr      = 0;
    s->ivr      = 0;
    s->ioar     = 0;
}

static void cdns_gpio_init(Object *obj)
{
    CadenceGPIOState *s = CADENCE_GPIO(obj);

    memory_region_init_io(&s->iomem, obj, &cdns_gpio_ops, s,
                          TYPE_CADENCE_GPIO, CDNS_GPIO_REG_SIZE);

    qdev_init_gpio_in(DEVICE(s), cdns_gpio_set, CDNS_GPIO_NUM);
    qdev_init_gpio_out(DEVICE(s), s->output, CDNS_GPIO_NUM);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void cdns_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, cdns_gpio_reset);
    dc->vmsd = &vmstate_cdns_gpio;
    dc->desc = "Cadence GPIO controller";
}

static const TypeInfo cdns_gpio_info = {
    .name = TYPE_CADENCE_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CadenceGPIOState),
    .instance_init = cdns_gpio_init,
    .class_init = cdns_gpio_class_init,
};

static void cdns_gpio_register_types(void)
{
    type_register_static(&cdns_gpio_info);
}

type_init(cdns_gpio_register_types)
