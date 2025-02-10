/*
 * QTest testcase for the M25P80 Flash (Using the Aspeed SPI
 * Controller)
 *
 * Copyright (C) 2016 IBM Corp.
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

#ifndef TESTS_ASPEED_SMC_UTILS_H
#define TESTS_ASPEED_SMC_UTILS_H

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest-single.h"
#include "qemu/bitops.h"

/*
 * ASPEED SPI Controller registers
 */
#define R_CONF              0x00
#define   CONF_ENABLE_W0       16
#define R_CE_CTRL           0x04
#define   CRTL_EXTENDED0       0  /* 32 bit addressing for SPI */
#define R_CTRL0             0x10
#define   CTRL_IO_QUAD_IO      BIT(31)
#define   CTRL_CE_STOP_ACTIVE  BIT(2)
#define   CTRL_READMODE        0x0
#define   CTRL_FREADMODE       0x1
#define   CTRL_WRITEMODE       0x2
#define   CTRL_USERMODE        0x3
#define SR_WEL BIT(1)

/*
 * Flash commands
 */
enum {
    JEDEC_READ = 0x9f,
    RDSR = 0x5,
    WRDI = 0x4,
    BULK_ERASE = 0xc7,
    READ = 0x03,
    PP = 0x02,
    WRSR = 0x1,
    WREN = 0x6,
    SRWD = 0x80,
    RESET_ENABLE = 0x66,
    RESET_MEMORY = 0x99,
    EN_4BYTE_ADDR = 0xB7,
    ERASE_SECTOR = 0xd8,
};

#define CTRL_IO_MODE_MASK  (BIT(31) | BIT(30) | BIT(29) | BIT(28))
#define FLASH_PAGE_SIZE           256

typedef struct AspeedSMCTestData {
    QTestState *s;
    uint64_t spi_base;
    uint64_t flash_base;
    uint32_t jedec_id;
    char *tmp_path;
    uint8_t cs;
    const char *node;
    uint32_t page_addr;
} AspeedSMCTestData;

void aspeed_smc_test_read_jedec(const void *data);
void aspeed_smc_test_erase_sector(const void *data);
void aspeed_smc_test_erase_all(const void *data);
void aspeed_smc_test_write_page(const void *data);
void aspeed_smc_test_read_page_mem(const void *data);
void aspeed_smc_test_write_page_mem(const void *data);
void aspeed_smc_test_read_status_reg(const void *data);
void aspeed_smc_test_status_reg_write_protection(const void *data);
void aspeed_smc_test_write_block_protect(const void *data);
void aspeed_smc_test_write_block_protect_bottom_bit(const void *data);
void aspeed_smc_test_write_page_qpi(const void *data);

#endif /* TESTS_ASPEED_SMC_UTILS_H */
