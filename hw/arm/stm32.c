/*
 * STM32 Microcontroller
 *
 * Copyright (C) 2010 Andre Beckus
 *
 * Implementation based on ST Microelectronics "RM0008 Reference Manual Rev 10"
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/arm/stm32.h"
#include "exec/address-spaces.h"
#include "exec/gdbstub.h"

/* DEFINITIONS */

/* COMMON */

void stm32_hw_warn(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "qemu stm32: hardware warning: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    cpu_dump_state(first_cpu, stderr, fprintf, 0);
    va_end(ap);
}




/* PERIPHERALS */

const char *stm32_periph_name_arr[] =
    {"RCC",
     "GPIOA",
     "GPIOB",
     "GPIOC",
     "GPIOD",
     "GPIOE",
     "GPIOF",
     "GPIOG",
     "AFIO",
     "UART1",
     "UART2",
     "UART3",
     "UART4",
     "UART5",
     "ADC1",
     "ADC2",
     "ADC3",
     "DAC",
     "TIM1",
     "TIM2",
     "TIM3",
     "TIM4",
     "TIM5",
     "TIM6",
     "TIM7",
     "TIM8",
     "BKP",
     "PWR",
     "I2C1",
     "I2C2",
     "I2S1",
     "I2S2",
     "WWDG",
     "CAN1",
     "CAN2",
     "CAN",
     "USB",
     "SPI1",
     "SPI2",
     "SPI3"};

const char *stm32_periph_name(stm32_periph_t periph)
{
    assert(periph < STM32_PERIPH_COUNT);

    return stm32_periph_name_arr[periph];
}





/* INITIALIZATION */

/* I copied sysbus_create_varargs and split it into two parts.  This is so that
 * you can set properties before calling the device init function.
 */

static DeviceState *stm32_init_periph(DeviceState *dev, stm32_periph_t periph,
                                        hwaddr addr, qemu_irq irq)
{
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    if (irq) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);
    }
    return dev;
}

static void stm32_create_uart_dev(
        Object *stm32_container,
        stm32_periph_t periph,
        int uart_num,
        DeviceState *rcc_dev,
        DeviceState **gpio_dev,
        DeviceState *afio_dev,
        hwaddr addr,
        qemu_irq irq)
{
    char child_name[8];
    DeviceState *uart_dev = qdev_create(NULL, "stm32-uart");
    QDEV_PROP_SET_PERIPH_T(uart_dev, "periph", periph);
    qdev_prop_set_ptr(uart_dev, "stm32_rcc", rcc_dev);
    qdev_prop_set_ptr(uart_dev, "stm32_gpio", gpio_dev);
    qdev_prop_set_ptr(uart_dev, "stm32_afio", afio_dev);
    snprintf(child_name, sizeof(child_name), "uart[%i]", uart_num);
    object_property_add_child(stm32_container, child_name, OBJECT(uart_dev), NULL);
    stm32_init_periph(uart_dev, periph, addr, irq);
}

static void stm32_create_timer_dev(
        Object *stm32_container,
        stm32_periph_t periph,
        int timer_num,
        DeviceState *rcc_dev,
        DeviceState **gpio_dev,
        DeviceState *afio_dev,
        hwaddr addr,
        qemu_irq *irq,
        int num_irqs)
{
    int i;
    char child_name[9];
    DeviceState *timer_dev = qdev_create(NULL, "stm32-timer");
    QDEV_PROP_SET_PERIPH_T(timer_dev, "periph", periph);
    qdev_prop_set_ptr(timer_dev, "stm32_rcc", rcc_dev);
    qdev_prop_set_ptr(timer_dev, "stm32_gpio", gpio_dev);
    qdev_prop_set_ptr(timer_dev, "stm32_afio", afio_dev);
    snprintf(child_name, sizeof(child_name), "timer[%i]", timer_num);
    object_property_add_child(stm32_container, child_name, OBJECT(timer_dev), NULL);
    stm32_init_periph(timer_dev, periph, addr, NULL);
    for (i = 0; i < num_irqs; i++) {
      if (irq[i]) {
        sysbus_connect_irq(SYS_BUS_DEVICE(timer_dev), 0, irq[i]);
      }
    }
}


