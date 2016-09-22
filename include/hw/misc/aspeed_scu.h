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

#define TYPE_ASPEED_SCU "aspeed.scu"
#define ASPEED_SCU(obj) OBJECT_CHECK(AspeedSCUState, (obj), TYPE_ASPEED_SCU)

#define ASPEED_SCU_NR_REGS (0x1A8 >> 2)

typedef struct AspeedSCUState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t regs[ASPEED_SCU_NR_REGS];
    uint32_t silicon_rev;
    uint32_t hw_strap1;
    uint32_t hw_strap2;
} AspeedSCUState;

#define AST2400_A0_SILICON_REV   0x02000303U
#define AST2500_A0_SILICON_REV   0x04000303U

extern bool is_supported_silicon_rev(uint32_t silicon_rev);

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

/* Hardware Strapping Register definition (for Aspeed AST2400 SOC)
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
#define     AST2400_CLK_25M_IN                         (0x1 << 23)
#define     AST2400_CLK_24M_IN                         0
#define     AST2400_CLK_48M_IN                         1
#define     AST2400_CLK_25M_IN_24M_USB_CKI             2
#define     AST2400_CLK_25M_IN_48M_USB_CKI             3

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

#endif /* ASPEED_SCU_H */
