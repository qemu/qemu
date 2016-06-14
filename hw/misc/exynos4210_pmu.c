/*
 *  Exynos4210 Power Management Unit (PMU) Emulation
 *
 *  Copyright (C) 2011 Samsung Electronics Co Ltd.
 *    Maksim Kozlov <m.kozlov@samsung.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This model implements PMU registers just as a bulk of memory. Currently,
 * the only reason this device exists is that secondary CPU boot loader
 * uses PMU INFORM5 register as a holding pen.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"

#ifndef DEBUG_PMU
#define DEBUG_PMU           0
#endif

#ifndef DEBUG_PMU_EXTEND
#define DEBUG_PMU_EXTEND    0
#endif

#if DEBUG_PMU
#define  PRINT_DEBUG(fmt, args...)  \
        do { \
            fprintf(stderr, "  [%s:%d]   "fmt, __func__, __LINE__, ##args); \
        } while (0)

#if DEBUG_PMU_EXTEND
#define  PRINT_DEBUG_EXTEND(fmt, args...) \
        do { \
            fprintf(stderr, "  [%s:%d]   "fmt, __func__, __LINE__, ##args); \
        } while (0)
#else
#define  PRINT_DEBUG_EXTEND(fmt, args...)  do {} while (0)
#endif /* EXTEND */

#else
#define  PRINT_DEBUG(fmt, args...)   do {} while (0)
#define  PRINT_DEBUG_EXTEND(fmt, args...)  do {} while (0)
#endif

/*
 *  Offsets for PMU registers
 */
