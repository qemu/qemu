/*
 * Aspeed i2c bus interface to reading and writing to i2c device registers
 *
 * Copyright (c) 2023 IBM Corporation
 *
 * Authors:
 *   Stefan Berger <stefanb@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QTEST_ASPEED_H
#define QTEST_ASPEED_H

#include "libqtest.h"

#define AST2600_ASPEED_I2C_BASE_ADDR 0x1e78a000

/* Implements only AST2600 I2C controller */

static inline uint32_t ast2600_i2c_calc_bus_addr(uint8_t bus_num)
{
    return AST2600_ASPEED_I2C_BASE_ADDR + 0x80 + bus_num * 0x80;
}

uint8_t aspeed_i2c_readb(QTestState *s,
                         uint32_t baseaddr, uint8_t slave_addr, uint8_t reg);
uint16_t aspeed_i2c_readw(QTestState *s,
                          uint32_t baseaddr, uint8_t slave_addr, uint8_t reg);
uint32_t aspeed_i2c_readl(QTestState *s,
                          uint32_t baseaddr, uint8_t slave_addr, uint8_t reg);
void aspeed_i2c_writeb(QTestState *s, uint32_t baseaddr, uint8_t slave_addr,
                       uint8_t reg, uint8_t v);
void aspeed_i2c_writew(QTestState *s, uint32_t baseaddr, uint8_t slave_addr,
                       uint8_t reg, uint16_t v);
void aspeed_i2c_writel(QTestState *s, uint32_t baseaddr, uint8_t slave_addr,
                       uint8_t reg, uint32_t v);

#endif
