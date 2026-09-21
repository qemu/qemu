/*
 * i.MX 8MM SoC Definitions
 *
 * Copyright (c) 2025, NXP Semiconductors
 * Author: Gaurav Sharma <gaurav.sharma_7@nxp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FSL_IMX8MM_H
#define FSL_IMX8MM_H

#include "cpu.h"
#include "hw/char/imx_serial.h"
#include "hw/gpio/imx_gpio.h"
#include "hw/i2c/imx_i2c.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/misc/imx7_snvs.h"
#include "hw/misc/imx8mp_analog.h"
#include "hw/misc/imx8mp_ccm.h"
#include "hw/net/imx_fec.h"
#include "hw/core/or-irq.h"
#include "hw/pci-host/designware.h"
#include "hw/pci-host/fsl_imx8m_phy.h"
#include "hw/sd/sdhci.h"
#include "hw/ssi/imx_spi.h"
#include "hw/timer/imx_gpt.h"
#include "hw/usb/hcd-dwc3.h"
#include "hw/watchdog/wdt_imx2.h"
#include "qom/object.h"
#include "qemu/units.h"

#define TYPE_FSL_IMX8MM "fsl-imx8mm"
OBJECT_DECLARE_SIMPLE_TYPE(FslImx8mmState, FSL_IMX8MM)

#define FSL_IMX8MM_RAM_START        0x40000000
#define FSL_IMX8MM_RAM_SIZE_MAX     (4 * GiB)

enum FslImx8mmConfiguration {
    FSL_IMX8MM_NUM_CPUS         = 4,
    FSL_IMX8MM_NUM_ECSPIS       = 3,
    FSL_IMX8MM_NUM_GPIOS        = 5,
    FSL_IMX8MM_NUM_GPTS         = 6,
    FSL_IMX8MM_NUM_I2CS         = 4,
    FSL_IMX8MM_NUM_IRQS         = 128,
    FSL_IMX8MM_NUM_UARTS        = 4,
    FSL_IMX8MM_NUM_USBS         = 2,
    FSL_IMX8MM_NUM_USDHCS       = 3,
    FSL_IMX8MM_NUM_WDTS         = 3,
};

struct FslImx8mmState {
    SysBusDevice   parent_obj;

    ARMCPU             cpu[FSL_IMX8MM_NUM_CPUS];
    GICv3State         gic;
    IMXGPTState        gpt[FSL_IMX8MM_NUM_GPTS];
    IMXGPIOState       gpio[FSL_IMX8MM_NUM_GPIOS];
    IMX8MPCCMState     ccm;
    IMX8MPAnalogState  analog;
    IMX7SNVSState      snvs;
    IMXSPIState        spi[FSL_IMX8MM_NUM_ECSPIS];
    IMXI2CState        i2c[FSL_IMX8MM_NUM_I2CS];
    IMXSerialState     uart[FSL_IMX8MM_NUM_UARTS];
    MemoryRegion ocram;
    IMXFECState        enet;
    SDHCIState         usdhc[FSL_IMX8MM_NUM_USDHCS];
    IMX2WdtState       wdt[FSL_IMX8MM_NUM_WDTS];
    USBDWC3            usb[FSL_IMX8MM_NUM_USBS];
    DesignwarePCIEHost pcie;
    FslImx8mPciePhyState   pcie_phy;
    OrIRQState         gpt5_gpt6_irq;

    uint32_t           phy_num;
    bool               phy_connected;
};

enum FslImx8mmMemoryRegions {
    FSL_IMX8MM_A53_DAP,
    FSL_IMX8MM_AIPS1_CONFIGURATION,
    FSL_IMX8MM_AIPS2_CONFIGURATION,
    FSL_IMX8MM_AIPS3_CONFIGURATION,
    FSL_IMX8MM_AIPS4_CONFIGURATION,
    FSL_IMX8MM_ANA_OSC,
    FSL_IMX8MM_ANA_PLL,
    FSL_IMX8MM_ANA_TSENSOR,
    FSL_IMX8MM_APBH_DMA,
    FSL_IMX8MM_BOOT_ROM,
    FSL_IMX8MM_BOOT_ROM_PROTECTED,
    FSL_IMX8MM_CAAM,
    FSL_IMX8MM_CAAM_MEM,
    FSL_IMX8MM_CCM,
    FSL_IMX8MM_CSU,
    FSL_IMX8MM_DDR_CTL,
    FSL_IMX8MM_DDR_PERF_MON,
    FSL_IMX8MM_DDR_PHY,
    FSL_IMX8MM_DDR_PHY_BROADCAST,
    FSL_IMX8MM_ECSPI1,
    FSL_IMX8MM_ECSPI2,
    FSL_IMX8MM_ECSPI3,
    FSL_IMX8MM_ENET1,
    FSL_IMX8MM_GIC_DIST,
    FSL_IMX8MM_GIC_REDIST,
    FSL_IMX8MM_GPC,
    FSL_IMX8MM_GPIO1,
    FSL_IMX8MM_GPIO2,
    FSL_IMX8MM_GPIO3,
    FSL_IMX8MM_GPIO4,
    FSL_IMX8MM_GPIO5,
    FSL_IMX8MM_GPT1,
    FSL_IMX8MM_GPT2,
    FSL_IMX8MM_GPT3,
    FSL_IMX8MM_GPT4,
    FSL_IMX8MM_GPT5,
    FSL_IMX8MM_GPT6,
    FSL_IMX8MM_GPU2D,
    FSL_IMX8MM_I2C1,
    FSL_IMX8MM_I2C2,
    FSL_IMX8MM_I2C3,
    FSL_IMX8MM_I2C4,
    FSL_IMX8MM_INTERCONNECT,
    FSL_IMX8MM_IOMUXC,
    FSL_IMX8MM_IOMUXC_GPR,
    FSL_IMX8MM_MEDIA_BLK_CTL,
    FSL_IMX8MM_LCDIF,
    FSL_IMX8MM_MIPI_CSI,
    FSL_IMX8MM_MIPI_DSI,
    FSL_IMX8MM_MU_A,
    FSL_IMX8MM_MU_B,
    FSL_IMX8MM_OCOTP_CTRL,
    FSL_IMX8MM_OCRAM,
    FSL_IMX8MM_OCRAM_S,
    FSL_IMX8MM_PCIE1,
    FSL_IMX8MM_PCIE1_MEM,
    FSL_IMX8MM_PCIE_PHY1,
    FSL_IMX8MM_PERFMON1,
    FSL_IMX8MM_PERFMON2,
    FSL_IMX8MM_PWM1,
    FSL_IMX8MM_PWM2,
    FSL_IMX8MM_PWM3,
    FSL_IMX8MM_PWM4,
    FSL_IMX8MM_QOSC,
    FSL_IMX8MM_QSPI,
    FSL_IMX8MM_QSPI1_RX_BUFFER,
    FSL_IMX8MM_QSPI1_TX_BUFFER,
    FSL_IMX8MM_QSPI_MEM,
    FSL_IMX8MM_RAM,
    FSL_IMX8MM_RDC,
    FSL_IMX8MM_SAI1,
    FSL_IMX8MM_SAI2,
    FSL_IMX8MM_SAI3,
    FSL_IMX8MM_SAI5,
    FSL_IMX8MM_SAI6,
    FSL_IMX8MM_SDMA1,
    FSL_IMX8MM_SDMA2,
    FSL_IMX8MM_SDMA3,
    FSL_IMX8MM_SEMAPHORE1,
    FSL_IMX8MM_SEMAPHORE2,
    FSL_IMX8MM_SEMAPHORE_HS,
    FSL_IMX8MM_SNVS_HP,
    FSL_IMX8MM_SPBA1,
    FSL_IMX8MM_SRC,
    FSL_IMX8MM_SYSCNT_CMP,
    FSL_IMX8MM_SYSCNT_CTRL,
    FSL_IMX8MM_SYSCNT_RD,
    FSL_IMX8MM_TCM_DTCM,
    FSL_IMX8MM_TCM_ITCM,
    FSL_IMX8MM_TZASC,
    FSL_IMX8MM_UART1,
    FSL_IMX8MM_UART2,
    FSL_IMX8MM_UART3,
    FSL_IMX8MM_UART4,
    FSL_IMX8MM_USB1,
    FSL_IMX8MM_USB2,
    FSL_IMX8MM_USB1_OTG,
    FSL_IMX8MM_USB2_OTG,
    FSL_IMX8MM_USDHC1,
    FSL_IMX8MM_USDHC2,
    FSL_IMX8MM_USDHC3,
    FSL_IMX8MM_VPU,
    FSL_IMX8MM_VPU_BLK_CTRL,
    FSL_IMX8MM_VPU_G1_DECODER,
    FSL_IMX8MM_VPU_G2_DECODER,
    FSL_IMX8MM_WDOG1,
    FSL_IMX8MM_WDOG2,
    FSL_IMX8MM_WDOG3,
};

enum FslImx8mmIrqs {
    FSL_IMX8MM_USDHC1_IRQ   = 22,
    FSL_IMX8MM_USDHC2_IRQ   = 23,
    FSL_IMX8MM_USDHC3_IRQ   = 24,

    FSL_IMX8MM_UART1_IRQ    = 26,
    FSL_IMX8MM_UART2_IRQ    = 27,
    FSL_IMX8MM_UART3_IRQ    = 28,
    FSL_IMX8MM_UART4_IRQ    = 29,

    FSL_IMX8MM_ECSPI1_IRQ   = 31,
    FSL_IMX8MM_ECSPI2_IRQ   = 32,
    FSL_IMX8MM_ECSPI3_IRQ   = 33,

    FSL_IMX8MM_I2C1_IRQ     = 35,
    FSL_IMX8MM_I2C2_IRQ     = 36,
    FSL_IMX8MM_I2C3_IRQ     = 37,
    FSL_IMX8MM_I2C4_IRQ     = 38,

    FSL_IMX8MM_USB1_IRQ     = 40,
    FSL_IMX8MM_USB2_IRQ     = 41,

    FSL_IMX8MM_GPT1_IRQ      = 55,
    FSL_IMX8MM_GPT2_IRQ      = 54,
    FSL_IMX8MM_GPT3_IRQ      = 53,
    FSL_IMX8MM_GPT4_IRQ      = 52,
    FSL_IMX8MM_GPT5_GPT6_IRQ = 51,

    FSL_IMX8MM_GPIO1_LOW_IRQ  = 64,
    FSL_IMX8MM_GPIO1_HIGH_IRQ = 65,
    FSL_IMX8MM_GPIO2_LOW_IRQ  = 66,
    FSL_IMX8MM_GPIO2_HIGH_IRQ = 67,
    FSL_IMX8MM_GPIO3_LOW_IRQ  = 68,
    FSL_IMX8MM_GPIO3_HIGH_IRQ = 69,
    FSL_IMX8MM_GPIO4_LOW_IRQ  = 70,
    FSL_IMX8MM_GPIO4_HIGH_IRQ = 71,
    FSL_IMX8MM_GPIO5_LOW_IRQ  = 72,
    FSL_IMX8MM_GPIO5_HIGH_IRQ = 73,

    FSL_IMX8MM_WDOG1_IRQ    = 78,
    FSL_IMX8MM_WDOG2_IRQ    = 79,
    FSL_IMX8MM_WDOG3_IRQ    = 10,

    FSL_IMX8MM_ENET1_MAC_IRQ      = 118,
    FSL_IMX8MM_ENET1_MAC_1588_IRQ = 121,

    FSL_IMX8MM_PCI_INTA_IRQ = 122,
    FSL_IMX8MM_PCI_INTB_IRQ = 123,
    FSL_IMX8MM_PCI_INTC_IRQ = 124,
    FSL_IMX8MM_PCI_INTD_IRQ = 125,
    FSL_IMX8MM_PCI_MSI_IRQ  = 127,
};

#endif /* FSL_IMX8MM_H */