#define OM_STAT                  0x0000 /* OM status register */
#define RTC_CLKO_SEL             0x000C /* Controls RTCCLKOUT */
#define GNSS_RTC_OUT_CTRL        0x0010 /* Controls GNSS_RTC_OUT */
/* Decides whether system-level low-power mode is used. */
#define SYSTEM_POWER_DOWN_CTRL   0x0200
/* Sets control options for CENTRAL_SEQ */
#define SYSTEM_POWER_DOWN_OPTION 0x0208
#define SWRESET                  0x0400 /* Generate software reset */
#define RST_STAT                 0x0404 /* Reset status register */
#define WAKEUP_STAT              0x0600 /* Wakeup status register  */
#define EINT_WAKEUP_MASK         0x0604 /* Configure External INTerrupt mask */
#define WAKEUP_MASK              0x0608 /* Configure wakeup source mask */
#define HDMI_PHY_CONTROL         0x0700 /* HDMI PHY control register */
#define USBDEVICE_PHY_CONTROL    0x0704 /* USB Device PHY control register */
#define USBHOST_PHY_CONTROL      0x0708 /* USB HOST PHY control register */
#define DAC_PHY_CONTROL          0x070C /* DAC control register  */
#define MIPI_PHY0_CONTROL        0x0710 /* MIPI PHY control register */
#define MIPI_PHY1_CONTROL        0x0714 /* MIPI PHY control register */
#define ADC_PHY_CONTROL          0x0718 /* TS-ADC control register */
#define PCIe_PHY_CONTROL         0x071C /* TS-PCIe control register */
#define SATA_PHY_CONTROL         0x0720 /* TS-SATA control register */
#define INFORM0                  0x0800 /* Information register 0  */
#define INFORM1                  0x0804 /* Information register 1  */
#define INFORM2                  0x0808 /* Information register 2  */
#define INFORM3                  0x080C /* Information register 3  */
#define INFORM4                  0x0810 /* Information register 4  */
#define INFORM5                  0x0814 /* Information register 5  */
#define INFORM6                  0x0818 /* Information register 6  */
#define INFORM7                  0x081C /* Information register 7  */
#define PMU_DEBUG                0x0A00 /* PMU debug register */
/* Registers to set system-level low-power option */
#define ARM_CORE0_SYS_PWR_REG              0x1000
#define ARM_CORE1_SYS_PWR_REG              0x1010
#define ARM_COMMON_SYS_PWR_REG             0x1080
#define ARM_CPU_L2_0_SYS_PWR_REG           0x10C0
#define ARM_CPU_L2_1_SYS_PWR_REG           0x10C4
#define CMU_ACLKSTOP_SYS_PWR_REG           0x1100
#define CMU_SCLKSTOP_SYS_PWR_REG           0x1104
#define CMU_RESET_SYS_PWR_REG              0x110C
#define APLL_SYSCLK_SYS_PWR_REG            0x1120
#define MPLL_SYSCLK_SYS_PWR_REG            0x1124
#define VPLL_SYSCLK_SYS_PWR_REG            0x1128
#define EPLL_SYSCLK_SYS_PWR_REG            0x112C
#define CMU_CLKSTOP_GPS_ALIVE_SYS_PWR_REG  0x1138
#define CMU_RESET_GPS_ALIVE_SYS_PWR_REG    0x113C
#define CMU_CLKSTOP_CAM_SYS_PWR_REG        0x1140
#define CMU_CLKSTOP_TV_SYS_PWR_REG         0x1144
#define CMU_CLKSTOP_MFC_SYS_PWR_REG        0x1148
#define CMU_CLKSTOP_G3D_SYS_PWR_REG        0x114C
#define CMU_CLKSTOP_LCD0_SYS_PWR_REG       0x1150
#define CMU_CLKSTOP_LCD1_SYS_PWR_REG       0x1154
#define CMU_CLKSTOP_MAUDIO_SYS_PWR_REG     0x1158
#define CMU_CLKSTOP_GPS_SYS_PWR_REG        0x115C
#define CMU_RESET_CAM_SYS_PWR_REG          0x1160
#define CMU_RESET_TV_SYS_PWR_REG           0x1164
#define CMU_RESET_MFC_SYS_PWR_REG          0x1168
#define CMU_RESET_G3D_SYS_PWR_REG          0x116C
#define CMU_RESET_LCD0_SYS_PWR_REG         0x1170
#define CMU_RESET_LCD1_SYS_PWR_REG         0x1174
#define CMU_RESET_MAUDIO_SYS_PWR_REG       0x1178
#define CMU_RESET_GPS_SYS_PWR_REG          0x117C
#define TOP_BUS_SYS_PWR_REG                0x1180
#define TOP_RETENTION_SYS_PWR_REG          0x1184
#define TOP_PWR_SYS_PWR_REG                0x1188
#define LOGIC_RESET_SYS_PWR_REG            0x11A0
#define OneNANDXL_MEM_SYS_PWR_REG          0x11C0
#define MODEMIF_MEM_SYS_PWR_REG            0x11C4
#define USBDEVICE_MEM_SYS_PWR_REG          0x11CC
#define SDMMC_MEM_SYS_PWR_REG              0x11D0
#define CSSYS_MEM_SYS_PWR_REG              0x11D4
#define SECSS_MEM_SYS_PWR_REG              0x11D8
#define PCIe_MEM_SYS_PWR_REG               0x11E0
#define SATA_MEM_SYS_PWR_REG               0x11E4
#define PAD_RETENTION_DRAM_SYS_PWR_REG     0x1200
#define PAD_RETENTION_MAUDIO_SYS_PWR_REG   0x1204
#define PAD_RETENTION_GPIO_SYS_PWR_REG     0x1220
#define PAD_RETENTION_UART_SYS_PWR_REG     0x1224
#define PAD_RETENTION_MMCA_SYS_PWR_REG     0x1228
#define PAD_RETENTION_MMCB_SYS_PWR_REG     0x122C
#define PAD_RETENTION_EBIA_SYS_PWR_REG     0x1230
#define PAD_RETENTION_EBIB_SYS_PWR_REG     0x1234
#define PAD_ISOLATION_SYS_PWR_REG          0x1240
#define PAD_ALV_SEL_SYS_PWR_REG            0x1260
#define XUSBXTI_SYS_PWR_REG                0x1280
#define XXTI_SYS_PWR_REG                   0x1284
#define EXT_REGULATOR_SYS_PWR_REG          0x12C0
#define GPIO_MODE_SYS_PWR_REG              0x1300
#define GPIO_MODE_MAUDIO_SYS_PWR_REG       0x1340
#define CAM_SYS_PWR_REG                    0x1380
#define TV_SYS_PWR_REG                     0x1384
#define MFC_SYS_PWR_REG                    0x1388
#define G3D_SYS_PWR_REG                    0x138C
#define LCD0_SYS_PWR_REG                   0x1390
#define LCD1_SYS_PWR_REG                   0x1394
#define MAUDIO_SYS_PWR_REG                 0x1398
#define GPS_SYS_PWR_REG                    0x139C
#define GPS_ALIVE_SYS_PWR_REG              0x13A0
#define ARM_CORE0_CONFIGURATION 0x2000 /* Configure power mode of ARM_CORE0 */
#define ARM_CORE0_STATUS        0x2004 /* Check power mode of ARM_CORE0 */
#define ARM_CORE0_OPTION        0x2008 /* Sets control options for ARM_CORE0 */
#define ARM_CORE1_CONFIGURATION 0x2080 /* Configure power mode of ARM_CORE1 */
#define ARM_CORE1_STATUS        0x2084 /* Check power mode of ARM_CORE1 */
#define ARM_CORE1_OPTION        0x2088 /* Sets control options for ARM_CORE0 */
#define ARM_COMMON_OPTION       0x2408 /* Sets control options for ARM_COMMON */
/* Configure power mode of ARM_CPU_L2_0 */
#define ARM_CPU_L2_0_CONFIGURATION 0x2600
#define ARM_CPU_L2_0_STATUS        0x2604 /* Check power mode of ARM_CPU_L2_0 */
/* Configure power mode of ARM_CPU_L2_1 */
#define ARM_CPU_L2_1_CONFIGURATION 0x2620
#define ARM_CPU_L2_1_STATUS        0x2624 /* Check power mode of ARM_CPU_L2_1 */
/* Sets control options for PAD_RETENTION_MAUDIO */
#define PAD_RETENTION_MAUDIO_OPTION 0x3028
/* Sets control options for PAD_RETENTION_GPIO */
#define PAD_RETENTION_GPIO_OPTION   0x3108
/* Sets control options for PAD_RETENTION_UART */
#define PAD_RETENTION_UART_OPTION   0x3128
/* Sets control options for PAD_RETENTION_MMCA */
#define PAD_RETENTION_MMCA_OPTION   0x3148
/* Sets control options for PAD_RETENTION_MMCB */
#define PAD_RETENTION_MMCB_OPTION   0x3168
/* Sets control options for PAD_RETENTION_EBIA */
#define PAD_RETENTION_EBIA_OPTION   0x3188
/* Sets control options for PAD_RETENTION_EBIB */
#define PAD_RETENTION_EBIB_OPTION   0x31A8
#define PS_HOLD_CONTROL         0x330C /* PS_HOLD control register */
#define XUSBXTI_CONFIGURATION   0x3400 /* Configure the pad of XUSBXTI */
#define XUSBXTI_STATUS          0x3404 /* Check the pad of XUSBXTI */
/* Sets time required for XUSBXTI to be stabilized */
#define XUSBXTI_DURATION        0x341C
#define XXTI_CONFIGURATION      0x3420 /* Configure the pad of XXTI */
#define XXTI_STATUS             0x3424 /* Check the pad of XXTI */
/* Sets time required for XXTI to be stabilized */
#define XXTI_DURATION           0x343C
/* Sets time required for EXT_REGULATOR to be stabilized */
#define EXT_REGULATOR_DURATION  0x361C
#define CAM_CONFIGURATION       0x3C00 /* Configure power mode of CAM */
#define CAM_STATUS              0x3C04 /* Check power mode of CAM */
#define CAM_OPTION              0x3C08 /* Sets control options for CAM */
#define TV_CONFIGURATION        0x3C20 /* Configure power mode of TV */
#define TV_STATUS               0x3C24 /* Check power mode of TV */
#define TV_OPTION               0x3C28 /* Sets control options for TV */
#define MFC_CONFIGURATION       0x3C40 /* Configure power mode of MFC */
#define MFC_STATUS              0x3C44 /* Check power mode of MFC */
#define MFC_OPTION              0x3C48 /* Sets control options for MFC */
#define G3D_CONFIGURATION       0x3C60 /* Configure power mode of G3D */
#define G3D_STATUS              0x3C64 /* Check power mode of G3D */
#define G3D_OPTION              0x3C68 /* Sets control options for G3D */
#define LCD0_CONFIGURATION      0x3C80 /* Configure power mode of LCD0 */
#define LCD0_STATUS             0x3C84 /* Check power mode of LCD0 */
#define LCD0_OPTION             0x3C88 /* Sets control options for LCD0 */
#define LCD1_CONFIGURATION      0x3CA0 /* Configure power mode of LCD1 */
#define LCD1_STATUS             0x3CA4 /* Check power mode of LCD1 */
#define LCD1_OPTION             0x3CA8 /* Sets control options for LCD1 */
#define GPS_CONFIGURATION       0x3CE0 /* Configure power mode of GPS */
#define GPS_STATUS              0x3CE4 /* Check power mode of GPS */
#define GPS_OPTION              0x3CE8 /* Sets control options for GPS */
#define GPS_ALIVE_CONFIGURATION 0x3D00 /* Configure power mode of GPS */
#define GPS_ALIVE_STATUS        0x3D04 /* Check power mode of GPS */
#define GPS_ALIVE_OPTION        0x3D08 /* Sets control options for GPS */

