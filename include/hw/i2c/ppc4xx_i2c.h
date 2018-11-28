/*
 * PPC4xx I2C controller emulation
 *
 * Copyright (c) 2007 Jocelyn Mayer
 * Copyright (c) 2012 François Revol
 * Copyright (c) 2016-2018 BALATON Zoltan
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef PPC4XX_I2C_H
#define PPC4XX_I2C_H

#include "qemu-common.h"
#include "hw/sysbus.h"
#include "hw/i2c/i2c.h"

#define TYPE_PPC4xx_I2C "ppc4xx-i2c"
#define PPC4xx_I2C(obj) OBJECT_CHECK(PPC4xxI2CState, (obj), TYPE_PPC4xx_I2C)

typedef struct PPC4xxI2CState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    I2CBus *bus;
    qemu_irq irq;
    MemoryRegion iomem;
    bitbang_i2c_interface *bitbang;
    int mdidx;
    uint8_t mdata[4];
    uint8_t lmadr;
    uint8_t hmadr;
    uint8_t cntl;
    uint8_t mdcntl;
    uint8_t sts;
    uint8_t extsts;
    uint8_t lsadr;
    uint8_t hsadr;
    uint8_t clkdiv;
    uint8_t intrmsk;
    uint8_t xfrcnt;
    uint8_t xtcntlss;
    uint8_t directcntl;
} PPC4xxI2CState;

#endif /* PPC4XX_I2C_H */
