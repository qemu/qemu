/*
 * Copyright (c) 2018 Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * i.MX6ul SoC definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef FSL_IMX6UL_H
#define FSL_IMX6UL_H

#include "hw/arm/boot.h"
#include "hw/cpu/a15mpcore.h"
#include "hw/misc/imx6ul_ccm.h"
#include "hw/misc/imx6_src.h"
#include "hw/misc/imx7_snvs.h"
#include "hw/misc/imx7_gpr.h"
#include "hw/intc/imx_gpcv2.h"
#include "hw/misc/imx2_wdt.h"
#include "hw/gpio/imx_gpio.h"
#include "hw/char/imx_serial.h"
#include "hw/timer/imx_gpt.h"
#include "hw/timer/imx_epit.h"
#include "hw/i2c/imx_i2c.h"
#include "hw/gpio/imx_gpio.h"
#include "hw/sd/sdhci.h"
#include "hw/ssi/imx_spi.h"
#include "hw/net/imx_fec.h"
#include "exec/memory.h"
#include "cpu.h"

#define TYPE_FSL_IMX6UL "fsl,imx6ul"
#define FSL_IMX6UL(obj) OBJECT_CHECK(FslIMX6ULState, (obj), TYPE_FSL_IMX6UL)

enum FslIMX6ULConfiguration {
    FSL_IMX6UL_NUM_CPUS         = 1,
    FSL_IMX6UL_NUM_UARTS        = 8,
    FSL_IMX6UL_NUM_ETHS         = 2,
    FSL_IMX6UL_ETH_NUM_TX_RINGS = 2,
    FSL_IMX6UL_NUM_USDHCS       = 2,
    FSL_IMX6UL_NUM_WDTS         = 3,
    FSL_IMX6UL_NUM_GPTS         = 2,
    FSL_IMX6UL_NUM_EPITS        = 2,
    FSL_IMX6UL_NUM_IOMUXCS      = 2,
    FSL_IMX6UL_NUM_GPIOS        = 5,
    FSL_IMX6UL_NUM_I2CS         = 4,
    FSL_IMX6UL_NUM_ECSPIS       = 4,
    FSL_IMX6UL_NUM_ADCS         = 2,
};

typedef struct FslIMX6ULState {
    /*< private >*/
    DeviceState    parent_obj;

    /*< public >*/
    ARMCPU             cpu[FSL_IMX6UL_NUM_CPUS];
    A15MPPrivState     a7mpcore;
    IMXGPTState        gpt[FSL_IMX6UL_NUM_GPTS];
    IMXEPITState       epit[FSL_IMX6UL_NUM_EPITS];
    IMXGPIOState       gpio[FSL_IMX6UL_NUM_GPIOS];
    IMX6ULCCMState     ccm;
    IMX6SRCState       src;
    IMX7SNVSState      snvs;
    IMXGPCv2State      gpcv2;
    IMX7GPRState       gpr;
    IMXSPIState        spi[FSL_IMX6UL_NUM_ECSPIS];
    IMXI2CState        i2c[FSL_IMX6UL_NUM_I2CS];
    IMXSerialState     uart[FSL_IMX6UL_NUM_UARTS];
    IMXFECState        eth[FSL_IMX6UL_NUM_ETHS];
    SDHCIState         usdhc[FSL_IMX6UL_NUM_USDHCS];
    IMX2WdtState       wdt[FSL_IMX6UL_NUM_WDTS];
    MemoryRegion       rom;
    MemoryRegion       caam;
    MemoryRegion       ocram;
    MemoryRegion       ocram_alias;
} FslIMX6ULState;

enum FslIMX6ULMemoryMap {
    FSL_IMX6UL_MMDC_ADDR            = 0x80000000,
    FSL_IMX6UL_MMDC_SIZE            = 2 * 1024 * 1024 * 1024UL,

