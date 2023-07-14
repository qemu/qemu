/*
 * STM32F2XX SYSCFG
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
#include "hw/misc/stm32f2xx_syscfg.h"
#include "qemu/log.h"
#include "qemu/module.h"

#ifndef STM_SYSCFG_ERR_DEBUG
#define STM_SYSCFG_ERR_DEBUG 0
#endif

#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (STM_SYSCFG_ERR_DEBUG >= lvl) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0)

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

static void stm32f2xx_syscfg_reset(DeviceState *dev)
{
    STM32F2XXSyscfgState *s = STM32F2XX_SYSCFG(dev);

    s->syscfg_memrmp = 0x00000000;
    s->syscfg_pmc = 0x00000000;
    s->syscfg_exticr1 = 0x00000000;
    s->syscfg_exticr2 = 0x00000000;
    s->syscfg_exticr3 = 0x00000000;
    s->syscfg_exticr4 = 0x00000000;
    s->syscfg_cmpcr = 0x00000000;
}

static uint64_t stm32f2xx_syscfg_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    STM32F2XXSyscfgState *s = opaque;

    DB_PRINT("0x%"HWADDR_PRIx"\n", addr);

    switch (addr) {
    case SYSCFG_MEMRMP:
        return s->syscfg_memrmp;
    case SYSCFG_PMC:
        return s->syscfg_pmc;
    case SYSCFG_EXTICR1:
        return s->syscfg_exticr1;
    case SYSCFG_EXTICR2:
        return s->syscfg_exticr2;
    case SYSCFG_EXTICR3:
        return s->syscfg_exticr3;
    case SYSCFG_EXTICR4:
        return s->syscfg_exticr4;
    case SYSCFG_CMPCR:
        return s->syscfg_cmpcr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        return 0;
    }

    return 0;
}

static void stm32f2xx_syscfg_write(void *opaque, hwaddr addr,
                       uint64_t val64, unsigned int size)
{
    STM32F2XXSyscfgState *s = opaque;
    uint32_t value = val64;

    DB_PRINT("0x%x, 0x%"HWADDR_PRIx"\n", value, addr);

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
    case SYSCFG_EXTICR1:
        s->syscfg_exticr1 = (value & 0xFFFF);
        return;
    case SYSCFG_EXTICR2:
        s->syscfg_exticr2 = (value & 0xFFFF);
        return;
    case SYSCFG_EXTICR3:
        s->syscfg_exticr3 = (value & 0xFFFF);
        return;
    case SYSCFG_EXTICR4:
        s->syscfg_exticr4 = (value & 0xFFFF);
        return;
    case SYSCFG_CMPCR:
        s->syscfg_cmpcr = value;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }
}

static const MemoryRegionOps stm32f2xx_syscfg_ops = {
    .read = stm32f2xx_syscfg_read,
    .write = stm32f2xx_syscfg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void stm32f2xx_syscfg_init(Object *obj)
{
    STM32F2XXSyscfgState *s = STM32F2XX_SYSCFG(obj);

    memory_region_init_io(&s->mmio, obj, &stm32f2xx_syscfg_ops, s,
                          TYPE_STM32F2XX_SYSCFG, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void stm32f2xx_syscfg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = stm32f2xx_syscfg_reset;
}

static const TypeInfo stm32f2xx_syscfg_info = {
    .name          = TYPE_STM32F2XX_SYSCFG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F2XXSyscfgState),
    .instance_init = stm32f2xx_syscfg_init,
    .class_init    = stm32f2xx_syscfg_class_init,
};

static void stm32f2xx_syscfg_register_types(void)
{
    type_register_static(&stm32f2xx_syscfg_info);
}

type_init(stm32f2xx_syscfg_register_types)
