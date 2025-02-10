/*
 * QTest testcase for the M25P80 Flash using the ASPEED SPI Controller since
 * AST2700.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 ASPEED Technology Inc.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest-single.h"
#include "qemu/bitops.h"
#include "aspeed-smc-utils.h"

static void test_ast2700_evb(AspeedSMCTestData *data)
{
    int ret;
    int fd;

    fd = g_file_open_tmp("qtest.m25p80.w25q01jvq.XXXXXX",
                         &data->tmp_path, NULL);
    g_assert(fd >= 0);
    ret = ftruncate(fd, 128 * 1024 * 1024);
    g_assert(ret == 0);
    close(fd);

    data->s = qtest_initf("-machine ast2700-evb "
                          "-drive file=%s,format=raw,if=mtd",
                          data->tmp_path);

    /* fmc cs0 with w25q01jvq flash */
    data->flash_base = 0x100000000;
    data->spi_base = 0x14000000;
    data->jedec_id = 0xef4021;
    data->cs = 0;
    data->node = "/machine/soc/fmc/ssi.0/child[0]";
    /* beyond 64MB */
    data->page_addr = 0x40000 * FLASH_PAGE_SIZE;

    qtest_add_data_func("/ast2700/smc/read_jedec",
                        data, aspeed_smc_test_read_jedec);
    qtest_add_data_func("/ast2700/smc/erase_sector",
                        data, aspeed_smc_test_erase_sector);
    qtest_add_data_func("/ast2700/smc/erase_all",
                        data, aspeed_smc_test_erase_all);
    qtest_add_data_func("/ast2700/smc/write_page",
                        data, aspeed_smc_test_write_page);
    qtest_add_data_func("/ast2700/smc/read_page_mem",
                        data, aspeed_smc_test_read_page_mem);
    qtest_add_data_func("/ast2700/smc/write_page_mem",
                        data, aspeed_smc_test_write_page_mem);
    qtest_add_data_func("/ast2700/smc/read_status_reg",
                        data, aspeed_smc_test_read_status_reg);
    qtest_add_data_func("/ast2700/smc/write_page_qpi",
                        data, aspeed_smc_test_write_page_qpi);
}

int main(int argc, char **argv)
{
    AspeedSMCTestData ast2700_evb_data;
    int ret;

    g_test_init(&argc, &argv, NULL);

    test_ast2700_evb(&ast2700_evb_data);
    ret = g_test_run();

    qtest_quit(ast2700_evb_data.s);
    unlink(ast2700_evb_data.tmp_path);
    return ret;
}