    FSL_IMX6UL_QSPI1_MEM_ADDR       = 0x60000000,
    FSL_IMX6UL_EIM_ALIAS_ADDR       = 0x58000000,
    FSL_IMX6UL_EIM_CS_ADDR          = 0x50000000,
    FSL_IMX6UL_AES_ENCRYPT_ADDR     = 0x10000000,
    FSL_IMX6UL_QSPI1_RX_ADDR        = 0x0C000000,

    /* AIPS-2 */
    FSL_IMX6UL_UART6_ADDR           = 0x021FC000,
    FSL_IMX6UL_I2C4_ADDR            = 0x021F8000,
    FSL_IMX6UL_UART5_ADDR           = 0x021F4000,
    FSL_IMX6UL_UART4_ADDR           = 0x021F0000,
    FSL_IMX6UL_UART3_ADDR           = 0x021EC000,
    FSL_IMX6UL_UART2_ADDR           = 0x021E8000,
    FSL_IMX6UL_WDOG3_ADDR           = 0x021E4000,
    FSL_IMX6UL_QSPI_ADDR            = 0x021E0000,
    FSL_IMX6UL_SYS_CNT_CTRL_ADDR    = 0x021DC000,
    FSL_IMX6UL_SYS_CNT_CMP_ADDR     = 0x021D8000,
    FSL_IMX6UL_SYS_CNT_RD_ADDR      = 0x021D4000,
    FSL_IMX6UL_TZASC_ADDR           = 0x021D0000,
    FSL_IMX6UL_PXP_ADDR             = 0x021CC000,
    FSL_IMX6UL_LCDIF_ADDR           = 0x021C8000,
    FSL_IMX6UL_CSI_ADDR             = 0x021C4000,
    FSL_IMX6UL_CSU_ADDR             = 0x021C0000,
    FSL_IMX6UL_OCOTP_CTRL_ADDR      = 0x021BC000,
    FSL_IMX6UL_EIM_ADDR             = 0x021B8000,
    FSL_IMX6UL_SIM2_ADDR            = 0x021B4000,
    FSL_IMX6UL_MMDC_CFG_ADDR        = 0x021B0000,
    FSL_IMX6UL_ROMCP_ADDR           = 0x021AC000,
    FSL_IMX6UL_I2C3_ADDR            = 0x021A8000,
    FSL_IMX6UL_I2C2_ADDR            = 0x021A4000,
    FSL_IMX6UL_I2C1_ADDR            = 0x021A0000,
    FSL_IMX6UL_ADC2_ADDR            = 0x0219C000,
    FSL_IMX6UL_ADC1_ADDR            = 0x02198000,
    FSL_IMX6UL_USDHC2_ADDR          = 0x02194000,
    FSL_IMX6UL_USDHC1_ADDR          = 0x02190000,
    FSL_IMX6UL_SIM1_ADDR            = 0x0218C000,
    FSL_IMX6UL_ENET1_ADDR           = 0x02188000,
    FSL_IMX6UL_USBO2_USBMISC_ADDR   = 0x02184800,
    FSL_IMX6UL_USBO2_USB_ADDR       = 0x02184000,
    FSL_IMX6UL_USBO2_PL301_ADDR     = 0x02180000,
    FSL_IMX6UL_AIPS2_CFG_ADDR       = 0x0217C000,
    FSL_IMX6UL_CAAM_ADDR            = 0x02140000,
    FSL_IMX6UL_A7MPCORE_DAP_ADDR    = 0x02100000,

