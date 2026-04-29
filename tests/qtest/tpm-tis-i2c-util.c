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
#include "tpm-emu.h"

uint32_t aspeed_bus_addr;

static uint8_t cur_locty = 0xff;

static void tpm_tis_i2c_set_locty(QTestState *s, uint8_t locty)
{
    if (cur_locty != locty) {
        cur_locty = locty;
        aspeed_i2c_writeb(s, aspeed_bus_addr, I2C_SLAVE_ADDR,
                          TPM_I2C_REG_LOC_SEL, locty);
    }
}

uint8_t tpm_tis_i2c_readb(QTestState *s, uint8_t locty, uint8_t reg)
{
    tpm_tis_i2c_set_locty(s, locty);
    return aspeed_i2c_readb(s, aspeed_bus_addr, I2C_SLAVE_ADDR, reg);
}

uint16_t tpm_tis_i2c_readw(QTestState *s, uint8_t locty, uint8_t reg)
{
    tpm_tis_i2c_set_locty(s, locty);
    return aspeed_i2c_readw(global_qtest, aspeed_bus_addr, I2C_SLAVE_ADDR, reg);
}

uint32_t tpm_tis_i2c_readl(QTestState *s, uint8_t locty, uint8_t reg)
{
    tpm_tis_i2c_set_locty(s, locty);
    return aspeed_i2c_readl(s, aspeed_bus_addr, I2C_SLAVE_ADDR, reg);
}

void tpm_tis_i2c_writeb(QTestState *s, uint8_t locty, uint8_t reg, uint8_t v)
{
    if (reg != TPM_I2C_REG_LOC_SEL) {
        tpm_tis_i2c_set_locty(s, locty);
    }
    aspeed_i2c_writeb(s, aspeed_bus_addr, I2C_SLAVE_ADDR, reg, v);
}

void tpm_tis_i2c_writel(QTestState *s, uint8_t locty, uint8_t reg, uint32_t v)
{
    if (reg != TPM_I2C_REG_LOC_SEL) {
        tpm_tis_i2c_set_locty(s, locty);
    }
    aspeed_i2c_writel(s, aspeed_bus_addr, I2C_SLAVE_ADDR, reg, v);
}

void tpm_tis_i2c_transfer(QTestState *s,
                          const unsigned char *req, size_t req_size,
                          unsigned char *rsp, size_t rsp_size)
{
    uint32_t sts;
    size_t i;

    /* request use of locality 0 */
    tpm_tis_i2c_writeb(s, 0, TPM_I2C_REG_ACCESS, TPM_TIS_ACCESS_REQUEST_USE);

    tpm_tis_i2c_writel(s, 0, TPM_I2C_REG_STS, TPM_TIS_STS_COMMAND_READY);

    /* transmit command */
    for (i = 0; i < req_size; i++) {
        tpm_tis_i2c_writeb(s, 0, TPM_I2C_REG_DATA_FIFO, req[i]);
    }

    /* start processing */
    tpm_tis_i2c_writeb(s, 0, TPM_I2C_REG_STS, TPM_TIS_STS_TPM_GO);

    uint64_t end_time = g_get_monotonic_time() + 50 * G_TIME_SPAN_SECOND;
    do {
        sts = tpm_tis_i2c_readl(s, 0, TPM_I2C_REG_STS);
        if ((sts & TPM_TIS_STS_DATA_AVAILABLE) != 0) {
            break;
        }
    } while (g_get_monotonic_time() < end_time);

    /* read response */
    for (i = 0; i < rsp_size; i++) {
        rsp[i] = tpm_tis_i2c_readb(s, 0, TPM_I2C_REG_DATA_FIFO);
    }
    /* relinquish use of locality 0 */
    tpm_tis_i2c_writeb(s, 0,
                       TPM_I2C_REG_ACCESS, TPM_TIS_ACCESS_ACTIVE_LOCALITY);
}
