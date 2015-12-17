/*
 * Freescale i.MX25 SoC emulation
 *
 * Copyright (C) 2015 Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#ifndef FSL_IMX25_H
#define FSL_IMX25_H

#include "hw/arm/arm.h"
#include "hw/intc/imx_avic.h"
#include "hw/misc/imx25_ccm.h"
#include "hw/char/imx_serial.h"
#include "hw/timer/imx_gpt.h"
#include "hw/timer/imx_epit.h"
#include "hw/net/imx_fec.h"
#include "hw/i2c/imx_i2c.h"
#include "hw/gpio/imx_gpio.h"
#include "exec/memory.h"

#define TYPE_FSL_IMX25 "fsl,imx25"
#define FSL_IMX25(obj) OBJECT_CHECK(FslIMX25State, (obj), TYPE_FSL_IMX25)

#define FSL_IMX25_NUM_UARTS 5
#define FSL_IMX25_NUM_GPTS 4
#define FSL_IMX25_NUM_EPITS 2
#define FSL_IMX25_NUM_I2CS 3
#define FSL_IMX25_NUM_GPIOS 4

typedef struct FslIMX25State {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    ARMCPU         cpu;
    IMXAVICState   avic;
    IMX25CCMState  ccm;
    IMXSerialState uart[FSL_IMX25_NUM_UARTS];
    IMXGPTState    gpt[FSL_IMX25_NUM_GPTS];
    IMXEPITState   epit[FSL_IMX25_NUM_EPITS];
    IMXFECState    fec;
    IMXI2CState    i2c[FSL_IMX25_NUM_I2CS];
    IMXGPIOState   gpio[FSL_IMX25_NUM_GPIOS];
    MemoryRegion   rom[2];
    MemoryRegion   iram;
    MemoryRegion   iram_alias;
} FslIMX25State;

/**
 * i.MX25 memory map
 ****************************************************************
 * 0x0000_0000 0x0000_3FFF 16 Kbytes    ROM (36 Kbytes)
 * 0x0000_4000 0x0040_3FFF 4 Mbytes     Reserved
 * 0x0040_4000 0x0040_8FFF 20 Kbytes    ROM (36 Kbytes)
 * 0x0040_9000 0x0FFF_FFFF 252 Mbytes (minus 36 Kbytes) Reserved
 * 0x1000_0000 0x1FFF_FFFF 256 Mbytes   Reserved
 * 0x2000_0000 0x2FFF_FFFF 256 Mbytes   Reserved
 * 0x3000_0000 0x3FFF_FFFF 256 Mbytes   Reserved
 * 0x4000_0000 0x43EF_FFFF 63 Mbytes    Reserved
 * 0x43F0_0000 0x43F0_3FFF 16 Kbytes    AIPS A control registers
 * 0x43F0_4000 0x43F0_7FFF 16 Kbytes    ARM926 platform MAX
 * 0x43F0_8000 0x43F0_BFFF 16 Kbytes    ARM926 platform CLKCTL
 * 0x43F0_C000 0x43F0_FFFF 16 Kbytes    ARM926 platform ETB registers
 * 0x43F1_0000 0x43F1_3FFF 16 Kbytes    ARM926 platform ETB memory
 * 0x43F1_4000 0x43F1_7FFF 16 Kbytes    ARM926 platform AAPE registers
 * 0x43F1_8000 0x43F7_FFFF 416 Kbytes   Reserved
 * 0x43F8_0000 0x43F8_3FFF 16 Kbytes    I2C-1
 * 0x43F8_4000 0x43F8_7FFF 16 Kbytes    I2C-3
 * 0x43F8_8000 0x43F8_BFFF 16 Kbytes    CAN-1
 * 0x43F8_C000 0x43F8_FFFF 16 Kbytes    CAN-2
 * 0x43F9_0000 0x43F9_3FFF 16 Kbytes    UART-1
 * 0x43F9_4000 0x43F9_7FFF 16 Kbytes    UART-2
 * 0x43F9_8000 0x43F9_BFFF 16 Kbytes    I2C-2
 * 0x43F9_C000 0x43F9_FFFF 16 Kbytes    1-Wire
 * 0x43FA_0000 0x43FA_3FFF 16 Kbytes    ATA (CPU side)
 * 0x43FA_4000 0x43FA_7FFF 16 Kbytes    CSPI-1
 * 0x43FA_8000 0x43FA_BFFF 16 Kbytes    KPP
 * 0x43FA_C000 0x43FA_FFFF 16 Kbytes    IOMUXC
 * 0x43FB_0000 0x43FB_3FFF 16 Kbytes    AUDMUX
 * 0x43FB_4000 0x43FB_7FFF 16 Kbytes    Reserved
 * 0x43FB_8000 0x43FB_BFFF 16 Kbytes    ECT (IP BUS A)
 * 0x43FB_C000 0x43FB_FFFF 16 Kbytes    ECT (IP BUS B)
 * 0x43FC_0000 0x43FF_FFFF 256 Kbytes   Reserved AIPS A off-platform slots
 * 0x4400_0000 0x4FFF_FFFF 192 Mbytes   Reserved
 * 0x5000_0000 0x5000_3FFF 16 Kbytes    SPBA base address
 * 0x5000_4000 0x5000_7FFF 16 Kbytes    CSPI-3
 * 0x5000_8000 0x5000_BFFF 16 Kbytes    UART-4
 * 0x5000_C000 0x5000_FFFF 16 Kbytes    UART-3
 * 0x5001_0000 0x5001_3FFF 16 Kbytes    CSPI-2
 * 0x5001_4000 0x5001_7FFF 16 Kbytes    SSI-2
 * 0x5001_C000 0x5001_FFFF 16 Kbytes    Reserved
 * 0x5002_0000 0x5002_3FFF 16 Kbytes    ATA
 * 0x5002_4000 0x5002_7FFF 16 Kbytes    SIM-1
 * 0x5002_8000 0x5002_BFFF 16 Kbytes    SIM-2
 * 0x5002_C000 0x5002_FFFF 16 Kbytes    UART-5
 * 0x5003_0000 0x5003_3FFF 16 Kbytes    TSC
 * 0x5003_4000 0x5003_7FFF 16 Kbytes    SSI-1
 * 0x5003_8000 0x5003_BFFF 16 Kbytes    FEC
 * 0x5003_C000 0x5003_FFFF 16 Kbytes    SPBA registers
 * 0x5004_0000 0x51FF_FFFF 32 Mbytes (minus 256 Kbytes)
 * 0x5200_0000 0x53EF_FFFF 31 Mbytes    Reserved
 * 0x53F0_0000 0x53F0_3FFF 16 Kbytes    AIPS B control registers
 * 0x53F0_4000 0x53F7_FFFF 496 Kbytes   Reserved
 * 0x53F8_0000 0x53F8_3FFF 16 Kbytes    CCM
 * 0x53F8_4000 0x53F8_7FFF 16 Kbytes    GPT-4
 * 0x53F8_8000 0x53F8_BFFF 16 Kbytes    GPT-3
 * 0x53F8_C000 0x53F8_FFFF 16 Kbytes    GPT-2
 * 0x53F9_0000 0x53F9_3FFF 16 Kbytes    GPT-1
 * 0x53F9_4000 0x53F9_7FFF 16 Kbytes    EPIT-1
 * 0x53F9_8000 0x53F9_BFFF 16 Kbytes    EPIT-2
 * 0x53F9_C000 0x53F9_FFFF 16 Kbytes    GPIO-4
 * 0x53FA_0000 0x53FA_3FFF 16 Kbytes    PWM-2
 * 0x53FA_4000 0x53FA_7FFF 16 Kbytes    GPIO-3
 * 0x53FA_8000 0x53FA_BFFF 16 Kbytes    PWM-3
 * 0x53FA_C000 0x53FA_FFFF 16 Kbytes    SCC
 * 0x53FB_0000 0x53FB_3FFF 16 Kbytes    RNGB
 * 0x53FB_4000 0x53FB_7FFF 16 Kbytes    eSDHC-1
 * 0x53FB_8000 0x53FB_BFFF 16 Kbytes    eSDHC-2
 * 0x53FB_C000 0x53FB_FFFF 16 Kbytes    LCDC
 * 0x53FC_0000 0x53FC_3FFF 16 Kbytes    SLCDC
 * 0x53FC_4000 0x53FC_7FFF 16 Kbytes    Reserved
 * 0x53FC_8000 0x53FC_BFFF 16 Kbytes    PWM-4
 * 0x53FC_C000 0x53FC_FFFF 16 Kbytes    GPIO-1
 * 0x53FD_0000 0x53FD_3FFF 16 Kbytes    GPIO-2
 * 0x53FD_4000 0x53FD_7FFF 16 Kbytes    SDMA
 * 0x53FD_8000 0x53FD_BFFF 16 Kbytes    Reserved
 * 0x53FD_C000 0x53FD_FFFF 16 Kbytes    WDOG
 * 0x53FE_0000 0x53FE_3FFF 16 Kbytes    PWM-1
 * 0x53FE_4000 0x53FE_7FFF 16 Kbytes    Reserved
 * 0x53FE_8000 0x53FE_BFFF 16 Kbytes    Reserved
 * 0x53FE_C000 0x53FE_FFFF 16 Kbytes    RTICv3
 * 0x53FF_0000 0x53FF_3FFF 16 Kbytes    IIM
 * 0x53FF_4000 0x53FF_7FFF 16 Kbytes    USB
 * 0x53FF_8000 0x53FF_BFFF 16 Kbytes    CSI
 * 0x53FF_C000 0x53FF_FFFF 16 Kbytes    DryIce
 * 0x5400_0000 0x5FFF_FFFF 192 Mbytes   Reserved (aliased AIPS B slots)
 * 0x6000_0000 0x67FF_FFFF 128 Mbytes   ARM926 platform ROMPATCH
 * 0x6800_0000 0x6FFF_FFFF 128 Mbytes   ARM926 platform ASIC
 * 0x7000_0000 0x77FF_FFFF 128 Mbytes   Reserved
 * 0x7800_0000 0x7801_FFFF 128 Kbytes   RAM
 * 0x7802_0000 0x7FFF_FFFF 128 Mbytes (minus 128 Kbytes)
 * 0x8000_0000 0x8FFF_FFFF 256 Mbytes   SDRAM bank 0
 * 0x9000_0000 0x9FFF_FFFF 256 Mbytes   SDRAM bank 1
 * 0xA000_0000 0xA7FF_FFFF 128 Mbytes   WEIM CS0 (flash 128) 1
 * 0xA800_0000 0xAFFF_FFFF 128 Mbytes   WEIM CS1 (flash 64) 1
 * 0xB000_0000 0xB1FF_FFFF 32 Mbytes    WEIM CS2 (SRAM)
 * 0xB200_0000 0xB3FF_FFFF 32 Mbytes    WEIM CS3 (SRAM)
 * 0xB400_0000 0xB5FF_FFFF 32 Mbytes    WEIM CS4
 * 0xB600_0000 0xB7FF_FFFF 32 Mbytes    Reserved
 * 0xB800_0000 0xB800_0FFF 4 Kbytes     Reserved
 * 0xB800_1000 0xB800_1FFF 4 Kbytes     SDRAM control registers
 * 0xB800_2000 0xB800_2FFF 4 Kbytes     WEIM control registers
 * 0xB800_3000 0xB800_3FFF 4 Kbytes     M3IF control registers
 * 0xB800_4000 0xB800_4FFF 4 Kbytes     EMI control registers
 * 0xB800_5000 0xBAFF_FFFF 32 Mbytes (minus 20 Kbytes)
 * 0xBB00_0000 0xBB00_0FFF 4 Kbytes     NAND flash main area buffer
 * 0xBB00_1000 0xBB00_11FF 512 B        NAND flash spare area buffer
 * 0xBB00_1200 0xBB00_1DFF 3 Kbytes     Reserved
 * 0xBB00_1E00 0xBB00_1FFF 512 B        NAND flash control regisers
 * 0xBB01_2000 0xBFFF_FFFF 96 Mbytes (minus 8 Kbytes) Reserved
 * 0xC000_0000 0xFFFF_FFFF 1024 Mbytes  Reserved
 */

