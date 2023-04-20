/*
 * Allwinner Watchdog emulation
 *
 * Copyright (C) 2023 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
 *
 *  This file is derived from Allwinner RTC,
 *  by Niek Linnenbank.
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

#ifndef HW_WATCHDOG_ALLWINNER_WDT_H
#define HW_WATCHDOG_ALLWINNER_WDT_H

#include "qom/object.h"
#include "hw/ptimer.h"
#include "hw/sysbus.h"

/*
 * This is a model of the Allwinner watchdog.
 * Since watchdog registers belong to the timer module (and are shared with the
 * RTC module), the interrupt line from watchdog is not handled right now.
 * In QEMU, we just wire up the watchdog reset to watchdog_perform_action(),
 * at least for the moment.
 */

#define TYPE_AW_WDT    "allwinner-wdt"

/** Allwinner WDT sun4i family (A10, A12), also sun7i (A20) */
#define TYPE_AW_WDT_SUN4I    TYPE_AW_WDT "-sun4i"

/** Allwinner WDT sun6i family and newer (A31, H2+, H3, etc) */
#define TYPE_AW_WDT_SUN6I    TYPE_AW_WDT "-sun6i"

/** Number of WDT registers */
#define AW_WDT_REGS_NUM      (5)

OBJECT_DECLARE_TYPE(AwWdtState, AwWdtClass, AW_WDT)

/**
 * Allwinner WDT object instance state.
 */
struct AwWdtState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    struct ptimer_state *timer;

    uint32_t regs[AW_WDT_REGS_NUM];
};

/**
 * Allwinner WDT class-level struct.
 *
 * This struct is filled by each sunxi device specific code
 * such that the generic code can use this struct to support
 * all devices.
 */
struct AwWdtClass {
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
    bool (*read)(AwWdtState *s, uint32_t offset);

    /**
     * Write device specific register
     *
     * @offset: register offset to write
     * @data: value to set in register
     * @return true if register write successful, false otherwise
     */
    bool (*write)(AwWdtState *s, uint32_t offset, uint32_t data);

    /**
     * Check if watchdog can generate system reset
     *
     * @return true if watchdog can generate system reset
     */
    bool (*can_reset_system)(AwWdtState *s);

    /**
     * Check if provided key is valid
     *
     * @value: value written to register
     * @return true if key is valid, false otherwise
     */
    bool (*is_key_valid)(AwWdtState *s, uint32_t val);

    /**
     * Get current INTV_VALUE setting
     *
     * @return current INTV_VALUE (0-15)
     */
    uint8_t (*get_intv_value)(AwWdtState *s);
};

#endif /* HW_WATCHDOG_ALLWINNER_WDT_H */
