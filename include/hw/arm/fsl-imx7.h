/*
 * Copyright (c) 2018, Impinj, Inc.
 *
 * i.MX7 SoC definitions
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
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

#ifndef FSL_IMX7_H
#define FSL_IMX7_H

#include "hw/cpu/a15mpcore.h"
#include "hw/intc/imx_gpcv2.h"
#include "hw/misc/imx7_ccm.h"
#include "hw/misc/imx7_snvs.h"
#include "hw/misc/imx7_gpr.h"
#include "hw/misc/imx7_src.h"
#include "hw/watchdog/wdt_imx2.h"
#include "hw/gpio/imx_gpio.h"
#include "hw/char/imx_serial.h"
#include "hw/timer/imx_gpt.h"
#include "hw/timer/imx_epit.h"
#include "hw/i2c/imx_i2c.h"
#include "hw/sd/sdhci.h"
#include "hw/ssi/imx_spi.h"
#include "hw/net/imx_fec.h"
#include "hw/pci-host/designware.h"
#include "hw/usb/chipidea.h"
#include "cpu.h"
#include "qom/object.h"
#include "qemu/units.h"

#define TYPE_FSL_IMX7 "fsl-imx7"
OBJECT_DECLARE_SIMPLE_TYPE(FslIMX7State, FSL_IMX7)

enum FslIMX7Configuration {
    FSL_IMX7_NUM_CPUS         = 2,
    FSL_IMX7_NUM_UARTS        = 7,
    FSL_IMX7_NUM_ETHS         = 2,
    FSL_IMX7_ETH_NUM_TX_RINGS = 3,
    FSL_IMX7_NUM_USDHCS       = 3,
    FSL_IMX7_NUM_WDTS         = 4,
    FSL_IMX7_NUM_GPTS         = 4,
    FSL_IMX7_NUM_IOMUXCS      = 2,
    FSL_IMX7_NUM_GPIOS        = 7,
    FSL_IMX7_NUM_I2CS         = 4,
    FSL_IMX7_NUM_ECSPIS       = 4,
    FSL_IMX7_NUM_USBS         = 3,
    FSL_IMX7_NUM_ADCS         = 2,
    FSL_IMX7_NUM_SAIS         = 3,
    FSL_IMX7_NUM_CANS         = 2,
    FSL_IMX7_NUM_PWMS         = 4,
};

struct FslIMX7State {
    /*< private >*/
    DeviceState    parent_obj;

    /*< public >*/
    ARMCPU             cpu[FSL_IMX7_NUM_CPUS];
    A15MPPrivState     a7mpcore;
    IMXGPTState        gpt[FSL_IMX7_NUM_GPTS];
    IMXGPIOState       gpio[FSL_IMX7_NUM_GPIOS];
    IMX7CCMState       ccm;
    IMX7AnalogState    analog;
    IMX7SNVSState      snvs;
    IMX7SRCState       src;
    IMXGPCv2State      gpcv2;
    IMXSPIState        spi[FSL_IMX7_NUM_ECSPIS];
    IMXI2CState        i2c[FSL_IMX7_NUM_I2CS];
    IMXSerialState     uart[FSL_IMX7_NUM_UARTS];
    IMXFECState        eth[FSL_IMX7_NUM_ETHS];
    SDHCIState         usdhc[FSL_IMX7_NUM_USDHCS];
    IMX2WdtState       wdt[FSL_IMX7_NUM_WDTS];
    IMX7GPRState       gpr;
    ChipideaState      usb[FSL_IMX7_NUM_USBS];
    DesignwarePCIEHost pcie;
    MemoryRegion       rom;
    MemoryRegion       caam;
    MemoryRegion       ocram;
    MemoryRegion       ocram_epdc;
    MemoryRegion       ocram_pxp;
    MemoryRegion       ocram_s;

    uint32_t           phy_num[FSL_IMX7_NUM_ETHS];
    bool               phy_connected[FSL_IMX7_NUM_ETHS];
};

enum FslIMX7MemoryMap {
    FSL_IMX7_MMDC_ADDR            = 0x80000000,
    FSL_IMX7_MMDC_SIZE            = (2 * GiB),

    FSL_IMX7_QSPI1_MEM_ADDR       = 0x60000000,
    FSL_IMX7_QSPI1_MEM_SIZE       = (256 * MiB),

    FSL_IMX7_PCIE1_MEM_ADDR       = 0x40000000,
    FSL_IMX7_PCIE1_MEM_SIZE       = (256 * MiB),

