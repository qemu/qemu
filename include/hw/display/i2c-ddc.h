/* A simple I2C slave for returning monitor EDID data via DDC.
 *
 * Copyright (c) 2011 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef I2C_DDC_H
#define I2C_DDC_H

#include "hw/display/edid.h"
#include "hw/i2c/i2c.h"

/* A simple I2C slave which just returns the contents of its EDID blob. */
struct I2CDDCState {
    /*< private >*/
    I2CSlave i2c;
    /*< public >*/
    bool firstbyte;
    uint8_t reg;
    qemu_edid_info edid_info;
    uint8_t edid_blob[128];
};

typedef struct I2CDDCState I2CDDCState;

#define TYPE_I2CDDC "i2c-ddc"
#define I2CDDC(obj) OBJECT_CHECK(I2CDDCState, (obj), TYPE_I2CDDC)

#endif /* I2C_DDC_H */
