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

#ifndef STM32_H
#define STM32_H

#include "qemu-timer.h"
#include "arm-misc.h"
#include "qemu-common.h"
#include "sysbus.h"



/* COMMON */
#define BYTE_ACCESS_SIZE 1
#define HALFWORD_ACCESS_SIZE 2
#define WORD_ACCESS_SIZE 4


/* VALUE_BETWEEN is inclusive */
#define VALUE_BETWEEN(value, start, end) ((value >= start) && (value <= end))

#define GET_BIT_MASK(position, value) ((value ? 1 : 0) << position)
#define GET_BIT_MASK_ONE(position) (1 << position)
#define GET_BIT_MASK_ZERO(position) (~(1 << position))
#define GET_BIT_VALUE(value, position) \
                ((value & GET_BIT_MASK_ONE(position)) >> position)
#define IS_BIT_SET(value, position) ((value & GET_BIT_MASK_ONE(position)) != 0)
#define IS_BIT_RESET(value, position) ((value & GET_BIT_MASK_ONE(position)) ==0)
#define SET_BIT(var, position)   var |= GET_BIT_MASK_ONE(position)
#define RESET_BIT(var, position) var &= GET_BIT_MASK_ZERO(position)

/* Can be true, false, 0, or 1 */
#define CHANGE_BIT(var, position, new_value) \
            var = new_value ? \
                    (var | GET_BIT_MASK_ONE(position)) : \
                    (var & GET_BIT_MASK_ZERO(position))
#define CHANGE_BITS(var, start, mask, new_value) \
            var = (var & ~mask) | ((new_value << start) & mask)

void stm32_hw_warn(const char *fmt, ...)
    __attribute__ ((__format__ (__printf__, 1, 2)));





/* PERIPHERALS - COMMON */
/* Indexes used for accessing a GPIO array */
#define STM32_GPIOA_INDEX 0
#define STM32_GPIOB_INDEX 1
#define STM32_GPIOC_INDEX 2
#define STM32_GPIOD_INDEX 3
#define STM32_GPIOE_INDEX 4
#define STM32_GPIOF_INDEX 5
#define STM32_GPIOG_INDEX 6

/* Indexes used for accessing a UART array */
#define STM32_UART1_INDEX 0
#define STM32_UART2_INDEX 1
#define STM32_UART3_INDEX 2
#define STM32_UART4_INDEX 3
#define STM32_UART5_INDEX 4

/* Used for uniquely identifying a peripheral */
typedef int32_t stm32_periph_t;

#define DEFINE_PROP_PERIPH_T DEFINE_PROP_INT32
#define QDEV_PROP_SET_PERIPH_T qdev_prop_set_int32

#define STM32_PERIPH_UNDEFINED -1
#define STM32_RCC 0
#define STM32_GPIOA 1
#define STM32_GPIOB 2
#define STM32_GPIOC 3
#define STM32_GPIOD 4
#define STM32_GPIOE 5
#define STM32_GPIOF 6
#define STM32_GPIOG 7
#define STM32_AFIO 8
#define STM32_UART1 9
#define STM32_UART2 10
#define STM32_UART3 11
#define STM32_UART4 12
#define STM32_UART5 13
#define STM32_ADC1 14
#define STM32_ADC2 15
#define STM32_ADC3 16
#define STM32_DAC 17
#define STM32_TIM1 18
#define STM32_TIM2 19
#define STM32_TIM3 20
#define STM32_TIM4 21
#define STM32_TIM5 22
#define STM32_TIM6 23
#define STM32_TIM7 24
#define STM32_TIM8 25
#define STM32_BKP 26
#define STM32_PWR 27
#define STM32_I2C1 28
#define STM32_I2C2 29
#define STM32_I2S2 30
#define STM32_I2S3 31
#define STM32_WWDG 32
#define STM32_CAN1 33
#define STM32_CAN2 34
#define STM32_CAN 35
#define STM32_USB 36
#define STM32_SPI1 37
#define STM32_SPI2 38
#define STM32_SPI3 39
#define STM32_EXTI 40
#define STM32_SDIO 41
#define STM32_FSMC 42
#define STM32_PERIPH_COUNT 43

const char *stm32_periph_name(stm32_periph_t periph);

/* Convert between a GPIO array index and stm32_periph_t, and vice-versa */
#define STM32_GPIO_INDEX_FROM_PERIPH(gpio_periph) (gpio_periph - STM32_GPIOA)
#define STM32_GPIO_PERIPH_FROM_INDEX(gpio_index) (STM32_GPIOA + gpio_index)




/* REGISTER HELPERS */
/* Macros used for converting a half-word into a word.
 * Assume that memory alignment can be determined by looking at offset
 * i.e. the base address should always be 4 byte aligned.
 * Also assume that odd offsets will never occur
 * i.e. all offsets must be 2 byte aligned.
 */
#define STM32_REG_READH_VALUE(offset, value32) \
          ((offset & 3) ? \
            (value32 & 0xffff0000) >> 16 : \
            value32 & 0x0000ffff)
#define STM32_REG_WRITEH_VALUE(offset, old_value32, new_value32) \
          ((offset & 3) ? \
            (old_value32 & 0x0000ffff) | ((new_value32 & 0x0000ffff) << 16) : \
            (old_value32 & 0xffff0000) | (new_value32 & 0x0000ffff) )

/* Error handlers */
# define STM32_BAD_REG(offset, size)       \
        hw_error("%s: Bad register 0x%x - size %u\n", __FUNCTION__, (int)offset, size)
