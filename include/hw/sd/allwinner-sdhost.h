/*
 * Allwinner (sun4i and above) SD Host Controller emulation
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

#ifndef HW_SD_ALLWINNER_SDHOST_H
#define HW_SD_ALLWINNER_SDHOST_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/sd/sd.h"

/**
 * Object model types
 * @{
 */

/** Generic Allwinner SD Host Controller (abstract) */
#define TYPE_AW_SDHOST "allwinner-sdhost"

/** Allwinner sun4i family (A10, A12) */
#define TYPE_AW_SDHOST_SUN4I TYPE_AW_SDHOST "-sun4i"

/** Allwinner sun5i family and newer (A13, H2+, H3, etc) */
#define TYPE_AW_SDHOST_SUN5I TYPE_AW_SDHOST "-sun5i"

/** @} */

/**
 * Object model macros
 * @{
 */

OBJECT_DECLARE_TYPE(AwSdHostState, AwSdHostClass, AW_SDHOST)

/** @} */

/**
 * Allwinner SD Host Controller object instance state.
 */
struct AwSdHostState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    /** Secure Digital (SD) bus, which connects to SD card (if present) */
    SDBus sdbus;

    /** Maps I/O registers in physical memory */
    MemoryRegion iomem;

    /** Interrupt output signal to notify CPU */
    qemu_irq irq;

    /** Memory region where DMA transfers are done */
    MemoryRegion *dma_mr;

    /** Address space used internally for DMA transfers */
    AddressSpace dma_as;

    /** Number of bytes left in current DMA transfer */
    uint32_t transfer_cnt;

    /**
     * @name Hardware Registers
     * @{
     */

    uint32_t global_ctl;        /**< Global Control */
    uint32_t clock_ctl;         /**< Clock Control */
    uint32_t timeout;           /**< Timeout */
    uint32_t bus_width;         /**< Bus Width */
    uint32_t block_size;        /**< Block Size */
    uint32_t byte_count;        /**< Byte Count */

    uint32_t command;           /**< Command */
    uint32_t command_arg;       /**< Command Argument */
    uint32_t response[4];       /**< Command Response */

    uint32_t irq_mask;          /**< Interrupt Mask */
    uint32_t irq_status;        /**< Raw Interrupt Status */
    uint32_t status;            /**< Status */

    uint32_t fifo_wlevel;       /**< FIFO Water Level */
    uint32_t fifo_func_sel;     /**< FIFO Function Select */
    uint32_t debug_enable;      /**< Debug Enable */
    uint32_t auto12_arg;        /**< Auto Command 12 Argument */
    uint32_t newtiming_set;     /**< SD New Timing Set */
    uint32_t newtiming_debug;   /**< SD New Timing Debug */
    uint32_t hardware_rst;      /**< Hardware Reset */
    uint32_t dmac;              /**< Internal DMA Controller Control */
    uint32_t desc_base;         /**< Descriptor List Base Address */
    uint32_t dmac_status;       /**< Internal DMA Controller Status */
    uint32_t dmac_irq;          /**< Internal DMA Controller IRQ Enable */
    uint32_t card_threshold;    /**< Card Threshold Control */
    uint32_t startbit_detect;   /**< eMMC DDR Start Bit Detection Control */
    uint32_t response_crc;      /**< Response CRC */
    uint32_t data_crc[8];       /**< Data CRC */
    uint32_t status_crc;        /**< Status CRC */

    /** @} */

};

/**
 * Allwinner SD Host Controller class-level struct.
 *
 * This struct is filled by each sunxi device specific code
 * such that the generic code can use this struct to support
 * all devices.
 */
struct AwSdHostClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/

    /** Maximum buffer size in bytes per DMA descriptor */
    size_t max_desc_size;
    bool   is_sun4i;

};

#endif /* HW_SD_ALLWINNER_SDHOST_H */
