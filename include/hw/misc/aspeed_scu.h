/*
 * ASPEED System Control Unit
 *
 * Andrew Jeffery <andrew@aj.id.au>
 *
 * Copyright 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */
#ifndef ASPEED_SCU_H
#define ASPEED_SCU_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_ASPEED_SCU "aspeed.scu"
OBJECT_DECLARE_TYPE(AspeedSCUState, AspeedSCUClass, ASPEED_SCU)
#define TYPE_ASPEED_2400_SCU TYPE_ASPEED_SCU "-ast2400"
#define TYPE_ASPEED_2500_SCU TYPE_ASPEED_SCU "-ast2500"
#define TYPE_ASPEED_2600_SCU TYPE_ASPEED_SCU "-ast2600"
#define TYPE_ASPEED_1030_SCU TYPE_ASPEED_SCU "-ast1030"

#define ASPEED_SCU_NR_REGS (0x1A8 >> 2)
#define ASPEED_AST2600_SCU_NR_REGS (0xE20 >> 2)

struct AspeedSCUState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t regs[ASPEED_AST2600_SCU_NR_REGS];
    uint32_t silicon_rev;
    uint32_t hw_strap1;
    uint32_t hw_strap2;
    uint32_t hw_prot_key;
};

#define AST2400_A0_SILICON_REV   0x02000303U
#define AST2400_A1_SILICON_REV   0x02010303U
#define AST2500_A0_SILICON_REV   0x04000303U
#define AST2500_A1_SILICON_REV   0x04010303U
#define AST2600_A0_SILICON_REV   0x05000303U
#define AST2600_A1_SILICON_REV   0x05010303U
#define AST2600_A2_SILICON_REV   0x05020303U
#define AST2600_A3_SILICON_REV   0x05030303U
#define AST1030_A0_SILICON_REV   0x80000000U
#define AST1030_A1_SILICON_REV   0x80010000U

#define ASPEED_IS_AST2500(si_rev)     ((((si_rev) >> 24) & 0xff) == 0x04)

bool is_supported_silicon_rev(uint32_t silicon_rev);


struct AspeedSCUClass {
    SysBusDeviceClass parent_class;

    const uint32_t *resets;
    uint32_t (*calc_hpll)(AspeedSCUState *s, uint32_t hpll_reg);
    uint32_t (*get_apb)(AspeedSCUState *s);
    uint32_t apb_divider;
    uint32_t nr_regs;
    bool clkin_25Mhz;
    const MemoryRegionOps *ops;
};

#define ASPEED_SCU_PROT_KEY      0x1688A8A8

uint32_t aspeed_scu_get_apb_freq(AspeedSCUState *s);

/*
 * Extracted from Aspeed SDK v00.03.21. Fixes and extra definitions
 * were added.
 *
 * Original header file :
 *    arch/arm/mach-aspeed/include/mach/regs-scu.h
 *
 *    Copyright (C) 2012-2020  ASPEED Technology Inc.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License version 2 as
 *    published by the Free Software Foundation.
 *
 *      History      :
 *       1. 2012/12/29 Ryan Chen Create
 */

/* SCU08   Clock Selection Register
 *
 *  31     Enable Video Engine clock dynamic slow down
 *  30:28  Video Engine clock slow down setting
 *  27     2D Engine GCLK clock source selection
 *  26     2D Engine GCLK clock throttling enable
 *  25:23  APB PCLK divider selection
 *  22:20  LPC Host LHCLK divider selection
 *  19     LPC Host LHCLK clock generation/output enable control
 *  18:16  MAC AHB bus clock divider selection
 *  15     SD/SDIO clock running enable
 *  14:12  SD/SDIO divider selection
 *  11     Reserved
 *  10:8   Video port output clock delay control bit
 *  7      ARM CPU/AHB clock slow down enable
 *  6:4    ARM CPU/AHB clock slow down setting
 *  3:2    ECLK clock source selection
 *  1      CPU/AHB clock slow down idle timer
 *  0      CPU/AHB clock dynamic slow down enable (defined in bit[6:4])
 */
#define SCU_CLK_GET_PCLK_DIV(x)                    (((x) >> 23) & 0x7)

/* SCU24   H-PLL Parameter Register (for Aspeed AST2400 SOC)
 *
 *  18     H-PLL parameter selection
 *           0: Select H-PLL by strapping resistors
 *           1: Select H-PLL by the programmed registers (SCU24[17:0])
 *  17     Enable H-PLL bypass mode
 *  16     Turn off H-PLL
 *  10:5   H-PLL Numerator
 *  4      H-PLL Output Divider
 *  3:0    H-PLL Denumerator
 *
 *  (Output frequency) = 24MHz * (2-OD) * [(Numerator+2) / (Denumerator+1)]
 */

