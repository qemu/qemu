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

#include "hw/arm/boot.h"
#include "hw/cpu/a15mpcore.h"
#include "hw/intc/imx_gpcv2.h"
#include "hw/misc/imx7_ccm.h"
#include "hw/misc/imx7_snvs.h"
#include "hw/misc/imx7_gpr.h"
#include "hw/misc/imx6_src.h"
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
#include "hw/pci-host/designware.h"
#include "hw/usb/chipidea.h"
#include "exec/memory.h"
#include "cpu.h"

#define TYPE_FSL_IMX7 "fsl,imx7"
#define FSL_IMX7(obj) OBJECT_CHECK(FslIMX7State, (obj), TYPE_FSL_IMX7)

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
};

typedef struct FslIMX7State {
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
} FslIMX7State;

enum FslIMX7MemoryMap {
    FSL_IMX7_MMDC_ADDR            = 0x80000000,
    FSL_IMX7_MMDC_SIZE            = 2 * 1024 * 1024 * 1024UL,

    FSL_IMX7_GPIO1_ADDR           = 0x30200000,
    FSL_IMX7_GPIO2_ADDR           = 0x30210000,
    FSL_IMX7_GPIO3_ADDR           = 0x30220000,
    FSL_IMX7_GPIO4_ADDR           = 0x30230000,
    FSL_IMX7_GPIO5_ADDR           = 0x30240000,
    FSL_IMX7_GPIO6_ADDR           = 0x30250000,
    FSL_IMX7_GPIO7_ADDR           = 0x30260000,

    FSL_IMX7_IOMUXC_LPSR_GPR_ADDR = 0x30270000,

    FSL_IMX7_WDOG1_ADDR           = 0x30280000,
    FSL_IMX7_WDOG2_ADDR           = 0x30290000,
    FSL_IMX7_WDOG3_ADDR           = 0x302A0000,
    FSL_IMX7_WDOG4_ADDR           = 0x302B0000,

    FSL_IMX7_IOMUXC_LPSR_ADDR     = 0x302C0000,

    FSL_IMX7_GPT1_ADDR            = 0x302D0000,
    FSL_IMX7_GPT2_ADDR            = 0x302E0000,
    FSL_IMX7_GPT3_ADDR            = 0x302F0000,
    FSL_IMX7_GPT4_ADDR            = 0x30300000,

    FSL_IMX7_IOMUXC_ADDR          = 0x30330000,
    FSL_IMX7_IOMUXC_GPR_ADDR      = 0x30340000,
    FSL_IMX7_IOMUXCn_SIZE         = 0x1000,

    FSL_IMX7_ANALOG_ADDR          = 0x30360000,
    FSL_IMX7_SNVS_ADDR            = 0x30370000,
    FSL_IMX7_CCM_ADDR             = 0x30380000,

    FSL_IMX7_SRC_ADDR             = 0x30390000,
    FSL_IMX7_SRC_SIZE             = 0x1000,

    FSL_IMX7_ADC1_ADDR            = 0x30610000,
    FSL_IMX7_ADC2_ADDR            = 0x30620000,
    FSL_IMX7_ADCn_SIZE            = 0x1000,

    FSL_IMX7_PCIE_PHY_ADDR        = 0x306D0000,
    FSL_IMX7_PCIE_PHY_SIZE        = 0x10000,

    FSL_IMX7_GPC_ADDR             = 0x303A0000,

    FSL_IMX7_I2C1_ADDR            = 0x30A20000,
    FSL_IMX7_I2C2_ADDR            = 0x30A30000,
    FSL_IMX7_I2C3_ADDR            = 0x30A40000,
    FSL_IMX7_I2C4_ADDR            = 0x30A50000,

    FSL_IMX7_ECSPI1_ADDR          = 0x30820000,
    FSL_IMX7_ECSPI2_ADDR          = 0x30830000,
    FSL_IMX7_ECSPI3_ADDR          = 0x30840000,
    FSL_IMX7_ECSPI4_ADDR          = 0x30630000,

    FSL_IMX7_LCDIF_ADDR           = 0x30730000,
    FSL_IMX7_LCDIF_SIZE           = 0x1000,

    FSL_IMX7_UART1_ADDR           = 0x30860000,
    /*
     * Some versions of the reference manual claim that UART2 is @
     * 0x30870000, but experiments with HW + DT files in upstream
     * Linux kernel show that not to be true and that block is
     * acutally located @ 0x30890000
     */
    FSL_IMX7_UART2_ADDR           = 0x30890000,
    FSL_IMX7_UART3_ADDR           = 0x30880000,
    FSL_IMX7_UART4_ADDR           = 0x30A60000,
    FSL_IMX7_UART5_ADDR           = 0x30A70000,
    FSL_IMX7_UART6_ADDR           = 0x30A80000,
    FSL_IMX7_UART7_ADDR           = 0x30A90000,

    FSL_IMX7_ENET1_ADDR           = 0x30BE0000,
    FSL_IMX7_ENET2_ADDR           = 0x30BF0000,

    FSL_IMX7_USB1_ADDR            = 0x30B10000,
    FSL_IMX7_USBMISC1_ADDR        = 0x30B10200,
    FSL_IMX7_USB2_ADDR            = 0x30B20000,
    FSL_IMX7_USBMISC2_ADDR        = 0x30B20200,
    FSL_IMX7_USB3_ADDR            = 0x30B30000,
    FSL_IMX7_USBMISC3_ADDR        = 0x30B30200,
    FSL_IMX7_USBMISCn_SIZE        = 0x200,

    FSL_IMX7_USDHC1_ADDR          = 0x30B40000,
    FSL_IMX7_USDHC2_ADDR          = 0x30B50000,
    FSL_IMX7_USDHC3_ADDR          = 0x30B60000,

    FSL_IMX7_SDMA_ADDR            = 0x30BD0000,
    FSL_IMX7_SDMA_SIZE            = 0x1000,

    FSL_IMX7_A7MPCORE_ADDR        = 0x31000000,
    FSL_IMX7_A7MPCORE_DAP_ADDR    = 0x30000000,

    FSL_IMX7_PCIE_REG_ADDR        = 0x33800000,
    FSL_IMX7_PCIE_REG_SIZE        = 16 * 1024,

    FSL_IMX7_GPR_ADDR             = 0x30340000,

    FSL_IMX7_DMA_APBH_ADDR        = 0x33000000,
    FSL_IMX7_DMA_APBH_SIZE        = 0x2000,
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

    FSL_IMX7_PCI_INTA_IRQ = 125,
    FSL_IMX7_PCI_INTB_IRQ = 124,
    FSL_IMX7_PCI_INTC_IRQ = 123,
    FSL_IMX7_PCI_INTD_IRQ = 122,

    FSL_IMX7_UART7_IRQ    = 126,

#define FSL_IMX7_ENET_IRQ(i, n)  ((n) + ((i) ? 100 : 118))

    FSL_IMX7_MAX_IRQ      = 128,
};

#endif /* FSL_IMX7_H */
