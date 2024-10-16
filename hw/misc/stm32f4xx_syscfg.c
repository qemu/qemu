/*
 * STM32F4xx SYSCFG
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
#include "hw/misc/stm32f4xx_syscfg.h"

static void stm32f4xx_syscfg_reset(DeviceState *dev)
{
    STM32F4xxSyscfgState *s = STM32F4XX_SYSCFG(dev);

    s->syscfg_memrmp = 0x00000000;
    s->syscfg_pmc = 0x00000000;
    s->syscfg_exticr[0] = 0x00000000;
    s->syscfg_exticr[1] = 0x00000000;
    s->syscfg_exticr[2] = 0x00000000;
    s->syscfg_exticr[3] = 0x00000000;
    s->syscfg_cmpcr = 0x00000000;
}

static void stm32f4xx_syscfg_set_irq(void *opaque, int irq, int level)
{
    STM32F4xxSyscfgState *s = opaque;
    int icrreg = irq / 4;
    int startbit = (irq & 3) * 4;
    uint8_t config = irq / 16;

    trace_stm32f4xx_syscfg_set_irq(irq / 16, irq % 16, level);

    g_assert(icrreg < SYSCFG_NUM_EXTICR);

    if (extract32(s->syscfg_exticr[icrreg], startbit, 4) == config) {
        qemu_set_irq(s->gpio_out[irq], level);
        trace_stm32f4xx_pulse_exti(irq);
   }
}

static uint64_t stm32f4xx_syscfg_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    STM32F4xxSyscfgState *s = opaque;

    trace_stm32f4xx_syscfg_read(addr);

    switch (addr) {
    case SYSCFG_MEMRMP:
        return s->syscfg_memrmp;
    case SYSCFG_PMC:
        return s->syscfg_pmc;
    case SYSCFG_EXTICR1...SYSCFG_EXTICR4:
        return s->syscfg_exticr[addr / 4 - SYSCFG_EXTICR1 / 4];
    case SYSCFG_CMPCR:
        return s->syscfg_cmpcr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        return 0;
    }
}

static void stm32f4xx_syscfg_write(void *opaque, hwaddr addr,
                       uint64_t val64, unsigned int size)
{
    STM32F4xxSyscfgState *s = opaque;
    uint32_t value = val64;

    trace_stm32f4xx_syscfg_write(value, addr);

    switch (addr) {
    case SYSCFG_MEMRMP:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Changing the memory mapping isn't supported " \
                      "in QEMU\n", __func__);
        return;
    case SYSCFG_PMC:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Changing the memory mapping isn't supported " \
                      "in QEMU\n", __func__);
        return;
    case SYSCFG_EXTICR1...SYSCFG_EXTICR4:
        s->syscfg_exticr[addr / 4 - SYSCFG_EXTICR1 / 4] = (value & 0xFFFF);
        return;
    case SYSCFG_CMPCR:
        s->syscfg_cmpcr = value;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }
}

static const MemoryRegionOps stm32f4xx_syscfg_ops = {
    .read = stm32f4xx_syscfg_read,
    .write = stm32f4xx_syscfg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void stm32f4xx_syscfg_init(Object *obj)
{
    STM32F4xxSyscfgState *s = STM32F4XX_SYSCFG(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &stm32f4xx_syscfg_ops, s,
                          TYPE_STM32F4XX_SYSCFG, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    qdev_init_gpio_in(DEVICE(obj), stm32f4xx_syscfg_set_irq, 16 * 9);
    qdev_init_gpio_out(DEVICE(obj), s->gpio_out, 16);
}

static const VMStateDescription vmstate_stm32f4xx_syscfg = {
    .name = TYPE_STM32F4XX_SYSCFG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(syscfg_memrmp, STM32F4xxSyscfgState),
        VMSTATE_UINT32(syscfg_pmc, STM32F4xxSyscfgState),
        VMSTATE_UINT32_ARRAY(syscfg_exticr, STM32F4xxSyscfgState,
                             SYSCFG_NUM_EXTICR),
        VMSTATE_UINT32(syscfg_cmpcr, STM32F4xxSyscfgState),
        VMSTATE_END_OF_LIST()
    }
};

static void stm32f4xx_syscfg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, stm32f4xx_syscfg_reset);
    dc->vmsd = &vmstate_stm32f4xx_syscfg;
}

static const TypeInfo stm32f4xx_syscfg_info = {
    .name          = TYPE_STM32F4XX_SYSCFG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F4xxSyscfgState),
    .instance_init = stm32f4xx_syscfg_init,
    .class_init    = stm32f4xx_syscfg_class_init,
};

static void stm32f4xx_syscfg_register_types(void)
{
    type_register_static(&stm32f4xx_syscfg_info);
}

type_init(stm32f4xx_syscfg_register_types)
