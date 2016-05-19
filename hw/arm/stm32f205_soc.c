/*
 * STM32F205 SoC
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
#include "qapi/error.h"
#include "qemu-common.h"
#include "hw/arm/arm.h"
#include "exec/address-spaces.h"
#include "hw/arm/stm32f205_soc.h"

/* At the moment only Timer 2 to 5 are modelled */
static const uint32_t timer_addr[STM_NUM_TIMERS] = { 0x40000000, 0x40000400,
    0x40000800, 0x40000C00 };
static const uint32_t usart_addr[STM_NUM_USARTS] = { 0x40011000, 0x40004400,
    0x40004800, 0x40004C00, 0x40005000, 0x40011400 };

static const int timer_irq[STM_NUM_TIMERS] = {28, 29, 30, 50};
static const int usart_irq[STM_NUM_USARTS] = {37, 38, 39, 52, 53, 71};

static void stm32f205_soc_initfn(Object *obj)
{
    STM32F205State *s = STM32F205_SOC(obj);
    int i;

    object_initialize(&s->syscfg, sizeof(s->syscfg), TYPE_STM32F2XX_SYSCFG);
    qdev_set_parent_bus(DEVICE(&s->syscfg), sysbus_get_default());

    for (i = 0; i < STM_NUM_USARTS; i++) {
        object_initialize(&s->usart[i], sizeof(s->usart[i]),
                          TYPE_STM32F2XX_USART);
        qdev_set_parent_bus(DEVICE(&s->usart[i]), sysbus_get_default());
    }

    for (i = 0; i < STM_NUM_TIMERS; i++) {
        object_initialize(&s->timer[i], sizeof(s->timer[i]),
                          TYPE_STM32F2XX_TIMER);
        qdev_set_parent_bus(DEVICE(&s->timer[i]), sysbus_get_default());
    }
}

static void stm32f205_soc_realize(DeviceState *dev_soc, Error **errp)
{
    STM32F205State *s = STM32F205_SOC(dev_soc);
    DeviceState *syscfgdev, *usartdev, *timerdev, *nvic;
    SysBusDevice *syscfgbusdev, *usartbusdev, *timerbusdev;
    Error *err = NULL;
    int i;

    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    MemoryRegion *flash_alias = g_new(MemoryRegion, 1);

    memory_region_init_ram(flash, NULL, "STM32F205.flash", FLASH_SIZE,
                           &error_fatal);
    memory_region_init_alias(flash_alias, NULL, "STM32F205.flash.alias",
                             flash, 0, FLASH_SIZE);

    vmstate_register_ram_global(flash);

    memory_region_set_readonly(flash, true);
    memory_region_set_readonly(flash_alias, true);

    memory_region_add_subregion(system_memory, FLASH_BASE_ADDRESS, flash);
    memory_region_add_subregion(system_memory, 0, flash_alias);

    memory_region_init_ram(sram, NULL, "STM32F205.sram", SRAM_SIZE,
                           &error_fatal);
    vmstate_register_ram_global(sram);
    memory_region_add_subregion(system_memory, SRAM_BASE_ADDRESS, sram);

    nvic = armv7m_init(get_system_memory(), FLASH_SIZE, 96,
                       s->kernel_filename, s->cpu_model);

    /* System configuration controller */
    syscfgdev = DEVICE(&s->syscfg);
    object_property_set_bool(OBJECT(&s->syscfg), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    syscfgbusdev = SYS_BUS_DEVICE(syscfgdev);
    sysbus_mmio_map(syscfgbusdev, 0, 0x40013800);
    sysbus_connect_irq(syscfgbusdev, 0, qdev_get_gpio_in(nvic, 71));

    /* Attach UART (uses USART registers) and USART controllers */
    for (i = 0; i < STM_NUM_USARTS; i++) {
        usartdev = DEVICE(&(s->usart[i]));
        object_property_set_bool(OBJECT(&s->usart[i]), true, "realized", &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
        usartbusdev = SYS_BUS_DEVICE(usartdev);
        sysbus_mmio_map(usartbusdev, 0, usart_addr[i]);
        sysbus_connect_irq(usartbusdev, 0,
                           qdev_get_gpio_in(nvic, usart_irq[i]));
    }

    /* Timer 2 to 5 */
    for (i = 0; i < STM_NUM_TIMERS; i++) {
        timerdev = DEVICE(&(s->timer[i]));
        qdev_prop_set_uint64(timerdev, "clock-frequency", 1000000000);
        object_property_set_bool(OBJECT(&s->timer[i]), true, "realized", &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
        timerbusdev = SYS_BUS_DEVICE(timerdev);
        sysbus_mmio_map(timerbusdev, 0, timer_addr[i]);
        sysbus_connect_irq(timerbusdev, 0,
                           qdev_get_gpio_in(nvic, timer_irq[i]));
    }
}

static Property stm32f205_soc_properties[] = {
    DEFINE_PROP_STRING("kernel-filename", STM32F205State, kernel_filename),
    DEFINE_PROP_STRING("cpu-model", STM32F205State, cpu_model),
    DEFINE_PROP_END_OF_LIST(),
};

static void stm32f205_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = stm32f205_soc_realize;
    dc->props = stm32f205_soc_properties;
}

static const TypeInfo stm32f205_soc_info = {
    .name          = TYPE_STM32F205_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F205State),
    .instance_init = stm32f205_soc_initfn,
    .class_init    = stm32f205_soc_class_init,
};

static void stm32f205_soc_types(void)
{
    type_register_static(&stm32f205_soc_info);
}

type_init(stm32f205_soc_types)