void stm32_init(
            ram_addr_t flash_size,
            ram_addr_t ram_size,
            const char *kernel_filename,
            uint32_t osc_freq,
            uint32_t osc32_freq)
{
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *flash_alias_mem = g_malloc(sizeof(MemoryRegion));
    qemu_irq *pic;
    int i;

    Object *stm32_container = container_get(qdev_get_machine(), "/stm32");

    pic = armv7m_init(
              stm32_container,
              address_space_mem,
              flash_size,
              ram_size,
              kernel_filename,
              "cortex-m3");

    /* The STM32 family stores its Flash memory at some base address in memory
     * (0x08000000 for medium density devices), and then aliases it to the
     * boot memory space, which starts at 0x00000000 (the "System Memory" can also
     * be aliased to 0x00000000, but this is not implemented here). The processor
     * executes the code in the aliased memory at 0x00000000.  We need to make a
     * QEMU alias so that reads in the 0x08000000 area are passed through to the
     * 0x00000000 area. Note that this is the opposite of real hardware, where the
     * memory at 0x00000000 passes reads through the "real" flash memory at
     * 0x08000000, but it works the same either way. */
    /* TODO: Parameterize the base address of the aliased memory. */
    memory_region_init_alias(
            flash_alias_mem,
            NULL,
            "stm32-flash-alias-mem",
            address_space_mem,
            0,
            flash_size);
    memory_region_add_subregion(address_space_mem, 0x08000000, flash_alias_mem);

    DeviceState *rcc_dev = qdev_create(NULL, "stm32-rcc");
    qdev_prop_set_uint32(rcc_dev, "osc_freq", osc_freq);
    qdev_prop_set_uint32(rcc_dev, "osc32_freq", osc32_freq);
    object_property_add_child(stm32_container, "rcc", OBJECT(rcc_dev), NULL);
    stm32_init_periph(rcc_dev, STM32_RCC_PERIPH, 0x40021000, pic[STM32_RCC_IRQ]);

    DeviceState **gpio_dev = (DeviceState **)g_malloc0(sizeof(DeviceState *) * STM32_GPIO_COUNT);
    for(i = 0; i < STM32_GPIO_COUNT; i++) {
        char child_name[8];
        stm32_periph_t periph = STM32_GPIOA + i;
        gpio_dev[i] = qdev_create(NULL, TYPE_STM32_GPIO);
        QDEV_PROP_SET_PERIPH_T(gpio_dev[i], "periph", periph);
        qdev_prop_set_ptr(gpio_dev[i], "stm32_rcc", rcc_dev);
        snprintf(child_name, sizeof(child_name), "gpio[%c]", 'a' + i);
        object_property_add_child(stm32_container, child_name, OBJECT(gpio_dev[i]), NULL);
        stm32_init_periph(gpio_dev[i], periph, 0x40010800 + (i * 0x400), NULL);
    }

    DeviceState *exti_dev = qdev_create(NULL, TYPE_STM32_EXTI);
    object_property_add_child(stm32_container, "exti", OBJECT(exti_dev), NULL);
    stm32_init_periph(exti_dev, STM32_EXTI_PERIPH, 0x40010400, NULL);
    SysBusDevice *exti_busdev = SYS_BUS_DEVICE(exti_dev);
    sysbus_connect_irq(exti_busdev, 0, pic[STM32_EXTI0_IRQ]);
    sysbus_connect_irq(exti_busdev, 1, pic[STM32_EXTI1_IRQ]);
    sysbus_connect_irq(exti_busdev, 2, pic[STM32_EXTI2_IRQ]);
    sysbus_connect_irq(exti_busdev, 3, pic[STM32_EXTI3_IRQ]);
    sysbus_connect_irq(exti_busdev, 4, pic[STM32_EXTI4_IRQ]);
    sysbus_connect_irq(exti_busdev, 5, pic[STM32_EXTI9_5_IRQ]);
    sysbus_connect_irq(exti_busdev, 6, pic[STM32_EXTI15_10_IRQ]);
    sysbus_connect_irq(exti_busdev, 7, pic[STM32_PVD_IRQ]);
    sysbus_connect_irq(exti_busdev, 8, pic[STM32_RTCAlarm_IRQ]);
    sysbus_connect_irq(exti_busdev, 9, pic[STM32_OTG_FS_WKUP_IRQ]);

    DeviceState *afio_dev = qdev_create(NULL, TYPE_STM32_AFIO);
    qdev_prop_set_ptr(afio_dev, "stm32_rcc", rcc_dev);
    object_property_set_link(OBJECT(afio_dev), OBJECT(gpio_dev[0]), "gpio[a]", NULL);
    object_property_set_link(OBJECT(afio_dev), OBJECT(gpio_dev[1]), "gpio[b]", NULL);
    object_property_set_link(OBJECT(afio_dev), OBJECT(gpio_dev[2]), "gpio[c]", NULL);
    object_property_set_link(OBJECT(afio_dev), OBJECT(gpio_dev[3]), "gpio[d]", NULL);
    object_property_set_link(OBJECT(afio_dev), OBJECT(gpio_dev[4]), "gpio[e]", NULL);
    object_property_set_link(OBJECT(afio_dev), OBJECT(gpio_dev[5]), "gpio[f]", NULL);
    object_property_set_link(OBJECT(afio_dev), OBJECT(gpio_dev[6]), "gpio[g]", NULL);
    object_property_set_link(OBJECT(afio_dev), OBJECT(exti_dev), "exti", NULL);
    object_property_add_child(stm32_container, "afio", OBJECT(afio_dev), NULL);
    stm32_init_periph(afio_dev, STM32_AFIO_PERIPH, 0x40010000, NULL);

    stm32_create_uart_dev(stm32_container, STM32_UART1, 1, rcc_dev, gpio_dev, afio_dev, 0x40013800, pic[STM32_UART1_IRQ]);
    stm32_create_uart_dev(stm32_container, STM32_UART2, 2, rcc_dev, gpio_dev, afio_dev, 0x40004400, pic[STM32_UART2_IRQ]);
    stm32_create_uart_dev(stm32_container, STM32_UART3, 3, rcc_dev, gpio_dev, afio_dev, 0x40004800, pic[STM32_UART3_IRQ]);
    stm32_create_uart_dev(stm32_container, STM32_UART4, 4, rcc_dev, gpio_dev, afio_dev, 0x40004c00, pic[STM32_UART4_IRQ]);
    stm32_create_uart_dev(stm32_container, STM32_UART5, 5, rcc_dev, gpio_dev, afio_dev, 0x40005000, pic[STM32_UART5_IRQ]);

    qemu_irq tim1_irqs[] = { pic[TIM1_BRK_IRQn], pic[TIM1_UP_IRQn], pic[TIM1_TRG_COM_IRQn],
                             pic[TIM1_CC_IRQn]};
    stm32_create_timer_dev(stm32_container, STM32_TIM1, 1, rcc_dev, gpio_dev, afio_dev, 0x40012C00, tim1_irqs, 5);

    stm32_create_timer_dev(stm32_container, STM32_TIM2, 1, rcc_dev, gpio_dev, afio_dev, 0x40000000, &pic[TIM2_IRQn], 1);
    stm32_create_timer_dev(stm32_container, STM32_TIM3, 1, rcc_dev, gpio_dev, afio_dev, 0x40000400, &pic[TIM3_IRQn], 1);
    stm32_create_timer_dev(stm32_container, STM32_TIM4, 1, rcc_dev, gpio_dev, afio_dev, 0x40000800, &pic[TIM4_IRQn], 1);
    stm32_create_timer_dev(stm32_container, STM32_TIM5, 1, rcc_dev, gpio_dev, afio_dev, 0x40000C00, &pic[TIM5_IRQn], 1);
}