    FSL_IMX7_QSPI1_RX_BUF_ADDR    = 0x34000000,
    FSL_IMX7_QSPI1_RX_BUF_SIZE    = (32 * MiB),

    /* PCIe Peripherals */
    FSL_IMX7_PCIE_REG_ADDR        = 0x33800000,

    /* MMAP Peripherals */
    FSL_IMX7_DMA_APBH_ADDR        = 0x33000000,
    FSL_IMX7_DMA_APBH_SIZE        = 0x8000,

    /* GPV configuration */
    FSL_IMX7_GPV6_ADDR            = 0x32600000,
    FSL_IMX7_GPV5_ADDR            = 0x32500000,
    FSL_IMX7_GPV4_ADDR            = 0x32400000,
    FSL_IMX7_GPV3_ADDR            = 0x32300000,
    FSL_IMX7_GPV2_ADDR            = 0x32200000,
    FSL_IMX7_GPV1_ADDR            = 0x32100000,
    FSL_IMX7_GPV0_ADDR            = 0x32000000,
    FSL_IMX7_GPVn_SIZE            = (1 * MiB),

    /* Arm Peripherals */
    FSL_IMX7_A7MPCORE_ADDR        = 0x31000000,

    /* AIPS-3 Begin */

    FSL_IMX7_ENET2_ADDR           = 0x30BF0000,
    FSL_IMX7_ENET1_ADDR           = 0x30BE0000,

    FSL_IMX7_SDMA_ADDR            = 0x30BD0000,
    FSL_IMX7_SDMA_SIZE            = (4 * KiB),

    FSL_IMX7_EIM_ADDR             = 0x30BC0000,
    FSL_IMX7_EIM_SIZE             = (4 * KiB),

    FSL_IMX7_QSPI_ADDR            = 0x30BB0000,
    FSL_IMX7_QSPI_SIZE            = 0x8000,

    FSL_IMX7_SIM2_ADDR            = 0x30BA0000,
    FSL_IMX7_SIM1_ADDR            = 0x30B90000,
    FSL_IMX7_SIMn_SIZE            = (4 * KiB),

    FSL_IMX7_USDHC3_ADDR          = 0x30B60000,
    FSL_IMX7_USDHC2_ADDR          = 0x30B50000,
    FSL_IMX7_USDHC1_ADDR          = 0x30B40000,

    FSL_IMX7_USB3_ADDR            = 0x30B30000,
    FSL_IMX7_USBMISC3_ADDR        = 0x30B30200,
    FSL_IMX7_USB2_ADDR            = 0x30B20000,
    FSL_IMX7_USBMISC2_ADDR        = 0x30B20200,
    FSL_IMX7_USB1_ADDR            = 0x30B10000,
    FSL_IMX7_USBMISC1_ADDR        = 0x30B10200,
    FSL_IMX7_USBMISCn_SIZE        = 0x200,

    FSL_IMX7_USB_PL301_ADDR       = 0x30AD0000,
    FSL_IMX7_USB_PL301_SIZE       = (64 * KiB),

    FSL_IMX7_SEMAPHORE_HS_ADDR    = 0x30AC0000,
    FSL_IMX7_SEMAPHORE_HS_SIZE    = (64 * KiB),

    FSL_IMX7_MUB_ADDR             = 0x30AB0000,
    FSL_IMX7_MUA_ADDR             = 0x30AA0000,
    FSL_IMX7_MUn_SIZE             = (KiB),

    FSL_IMX7_UART7_ADDR           = 0x30A90000,
    FSL_IMX7_UART6_ADDR           = 0x30A80000,
    FSL_IMX7_UART5_ADDR           = 0x30A70000,
    FSL_IMX7_UART4_ADDR           = 0x30A60000,

    FSL_IMX7_I2C4_ADDR            = 0x30A50000,
    FSL_IMX7_I2C3_ADDR            = 0x30A40000,
    FSL_IMX7_I2C2_ADDR            = 0x30A30000,
    FSL_IMX7_I2C1_ADDR            = 0x30A20000,

    FSL_IMX7_CAN2_ADDR            = 0x30A10000,
    FSL_IMX7_CAN1_ADDR            = 0x30A00000,
    FSL_IMX7_CANn_SIZE            = (4 * KiB),

    FSL_IMX7_AIPS3_CONF_ADDR      = 0x309F0000,
    FSL_IMX7_AIPS3_CONF_SIZE      = (64 * KiB),

