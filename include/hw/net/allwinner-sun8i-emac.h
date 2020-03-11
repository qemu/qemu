/*
 * Allwinner Sun8i Ethernet MAC emulation
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

#ifndef HW_NET_ALLWINNER_SUN8I_EMAC_H
#define HW_NET_ALLWINNER_SUN8I_EMAC_H

#include "qom/object.h"
#include "net/net.h"
#include "hw/sysbus.h"

/**
 * Object model
 * @{
 */

#define TYPE_AW_SUN8I_EMAC "allwinner-sun8i-emac"
#define AW_SUN8I_EMAC(obj) \
    OBJECT_CHECK(AwSun8iEmacState, (obj), TYPE_AW_SUN8I_EMAC)

/** @} */

/**
 * Allwinner Sun8i EMAC object instance state
 */
typedef struct AwSun8iEmacState {
    /*< private >*/
    SysBusDevice  parent_obj;
    /*< public >*/

    /** Maps I/O registers in physical memory */
    MemoryRegion iomem;

    /** Interrupt output signal to notify CPU */
    qemu_irq     irq;

    /** Generic Network Interface Controller (NIC) for networking API */
    NICState     *nic;

    /** Generic Network Interface Controller (NIC) configuration */
    NICConf      conf;

    /**
     * @name Media Independent Interface (MII)
     * @{
     */

    uint8_t      mii_phy_addr;  /**< PHY address */
    uint32_t     mii_cr;        /**< Control */
    uint32_t     mii_st;        /**< Status */
    uint32_t     mii_adv;       /**< Advertised Abilities */

    /** @} */

    /**
     * @name Hardware Registers
     * @{
     */

    uint32_t     basic_ctl0;    /**< Basic Control 0 */
    uint32_t     basic_ctl1;    /**< Basic Control 1 */
    uint32_t     int_en;        /**< Interrupt Enable */
    uint32_t     int_sta;       /**< Interrupt Status */
    uint32_t     frm_flt;       /**< Receive Frame Filter */

    uint32_t     rx_ctl0;       /**< Receive Control 0 */
    uint32_t     rx_ctl1;       /**< Receive Control 1 */
    uint32_t     rx_desc_head;  /**< Receive Descriptor List Address */
    uint32_t     rx_desc_curr;  /**< Current Receive Descriptor Address */

    uint32_t     tx_ctl0;       /**< Transmit Control 0 */
    uint32_t     tx_ctl1;       /**< Transmit Control 1 */
    uint32_t     tx_desc_head;  /**< Transmit Descriptor List Address */
    uint32_t     tx_desc_curr;  /**< Current Transmit Descriptor Address */
    uint32_t     tx_flowctl;    /**< Transmit Flow Control */

    uint32_t     mii_cmd;       /**< Management Interface Command */
    uint32_t     mii_data;      /**< Management Interface Data */

    /** @} */

} AwSun8iEmacState;

#endif /* HW_NET_ALLWINNER_SUN8I_H */