#define SCU_AST2400_H_PLL_PROGRAMMED               (0x1 << 18)
#define SCU_AST2400_H_PLL_BYPASS_EN                (0x1 << 17)
#define SCU_AST2400_H_PLL_OFF                      (0x1 << 16)

/* SCU24   H-PLL Parameter Register (for Aspeed AST2500 SOC)
 *
 *  21     Enable H-PLL reset
 *  20     Enable H-PLL bypass mode
 *  19     Turn off H-PLL
 *  18:13  H-PLL Post Divider
 *  12:5   H-PLL Numerator (M)
 *  4:0    H-PLL Denumerator (N)
 *
 *  (Output frequency) = CLKIN(24MHz) * [(M+1) / (N+1)] / (P+1)
 *
 * The default frequency is 792Mhz when CLKIN = 24MHz
 */

#define SCU_H_PLL_BYPASS_EN                        (0x1 << 20)
#define SCU_H_PLL_OFF                              (0x1 << 19)

/* SCU70  Hardware Strapping Register definition (for Aspeed AST2400 SOC)
 *
 * 31:29  Software defined strapping registers
 * 28:27  DRAM size setting (for VGA driver use)
 * 26:24  DRAM configuration setting
 * 23     Enable 25 MHz reference clock input
 * 22     Enable GPIOE pass-through mode
 * 21     Enable GPIOD pass-through mode
 * 20     Disable LPC to decode SuperIO 0x2E/0x4E address
 * 19     Disable ACPI function
 * 23,18  Clock source selection
 * 17     Enable BMC 2nd boot watchdog timer
 * 16     SuperIO configuration address selection
 * 15     VGA Class Code selection
 * 14     Enable LPC dedicated reset pin function
 * 13:12  SPI mode selection
 * 11:10  CPU/AHB clock frequency ratio selection
 * 9:8    H-PLL default clock frequency selection
 * 7      Define MAC#2 interface
 * 6      Define MAC#1 interface
 * 5      Enable VGA BIOS ROM
 * 4      Boot flash memory extended option
 * 3:2    VGA memory size selection
 * 1:0    BMC CPU boot code selection
 */
#define SCU_AST2400_HW_STRAP_SW_DEFINE(x)          ((x) << 29)
#define SCU_AST2400_HW_STRAP_SW_DEFINE_MASK        (0x7 << 29)

#define SCU_AST2400_HW_STRAP_DRAM_SIZE(x)          ((x) << 27)
#define SCU_AST2400_HW_STRAP_DRAM_SIZE_MASK        (0x3 << 27)
#define     DRAM_SIZE_64MB                             0
#define     DRAM_SIZE_128MB                            1
#define     DRAM_SIZE_256MB                            2
#define     DRAM_SIZE_512MB                            3

#define SCU_AST2400_HW_STRAP_DRAM_CONFIG(x)        ((x) << 24)
#define SCU_AST2400_HW_STRAP_DRAM_CONFIG_MASK      (0x7 << 24)

#define SCU_HW_STRAP_GPIOE_PT_EN                   (0x1 << 22)
#define SCU_HW_STRAP_GPIOD_PT_EN                   (0x1 << 21)
#define SCU_HW_STRAP_LPC_DEC_SUPER_IO              (0x1 << 20)
#define SCU_AST2400_HW_STRAP_ACPI_DIS              (0x1 << 19)

/* bit 23, 18 [1,0] */
#define SCU_AST2400_HW_STRAP_SET_CLK_SOURCE(x)     (((((x) & 0x3) >> 1) << 23) \
                                                    | (((x) & 0x1) << 18))
#define SCU_AST2400_HW_STRAP_GET_CLK_SOURCE(x)     (((((x) >> 23) & 0x1) << 1) \
                                                    | (((x) >> 18) & 0x1))
#define SCU_AST2400_HW_STRAP_CLK_SOURCE_MASK       ((0x1 << 23) | (0x1 << 18))
#define SCU_HW_STRAP_CLK_25M_IN                    (0x1 << 23)
#define     AST2400_CLK_24M_IN                         0
#define     AST2400_CLK_48M_IN                         1
#define     AST2400_CLK_25M_IN_24M_USB_CKI             2
#define     AST2400_CLK_25M_IN_48M_USB_CKI             3

