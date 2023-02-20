/*
 * Allwinner H3 System on Chip emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * The Allwinner H3 is a System on Chip containing four ARM Cortex-A7
 * processor cores. Features and specifications include DDR2/DDR3 memory,
 * SD/MMC storage cards, 10/100/1000Mbit Ethernet, USB 2.0, HDMI and
 * various I/O modules.
 *
 * This implementation is based on the following datasheet:
 *
 *   https://linux-sunxi.org/File:Allwinner_H3_Datasheet_V1.2.pdf
 *
 * The latest datasheet and more info can be found on the Linux Sunxi wiki:
 *
 *   https://linux-sunxi.org/H3
 */

#ifndef HW_ARM_ALLWINNER_H3_H
#define HW_ARM_ALLWINNER_H3_H

#include "qom/object.h"
#include "hw/arm/boot.h"
#include "hw/timer/allwinner-a10-pit.h"
#include "hw/intc/arm_gic.h"
#include "hw/misc/allwinner-h3-ccu.h"
#include "hw/misc/allwinner-cpucfg.h"
#include "hw/misc/allwinner-h3-dramc.h"
#include "hw/misc/allwinner-h3-sysctrl.h"
#include "hw/misc/allwinner-sid.h"
#include "hw/sd/allwinner-sdhost.h"
#include "hw/net/allwinner-sun8i-emac.h"
#include "hw/rtc/allwinner-rtc.h"
#include "hw/i2c/allwinner-i2c.h"
#include "target/arm/cpu.h"
#include "sysemu/block-backend.h"

/**
 * Allwinner H3 device list
 *
 * This enumeration is can be used refer to a particular device in the
 * Allwinner H3 SoC. For example, the physical memory base address for
 * each device can be found in the AwH3State object in the memmap member
 * using the device enum value as index.
 *
 * @see AwH3State
 */
enum {
    AW_H3_DEV_SRAM_A1,
    AW_H3_DEV_SRAM_A2,
    AW_H3_DEV_SRAM_C,
    AW_H3_DEV_SYSCTRL,
    AW_H3_DEV_MMC0,
    AW_H3_DEV_SID,
    AW_H3_DEV_EHCI0,
    AW_H3_DEV_OHCI0,
    AW_H3_DEV_EHCI1,
    AW_H3_DEV_OHCI1,
    AW_H3_DEV_EHCI2,
    AW_H3_DEV_OHCI2,
    AW_H3_DEV_EHCI3,
    AW_H3_DEV_OHCI3,
    AW_H3_DEV_CCU,
    AW_H3_DEV_PIT,
    AW_H3_DEV_UART0,
    AW_H3_DEV_UART1,
    AW_H3_DEV_UART2,
    AW_H3_DEV_UART3,
    AW_H3_DEV_EMAC,
    AW_H3_DEV_TWI0,
    AW_H3_DEV_TWI1,
    AW_H3_DEV_TWI2,
    AW_H3_DEV_DRAMCOM,
    AW_H3_DEV_DRAMCTL,
    AW_H3_DEV_DRAMPHY,
    AW_H3_DEV_GIC_DIST,
    AW_H3_DEV_GIC_CPU,
    AW_H3_DEV_GIC_HYP,
    AW_H3_DEV_GIC_VCPU,
    AW_H3_DEV_RTC,
    AW_H3_DEV_CPUCFG,
    AW_H3_DEV_R_TWI,
    AW_H3_DEV_SDRAM
};

/** Total number of CPU cores in the H3 SoC */
#define AW_H3_NUM_CPUS      (4)

/**
 * Allwinner H3 object model
 * @{
 */

/** Object type for the Allwinner H3 SoC */
#define TYPE_AW_H3 "allwinner-h3"

/** Convert input object to Allwinner H3 state object */
OBJECT_DECLARE_SIMPLE_TYPE(AwH3State, AW_H3)

/** @} */

/**
 * Allwinner H3 object
 *
 * This struct contains the state of all the devices
 * which are currently emulated by the H3 SoC code.
 */
struct AwH3State {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    ARMCPU cpus[AW_H3_NUM_CPUS];
    const hwaddr *memmap;
    AwA10PITState timer;
    AwH3ClockCtlState ccu;
    AwCpuCfgState cpucfg;
    AwH3DramCtlState dramc;
    AwH3SysCtrlState sysctrl;
    AwSidState sid;
    AwSdHostState mmc0;
    AWI2CState i2c0;
    AWI2CState i2c1;
    AWI2CState i2c2;
    AWI2CState r_twi;
    AwSun8iEmacState emac;
    AwRtcState rtc;
    GICState gic;
    MemoryRegion sram_a1;
    MemoryRegion sram_a2;
    MemoryRegion sram_c;
};

/**
 * Emulate Boot ROM firmware setup functionality.
 *
 * A real Allwinner H3 SoC contains a Boot ROM
 * which is the first code that runs right after
 * the SoC is powered on. The Boot ROM is responsible
 * for loading user code (e.g. a bootloader) from any
 * of the supported external devices and writing the
 * downloaded code to internal SRAM. After loading the SoC
 * begins executing the code written to SRAM.
 *
 * This function emulates the Boot ROM by copying 32 KiB
 * of data from the given block device and writes it to
 * the start of the first internal SRAM memory.
 *
 * @s: Allwinner H3 state object pointer
 * @blk: Block backend device object pointer
 */
void allwinner_h3_bootrom_setup(AwH3State *s, BlockBackend *blk);

#endif /* HW_ARM_ALLWINNER_H3_H */