    FSL_IMX7_CAAM_ADDR            = 0x30900000,
    FSL_IMX7_CAAM_SIZE            = (256 * KiB),

    FSL_IMX7_SPBA_ADDR            = 0x308F0000,
    FSL_IMX7_SPBA_SIZE            = (4 * KiB),

    FSL_IMX7_SAI3_ADDR            = 0x308C0000,
    FSL_IMX7_SAI2_ADDR            = 0x308B0000,
    FSL_IMX7_SAI1_ADDR            = 0x308A0000,
    FSL_IMX7_SAIn_SIZE            = (4 * KiB),

    FSL_IMX7_UART3_ADDR           = 0x30880000,
    /*
     * Some versions of the reference manual claim that UART2 is @
     * 0x30870000, but experiments with HW + DT files in upstream
     * Linux kernel show that not to be true and that block is
     * actually located @ 0x30890000
     */
    FSL_IMX7_UART2_ADDR           = 0x30890000,
    FSL_IMX7_UART1_ADDR           = 0x30860000,

    FSL_IMX7_ECSPI3_ADDR          = 0x30840000,
    FSL_IMX7_ECSPI2_ADDR          = 0x30830000,
    FSL_IMX7_ECSPI1_ADDR          = 0x30820000,
    FSL_IMX7_ECSPIn_SIZE          = (4 * KiB),

    /* AIPS-3 End */

    /* AIPS-2 Begin */

    FSL_IMX7_AXI_DEBUG_MON_ADDR   = 0x307E0000,
    FSL_IMX7_AXI_DEBUG_MON_SIZE   = (64 * KiB),

    FSL_IMX7_PERFMON2_ADDR        = 0x307D0000,
    FSL_IMX7_PERFMON1_ADDR        = 0x307C0000,
    FSL_IMX7_PERFMONn_SIZE        = (64 * KiB),

    FSL_IMX7_DDRC_ADDR            = 0x307A0000,
    FSL_IMX7_DDRC_SIZE            = (4 * KiB),

    FSL_IMX7_DDRC_PHY_ADDR        = 0x30790000,
    FSL_IMX7_DDRC_PHY_SIZE        = (4 * KiB),

    FSL_IMX7_TZASC_ADDR           = 0x30780000,
    FSL_IMX7_TZASC_SIZE           = (64 * KiB),

    FSL_IMX7_MIPI_DSI_ADDR        = 0x30760000,
    FSL_IMX7_MIPI_DSI_SIZE        = (4 * KiB),

    FSL_IMX7_MIPI_CSI_ADDR        = 0x30750000,
    FSL_IMX7_MIPI_CSI_SIZE        = 0x4000,

    FSL_IMX7_LCDIF_ADDR           = 0x30730000,
    FSL_IMX7_LCDIF_SIZE           = 0x8000,

    FSL_IMX7_CSI_ADDR             = 0x30710000,
    FSL_IMX7_CSI_SIZE             = (4 * KiB),

    FSL_IMX7_PXP_ADDR             = 0x30700000,
    FSL_IMX7_PXP_SIZE             = 0x4000,

    FSL_IMX7_EPDC_ADDR            = 0x306F0000,
    FSL_IMX7_EPDC_SIZE            = (4 * KiB),

    FSL_IMX7_PCIE_PHY_ADDR        = 0x306D0000,
    FSL_IMX7_PCIE_PHY_SIZE        = (4 * KiB),

    FSL_IMX7_SYSCNT_CTRL_ADDR     = 0x306C0000,
    FSL_IMX7_SYSCNT_CMP_ADDR      = 0x306B0000,
    FSL_IMX7_SYSCNT_RD_ADDR       = 0x306A0000,

    FSL_IMX7_PWM4_ADDR            = 0x30690000,
    FSL_IMX7_PWM3_ADDR            = 0x30680000,
    FSL_IMX7_PWM2_ADDR            = 0x30670000,
    FSL_IMX7_PWM1_ADDR            = 0x30660000,
    FSL_IMX7_PWMn_SIZE            = (4 * KiB),

    FSL_IMX7_FlEXTIMER2_ADDR      = 0x30650000,
    FSL_IMX7_FlEXTIMER1_ADDR      = 0x30640000,
    FSL_IMX7_FLEXTIMERn_SIZE      = (4 * KiB),

    FSL_IMX7_ECSPI4_ADDR          = 0x30630000,