    /* AIPS-1 */
    FSL_IMX6UL_PWM8_ADDR            = 0x020FC000,
    FSL_IMX6UL_PWM7_ADDR            = 0x020F8000,
    FSL_IMX6UL_PWM6_ADDR            = 0x020F4000,
    FSL_IMX6UL_PWM5_ADDR            = 0x020F0000,
    FSL_IMX6UL_SDMA_ADDR            = 0x020EC000,
    FSL_IMX6UL_GPT2_ADDR            = 0x020E8000,
    FSL_IMX6UL_IOMUXC_GPR_ADDR      = 0x020E4000,
    FSL_IMX6UL_IOMUXC_ADDR          = 0x020E0000,
    FSL_IMX6UL_GPC_ADDR             = 0x020DC000,
    FSL_IMX6UL_SRC_ADDR             = 0x020D8000,
    FSL_IMX6UL_EPIT2_ADDR           = 0x020D4000,
    FSL_IMX6UL_EPIT1_ADDR           = 0x020D0000,
    FSL_IMX6UL_SNVS_HP_ADDR         = 0x020CC000,
    FSL_IMX6UL_ANALOG_ADDR          = 0x020C8000,
    FSL_IMX6UL_CCM_ADDR             = 0x020C4000,
    FSL_IMX6UL_WDOG2_ADDR           = 0x020C0000,
    FSL_IMX6UL_WDOG1_ADDR           = 0x020BC000,
    FSL_IMX6UL_KPP_ADDR             = 0x020B8000,
    FSL_IMX6UL_ENET2_ADDR           = 0x020B4000,
    FSL_IMX6UL_SNVS_LP_ADDR         = 0x020B0000,
    FSL_IMX6UL_GPIO5_ADDR           = 0x020AC000,
    FSL_IMX6UL_GPIO4_ADDR           = 0x020A8000,
    FSL_IMX6UL_GPIO3_ADDR           = 0x020A4000,
    FSL_IMX6UL_GPIO2_ADDR           = 0x020A0000,
    FSL_IMX6UL_GPIO1_ADDR           = 0x0209C000,
    FSL_IMX6UL_GPT1_ADDR            = 0x02098000,
    FSL_IMX6UL_CAN2_ADDR            = 0x02094000,
    FSL_IMX6UL_CAN1_ADDR            = 0x02090000,
    FSL_IMX6UL_PWM4_ADDR            = 0x0208C000,
    FSL_IMX6UL_PWM3_ADDR            = 0x02088000,
    FSL_IMX6UL_PWM2_ADDR            = 0x02084000,
    FSL_IMX6UL_PWM1_ADDR            = 0x02080000,
    FSL_IMX6UL_AIPS1_CFG_ADDR       = 0x0207C000,
    FSL_IMX6UL_BEE_ADDR             = 0x02044000,
    FSL_IMX6UL_TOUCH_CTRL_ADDR      = 0x02040000,
    FSL_IMX6UL_SPBA_ADDR            = 0x0203C000,
    FSL_IMX6UL_ASRC_ADDR            = 0x02034000,
    FSL_IMX6UL_SAI3_ADDR            = 0x02030000,
    FSL_IMX6UL_SAI2_ADDR            = 0x0202C000,
    FSL_IMX6UL_SAI1_ADDR            = 0x02028000,
    FSL_IMX6UL_UART8_ADDR           = 0x02024000,
    FSL_IMX6UL_UART1_ADDR           = 0x02020000,
    FSL_IMX6UL_UART7_ADDR           = 0x02018000,
    FSL_IMX6UL_ECSPI4_ADDR          = 0x02014000,
    FSL_IMX6UL_ECSPI3_ADDR          = 0x02010000,
    FSL_IMX6UL_ECSPI2_ADDR          = 0x0200C000,
    FSL_IMX6UL_ECSPI1_ADDR          = 0x02008000,
    FSL_IMX6UL_SPDIF_ADDR           = 0x02004000,

    FSL_IMX6UL_APBH_DMA_ADDR        = 0x01804000,
    FSL_IMX6UL_APBH_DMA_SIZE        = (32 * 1024),

    FSL_IMX6UL_A7MPCORE_ADDR        = 0x00A00000,

    FSL_IMX6UL_OCRAM_ALIAS_ADDR     = 0x00920000,
    FSL_IMX6UL_OCRAM_ALIAS_SIZE     = 0x00060000,
    FSL_IMX6UL_OCRAM_MEM_ADDR       = 0x00900000,
    FSL_IMX6UL_OCRAM_MEM_SIZE       = 0x00020000,
    FSL_IMX6UL_CAAM_MEM_ADDR        = 0x00100000,
    FSL_IMX6UL_CAAM_MEM_SIZE        = 0x00008000,
    FSL_IMX6UL_ROM_ADDR             = 0x00000000,
    FSL_IMX6UL_ROM_SIZE             = 0x00018000,
};

