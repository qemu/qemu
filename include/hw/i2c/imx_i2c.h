/*
 *  i.MX I2C Bus Serial Interface registers definition
 *
 *  Copyright (C) 2013 Jean-Christophe Dubois. <jcd@tribudubois.net>
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
 *
 */

#ifndef __IMX_I2C_H_
#define __IMX_I2C_H_

#include <hw/sysbus.h>

#define TYPE_IMX_I2C "imx.i2c"
#define IMX_I2C(obj) OBJECT_CHECK(IMXI2CState, (obj), TYPE_IMX_I2C)

#define IMX_I2C_MEM_SIZE           0x14

/* i.MX I2C memory map */
#define IADR_ADDR                  0x00  /* address register */
#define IFDR_ADDR                  0x04  /* frequency divider register */
#define I2CR_ADDR                  0x08  /* control register */
#define I2SR_ADDR                  0x0c  /* status register */
#define I2DR_ADDR                  0x10  /* data register */

#define IADR_MASK                  0xFE
#define IADR_RESET                 0

#define IFDR_MASK                  0x3F
#define IFDR_RESET                 0

#define I2CR_IEN                   (1 << 7)
#define I2CR_IIEN                  (1 << 6)
#define I2CR_MSTA                  (1 << 5)
#define I2CR_MTX                   (1 << 4)
#define I2CR_TXAK                  (1 << 3)
#define I2CR_RSTA                  (1 << 2)
#define I2CR_MASK                  0xFC
#define I2CR_RESET                 0

#define I2SR_ICF                   (1 << 7)
#define I2SR_IAAF                  (1 << 6)
#define I2SR_IBB                   (1 << 5)
#define I2SR_IAL                   (1 << 4)
#define I2SR_SRW                   (1 << 2)
#define I2SR_IIF                   (1 << 1)
#define I2SR_RXAK                  (1 << 0)
#define I2SR_MASK                  0xE9
#define I2SR_RESET                 0x81

#define I2DR_MASK                  0xFF
#define I2DR_RESET                 0

#define ADDR_RESET                 0xFF00

typedef struct IMXI2CState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    I2CBus *bus;
    qemu_irq irq;

    uint16_t  address;

    uint16_t iadr;
    uint16_t ifdr;
    uint16_t i2cr;
    uint16_t i2sr;
    uint16_t i2dr_read;
    uint16_t i2dr_write;
} IMXI2CState;

#endif /* __IMX_I2C_H_ */
