/*
 * Broadcom Serial Controller (BSC)
 *
 * Copyright (c) 2024 Rayhan Faizel <rayhan.faizel@gmail.com>
 *
 * SPDX-License-Identifier: MIT
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

#include "hw/sysbus.h"
#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define TYPE_BCM2835_I2C "bcm2835-i2c"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2835I2CState, BCM2835_I2C)

#define BCM2835_I2C_C       0x0                   /* Control */
#define BCM2835_I2C_S       0x4                   /* Status */
#define BCM2835_I2C_DLEN    0x8                   /* Data Length */
#define BCM2835_I2C_A       0xc                   /* Slave Address */
#define BCM2835_I2C_FIFO    0x10                  /* FIFO */
#define BCM2835_I2C_DIV     0x14                  /* Clock Divider */
#define BCM2835_I2C_DEL     0x18                  /* Data Delay */
#define BCM2835_I2C_CLKT    0x20                  /* Clock Stretch Timeout */

#define BCM2835_I2C_C_I2CEN     BIT(15)           /* I2C enable */
#define BCM2835_I2C_C_INTR      BIT(10)           /* Interrupt on RXR */
#define BCM2835_I2C_C_INTT      BIT(9)            /* Interrupt on TXW */
#define BCM2835_I2C_C_INTD      BIT(8)            /* Interrupt on DONE */
#define BCM2835_I2C_C_ST        BIT(7)            /* Start transfer */
#define BCM2835_I2C_C_CLEAR     (BIT(5) | BIT(4)) /* Clear FIFO */
#define BCM2835_I2C_C_READ      BIT(0)            /* I2C read mode */

#define BCM2835_I2C_S_CLKT      BIT(9)            /* Clock stretch timeout */
#define BCM2835_I2C_S_ERR       BIT(8)            /* Slave error */
#define BCM2835_I2C_S_RXF       BIT(7)            /* RX FIFO full */
#define BCM2835_I2C_S_TXE       BIT(6)            /* TX FIFO empty */
#define BCM2835_I2C_S_RXD       BIT(5)            /* RX bytes available */
#define BCM2835_I2C_S_TXD       BIT(4)            /* TX space available */
#define BCM2835_I2C_S_RXR       BIT(3)            /* RX FIFO needs reading */
#define BCM2835_I2C_S_TXW       BIT(2)            /* TX FIFO needs writing */
#define BCM2835_I2C_S_DONE      BIT(1)            /* I2C Transfer complete */
#define BCM2835_I2C_S_TA        BIT(0)            /* I2C Transfer active */

struct BCM2835I2CState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;
    I2CBus *bus;
    qemu_irq irq;

    uint32_t c;
    uint32_t s;
    uint32_t dlen;
    uint32_t a;
    uint32_t div;
    uint32_t del;
    uint32_t clkt;

    uint32_t last_dlen;
};
