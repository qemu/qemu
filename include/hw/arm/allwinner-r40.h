/*
 * Allwinner R40/A40i/T3 System on Chip emulation
 *
 * Copyright (C) 2023 qianfan Zhao <qianfanguijin@163.com>
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

#ifndef HW_ARM_ALLWINNER_R40_H
#define HW_ARM_ALLWINNER_R40_H

#include "qom/object.h"
#include "hw/arm/boot.h"
#include "hw/timer/allwinner-a10-pit.h"
#include "hw/intc/arm_gic.h"
#include "hw/sd/allwinner-sdhost.h"
#include "hw/misc/allwinner-r40-ccu.h"
#include "hw/misc/allwinner-r40-dramc.h"
#include "hw/misc/allwinner-sramc.h"
#include "hw/i2c/allwinner-i2c.h"
#include "hw/net/allwinner_emac.h"
#include "hw/net/allwinner-sun8i-emac.h"
#include "target/arm/cpu.h"
#include "sysemu/block-backend.h"

enum {
    AW_R40_DEV_SRAM_A1,
    AW_R40_DEV_SRAM_A2,
    AW_R40_DEV_SRAM_A3,
    AW_R40_DEV_SRAM_A4,
    AW_R40_DEV_SRAMC,
    AW_R40_DEV_EMAC,
    AW_R40_DEV_MMC0,
    AW_R40_DEV_MMC1,
    AW_R40_DEV_MMC2,
    AW_R40_DEV_MMC3,
    AW_R40_DEV_CCU,
    AW_R40_DEV_PIT,
    AW_R40_DEV_UART0,
    AW_R40_DEV_UART1,
    AW_R40_DEV_UART2,
    AW_R40_DEV_UART3,
    AW_R40_DEV_UART4,
    AW_R40_DEV_UART5,
    AW_R40_DEV_UART6,
    AW_R40_DEV_UART7,
    AW_R40_DEV_TWI0,
    AW_R40_DEV_GMAC,
    AW_R40_DEV_GIC_DIST,
    AW_R40_DEV_GIC_CPU,
    AW_R40_DEV_GIC_HYP,
    AW_R40_DEV_GIC_VCPU,
    AW_R40_DEV_SDRAM,
    AW_R40_DEV_DRAMCOM,
    AW_R40_DEV_DRAMCTL,
    AW_R40_DEV_DRAMPHY,
};

#define AW_R40_NUM_CPUS      (4)

/**
 * Allwinner R40 object model
 * @{
 */

/** Object type for the Allwinner R40 SoC */
#define TYPE_AW_R40 "allwinner-r40"

/** Convert input object to Allwinner R40 state object */
OBJECT_DECLARE_SIMPLE_TYPE(AwR40State, AW_R40)

/** @} */

/**
 * Allwinner R40 object
 *
 * This struct contains the state of all the devices
 * which are currently emulated by the R40 SoC code.
 */
#define AW_R40_NUM_MMCS         4
#define AW_R40_NUM_UARTS        8

struct AwR40State {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    /** Physical base address for start of RAM */
    hwaddr ram_addr;

    /** Total RAM size in megabytes */
    uint32_t ram_size;

    ARMCPU cpus[AW_R40_NUM_CPUS];
    const hwaddr *memmap;
    AwSRAMCState sramc;
    AwA10PITState timer;
    AwSdHostState mmc[AW_R40_NUM_MMCS];
    AwR40ClockCtlState ccu;
    AwR40DramCtlState dramc;
    AWI2CState i2c0;
    AwEmacState emac;
    AwSun8iEmacState gmac;
    GICState gic;
    MemoryRegion sram_a1;
    MemoryRegion sram_a2;
    MemoryRegion sram_a3;
    MemoryRegion sram_a4;
};

/**
 * Emulate Boot ROM firmware setup functionality.
 *
 * A real Allwinner R40 SoC contains a Boot ROM
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
 * @s: Allwinner R40 state object pointer
 * @blk: Block backend device object pointer
 * @unit: the mmc control's unit
 */
bool allwinner_r40_bootrom_setup(AwR40State *s, BlockBackend *blk, int unit);

#endif /* HW_ARM_ALLWINNER_R40_H */
