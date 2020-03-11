/*
 * Allwinner Real Time Clock emulation
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

#ifndef HW_MISC_ALLWINNER_RTC_H
#define HW_MISC_ALLWINNER_RTC_H

#include "qom/object.h"
#include "hw/sysbus.h"

/**
 * Constants
 * @{
 */

/** Highest register address used by RTC device */
#define AW_RTC_REGS_MAXADDR     (0x200)

/** Total number of known registers */
#define AW_RTC_REGS_NUM         (AW_RTC_REGS_MAXADDR / sizeof(uint32_t))

/** @} */

/**
 * Object model types
 * @{
 */

/** Generic Allwinner RTC device (abstract) */
#define TYPE_AW_RTC          "allwinner-rtc"

/** Allwinner RTC sun4i family (A10, A12) */
#define TYPE_AW_RTC_SUN4I    TYPE_AW_RTC "-sun4i"

/** Allwinner RTC sun6i family and newer (A31, H2+, H3, etc) */
#define TYPE_AW_RTC_SUN6I    TYPE_AW_RTC "-sun6i"

/** Allwinner RTC sun7i family (A20) */
#define TYPE_AW_RTC_SUN7I    TYPE_AW_RTC "-sun7i"

/** @} */

/**
 * Object model macros
 * @{
 */

#define AW_RTC(obj) \
    OBJECT_CHECK(AwRtcState, (obj), TYPE_AW_RTC)
#define AW_RTC_CLASS(klass) \
     OBJECT_CLASS_CHECK(AwRtcClass, (klass), TYPE_AW_RTC)
#define AW_RTC_GET_CLASS(obj) \
     OBJECT_GET_CLASS(AwRtcClass, (obj), TYPE_AW_RTC)

/** @} */

/**
 * Allwinner RTC per-object instance state.
 */
typedef struct AwRtcState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    /**
     * Actual year represented by the device when year counter is zero
     *
     * Can be overridden by the user using the corresponding 'base-year'
     * property. The base year used by the target OS driver can vary, for
     * example the Linux driver for sun6i uses 1970 while NetBSD uses 2000.
     */
    int base_year;

    /** Maps I/O registers in physical memory */
    MemoryRegion iomem;

    /** Array of hardware registers */
    uint32_t regs[AW_RTC_REGS_NUM];

} AwRtcState;

/**
 * Allwinner RTC class-level struct.
 *
 * This struct is filled by each sunxi device specific code
 * such that the generic code can use this struct to support
 * all devices.
 */
typedef struct AwRtcClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/

    /** Defines device specific register map */
    const uint8_t *regmap;

    /** Size of the regmap in bytes */
    size_t regmap_size;

    /**
     * Read device specific register
     *
     * @offset: register offset to read
     * @return true if register read successful, false otherwise
     */
    bool (*read)(AwRtcState *s, uint32_t offset);

    /**
     * Write device specific register
     *
     * @offset: register offset to write
     * @data: value to set in register
     * @return true if register write successful, false otherwise
     */
    bool (*write)(AwRtcState *s, uint32_t offset, uint32_t data);

} AwRtcClass;

#endif /* HW_MISC_ALLWINNER_RTC_H */
