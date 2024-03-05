/*
 * STM32L4X5 RCC (Reset and clock control)
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * The reference used is the STMicroElectronics RM0351 Reference manual
 * for STM32L4x5 and STM32L4x6 advanced Arm ® -based 32-bit MCUs.
 *
 * Inspired by the BCM2835 CPRMAN clock manager by Luc Michel.
 */

#ifndef HW_STM32L4X5_RCC_H
#define HW_STM32L4X5_RCC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_STM32L4X5_RCC "stm32l4x5-rcc"
OBJECT_DECLARE_SIMPLE_TYPE(Stm32l4x5RccState, STM32L4X5_RCC)

/* In the Stm32l4x5 clock tree, mux have at most 7 sources */
#define RCC_NUM_CLOCK_MUX_SRC 7

typedef enum PllCommonChannels {
    RCC_PLL_COMMON_CHANNEL_P = 0,
    RCC_PLL_COMMON_CHANNEL_Q = 1,
    RCC_PLL_COMMON_CHANNEL_R = 2,

    RCC_NUM_CHANNEL_PLL_OUT = 3
} PllCommonChannels;

/* NB: Prescaler are assimilated to mux with one source and one output */
typedef enum RccClockMux {
    /* Internal muxes that arent't exposed publicly to other peripherals */
    RCC_CLOCK_MUX_SYSCLK,
    RCC_CLOCK_MUX_PLL_INPUT,
    RCC_CLOCK_MUX_HCLK,
    RCC_CLOCK_MUX_PCLK1,
    RCC_CLOCK_MUX_PCLK2,
    RCC_CLOCK_MUX_HSE_OVER_32,
    RCC_CLOCK_MUX_LCD_AND_RTC_COMMON,

    /* Muxes with a publicly available output */
    RCC_CLOCK_MUX_CORTEX_REFCLK,
    RCC_CLOCK_MUX_USART1,
    RCC_CLOCK_MUX_USART2,
    RCC_CLOCK_MUX_USART3,
    RCC_CLOCK_MUX_UART4,
    RCC_CLOCK_MUX_UART5,
    RCC_CLOCK_MUX_LPUART1,
    RCC_CLOCK_MUX_I2C1,
    RCC_CLOCK_MUX_I2C2,
    RCC_CLOCK_MUX_I2C3,
    RCC_CLOCK_MUX_LPTIM1,
    RCC_CLOCK_MUX_LPTIM2,
    RCC_CLOCK_MUX_SWPMI1,
    RCC_CLOCK_MUX_MCO,
    RCC_CLOCK_MUX_LSCO,
    RCC_CLOCK_MUX_DFSDM1,
    RCC_CLOCK_MUX_ADC,
    RCC_CLOCK_MUX_CLK48,
    RCC_CLOCK_MUX_SAI1,
    RCC_CLOCK_MUX_SAI2,

    /*
     * Mux that have only one input and one output assigned to as peripheral.
     * They could be direct lines but it is simpler
     * to use the same logic for all outputs.
     */
    /* - AHB1 */
    RCC_CLOCK_MUX_TSC,
    RCC_CLOCK_MUX_CRC,
    RCC_CLOCK_MUX_FLASH,
    RCC_CLOCK_MUX_DMA2,
    RCC_CLOCK_MUX_DMA1,

    /* - AHB2 */
    RCC_CLOCK_MUX_RNG,
    RCC_CLOCK_MUX_AES,
    RCC_CLOCK_MUX_OTGFS,
    RCC_CLOCK_MUX_GPIOA,
    RCC_CLOCK_MUX_GPIOB,
    RCC_CLOCK_MUX_GPIOC,
    RCC_CLOCK_MUX_GPIOD,
    RCC_CLOCK_MUX_GPIOE,
    RCC_CLOCK_MUX_GPIOF,
    RCC_CLOCK_MUX_GPIOG,
    RCC_CLOCK_MUX_GPIOH,

    /* - AHB3 */
    RCC_CLOCK_MUX_QSPI,
    RCC_CLOCK_MUX_FMC,

    /* - APB1 */
    RCC_CLOCK_MUX_OPAMP,
    RCC_CLOCK_MUX_DAC1,
    RCC_CLOCK_MUX_PWR,
    RCC_CLOCK_MUX_CAN1,
    RCC_CLOCK_MUX_SPI3,
    RCC_CLOCK_MUX_SPI2,
    RCC_CLOCK_MUX_WWDG,
    RCC_CLOCK_MUX_LCD,
    RCC_CLOCK_MUX_TIM7,
    RCC_CLOCK_MUX_TIM6,
    RCC_CLOCK_MUX_TIM5,
    RCC_CLOCK_MUX_TIM4,
    RCC_CLOCK_MUX_TIM3,
    RCC_CLOCK_MUX_TIM2,

    /* - APB2 */
    RCC_CLOCK_MUX_TIM17,
    RCC_CLOCK_MUX_TIM16,
    RCC_CLOCK_MUX_TIM15,
    RCC_CLOCK_MUX_TIM8,
    RCC_CLOCK_MUX_SPI1,
    RCC_CLOCK_MUX_TIM1,
    RCC_CLOCK_MUX_SDMMC1,
    RCC_CLOCK_MUX_FW,
    RCC_CLOCK_MUX_SYSCFG,

    /* - BDCR */
    RCC_CLOCK_MUX_RTC,

    /* - OTHER */
    RCC_CLOCK_MUX_CORTEX_FCLK,

    RCC_NUM_CLOCK_MUX
} RccClockMux;