#define FSL_IMX25_ROM0_ADDR     0x00000000
#define FSL_IMX25_ROM0_SIZE     0x4000
#define FSL_IMX25_ROM1_ADDR     0x00404000
#define FSL_IMX25_ROM1_SIZE     0x4000
#define FSL_IMX25_I2C1_ADDR     0x43F80000
#define FSL_IMX25_I2C1_SIZE     0x4000
#define FSL_IMX25_I2C3_ADDR     0x43F84000
#define FSL_IMX25_I2C3_SIZE     0x4000
#define FSL_IMX25_UART1_ADDR    0x43F90000
#define FSL_IMX25_UART1_SIZE    0x4000
#define FSL_IMX25_UART2_ADDR    0x43F94000
#define FSL_IMX25_UART2_SIZE    0x4000
#define FSL_IMX25_I2C2_ADDR     0x43F98000
#define FSL_IMX25_I2C2_SIZE     0x4000
#define FSL_IMX25_UART4_ADDR    0x50008000
#define FSL_IMX25_UART4_SIZE    0x4000
#define FSL_IMX25_UART3_ADDR    0x5000C000
#define FSL_IMX25_UART3_SIZE    0x4000
#define FSL_IMX25_UART5_ADDR    0x5002C000
#define FSL_IMX25_UART5_SIZE    0x4000
#define FSL_IMX25_FEC_ADDR      0x50038000
#define FSL_IMX25_FEC_SIZE      0x4000
#define FSL_IMX25_CCM_ADDR      0x53F80000
#define FSL_IMX25_CCM_SIZE      0x4000
#define FSL_IMX25_GPT4_ADDR     0x53F84000
#define FSL_IMX25_GPT4_SIZE     0x4000
#define FSL_IMX25_GPT3_ADDR     0x53F88000
#define FSL_IMX25_GPT3_SIZE     0x4000
#define FSL_IMX25_GPT2_ADDR     0x53F8C000
#define FSL_IMX25_GPT2_SIZE     0x4000
#define FSL_IMX25_GPT1_ADDR     0x53F90000
#define FSL_IMX25_GPT1_SIZE     0x4000
#define FSL_IMX25_EPIT1_ADDR    0x53F94000
#define FSL_IMX25_EPIT1_SIZE    0x4000
#define FSL_IMX25_EPIT2_ADDR    0x53F98000
#define FSL_IMX25_EPIT2_SIZE    0x4000
#define FSL_IMX25_GPIO4_ADDR    0x53F9C000
#define FSL_IMX25_GPIO4_SIZE    0x4000
#define FSL_IMX25_GPIO3_ADDR    0x53FA4000
#define FSL_IMX25_GPIO3_SIZE    0x4000
#define FSL_IMX25_GPIO1_ADDR    0x53FCC000
#define FSL_IMX25_GPIO1_SIZE    0x4000
#define FSL_IMX25_GPIO2_ADDR    0x53FD0000
#define FSL_IMX25_GPIO2_SIZE    0x4000
#define FSL_IMX25_AVIC_ADDR     0x68000000
#define FSL_IMX25_AVIC_SIZE     0x4000
#define FSL_IMX25_IRAM_ADDR     0x78000000
#define FSL_IMX25_IRAM_SIZE     0x20000
#define FSL_IMX25_IRAM_ALIAS_ADDR     0x78020000
#define FSL_IMX25_IRAM_ALIAS_SIZE     0x7FE0000
#define FSL_IMX25_SDRAM0_ADDR   0x80000000
#define FSL_IMX25_SDRAM0_SIZE   0x10000000
#define FSL_IMX25_SDRAM1_ADDR   0x90000000
#define FSL_IMX25_SDRAM1_SIZE   0x10000000

#define FSL_IMX25_UART1_IRQ     45
#define FSL_IMX25_UART2_IRQ     32
#define FSL_IMX25_UART3_IRQ     18
#define FSL_IMX25_UART4_IRQ     5
#define FSL_IMX25_UART5_IRQ     40
#define FSL_IMX25_GPT1_IRQ      54
#define FSL_IMX25_GPT2_IRQ      53
#define FSL_IMX25_GPT3_IRQ      29
#define FSL_IMX25_GPT4_IRQ      1
#define FSL_IMX25_EPIT1_IRQ     28
#define FSL_IMX25_EPIT2_IRQ     27
#define FSL_IMX25_FEC_IRQ       57
#define FSL_IMX25_I2C1_IRQ      3
#define FSL_IMX25_I2C2_IRQ      4
#define FSL_IMX25_I2C3_IRQ      10
#define FSL_IMX25_GPIO1_IRQ     52
#define FSL_IMX25_GPIO2_IRQ     51
#define FSL_IMX25_GPIO3_IRQ     16
#define FSL_IMX25_GPIO4_IRQ     23

#endif /* FSL_IMX25_H */