#define SCU_HW_STRAP_CLK_48M_IN                    (0x1 << 18)
#define SCU_HW_STRAP_2ND_BOOT_WDT                  (0x1 << 17)
#define SCU_HW_STRAP_SUPER_IO_CONFIG               (0x1 << 16)
#define SCU_HW_STRAP_VGA_CLASS_CODE                (0x1 << 15)
#define SCU_HW_STRAP_LPC_RESET_PIN                 (0x1 << 14)

#define SCU_HW_STRAP_SPI_MODE(x)                   ((x) << 12)
#define SCU_HW_STRAP_SPI_MODE_MASK                 (0x3 << 12)
#define     SCU_HW_STRAP_SPI_DIS                       0
#define     SCU_HW_STRAP_SPI_MASTER                    1
#define     SCU_HW_STRAP_SPI_M_S_EN                    2
#define     SCU_HW_STRAP_SPI_PASS_THROUGH              3

#define SCU_AST2400_HW_STRAP_SET_CPU_AHB_RATIO(x)  ((x) << 10)
#define SCU_AST2400_HW_STRAP_GET_CPU_AHB_RATIO(x)  (((x) >> 10) & 3)
#define SCU_AST2400_HW_STRAP_CPU_AHB_RATIO_MASK    (0x3 << 10)
#define     AST2400_CPU_AHB_RATIO_1_1                  0
#define     AST2400_CPU_AHB_RATIO_2_1                  1
#define     AST2400_CPU_AHB_RATIO_4_1                  2
#define     AST2400_CPU_AHB_RATIO_3_1                  3

#define SCU_AST2400_HW_STRAP_GET_H_PLL_CLK(x)      (((x) >> 8) & 0x3)
#define SCU_AST2400_HW_STRAP_H_PLL_CLK_MASK        (0x3 << 8)
#define     AST2400_CPU_384MHZ                         0
#define     AST2400_CPU_360MHZ                         1
#define     AST2400_CPU_336MHZ                         2
#define     AST2400_CPU_408MHZ                         3

#define SCU_HW_STRAP_MAC1_RGMII                    (0x1 << 7)
#define SCU_HW_STRAP_MAC0_RGMII                    (0x1 << 6)
#define SCU_HW_STRAP_VGA_BIOS_ROM                  (0x1 << 5)
#define SCU_HW_STRAP_SPI_WIDTH                     (0x1 << 4)

#define SCU_HW_STRAP_VGA_SIZE_GET(x)               (((x) >> 2) & 0x3)
#define SCU_HW_STRAP_VGA_MASK                      (0x3 << 2)
#define SCU_HW_STRAP_VGA_SIZE_SET(x)               ((x) << 2)
#define     VGA_8M_DRAM                                0
#define     VGA_16M_DRAM                               1
#define     VGA_32M_DRAM                               2
#define     VGA_64M_DRAM                               3

#define SCU_AST2400_HW_STRAP_BOOT_MODE(x)          (x)
#define     AST2400_NOR_BOOT                           0
#define     AST2400_NAND_BOOT                          1
#define     AST2400_SPI_BOOT                           2
#define     AST2400_DIS_BOOT                           3

/*
 * SCU70  Hardware strapping register definition (for Aspeed AST2500
 *        SoC and higher)
 *
 * 31     Enable SPI Flash Strap Auto Fetch Mode
 * 30     Enable GPIO Strap Mode
 * 29     Select UART Debug Port
 * 28     Reserved (1)
 * 27     Enable fast reset mode for ARM ICE debugger
 * 26     Enable eSPI flash mode
 * 25     Enable eSPI mode
 * 24     Select DDR4 SDRAM
 * 23     Select 25 MHz reference clock input mode
 * 22     Enable GPIOE pass-through mode
 * 21     Enable GPIOD pass-through mode
 * 20     Disable LPC to decode SuperIO 0x2E/0x4E address
 * 19     Enable ACPI function
 * 18     Select USBCKI input frequency
 * 17     Enable BMC 2nd boot watchdog timer
 * 16     SuperIO configuration address selection
 * 15     VGA Class Code selection
 * 14     Select dedicated LPC reset input
 * 13:12  SPI mode selection
 * 11:9   AXI/AHB clock frequency ratio selection
 * 8      Reserved (0)
 * 7      Define MAC#2 interface
 * 6      Define MAC#1 interface
 * 5      Enable dedicated VGA BIOS ROM
 * 4      Reserved (0)
 * 3:2    VGA memory size selection
 * 1      Reserved (1)
 * 0      Disable CPU boot
 */