#define EXYNOS4210_PMU_REGS_MEM_SIZE 0x3d0c

typedef struct Exynos4210PmuReg {
    const char  *name; /* for debug only */
    uint32_t     offset;
    uint32_t     reset_value;
} Exynos4210PmuReg;

static const Exynos4210PmuReg exynos4210_pmu_regs[] = {
    {"OM_STAT", OM_STAT, 0x00000000},
    {"RTC_CLKO_SEL", RTC_CLKO_SEL, 0x00000000},
    {"GNSS_RTC_OUT_CTRL", GNSS_RTC_OUT_CTRL, 0x00000001},
    {"SYSTEM_POWER_DOWN_CTRL", SYSTEM_POWER_DOWN_CTRL, 0x00010000},
    {"SYSTEM_POWER_DOWN_OPTION", SYSTEM_POWER_DOWN_OPTION, 0x03030000},
    {"SWRESET", SWRESET, 0x00000000},
    {"RST_STAT", RST_STAT, 0x00000000},
    {"WAKEUP_STAT", WAKEUP_STAT, 0x00000000},
    {"EINT_WAKEUP_MASK", EINT_WAKEUP_MASK, 0x00000000},
    {"WAKEUP_MASK", WAKEUP_MASK, 0x00000000},
    {"HDMI_PHY_CONTROL", HDMI_PHY_CONTROL, 0x00960000},
    {"USBDEVICE_PHY_CONTROL", USBDEVICE_PHY_CONTROL, 0x00000000},
    {"USBHOST_PHY_CONTROL", USBHOST_PHY_CONTROL, 0x00000000},
    {"DAC_PHY_CONTROL", DAC_PHY_CONTROL, 0x00000000},
    {"MIPI_PHY0_CONTROL", MIPI_PHY0_CONTROL, 0x00000000},
    {"MIPI_PHY1_CONTROL", MIPI_PHY1_CONTROL, 0x00000000},
    {"ADC_PHY_CONTROL", ADC_PHY_CONTROL, 0x00000001},
    {"PCIe_PHY_CONTROL", PCIe_PHY_CONTROL, 0x00000000},
    {"SATA_PHY_CONTROL", SATA_PHY_CONTROL, 0x00000000},
    {"INFORM0", INFORM0, 0x00000000},
    {"INFORM1", INFORM1, 0x00000000},
    {"INFORM2", INFORM2, 0x00000000},
    {"INFORM3", INFORM3, 0x00000000},
    {"INFORM4", INFORM4, 0x00000000},
    {"INFORM5", INFORM5, 0x00000000},
    {"INFORM6", INFORM6, 0x00000000},
    {"INFORM7", INFORM7, 0x00000000},
    {"PMU_DEBUG", PMU_DEBUG, 0x00000000},
    {"ARM_CORE0_SYS_PWR_REG", ARM_CORE0_SYS_PWR_REG, 0xFFFFFFFF},
    {"ARM_CORE1_SYS_PWR_REG", ARM_CORE1_SYS_PWR_REG, 0xFFFFFFFF},
    {"ARM_COMMON_SYS_PWR_REG", ARM_COMMON_SYS_PWR_REG, 0xFFFFFFFF},
    {"ARM_CPU_L2_0_SYS_PWR_REG", ARM_CPU_L2_0_SYS_PWR_REG, 0xFFFFFFFF},
    {"ARM_CPU_L2_1_SYS_PWR_REG", ARM_CPU_L2_1_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_ACLKSTOP_SYS_PWR_REG", CMU_ACLKSTOP_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_SCLKSTOP_SYS_PWR_REG", CMU_SCLKSTOP_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_RESET_SYS_PWR_REG", CMU_RESET_SYS_PWR_REG, 0xFFFFFFFF},
    {"APLL_SYSCLK_SYS_PWR_REG", APLL_SYSCLK_SYS_PWR_REG, 0xFFFFFFFF},
    {"MPLL_SYSCLK_SYS_PWR_REG", MPLL_SYSCLK_SYS_PWR_REG, 0xFFFFFFFF},
    {"VPLL_SYSCLK_SYS_PWR_REG", VPLL_SYSCLK_SYS_PWR_REG, 0xFFFFFFFF},
    {"EPLL_SYSCLK_SYS_PWR_REG", EPLL_SYSCLK_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_CLKSTOP_GPS_ALIVE_SYS_PWR_REG", CMU_CLKSTOP_GPS_ALIVE_SYS_PWR_REG,
            0xFFFFFFFF},
    {"CMU_RESET_GPS_ALIVE_SYS_PWR_REG", CMU_RESET_GPS_ALIVE_SYS_PWR_REG,
            0xFFFFFFFF},
    {"CMU_CLKSTOP_CAM_SYS_PWR_REG", CMU_CLKSTOP_CAM_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_CLKSTOP_TV_SYS_PWR_REG", CMU_CLKSTOP_TV_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_CLKSTOP_MFC_SYS_PWR_REG", CMU_CLKSTOP_MFC_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_CLKSTOP_G3D_SYS_PWR_REG", CMU_CLKSTOP_G3D_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_CLKSTOP_LCD0_SYS_PWR_REG", CMU_CLKSTOP_LCD0_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_CLKSTOP_LCD1_SYS_PWR_REG", CMU_CLKSTOP_LCD1_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_CLKSTOP_MAUDIO_SYS_PWR_REG", CMU_CLKSTOP_MAUDIO_SYS_PWR_REG,
            0xFFFFFFFF},
    {"CMU_CLKSTOP_GPS_SYS_PWR_REG", CMU_CLKSTOP_GPS_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_RESET_CAM_SYS_PWR_REG", CMU_RESET_CAM_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_RESET_TV_SYS_PWR_REG", CMU_RESET_TV_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_RESET_MFC_SYS_PWR_REG", CMU_RESET_MFC_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_RESET_G3D_SYS_PWR_REG", CMU_RESET_G3D_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_RESET_LCD0_SYS_PWR_REG", CMU_RESET_LCD0_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_RESET_LCD1_SYS_PWR_REG", CMU_RESET_LCD1_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_RESET_MAUDIO_SYS_PWR_REG", CMU_RESET_MAUDIO_SYS_PWR_REG, 0xFFFFFFFF},
    {"CMU_RESET_GPS_SYS_PWR_REG", CMU_RESET_GPS_SYS_PWR_REG, 0xFFFFFFFF},
    {"TOP_BUS_SYS_PWR_REG", TOP_BUS_SYS_PWR_REG, 0xFFFFFFFF},
    {"TOP_RETENTION_SYS_PWR_REG", TOP_RETENTION_SYS_PWR_REG, 0xFFFFFFFF},
    {"TOP_PWR_SYS_PWR_REG", TOP_PWR_SYS_PWR_REG, 0xFFFFFFFF},
    {"LOGIC_RESET_SYS_PWR_REG", LOGIC_RESET_SYS_PWR_REG, 0xFFFFFFFF},
    {"OneNANDXL_MEM_SYS_PWR_REG", OneNANDXL_MEM_SYS_PWR_REG, 0xFFFFFFFF},
    {"MODEMIF_MEM_SYS_PWR_REG", MODEMIF_MEM_SYS_PWR_REG, 0xFFFFFFFF},
    {"USBDEVICE_MEM_SYS_PWR_REG", USBDEVICE_MEM_SYS_PWR_REG, 0xFFFFFFFF},
    {"SDMMC_MEM_SYS_PWR_REG", SDMMC_MEM_SYS_PWR_REG, 0xFFFFFFFF},
    {"CSSYS_MEM_SYS_PWR_REG", CSSYS_MEM_SYS_PWR_REG, 0xFFFFFFFF},
    {"SECSS_MEM_SYS_PWR_REG", SECSS_MEM_SYS_PWR_REG, 0xFFFFFFFF},
    {"PCIe_MEM_SYS_PWR_REG", PCIe_MEM_SYS_PWR_REG, 0xFFFFFFFF},
    {"SATA_MEM_SYS_PWR_REG", SATA_MEM_SYS_PWR_REG, 0xFFFFFFFF},
    {"PAD_RETENTION_DRAM_SYS_PWR_REG", PAD_RETENTION_DRAM_SYS_PWR_REG,
            0xFFFFFFFF},
    {"PAD_RETENTION_MAUDIO_SYS_PWR_REG", PAD_RETENTION_MAUDIO_SYS_PWR_REG,
            0xFFFFFFFF},
    {"PAD_RETENTION_GPIO_SYS_PWR_REG", PAD_RETENTION_GPIO_SYS_PWR_REG,
            0xFFFFFFFF},
    {"PAD_RETENTION_UART_SYS_PWR_REG", PAD_RETENTION_UART_SYS_PWR_REG,
            0xFFFFFFFF},
    {"PAD_RETENTION_MMCA_SYS_PWR_REG", PAD_RETENTION_MMCA_SYS_PWR_REG,
            0xFFFFFFFF},
    {"PAD_RETENTION_MMCB_SYS_PWR_REG", PAD_RETENTION_MMCB_SYS_PWR_REG,
            0xFFFFFFFF},
    {"PAD_RETENTION_EBIA_SYS_PWR_REG", PAD_RETENTION_EBIA_SYS_PWR_REG,
            0xFFFFFFFF},
    {"PAD_RETENTION_EBIB_SYS_PWR_REG", PAD_RETENTION_EBIB_SYS_PWR_REG,
            0xFFFFFFFF},
    {"PAD_ISOLATION_SYS_PWR_REG", PAD_ISOLATION_SYS_PWR_REG, 0xFFFFFFFF},
    {"PAD_ALV_SEL_SYS_PWR_REG", PAD_ALV_SEL_SYS_PWR_REG, 0xFFFFFFFF},
    {"XUSBXTI_SYS_PWR_REG", XUSBXTI_SYS_PWR_REG, 0xFFFFFFFF},
    {"XXTI_SYS_PWR_REG", XXTI_SYS_PWR_REG, 0xFFFFFFFF},
    {"EXT_REGULATOR_SYS_PWR_REG", EXT_REGULATOR_SYS_PWR_REG, 0xFFFFFFFF},
    {"GPIO_MODE_SYS_PWR_REG", GPIO_MODE_SYS_PWR_REG, 0xFFFFFFFF},
    {"GPIO_MODE_MAUDIO_SYS_PWR_REG", GPIO_MODE_MAUDIO_SYS_PWR_REG, 0xFFFFFFFF},
    {"CAM_SYS_PWR_REG", CAM_SYS_PWR_REG, 0xFFFFFFFF},
    {"TV_SYS_PWR_REG", TV_SYS_PWR_REG, 0xFFFFFFFF},
    {"MFC_SYS_PWR_REG", MFC_SYS_PWR_REG, 0xFFFFFFFF},
    {"G3D_SYS_PWR_REG", G3D_SYS_PWR_REG, 0xFFFFFFFF},
    {"LCD0_SYS_PWR_REG", LCD0_SYS_PWR_REG, 0xFFFFFFFF},
    {"LCD1_SYS_PWR_REG", LCD1_SYS_PWR_REG, 0xFFFFFFFF},
    {"MAUDIO_SYS_PWR_REG", MAUDIO_SYS_PWR_REG, 0xFFFFFFFF},
    {"GPS_SYS_PWR_REG", GPS_SYS_PWR_REG, 0xFFFFFFFF},
    {"GPS_ALIVE_SYS_PWR_REG", GPS_ALIVE_SYS_PWR_REG, 0xFFFFFFFF},
    {"ARM_CORE0_CONFIGURATION", ARM_CORE0_CONFIGURATION, 0x00000003},
    {"ARM_CORE0_STATUS", ARM_CORE0_STATUS, 0x00030003},
    {"ARM_CORE0_OPTION", ARM_CORE0_OPTION, 0x01010001},
    {"ARM_CORE1_CONFIGURATION", ARM_CORE1_CONFIGURATION, 0x00000003},
    {"ARM_CORE1_STATUS", ARM_CORE1_STATUS, 0x00030003},
    {"ARM_CORE1_OPTION", ARM_CORE1_OPTION, 0x01010001},
    {"ARM_COMMON_OPTION", ARM_COMMON_OPTION, 0x00000001},
    {"ARM_CPU_L2_0_CONFIGURATION", ARM_CPU_L2_0_CONFIGURATION, 0x00000003},
    {"ARM_CPU_L2_0_STATUS", ARM_CPU_L2_0_STATUS, 0x00000003},
    {"ARM_CPU_L2_1_CONFIGURATION", ARM_CPU_L2_1_CONFIGURATION, 0x00000003},
    {"ARM_CPU_L2_1_STATUS", ARM_CPU_L2_1_STATUS, 0x00000003},
    {"PAD_RETENTION_MAUDIO_OPTION", PAD_RETENTION_MAUDIO_OPTION, 0x00000000},
    {"PAD_RETENTION_GPIO_OPTION", PAD_RETENTION_GPIO_OPTION, 0x00000000},
    {"PAD_RETENTION_UART_OPTION", PAD_RETENTION_UART_OPTION, 0x00000000},
    {"PAD_RETENTION_MMCA_OPTION", PAD_RETENTION_MMCA_OPTION, 0x00000000},
    {"PAD_RETENTION_MMCB_OPTION", PAD_RETENTION_MMCB_OPTION, 0x00000000},
    {"PAD_RETENTION_EBIA_OPTION", PAD_RETENTION_EBIA_OPTION, 0x00000000},
    {"PAD_RETENTION_EBIB_OPTION", PAD_RETENTION_EBIB_OPTION, 0x00000000},
    {"PS_HOLD_CONTROL", PS_HOLD_CONTROL, 0x00005200},
    {"XUSBXTI_CONFIGURATION", XUSBXTI_CONFIGURATION, 0x00000001},
    {"XUSBXTI_STATUS", XUSBXTI_STATUS, 0x00000001},
    {"XUSBXTI_DURATION", XUSBXTI_DURATION, 0xFFF00000},
    {"XXTI_CONFIGURATION", XXTI_CONFIGURATION, 0x00000001},
    {"XXTI_STATUS", XXTI_STATUS, 0x00000001},
    {"XXTI_DURATION", XXTI_DURATION, 0xFFF00000},
    {"EXT_REGULATOR_DURATION", EXT_REGULATOR_DURATION, 0xFFF03FFF},
    {"CAM_CONFIGURATION", CAM_CONFIGURATION, 0x00000007},
    {"CAM_STATUS", CAM_STATUS, 0x00060007},
    {"CAM_OPTION", CAM_OPTION, 0x00000001},
    {"TV_CONFIGURATION", TV_CONFIGURATION, 0x00000007},
    {"TV_STATUS", TV_STATUS, 0x00060007},
    {"TV_OPTION", TV_OPTION, 0x00000001},
    {"MFC_CONFIGURATION", MFC_CONFIGURATION, 0x00000007},
    {"MFC_STATUS", MFC_STATUS, 0x00060007},
    {"MFC_OPTION", MFC_OPTION, 0x00000001},
    {"G3D_CONFIGURATION", G3D_CONFIGURATION, 0x00000007},
    {"G3D_STATUS", G3D_STATUS, 0x00060007},
    {"G3D_OPTION", G3D_OPTION, 0x00000001},
    {"LCD0_CONFIGURATION", LCD0_CONFIGURATION, 0x00000007},
    {"LCD0_STATUS", LCD0_STATUS, 0x00060007},
    {"LCD0_OPTION", LCD0_OPTION, 0x00000001},
    {"LCD1_CONFIGURATION", LCD1_CONFIGURATION, 0x00000007},
    {"LCD1_STATUS", LCD1_STATUS, 0x00060007},
    {"LCD1_OPTION", LCD1_OPTION, 0x00000001},
    {"GPS_CONFIGURATION", GPS_CONFIGURATION, 0x00000007},
    {"GPS_STATUS", GPS_STATUS, 0x00060007},
    {"GPS_OPTION", GPS_OPTION, 0x00000001},
    {"GPS_ALIVE_CONFIGURATION", GPS_ALIVE_CONFIGURATION, 0x00000007},
    {"GPS_ALIVE_STATUS", GPS_ALIVE_STATUS, 0x00060007},
    {"GPS_ALIVE_OPTION", GPS_ALIVE_OPTION, 0x00000001},
};

#define PMU_NUM_OF_REGISTERS ARRAY_SIZE(exynos4210_pmu_regs)

#define TYPE_EXYNOS4210_PMU "exynos4210.pmu"
#define EXYNOS4210_PMU(obj) \
    OBJECT_CHECK(Exynos4210PmuState, (obj), TYPE_EXYNOS4210_PMU)

typedef struct Exynos4210PmuState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t reg[PMU_NUM_OF_REGISTERS];
} Exynos4210PmuState;

static uint64_t exynos4210_pmu_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    Exynos4210PmuState *s = (Exynos4210PmuState *)opaque;
    unsigned i;
    const Exynos4210PmuReg *reg_p = exynos4210_pmu_regs;

    for (i = 0; i < PMU_NUM_OF_REGISTERS; i++) {
        if (reg_p->offset == offset) {
            PRINT_DEBUG_EXTEND("%s [0x%04x] -> 0x%04x\n", reg_p->name,
                                   (uint32_t)offset, s->reg[i]);
            return s->reg[i];
        }
        reg_p++;
    }
    PRINT_DEBUG("QEMU PMU ERROR: bad read offset 0x%04x\n", (uint32_t)offset);
    return 0;
}

