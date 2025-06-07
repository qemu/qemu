/*
 * STM32 RCC (only reset and enable registers are implemented)
 *
 * Copyright (c) 2024 Román Cárdenas <rcardenas.rod@gmail.com>
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
#include "hw/misc/stm32_rcc.h"

static void stm32_rcc_reset(DeviceState *dev)
{
    STM32RccState *s = STM32_RCC(dev);

    for (int i = 0; i < STM32_RCC_NREGS; i++) {
        s->regs[i] = 0;
    }
}

static uint64_t stm32_rcc_read(void *opaque, hwaddr addr, unsigned int size)
{
    STM32RccState *s = STM32_RCC(opaque);

    uint32_t value = 0;
    if (addr > STM32_RCC_DCKCFGR2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
    } else {
        value = s->regs[addr >> 2];
    }
    trace_stm32_rcc_read(addr, value);
    return value;
}

static void stm32_rcc_write(void *opaque, hwaddr addr,
                            uint64_t val64, unsigned int size)
{
    STM32RccState *s = STM32_RCC(opaque);
    uint32_t value = val64;
    uint32_t prev_value, new_value, irq_offset;

    trace_stm32_rcc_write(addr, value);

    if (addr > STM32_RCC_DCKCFGR2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        return;
    }

    switch (addr) {
    case STM32_RCC_AHB1_RSTR...STM32_RCC_APB2_RSTR:
        prev_value = s->regs[addr / 4];
        s->regs[addr / 4] = value;

        irq_offset = ((addr - STM32_RCC_AHB1_RSTR) / 4) * 32;
        for (int i = 0; i < 32; i++) {
            new_value = extract32(value, i, 1);
            if (extract32(prev_value, i, 1) && !new_value) {
                trace_stm32_rcc_pulse_reset(irq_offset + i, new_value);
                qemu_set_irq(s->reset_irq[irq_offset + i], new_value);
            }
        }
        return;
    case STM32_RCC_AHB1_ENR...STM32_RCC_APB2_ENR:
        prev_value = s->regs[addr / 4];
        s->regs[addr / 4] = value;

        irq_offset = ((addr - STM32_RCC_AHB1_ENR) / 4) * 32;
        for (int i = 0; i < 32; i++) {
            new_value = extract32(value, i, 1);
            if (!extract32(prev_value, i, 1) && new_value) {
                trace_stm32_rcc_pulse_enable(irq_offset + i, new_value);
                qemu_set_irq(s->enable_irq[irq_offset + i], new_value);
            }
        }
        return;
    default:
        qemu_log_mask(
            LOG_UNIMP,
            "%s: The RCC peripheral only supports enable and reset in QEMU\n",
            __func__
        );
        s->regs[addr >> 2] = value;
    }
}

static const MemoryRegionOps stm32_rcc_ops = {
    .read = stm32_rcc_read,
    .write = stm32_rcc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void stm32_rcc_init(Object *obj)
{
    STM32RccState *s = STM32_RCC(obj);

    memory_region_init_io(&s->mmio, obj, &stm32_rcc_ops, s,
                          TYPE_STM32_RCC, STM32_RCC_PERIPHERAL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    qdev_init_gpio_out(DEVICE(obj), s->reset_irq, STM32_RCC_NIRQS);
    qdev_init_gpio_out(DEVICE(obj), s->enable_irq, STM32_RCC_NIRQS);

    for (int i = 0; i < STM32_RCC_NIRQS; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->reset_irq[i]);
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->enable_irq[i]);
    }
}

static const VMStateDescription vmstate_stm32_rcc = {
    .name = TYPE_STM32_RCC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, STM32RccState, STM32_RCC_NREGS),
        VMSTATE_END_OF_LIST()
    }
};

static void stm32_rcc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_stm32_rcc;
    device_class_set_legacy_reset(dc, stm32_rcc_reset);
}

static const TypeInfo stm32_rcc_info = {
    .name          = TYPE_STM32_RCC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32RccState),
    .instance_init = stm32_rcc_init,
    .class_init    = stm32_rcc_class_init,
};

static void stm32_rcc_register_types(void)
{
    type_register_static(&stm32_rcc_info);
}

type_init(stm32_rcc_register_types)