    FSL_IMX7_ADC2_ADDR            = 0x30620000,
    FSL_IMX7_ADC1_ADDR            = 0x30610000,
    FSL_IMX7_ADCn_SIZE            = (4 * KiB),

    FSL_IMX7_AIPS2_CONF_ADDR      = 0x305F0000,
    FSL_IMX7_AIPS2_CONF_SIZE      = (64 * KiB),

    /* AIPS-2 End */

    /* AIPS-1 Begin */

    FSL_IMX7_CSU_ADDR             = 0x303E0000,
    FSL_IMX7_CSU_SIZE             = (64 * KiB),

    FSL_IMX7_RDC_ADDR             = 0x303D0000,
    FSL_IMX7_RDC_SIZE             = (4 * KiB),

    FSL_IMX7_SEMAPHORE2_ADDR      = 0x303C0000,
    FSL_IMX7_SEMAPHORE1_ADDR      = 0x303B0000,
    FSL_IMX7_SEMAPHOREn_SIZE      = (4 * KiB),

    FSL_IMX7_GPC_ADDR             = 0x303A0000,

    FSL_IMX7_SRC_ADDR             = 0x30390000,

    FSL_IMX7_CCM_ADDR             = 0x30380000,

    FSL_IMX7_SNVS_HP_ADDR         = 0x30370000,

    FSL_IMX7_ANALOG_ADDR          = 0x30360000,

    FSL_IMX7_OCOTP_ADDR           = 0x30350000,
    FSL_IMX7_OCOTP_SIZE           = 0x10000,

    FSL_IMX7_IOMUXC_GPR_ADDR      = 0x30340000,
    FSL_IMX7_IOMUXC_GPR_SIZE      = (4 * KiB),

    FSL_IMX7_IOMUXC_ADDR          = 0x30330000,
    FSL_IMX7_IOMUXC_SIZE          = (4 * KiB),

    FSL_IMX7_KPP_ADDR             = 0x30320000,
    FSL_IMX7_KPP_SIZE             = (4 * KiB),

    FSL_IMX7_ROMCP_ADDR           = 0x30310000,
    FSL_IMX7_ROMCP_SIZE           = (4 * KiB),

    FSL_IMX7_GPT4_ADDR            = 0x30300000,
    FSL_IMX7_GPT3_ADDR            = 0x302F0000,
    FSL_IMX7_GPT2_ADDR            = 0x302E0000,
    FSL_IMX7_GPT1_ADDR            = 0x302D0000,

    FSL_IMX7_IOMUXC_LPSR_ADDR     = 0x302C0000,
    FSL_IMX7_IOMUXC_LPSR_SIZE     = (4 * KiB),

    FSL_IMX7_WDOG4_ADDR           = 0x302B0000,
    FSL_IMX7_WDOG3_ADDR           = 0x302A0000,
    FSL_IMX7_WDOG2_ADDR           = 0x30290000,
    FSL_IMX7_WDOG1_ADDR           = 0x30280000,

    FSL_IMX7_IOMUXC_LPSR_GPR_ADDR = 0x30270000,

    FSL_IMX7_GPIO7_ADDR           = 0x30260000,
    FSL_IMX7_GPIO6_ADDR           = 0x30250000,
    FSL_IMX7_GPIO5_ADDR           = 0x30240000,
    FSL_IMX7_GPIO4_ADDR           = 0x30230000,
    FSL_IMX7_GPIO3_ADDR           = 0x30220000,
    FSL_IMX7_GPIO2_ADDR           = 0x30210000,
    FSL_IMX7_GPIO1_ADDR           = 0x30200000,

    FSL_IMX7_AIPS1_CONF_ADDR      = 0x301F0000,
    FSL_IMX7_AIPS1_CONF_SIZE      = (64 * KiB),

    FSL_IMX7_A7MPCORE_DAP_ADDR    = 0x30000000,
    FSL_IMX7_A7MPCORE_DAP_SIZE    = (1 * MiB),

    /* AIPS-1 End */

    FSL_IMX7_EIM_CS0_ADDR         = 0x28000000,
    FSL_IMX7_EIM_CS0_SIZE         = (128 * MiB),

    FSL_IMX7_OCRAM_PXP_ADDR       = 0x00940000,
    FSL_IMX7_OCRAM_PXP_SIZE       = (32 * KiB),

    FSL_IMX7_OCRAM_EPDC_ADDR      = 0x00920000,
    FSL_IMX7_OCRAM_EPDC_SIZE      = (128 * KiB),