static void exynos4210_pmu_write(void *opaque, hwaddr offset,
                                 uint64_t val, unsigned size)
{
    Exynos4210PmuState *s = (Exynos4210PmuState *)opaque;
    unsigned i;
    const Exynos4210PmuReg *reg_p = exynos4210_pmu_regs;

    for (i = 0; i < PMU_NUM_OF_REGISTERS; i++) {
        if (reg_p->offset == offset) {
            PRINT_DEBUG_EXTEND("%s <0x%04x> <- 0x%04x\n", reg_p->name,
                    (uint32_t)offset, (uint32_t)val);
            s->reg[i] = val;
            return;
        }
        reg_p++;
    }
    PRINT_DEBUG("QEMU PMU ERROR: bad write offset 0x%04x\n", (uint32_t)offset);
}

static const MemoryRegionOps exynos4210_pmu_ops = {
    .read = exynos4210_pmu_read,
    .write = exynos4210_pmu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false
    }
};

static void exynos4210_pmu_reset(DeviceState *dev)
{
    Exynos4210PmuState *s = EXYNOS4210_PMU(dev);
    unsigned i;

    /* Set default values for registers */
    for (i = 0; i < PMU_NUM_OF_REGISTERS; i++) {
        s->reg[i] = exynos4210_pmu_regs[i].reset_value;
    }
}

static void exynos4210_pmu_init(Object *obj)
{
    Exynos4210PmuState *s = EXYNOS4210_PMU(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    /* memory mapping */
    memory_region_init_io(&s->iomem, obj, &exynos4210_pmu_ops, s,
                          "exynos4210.pmu", EXYNOS4210_PMU_REGS_MEM_SIZE);
    sysbus_init_mmio(dev, &s->iomem);
}

static const VMStateDescription exynos4210_pmu_vmstate = {
    .name = "exynos4210.pmu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg, Exynos4210PmuState, PMU_NUM_OF_REGISTERS),
        VMSTATE_END_OF_LIST()
    }
};

static void exynos4210_pmu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = exynos4210_pmu_reset;
    dc->vmsd = &exynos4210_pmu_vmstate;
}

static const TypeInfo exynos4210_pmu_info = {
    .name          = TYPE_EXYNOS4210_PMU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Exynos4210PmuState),
    .instance_init = exynos4210_pmu_init,
    .class_init    = exynos4210_pmu_class_init,
};

static void exynos4210_pmu_register(void)
{
    type_register_static(&exynos4210_pmu_info);
}

type_init(exynos4210_pmu_register)
