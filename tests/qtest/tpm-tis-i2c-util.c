/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QTest utilities for TPM TIS over I2C
 *
 * Copyright (c) 2018, 2026 IBM Corporation
 *
 * Authors:
 *   Stefan Berger <stefanb@linux.ibm.com>
 *
 */

#include "qemu/osdep.h"
#include "hw/acpi/tpm.h"
#include "libqtest-single.h"
#include "qtest_aspeed.h"
#include "tpm-tis-i2c-util.h"

uint32_t aspeed_bus_addr;

static uint8_t cur_locty = 0xff;

static void tpm_tis_i2c_set_locty(uint8_t locty)
{
    if (cur_locty != locty) {
        cur_locty = locty;
        aspeed_i2c_writeb(global_qtest, aspeed_bus_addr, I2C_SLAVE_ADDR,
                          TPM_I2C_REG_LOC_SEL, locty);
    }
}

uint8_t tpm_tis_i2c_readb(uint8_t locty, uint8_t reg)
{
    tpm_tis_i2c_set_locty(locty);
    return aspeed_i2c_readb(global_qtest, aspeed_bus_addr, I2C_SLAVE_ADDR, reg);
}

uint16_t tpm_tis_i2c_readw(uint8_t locty, uint8_t reg)
{
    tpm_tis_i2c_set_locty(locty);
    return aspeed_i2c_readw(global_qtest, aspeed_bus_addr, I2C_SLAVE_ADDR, reg);
}

uint32_t tpm_tis_i2c_readl(uint8_t locty, uint8_t reg)
{
    tpm_tis_i2c_set_locty(locty);
    return aspeed_i2c_readl(global_qtest, aspeed_bus_addr, I2C_SLAVE_ADDR, reg);
}

void tpm_tis_i2c_writeb(uint8_t locty, uint8_t reg, uint8_t v)
{
    if (reg != TPM_I2C_REG_LOC_SEL) {
        tpm_tis_i2c_set_locty(locty);
    }
    aspeed_i2c_writeb(global_qtest, aspeed_bus_addr, I2C_SLAVE_ADDR, reg, v);
}

void tpm_tis_i2c_writel(uint8_t locty, uint8_t reg, uint32_t v)
{
    if (reg != TPM_I2C_REG_LOC_SEL) {
        tpm_tis_i2c_set_locty(locty);
    }
    aspeed_i2c_writel(global_qtest, aspeed_bus_addr, I2C_SLAVE_ADDR, reg, v);
}