enum FslIMX6ULIRQs {
    FSL_IMX6UL_IOMUXC_IRQ   = 0,
    FSL_IMX6UL_DAP_IRQ      = 1,
    FSL_IMX6UL_SDMA_IRQ     = 2,
    FSL_IMX6UL_TSC_IRQ      = 3,
    FSL_IMX6UL_SNVS_IRQ     = 4,
    FSL_IMX6UL_LCDIF_IRQ    = 5,
    FSL_IMX6UL_BEE_IRQ      = 6,
    FSL_IMX6UL_CSI_IRQ      = 7,
    FSL_IMX6UL_PXP_IRQ      = 8,
    FSL_IMX6UL_SCTR1_IRQ    = 9,
    FSL_IMX6UL_SCTR2_IRQ    = 10,
    FSL_IMX6UL_WDOG3_IRQ    = 11,
    FSL_IMX6UL_APBH_DMA_IRQ = 13,
    FSL_IMX6UL_WEIM_IRQ     = 14,
    FSL_IMX6UL_RAWNAND1_IRQ = 15,
    FSL_IMX6UL_RAWNAND2_IRQ = 16,
    FSL_IMX6UL_UART6_IRQ    = 17,
    FSL_IMX6UL_SRTC_IRQ     = 19,
    FSL_IMX6UL_SRTC_SEC_IRQ = 20,
    FSL_IMX6UL_CSU_IRQ      = 21,
    FSL_IMX6UL_USDHC1_IRQ   = 22,
    FSL_IMX6UL_USDHC2_IRQ   = 23,
    FSL_IMX6UL_SAI3_IRQ     = 24,
    FSL_IMX6UL_SAI32_IRQ    = 25,

    FSL_IMX6UL_UART1_IRQ    = 26,
    FSL_IMX6UL_UART2_IRQ    = 27,
    FSL_IMX6UL_UART3_IRQ    = 28,
    FSL_IMX6UL_UART4_IRQ    = 29,
    FSL_IMX6UL_UART5_IRQ    = 30,

    FSL_IMX6UL_ECSPI1_IRQ   = 31,
    FSL_IMX6UL_ECSPI2_IRQ   = 32,
    FSL_IMX6UL_ECSPI3_IRQ   = 33,
    FSL_IMX6UL_ECSPI4_IRQ   = 34,

    FSL_IMX6UL_I2C4_IRQ     = 35,
    FSL_IMX6UL_I2C1_IRQ     = 36,
    FSL_IMX6UL_I2C2_IRQ     = 37,
    FSL_IMX6UL_I2C3_IRQ     = 38,

    FSL_IMX6UL_UART7_IRQ    = 39,
    FSL_IMX6UL_UART8_IRQ    = 40,

    FSL_IMX6UL_USB1_IRQ     = 42,
    FSL_IMX6UL_USB2_IRQ     = 43,
    FSL_IMX6UL_USB_PHY1_IRQ = 44,
    FSL_IMX6UL_USB_PHY2_IRQ = 44,

    FSL_IMX6UL_CAAM_JQ2_IRQ = 46,
    FSL_IMX6UL_CAAM_ERR_IRQ = 47,
    FSL_IMX6UL_CAAM_RTIC_IRQ = 48,
    FSL_IMX6UL_TEMP_IRQ     = 49,
    FSL_IMX6UL_ASRC_IRQ     = 50,
    FSL_IMX6UL_SPDIF_IRQ    = 52,
    FSL_IMX6UL_PMU_REG_IRQ  = 54,
    FSL_IMX6UL_GPT1_IRQ     = 55,

    FSL_IMX6UL_EPIT1_IRQ    = 56,
    FSL_IMX6UL_EPIT2_IRQ    = 57,

