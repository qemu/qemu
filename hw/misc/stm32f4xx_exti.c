/*
 * STM32F4XX EXTI
 *
 * Copyright (c) 2014 Alistair Francis <alistair@alistair23.me>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/misc/stm32f4xx_exti.h"

static void stm32f4xx_exti_reset(DeviceState *dev)
{
    STM32F4xxExtiState *s = STM32F4XX_EXTI(dev);

    s->exti_imr = 0x00000000;
    s->exti_emr = 0x00000000;
    s->exti_rtsr = 0x00000000;
    s->exti_ftsr = 0x00000000;
    s->exti_swier = 0x00000000;
    s->exti_pr = 0x00000000;
}

static void stm32f4xx_exti_set_irq(void *opaque, int irq, int level)
{
    STM32F4xxExtiState *s = opaque;

    trace_stm32f4xx_exti_set_irq(irq, level);

    if (((1 << irq) & s->exti_rtsr) && level) {
        /* Rising Edge */
        s->exti_pr |= 1 << irq;
    }

    if (((1 << irq) & s->exti_ftsr) && !level) {
        /* Falling Edge */
        s->exti_pr |= 1 << irq;
    }

    if (!((1 << irq) & s->exti_imr)) {
        /* Interrupt is masked */
        return;
    }
    qemu_irq_pulse(s->irq[irq]);
}

static uint64_t stm32f4xx_exti_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    STM32F4xxExtiState *s = opaque;

    trace_stm32f4xx_exti_read(addr);

    switch (addr) {
    case EXTI_IMR:
        return s->exti_imr;
    case EXTI_EMR:
        return s->exti_emr;
    case EXTI_RTSR:
        return s->exti_rtsr;
    case EXTI_FTSR:
        return s->exti_ftsr;
    case EXTI_SWIER:
        return s->exti_swier;
    case EXTI_PR:
        return s->exti_pr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "STM32F4XX_exti_read: Bad offset %x\n", (int)addr);
        return 0;
    }
    return 0;
}

static void stm32f4xx_exti_write(void *opaque, hwaddr addr,
                       uint64_t val64, unsigned int size)
{
    STM32F4xxExtiState *s = opaque;
    uint32_t value = (uint32_t) val64;

    trace_stm32f4xx_exti_write(addr, value);

    switch (addr) {
    case EXTI_IMR:
        s->exti_imr = value;
        return;
    case EXTI_EMR:
        s->exti_emr = value;
        return;
    case EXTI_RTSR:
        s->exti_rtsr = value;
        return;
    case EXTI_FTSR:
        s->exti_ftsr = value;
        return;
    case EXTI_SWIER:
        s->exti_swier = value;
        return;
    case EXTI_PR:
        /* This bit is cleared by writing a 1 to it */
        s->exti_pr &= ~value;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "STM32F4XX_exti_write: Bad offset %x\n", (int)addr);
    }
}

static const MemoryRegionOps stm32f4xx_exti_ops = {
    .read = stm32f4xx_exti_read,
    .write = stm32f4xx_exti_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void stm32f4xx_exti_init(Object *obj)
{
    STM32F4xxExtiState *s = STM32F4XX_EXTI(obj);
    int i;

    for (i = 0; i < NUM_INTERRUPT_OUT_LINES; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[i]);
    }

    memory_region_init_io(&s->mmio, obj, &stm32f4xx_exti_ops, s,
                          TYPE_STM32F4XX_EXTI, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    qdev_init_gpio_in(DEVICE(obj), stm32f4xx_exti_set_irq,
                      NUM_GPIO_EVENT_IN_LINES);
}

static const VMStateDescription vmstate_stm32f4xx_exti = {
    .name = TYPE_STM32F4XX_EXTI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(exti_imr, STM32F4xxExtiState),
        VMSTATE_UINT32(exti_emr, STM32F4xxExtiState),
        VMSTATE_UINT32(exti_rtsr, STM32F4xxExtiState),
        VMSTATE_UINT32(exti_ftsr, STM32F4xxExtiState),
        VMSTATE_UINT32(exti_swier, STM32F4xxExtiState),
        VMSTATE_UINT32(exti_pr, STM32F4xxExtiState),
        VMSTATE_END_OF_LIST()
    }
};

static void stm32f4xx_exti_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = stm32f4xx_exti_reset;
    dc->vmsd = &vmstate_stm32f4xx_exti;
}

static const TypeInfo stm32f4xx_exti_info = {
    .name          = TYPE_STM32F4XX_EXTI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F4xxExtiState),
    .instance_init = stm32f4xx_exti_init,
    .class_init    = stm32f4xx_exti_class_init,
};

static void stm32f4xx_exti_register_types(void)
{
    type_register_static(&stm32f4xx_exti_info);
}

type_init(stm32f4xx_exti_register_types)