    FSL_IMX7_OCRAM_MEM_ADDR       = 0x00900000,
    FSL_IMX7_OCRAM_MEM_SIZE       = (128 * KiB),

    FSL_IMX7_TCMU_ADDR            = 0x00800000,
    FSL_IMX7_TCMU_SIZE            = (32 * KiB),

    FSL_IMX7_TCML_ADDR            = 0x007F8000,
    FSL_IMX7_TCML_SIZE            = (32 * KiB),

    FSL_IMX7_OCRAM_S_ADDR         = 0x00180000,
    FSL_IMX7_OCRAM_S_SIZE         = (32 * KiB),

    FSL_IMX7_CAAM_MEM_ADDR        = 0x00100000,
    FSL_IMX7_CAAM_MEM_SIZE        = (32 * KiB),

    FSL_IMX7_ROM_ADDR             = 0x00000000,
    FSL_IMX7_ROM_SIZE             = (96 * KiB),
};

enum FslIMX7IRQs {
    FSL_IMX7_USDHC1_IRQ   = 22,
    FSL_IMX7_USDHC2_IRQ   = 23,
    FSL_IMX7_USDHC3_IRQ   = 24,

    FSL_IMX7_UART1_IRQ    = 26,
    FSL_IMX7_UART2_IRQ    = 27,
    FSL_IMX7_UART3_IRQ    = 28,
    FSL_IMX7_UART4_IRQ    = 29,
    FSL_IMX7_UART5_IRQ    = 30,
    FSL_IMX7_UART6_IRQ    = 16,

    FSL_IMX7_ECSPI1_IRQ   = 31,
    FSL_IMX7_ECSPI2_IRQ   = 32,
    FSL_IMX7_ECSPI3_IRQ   = 33,
    FSL_IMX7_ECSPI4_IRQ   = 34,

    FSL_IMX7_I2C1_IRQ     = 35,
    FSL_IMX7_I2C2_IRQ     = 36,
    FSL_IMX7_I2C3_IRQ     = 37,
    FSL_IMX7_I2C4_IRQ     = 38,

    FSL_IMX7_USB1_IRQ     = 43,
    FSL_IMX7_USB2_IRQ     = 42,
    FSL_IMX7_USB3_IRQ     = 40,

    FSL_IMX7_GPT1_IRQ     = 55,
    FSL_IMX7_GPT2_IRQ     = 54,
    FSL_IMX7_GPT3_IRQ     = 53,
    FSL_IMX7_GPT4_IRQ     = 52,

    FSL_IMX7_GPIO1_LOW_IRQ  = 64,
    FSL_IMX7_GPIO1_HIGH_IRQ = 65,
    FSL_IMX7_GPIO2_LOW_IRQ  = 66,
    FSL_IMX7_GPIO2_HIGH_IRQ = 67,
    FSL_IMX7_GPIO3_LOW_IRQ  = 68,
    FSL_IMX7_GPIO3_HIGH_IRQ = 69,
    FSL_IMX7_GPIO4_LOW_IRQ  = 70,
    FSL_IMX7_GPIO4_HIGH_IRQ = 71,
    FSL_IMX7_GPIO5_LOW_IRQ  = 72,
    FSL_IMX7_GPIO5_HIGH_IRQ = 73,
    FSL_IMX7_GPIO6_LOW_IRQ  = 74,
    FSL_IMX7_GPIO6_HIGH_IRQ = 75,
    FSL_IMX7_GPIO7_LOW_IRQ  = 76,
    FSL_IMX7_GPIO7_HIGH_IRQ = 77,

    FSL_IMX7_WDOG1_IRQ    = 78,
    FSL_IMX7_WDOG2_IRQ    = 79,
    FSL_IMX7_WDOG3_IRQ    = 10,
    FSL_IMX7_WDOG4_IRQ    = 109,

    FSL_IMX7_PCI_INTA_IRQ = 125,
    FSL_IMX7_PCI_INTB_IRQ = 124,
    FSL_IMX7_PCI_INTC_IRQ = 123,
    FSL_IMX7_PCI_INTD_IRQ = 122,

    FSL_IMX7_UART7_IRQ    = 126,

#define FSL_IMX7_ENET_IRQ(i, n)  ((n) + ((i) ? 100 : 118))

    FSL_IMX7_MAX_IRQ      = 128,
};

#endif /* FSL_IMX7_H */