typedef enum RccPll {
    RCC_PLL_PLL,
    RCC_PLL_PLLSAI1,
    RCC_PLL_PLLSAI2,

    RCC_NUM_PLL
} RccPll;

typedef struct RccClockMuxState {
    DeviceState parent_obj;

    RccClockMux id;
    Clock *srcs[RCC_NUM_CLOCK_MUX_SRC];
    Clock *out;
    bool enabled;
    uint32_t src;
    uint32_t multiplier;
    uint32_t divider;

    /*
     * Used by clock srcs update callback to retrieve both the clock and the
     * source number.
     */
    struct RccClockMuxState *backref[RCC_NUM_CLOCK_MUX_SRC];
} RccClockMuxState;

typedef struct RccPllState {
    DeviceState parent_obj;

    RccPll id;
    Clock *in;
    uint32_t vco_multiplier;
    Clock *channels[RCC_NUM_CHANNEL_PLL_OUT];
    /* Global pll enabled flag */
    bool enabled;
    /* 'enabled' refers to the runtime configuration */
    bool channel_enabled[RCC_NUM_CHANNEL_PLL_OUT];
    /*
     * 'exists' refers to the physical configuration
     * It should only be set at pll initialization.
     * e.g. pllsai2 doesn't have a Q output.
     */
    bool channel_exists[RCC_NUM_CHANNEL_PLL_OUT];
    uint32_t channel_divider[RCC_NUM_CHANNEL_PLL_OUT];
} RccPllState;

struct Stm32l4x5RccState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t cr;
    uint32_t icscr;
    uint32_t cfgr;
    uint32_t pllcfgr;
    uint32_t pllsai1cfgr;
    uint32_t pllsai2cfgr;
    uint32_t cier;
    uint32_t cifr;
    uint32_t ahb1rstr;
    uint32_t ahb2rstr;
    uint32_t ahb3rstr;
    uint32_t apb1rstr1;
    uint32_t apb1rstr2;
    uint32_t apb2rstr;
    uint32_t ahb1enr;
    uint32_t ahb2enr;
    uint32_t ahb3enr;
    uint32_t apb1enr1;
    uint32_t apb1enr2;
    uint32_t apb2enr;
    uint32_t ahb1smenr;
    uint32_t ahb2smenr;
    uint32_t ahb3smenr;
    uint32_t apb1smenr1;
    uint32_t apb1smenr2;
    uint32_t apb2smenr;
    uint32_t ccipr;
    uint32_t bdcr;
    uint32_t csr;

    /* Clock sources */
    Clock *gnd;
    Clock *hsi16_rc;
    Clock *msi_rc;
    Clock *hse;
    Clock *lsi_rc;
    Clock *lse_crystal;
    Clock *sai1_extclk;
    Clock *sai2_extclk;

    /* PLLs */
    RccPllState plls[RCC_NUM_PLL];

    /* Muxes ~= outputs */
    RccClockMuxState clock_muxes[RCC_NUM_CLOCK_MUX];

    qemu_irq irq;
    uint64_t hse_frequency;
    uint64_t sai1_extclk_frequency;
    uint64_t sai2_extclk_frequency;
};

#endif /* HW_STM32L4X5_RCC_H */
