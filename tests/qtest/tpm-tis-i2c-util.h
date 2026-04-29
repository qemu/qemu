/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QTest TPM TIS I2C: Common test functions used for TPM I2C on Aspeed bus
 *
 * Copyright (c) 2026 IBM Corporation
 *
 * Authors:
 *   Stefan Berger <stefanb@linux.ibm.com>
 *
 */

#ifndef TESTS_TPM_TIS_I2C_UTIL_H
#define TESTS_TPM_TIS_I2C_UTIL_H

#include "qemu/osdep.h"

extern uint32_t aspeed_bus_addr;

#define I2C_SLAVE_ADDR   0x2e
#define I2C_DEV_BUS_NUM  10

uint8_t tpm_tis_i2c_readb(uint8_t locty, uint8_t reg);
uint16_t tpm_tis_i2c_readw(uint8_t locty, uint8_t reg);
uint32_t tpm_tis_i2c_readl(uint8_t locty, uint8_t reg);

void tpm_tis_i2c_writeb(uint8_t locty, uint8_t reg, uint8_t v);
void tpm_tis_i2c_writel(uint8_t locty, uint8_t reg, uint32_t v);

#endif /* TESTS_TPM_TIS_I2C_UTIL_H */