# define STM32_RO_REG(offset)        \
        hw_error("%s: Read-only register 0x%x\n", __FUNCTION__, (int)offset)
# define STM32_WO_REG(offset)        \
        hw_error("%s: Write-only register 0x%x\n", __FUNCTION__, (int)offset)
# define STM32_NOT_IMPL_REG(offset, size)      \
        hw_error("%s: Not implemented yet 0x%x - size %u\n", __FUNCTION__, (int)offset, size)




/* IRQs */
#define STM32_RCC_IRQ 5

#define STM32_UART1_IRQ 37
#define STM32_UART2_IRQ 38
#define STM32_UART3_IRQ 39
#define STM32_UART4_IRQ 52
#define STM32_UART5_IRQ 53

#define STM32_EXTI0_IRQ 6
#define STM32_EXTI1_IRQ 7
#define STM32_EXTI2_IRQ 8
#define STM32_EXTI3_IRQ 9
#define STM32_EXTI4_IRQ 10
#define STM32_EXTI9_5_IRQ 23
#define STM32_EXTI15_10_IRQ 40
#define STM32_PVD_IRQ 1
#define STM32_RTCAlarm_IRQ 41
#define STM32_OTG_FS_WKUP_IRQ 42
#define STM32_ETH_WKUP_IRQ 62





/* AFIO */
typedef struct Stm32Afio Stm32Afio;

/* AFIO Peripheral Mapping */
#define STM32_USART1_NO_REMAP 0
#define STM32_USART1_REMAP 1

#define STM32_USART2_NO_REMAP 0
#define STM32_USART2_REMAP 1

#define STM32_USART3_NO_REMAP 0
#define STM32_USART3_PARTIAL_REMAP 1
#define STM32_USART3_FULL_REMAP 3

/* Gets the pin mapping for the specified peripheral.  Will return one
 * of the mapping values defined above. */
uint32_t stm32_afio_get_periph_map(Stm32Afio *s, int32_t periph_num);





/* EXTI */
typedef struct Stm32Exti Stm32Exti;

/* Assigns the specified EXTI line to the specified GPIO. */
void stm32_exti_set_gpio(Stm32Exti *s, unsigned exti_line, stm32_periph_t gpio);

/* Unassigns the specified EXTI line from the specified GPIO. */
void stm32_exti_reset_gpio(Stm32Exti *s, unsigned exti_line, stm32_periph_t gpio);





/* GPIO */
typedef struct Stm32Gpio Stm32Gpio;

#define STM32_GPIO_COUNT (STM32_GPIOG - STM32_GPIOA + 1)
#define STM32_GPIO_PIN_COUNT 16

/* Sets the EXTI IRQ for the specified pin.  When a change occurs
 * on this pin, and interrupt will be generated on this IRQ.
 */
void stm32_gpio_set_exti_irq(Stm32Gpio *s, unsigned pin, qemu_irq in_irq);

/* GPIO pin mode */
#define STM32_GPIO_MODE_IN 0
#define STM32_GPIO_MODE_OUT_10MHZ 1
#define STM32_GPIO_MODE_OUT_2MHZ 2
#define STM32_GPIO_MODE_OUT_50MHZ 3
uint8_t stm32_gpio_get_mode_bits(Stm32Gpio *s, unsigned pin);

/* GPIO pin config */
#define STM32_GPIO_IN_ANALOG 0
#define STM32_GPIO_IN_FLOAT 1
#define STM32_GPIO_IN_PULLUPDOWN 2
#define STM32_GPIO_OUT_PUSHPULL 0
#define STM32_GPIO_OUT_OPENDRAIN 1
#define STM32_GPIO_OUT_ALT_PUSHPULL 2
#define STM32_GPIO_OUT_ALT_OPEN 3
uint8_t stm32_gpio_get_config_bits(Stm32Gpio *s, unsigned pin);







/* RCC */
typedef struct Stm32Rcc Stm32Rcc;

/* Checks if the specified peripheral clock is enabled.
 * Generates a hardware error if not.
 */
void stm32_rcc_check_periph_clk(Stm32Rcc *s, stm32_periph_t periph);

/* Sets the IRQ to be called when the specified peripheral clock changes
 * frequency. */
void stm32_rcc_set_periph_clk_irq(
        Stm32Rcc *s,
        stm32_periph_t periph,
        qemu_irq periph_irq);

/* Gets the frequency of the specified peripheral clock. */
uint32_t stm32_rcc_get_periph_freq(
        Stm32Rcc *s,
        stm32_periph_t periph);









/* UART */
#define STM32_UART_COUNT 5

typedef struct Stm32Uart Stm32Uart;

/* Connects the character driver to the specified UART.  The
 * board's pin mapping should be passed in.  This will be used to
 * verify the correct mapping is configured by the software.
 */
void stm32_uart_connect(Stm32Uart *s, CharDriverState *chr,
                        uint32_t afio_board_map);




/* STM32 MICROCONTROLLER - GENERAL */
typedef struct Stm32 Stm32;

/* Initialize the STM32 microcontroller.  Returns arrays
 * of GPIOs and UARTs so that connections can be made. */
void stm32_init(
            ram_addr_t flash_size,
            ram_addr_t ram_size,
            const char *kernel_filename,
            Stm32Gpio **stm32_gpio,
            Stm32Uart **stm32_uart,
            uint32_t osc_freq,
            uint32_t osc32_freq);

#endif /* STM32_H */