#define SCU_AST2500_HW_STRAP_SPI_AUTOFETCH_ENABLE  (0x1 << 31)
#define SCU_AST2500_HW_STRAP_GPIO_STRAP_ENABLE     (0x1 << 30)
#define SCU_AST2500_HW_STRAP_UART_DEBUG            (0x1 << 29)
#define     UART_DEBUG_UART1                           0
#define     UART_DEBUG_UART5                           1
#define SCU_AST2500_HW_STRAP_RESERVED28            (0x1 << 28)

#define SCU_AST2500_HW_STRAP_FAST_RESET_DBG        (0x1 << 27)
#define SCU_AST2500_HW_STRAP_ESPI_FLASH_ENABLE     (0x1 << 26)
#define SCU_AST2500_HW_STRAP_ESPI_ENABLE           (0x1 << 25)
#define SCU_AST2500_HW_STRAP_DDR4_ENABLE           (0x1 << 24)
#define SCU_AST2500_HW_STRAP_25HZ_CLOCK_MODE       (0x1 << 23)

#define SCU_AST2500_HW_STRAP_ACPI_ENABLE           (0x1 << 19)
#define SCU_AST2500_HW_STRAP_USBCKI_FREQ           (0x1 << 18)
#define     USBCKI_FREQ_24MHZ                          0
#define     USBCKI_FREQ_28MHZ                          1

#define SCU_AST2500_HW_STRAP_SET_AXI_AHB_RATIO(x)  ((x) << 9)
#define SCU_AST2500_HW_STRAP_GET_AXI_AHB_RATIO(x)  (((x) >> 9) & 7)
#define SCU_AST2500_HW_STRAP_CPU_AXI_RATIO_MASK    (0x7 << 9)
#define     AXI_AHB_RATIO_UNDEFINED                    0
#define     AXI_AHB_RATIO_2_1                          1
#define     AXI_AHB_RATIO_3_1                          2
#define     AXI_AHB_RATIO_4_1                          3
#define     AXI_AHB_RATIO_5_1                          4
#define     AXI_AHB_RATIO_6_1                          5
#define     AXI_AHB_RATIO_7_1                          6
#define     AXI_AHB_RATIO_8_1                          7

#define SCU_AST2500_HW_STRAP_RESERVED1             (0x1 << 1)
#define SCU_AST2500_HW_STRAP_DIS_BOOT              (0x1 << 0)

#define AST2500_HW_STRAP1_DEFAULTS (                                    \
        SCU_AST2500_HW_STRAP_RESERVED28 |                               \
        SCU_HW_STRAP_2ND_BOOT_WDT |                                     \
        SCU_HW_STRAP_VGA_CLASS_CODE |                                   \
        SCU_HW_STRAP_LPC_RESET_PIN |                                    \
        SCU_AST2500_HW_STRAP_SET_AXI_AHB_RATIO(AXI_AHB_RATIO_2_1) |     \
        SCU_HW_STRAP_VGA_SIZE_SET(VGA_16M_DRAM) |                       \
        SCU_AST2500_HW_STRAP_RESERVED1)

/*
 * SCU200   H-PLL Parameter Register (for Aspeed AST2600 SOC)
 *
 *  28:26  H-PLL Parameters
 *  25     Enable H-PLL reset
 *  24     Enable H-PLL bypass mode
 *  23     Turn off H-PLL
 *  22:19  H-PLL Post Divider (P)
 *  18:13  H-PLL Numerator (M)
 *  12:0   H-PLL Denumerator (N)
 *
 *  (Output frequency) = CLKIN(25MHz) * [(M+1) / (N+1)] / (P+1)
 *
 * The default frequency is 1200Mhz when CLKIN = 25MHz
 */
#define SCU_AST2600_H_PLL_BYPASS_EN                        (0x1 << 24)
#define SCU_AST2600_H_PLL_OFF                              (0x1 << 23)

/*
 * SCU310   Clock Selection Register Set 4 (for Aspeed AST1030 SOC)
 *
 *  31     I3C Clock Source selection
 *  30:28  I3C clock divider selection
 *  26:24  MAC AHB clock divider selection
 *  22:20  RGMII 125MHz clock divider ration
 *  19:16  RGMII 50MHz clock divider ration
 *  15     LHCLK clock generation/output enable control
 *  14:12  LHCLK divider selection
 *  11:8   APB Bus PCLK divider selection
 *  7      Select PECI clock source
 *  6      Select UART debug port clock source
 *  5      Select UART6 clock source
 *  4      Select UART5 clock source
 *  3      Select UART4 clock source
 *  2      Select UART3 clock source
 *  1      Select UART2 clock source
 *  0      Select UART1 clock source
 */
#define SCU_AST1030_CLK_GET_PCLK_DIV(x)                    (((x) >> 8) & 0xf)

#endif /* ASPEED_SCU_H */