    FSL_IMX6UL_GPIO1_INT7_IRQ = 58,
    FSL_IMX6UL_GPIO1_INT6_IRQ = 59,
    FSL_IMX6UL_GPIO1_INT5_IRQ = 60,
    FSL_IMX6UL_GPIO1_INT4_IRQ = 61,
    FSL_IMX6UL_GPIO1_INT3_IRQ = 62,
    FSL_IMX6UL_GPIO1_INT2_IRQ = 63,
    FSL_IMX6UL_GPIO1_INT1_IRQ = 64,
    FSL_IMX6UL_GPIO1_INT0_IRQ = 65,
    FSL_IMX6UL_GPIO1_LOW_IRQ  = 66,
    FSL_IMX6UL_GPIO1_HIGH_IRQ = 67,
    FSL_IMX6UL_GPIO2_LOW_IRQ  = 68,
    FSL_IMX6UL_GPIO2_HIGH_IRQ = 69,
    FSL_IMX6UL_GPIO3_LOW_IRQ  = 70,
    FSL_IMX6UL_GPIO3_HIGH_IRQ = 71,
    FSL_IMX6UL_GPIO4_LOW_IRQ  = 72,
    FSL_IMX6UL_GPIO4_HIGH_IRQ = 73,
    FSL_IMX6UL_GPIO5_LOW_IRQ  = 74,
    FSL_IMX6UL_GPIO5_HIGH_IRQ = 75,

    FSL_IMX6UL_WDOG1_IRQ    = 80,
    FSL_IMX6UL_WDOG2_IRQ    = 81,

    FSL_IMX6UL_KPP_IRQ      = 82,

    FSL_IMX6UL_PWM1_IRQ     = 83,
    FSL_IMX6UL_PWM2_IRQ     = 84,
    FSL_IMX6UL_PWM3_IRQ     = 85,
    FSL_IMX6UL_PWM4_IRQ     = 86,

    FSL_IMX6UL_CCM1_IRQ     = 87,
    FSL_IMX6UL_CCM2_IRQ     = 88,

    FSL_IMX6UL_GPC_IRQ      = 89,

    FSL_IMX6UL_SRC_IRQ      = 91,

    FSL_IMX6UL_CPU_PERF_IRQ = 94,
    FSL_IMX6UL_CPU_CTI_IRQ  = 95,

    FSL_IMX6UL_SRC_WDOG_IRQ = 96,

    FSL_IMX6UL_SAI1_IRQ     = 97,
    FSL_IMX6UL_SAI2_IRQ     = 98,

    FSL_IMX6UL_ADC1_IRQ     = 100,
    FSL_IMX6UL_ADC2_IRQ     = 101,

    FSL_IMX6UL_SJC_IRQ      = 104,

    FSL_IMX6UL_CAAM_RING0_IRQ = 105,
    FSL_IMX6UL_CAAM_RING1_IRQ = 106,

    FSL_IMX6UL_QSPI_IRQ     = 107,

    FSL_IMX6UL_TZASC_IRQ    = 108,

    FSL_IMX6UL_GPT2_IRQ     = 109,

    FSL_IMX6UL_CAN1_IRQ     = 110,
    FSL_IMX6UL_CAN2_IRQ     = 111,

    FSL_IMX6UL_SIM1_IRQ     = 112,
    FSL_IMX6UL_SIM2_IRQ     = 113,

    FSL_IMX6UL_PWM5_IRQ     = 114,
    FSL_IMX6UL_PWM6_IRQ     = 115,
    FSL_IMX6UL_PWM7_IRQ     = 116,
    FSL_IMX6UL_PWM8_IRQ     = 117,

    FSL_IMX6UL_ENET1_IRQ    = 118,
    FSL_IMX6UL_ENET1_TIMER_IRQ = 119,
    FSL_IMX6UL_ENET2_IRQ    = 120,
    FSL_IMX6UL_ENET2_TIMER_IRQ = 121,

    FSL_IMX6UL_PMU_CORE_IRQ = 127,
    FSL_IMX6UL_MAX_IRQ      = 128,
};

#endif /* FSL_IMX6UL_H */
