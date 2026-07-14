/*
 * QTest testcase for the M25P80 Flash (Using the Aspeed SPI
 * Controller)
 *
 * Copyright (C) 2016 IBM Corp.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef TESTS_ASPEED_SMC_UTILS_H
#define TESTS_ASPEED_SMC_UTILS_H

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
#define   CTRL_IO_DUAL_DATA     BIT(29)
#define   CTRL_DUMMY_LOW_SHIFT   6
#define   CTRL_DUMMY_HIGH_SHIFT  14
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
    FAST_READ = 0x0b,
    DOR = 0x3b,
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
void aspeed_smc_test_read_page_mem_fast_read(const void *data);
void aspeed_smc_test_write_page_fast_read(const void *data);
void aspeed_smc_test_read_page_mem_dor(const void *data);
void aspeed_smc_test_write_page_dor(const void *data);

#endif /* TESTS_ASPEED_SMC_UTILS_H */
