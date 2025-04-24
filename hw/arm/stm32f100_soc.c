/*
 * STM32F100 SoC
 *
 * Copyright (c) 2021 Alexandre Iooss <erdnaxe@crans.org>
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
#include "qemu/module.h"
#include "hw/arm/boot.h"
#include "system/address-spaces.h"
#include "hw/arm/stm32f100_soc.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-clock.h"
#include "hw/misc/unimp.h"
#include "system/system.h"

/* stm32f100_soc implementation is derived from stm32f205_soc */

static const uint32_t usart_addr[STM_NUM_USARTS] = { 0x40013800, 0x40004400,
    0x40004800 };
static const uint32_t spi_addr[STM_NUM_SPIS] = { 0x40013000, 0x40003800 };

static const int usart_irq[STM_NUM_USARTS] = {37, 38, 39};
static const int spi_irq[STM_NUM_SPIS] = {35, 36};

static void stm32f100_soc_initfn(Object *obj)
{
    STM32F100State *s = STM32F100_SOC(obj);
    int i;

    object_initialize_child(obj, "armv7m", &s->armv7m, TYPE_ARMV7M);

    for (i = 0; i < STM_NUM_USARTS; i++) {
        object_initialize_child(obj, "usart[*]", &s->usart[i],
                                TYPE_STM32F2XX_USART);
    }

    for (i = 0; i < STM_NUM_SPIS; i++) {
        object_initialize_child(obj, "spi[*]", &s->spi[i], TYPE_STM32F2XX_SPI);
    }

    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);
    s->refclk = qdev_init_clock_in(DEVICE(s), "refclk", NULL, NULL, 0);
}

static void stm32f100_soc_realize(DeviceState *dev_soc, Error **errp)
{
    STM32F100State *s = STM32F100_SOC(dev_soc);
    DeviceState *dev, *armv7m;
    SysBusDevice *busdev;
    int i;

    MemoryRegion *system_memory = get_system_memory();

    /*
     * We use s->refclk internally and only define it with qdev_init_clock_in()
     * so it is correctly parented and not leaked on an init/deinit; it is not
     * intended as an externally exposed clock.
     */
    if (clock_has_source(s->refclk)) {
        error_setg(errp, "refclk clock must not be wired up by the board code");
        return;
    }

    if (!clock_has_source(s->sysclk)) {
        error_setg(errp, "sysclk clock must be wired up by the board code");
        return;
    }

    /*
     * TODO: ideally we should model the SoC RCC and its ability to
     * change the sysclk frequency and define different sysclk sources.
     */

    /* The refclk always runs at frequency HCLK / 8 */
    clock_set_mul_div(s->refclk, 8, 1);
    clock_set_source(s->refclk, s->sysclk);

    /*
     * Init flash region
     * Flash starts at 0x08000000 and then is aliased to boot memory at 0x0
     */
    memory_region_init_rom(&s->flash, OBJECT(dev_soc), "STM32F100.flash",
                           FLASH_SIZE, &error_fatal);
    memory_region_init_alias(&s->flash_alias, OBJECT(dev_soc),
                             "STM32F100.flash.alias", &s->flash, 0, FLASH_SIZE);
    memory_region_add_subregion(system_memory, FLASH_BASE_ADDRESS, &s->flash);
    memory_region_add_subregion(system_memory, 0, &s->flash_alias);

    /* Init SRAM region */
    memory_region_init_ram(&s->sram, NULL, "STM32F100.sram", SRAM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(system_memory, SRAM_BASE_ADDRESS, &s->sram);

    /* Init ARMv7m */
    armv7m = DEVICE(&s->armv7m);
    qdev_prop_set_uint32(armv7m, "num-irq", 61);
    qdev_prop_set_uint8(armv7m, "num-prio-bits", 4);
    qdev_prop_set_string(armv7m, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m3"));
    qdev_prop_set_bit(armv7m, "enable-bitband", true);
    qdev_connect_clock_in(armv7m, "cpuclk", s->sysclk);
    qdev_connect_clock_in(armv7m, "refclk", s->refclk);
    object_property_set_link(OBJECT(&s->armv7m), "memory",
                             OBJECT(get_system_memory()), &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->armv7m), errp)) {
        return;
    }

    /* Attach UART (uses USART registers) and USART controllers */
    for (i = 0; i < STM_NUM_USARTS; i++) {
        dev = DEVICE(&(s->usart[i]));
        qdev_prop_set_chr(dev, "chardev", serial_hd(i));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->usart[i]), errp)) {
            return;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, usart_addr[i]);
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m, usart_irq[i]));
    }

    /* SPI 1 and 2 */
    for (i = 0; i < STM_NUM_SPIS; i++) {
        dev = DEVICE(&(s->spi[i]));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->spi[i]), errp)) {
            return;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, spi_addr[i]);
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m, spi_irq[i]));
    }

    create_unimplemented_device("timer[2]",  0x40000000, 0x400);
    create_unimplemented_device("timer[3]",  0x40000400, 0x400);
    create_unimplemented_device("timer[4]",  0x40000800, 0x400);
    create_unimplemented_device("timer[6]",  0x40001000, 0x400);
    create_unimplemented_device("timer[7]",  0x40001400, 0x400);
    create_unimplemented_device("RTC",       0x40002800, 0x400);
    create_unimplemented_device("WWDG",      0x40002C00, 0x400);
    create_unimplemented_device("IWDG",      0x40003000, 0x400);
    create_unimplemented_device("I2C1",      0x40005400, 0x400);
    create_unimplemented_device("I2C2",      0x40005800, 0x400);
    create_unimplemented_device("BKP",       0x40006C00, 0x400);
    create_unimplemented_device("PWR",       0x40007000, 0x400);
    create_unimplemented_device("DAC",       0x40007400, 0x400);
    create_unimplemented_device("CEC",       0x40007800, 0x400);
    create_unimplemented_device("AFIO",      0x40010000, 0x400);
    create_unimplemented_device("EXTI",      0x40010400, 0x400);
    create_unimplemented_device("GPIOA",     0x40010800, 0x400);
    create_unimplemented_device("GPIOB",     0x40010C00, 0x400);
    create_unimplemented_device("GPIOC",     0x40011000, 0x400);
    create_unimplemented_device("GPIOD",     0x40011400, 0x400);
    create_unimplemented_device("GPIOE",     0x40011800, 0x400);
    create_unimplemented_device("ADC1",      0x40012400, 0x400);
    create_unimplemented_device("timer[1]",  0x40012C00, 0x400);
    create_unimplemented_device("timer[15]", 0x40014000, 0x400);
    create_unimplemented_device("timer[16]", 0x40014400, 0x400);
    create_unimplemented_device("timer[17]", 0x40014800, 0x400);
    create_unimplemented_device("DMA",       0x40020000, 0x400);
    create_unimplemented_device("RCC",       0x40021000, 0x400);
    create_unimplemented_device("Flash Int", 0x40022000, 0x400);
    create_unimplemented_device("CRC",       0x40023000, 0x400);
}

static void stm32f100_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = stm32f100_soc_realize;
    /* No vmstate or reset required: device has no internal state */
}

static const TypeInfo stm32f100_soc_info = {
    .name          = TYPE_STM32F100_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F100State),
    .instance_init = stm32f100_soc_initfn,
    .class_init    = stm32f100_soc_class_init,
};

static void stm32f100_soc_types(void)
{
    type_register_static(&stm32f100_soc_info);
}

type_init(stm32f100_soc_types)
