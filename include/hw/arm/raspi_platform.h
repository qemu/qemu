/*
 * bcm2708 aka bcm2835/2836 aka Raspberry Pi/Pi2 SoC platform defines
 *
 * These definitions are derived from those in Raspbian Linux at
 * arch/arm/mach-{bcm2708,bcm2709}/include/mach/platform.h
 * where they carry the following notice:
 *
 * Copyright (C) 2010 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef HW_ARM_RASPI_PLATFORM_H
#define HW_ARM_RASPI_PLATFORM_H

#define MSYNC_OFFSET            0x0000   /* Multicore Sync Block */
#define IC0_OFFSET              0x2000
#define ST_OFFSET               0x3000   /* System Timer */
#define MPHI_OFFSET             0x6000   /* Message-based Parallel Host Intf. */
#define DMA_OFFSET              0x7000   /* DMA controller, channels 0-14 */
#define ARM_OFFSET              0xB000   /* BCM2708 ARM control block */
#define ARMCTRL_OFFSET          (ARM_OFFSET + 0x000)
#define ARMCTRL_IC_OFFSET       (ARM_OFFSET + 0x200) /* Interrupt controller */
#define ARMCTRL_TIMER0_1_OFFSET (ARM_OFFSET + 0x400) /* Timer 0 and 1 */
#define ARMCTRL_0_SBM_OFFSET    (ARM_OFFSET + 0x800) /* User 0 (ARM) Semaphores
                                                      * Doorbells & Mailboxes */
#define CPRMAN_OFFSET           0x100000 /* Power Management, Watchdog */
#define CM_OFFSET               0x101000 /* Clock Management */
#define A2W_OFFSET              0x102000 /* Reset controller */
#define AVS_OFFSET              0x103000 /* Audio Video Standard */
#define RNG_OFFSET              0x104000
#define GPIO_OFFSET             0x200000
#define UART0_OFFSET            0x201000
#define MMCI0_OFFSET            0x202000
#define I2S_OFFSET              0x203000
#define SPI0_OFFSET             0x204000
#define BSC0_OFFSET             0x205000 /* BSC0 I2C/TWI */
#define OTP_OFFSET              0x20f000
#define THERMAL_OFFSET          0x212000
#define BSC_SL_OFFSET           0x214000 /* SPI slave */
#define AUX_OFFSET              0x215000 /* AUX: UART1/SPI1/SPI2 */
#define EMMC1_OFFSET            0x300000
#define SMI_OFFSET              0x600000
#define BSC1_OFFSET             0x804000 /* BSC1 I2C/TWI */
#define BSC2_OFFSET             0x805000 /* BSC2 I2C/TWI */
#define DBUS_OFFSET             0x900000
#define AVE0_OFFSET             0x910000
#define USB_OTG_OFFSET          0x980000 /* DTC_OTG USB controller */
#define SDRAMC_OFFSET           0xe00000
#define DMA15_OFFSET            0xE05000 /* DMA controller, channel 15 */

/* GPU interrupts */
#define INTERRUPT_TIMER0               0
#define INTERRUPT_TIMER1               1
#define INTERRUPT_TIMER2               2
#define INTERRUPT_TIMER3               3
#define INTERRUPT_CODEC0               4
#define INTERRUPT_CODEC1               5
#define INTERRUPT_CODEC2               6
#define INTERRUPT_JPEG                 7
#define INTERRUPT_ISP                  8
#define INTERRUPT_USB                  9
#define INTERRUPT_3D                   10
#define INTERRUPT_TRANSPOSER           11
#define INTERRUPT_MULTICORESYNC0       12
#define INTERRUPT_MULTICORESYNC1       13
#define INTERRUPT_MULTICORESYNC2       14
#define INTERRUPT_MULTICORESYNC3       15
#define INTERRUPT_DMA0                 16
#define INTERRUPT_DMA1                 17
#define INTERRUPT_DMA2                 18
#define INTERRUPT_DMA3                 19
#define INTERRUPT_DMA4                 20
#define INTERRUPT_DMA5                 21
#define INTERRUPT_DMA6                 22
#define INTERRUPT_DMA7                 23
#define INTERRUPT_DMA8                 24
#define INTERRUPT_DMA9                 25
#define INTERRUPT_DMA10                26
#define INTERRUPT_DMA11                27
#define INTERRUPT_DMA12                28
#define INTERRUPT_AUX                  29
#define INTERRUPT_ARM                  30
#define INTERRUPT_VPUDMA               31
#define INTERRUPT_HOSTPORT             32
#define INTERRUPT_VIDEOSCALER          33
#define INTERRUPT_CCP2TX               34
#define INTERRUPT_SDC                  35
#define INTERRUPT_DSI0                 36
#define INTERRUPT_AVE                  37
#define INTERRUPT_CAM0                 38
#define INTERRUPT_CAM1                 39
#define INTERRUPT_HDMI0                40
#define INTERRUPT_HDMI1                41
#define INTERRUPT_PIXELVALVE1          42
#define INTERRUPT_I2CSPISLV            43
#define INTERRUPT_DSI1                 44
#define INTERRUPT_PWA0                 45
#define INTERRUPT_PWA1                 46
#define INTERRUPT_CPR                  47
#define INTERRUPT_SMI                  48
#define INTERRUPT_GPIO0                49
#define INTERRUPT_GPIO1                50
#define INTERRUPT_GPIO2                51
#define INTERRUPT_GPIO3                52
#define INTERRUPT_I2C                  53
#define INTERRUPT_SPI                  54
#define INTERRUPT_I2SPCM               55
#define INTERRUPT_SDIO                 56
#define INTERRUPT_UART0                57
#define INTERRUPT_SLIMBUS              58
#define INTERRUPT_VEC                  59
#define INTERRUPT_CPG                  60
#define INTERRUPT_RNG                  61
#define INTERRUPT_ARASANSDIO           62
#define INTERRUPT_AVSPMON              63

/* ARM CPU IRQs use a private number space */
#define INTERRUPT_ARM_TIMER            0
#define INTERRUPT_ARM_MAILBOX          1
#define INTERRUPT_ARM_DOORBELL_0       2
#define INTERRUPT_ARM_DOORBELL_1       3
#define INTERRUPT_VPU0_HALTED          4
#define INTERRUPT_VPU1_HALTED          5
#define INTERRUPT_ILLEGAL_TYPE0        6
#define INTERRUPT_ILLEGAL_TYPE1        7

#endif
