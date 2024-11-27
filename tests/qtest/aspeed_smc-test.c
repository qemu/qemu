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

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest-single.h"
#include "qemu/bitops.h"
#include "aspeed-smc-utils.h"

static void test_palmetto_bmc(AspeedSMCTestData *data)
{
    int ret;
    int fd;

    fd = g_file_open_tmp("qtest.m25p80.n25q256a.XXXXXX", &data->tmp_path, NULL);
    g_assert(fd >= 0);
    ret = ftruncate(fd, 32 * 1024 * 1024);
    g_assert(ret == 0);
    close(fd);

    data->s = qtest_initf("-m 256 -machine palmetto-bmc "
                          "-drive file=%s,format=raw,if=mtd",
                          data->tmp_path);

    /* fmc cs0 with n25q256a flash */
    data->flash_base = 0x20000000;
    data->spi_base = 0x1E620000;
    data->jedec_id = 0x20ba19;
    data->cs = 0;
    data->node = "/machine/soc/fmc/ssi.0/child[0]";
    /* beyond 16MB */
    data->page_addr = 0x14000 * FLASH_PAGE_SIZE;

    qtest_add_data_func("/ast2400/smc/read_jedec",
                        data, aspeed_smc_test_read_jedec);
    qtest_add_data_func("/ast2400/smc/erase_sector",
                        data, aspeed_smc_test_erase_sector);
    qtest_add_data_func("/ast2400/smc/erase_all",
                        data, aspeed_smc_test_erase_all);
    qtest_add_data_func("/ast2400/smc/write_page",
                        data, aspeed_smc_test_write_page);
    qtest_add_data_func("/ast2400/smc/read_page_mem",
                        data, aspeed_smc_test_read_page_mem);
    qtest_add_data_func("/ast2400/smc/write_page_mem",
                        data, aspeed_smc_test_write_page_mem);
    qtest_add_data_func("/ast2400/smc/read_status_reg",
                        data, aspeed_smc_test_read_status_reg);
    qtest_add_data_func("/ast2400/smc/status_reg_write_protection",
                        data, aspeed_smc_test_status_reg_write_protection);
    qtest_add_data_func("/ast2400/smc/write_block_protect",
                        data, aspeed_smc_test_write_block_protect);
    qtest_add_data_func("/ast2400/smc/write_block_protect_bottom_bit",
                        data, aspeed_smc_test_write_block_protect_bottom_bit);
}

static void test_ast2500_evb(AspeedSMCTestData *data)
{
    int ret;
    int fd;

    fd = g_file_open_tmp("qtest.m25p80.mx25l25635e.XXXXXX",
                         &data->tmp_path, NULL);
    g_assert(fd >= 0);
    ret = ftruncate(fd, 32 * 1024 * 1024);
    g_assert(ret == 0);
    close(fd);

    data->s = qtest_initf("-machine ast2500-evb "
                          "-drive file=%s,format=raw,if=mtd",
                          data->tmp_path);

    /* fmc cs0 with mx25l25635e flash */
    data->flash_base = 0x20000000;
    data->spi_base = 0x1E620000;
    data->jedec_id = 0xc22019;
    data->cs = 0;
    data->node = "/machine/soc/fmc/ssi.0/child[0]";
    /* beyond 16MB */
    data->page_addr = 0x14000 * FLASH_PAGE_SIZE;

    qtest_add_data_func("/ast2500/smc/read_jedec",
                        data, aspeed_smc_test_read_jedec);
    qtest_add_data_func("/ast2500/smc/erase_sector",
                        data, aspeed_smc_test_erase_sector);
    qtest_add_data_func("/ast2500/smc/erase_all",
                        data, aspeed_smc_test_erase_all);
    qtest_add_data_func("/ast2500/smc/write_page",
                        data, aspeed_smc_test_write_page);
    qtest_add_data_func("/ast2500/smc/read_page_mem",
                        data, aspeed_smc_test_read_page_mem);
    qtest_add_data_func("/ast2500/smc/write_page_mem",
                        data, aspeed_smc_test_write_page_mem);
    qtest_add_data_func("/ast2500/smc/read_status_reg",
                        data, aspeed_smc_test_read_status_reg);
    qtest_add_data_func("/ast2500/smc/write_page_qpi",
                        data, aspeed_smc_test_write_page_qpi);
}

static void test_ast2600_evb(AspeedSMCTestData *data)
{
    int ret;
    int fd;

    fd = g_file_open_tmp("qtest.m25p80.mx66u51235f.XXXXXX",
                         &data->tmp_path, NULL);
    g_assert(fd >= 0);
    ret = ftruncate(fd, 64 * 1024 * 1024);
    g_assert(ret == 0);
    close(fd);

    data->s = qtest_initf("-machine ast2600-evb "
                          "-drive file=%s,format=raw,if=mtd",
                          data->tmp_path);

    /* fmc cs0 with mx66u51235f flash */
    data->flash_base = 0x20000000;
    data->spi_base = 0x1E620000;
    data->jedec_id = 0xc2253a;
    data->cs = 0;
    data->node = "/machine/soc/fmc/ssi.0/child[0]";
    /* beyond 16MB */
    data->page_addr = 0x14000 * FLASH_PAGE_SIZE;

    qtest_add_data_func("/ast2600/smc/read_jedec",
                        data, aspeed_smc_test_read_jedec);
    qtest_add_data_func("/ast2600/smc/erase_sector",
                        data, aspeed_smc_test_erase_sector);
    qtest_add_data_func("/ast2600/smc/erase_all",
                        data, aspeed_smc_test_erase_all);
    qtest_add_data_func("/ast2600/smc/write_page",
                        data, aspeed_smc_test_write_page);
    qtest_add_data_func("/ast2600/smc/read_page_mem",
                        data, aspeed_smc_test_read_page_mem);
    qtest_add_data_func("/ast2600/smc/write_page_mem",
                        data, aspeed_smc_test_write_page_mem);
    qtest_add_data_func("/ast2600/smc/read_status_reg",
                        data, aspeed_smc_test_read_status_reg);
    qtest_add_data_func("/ast2600/smc/write_page_qpi",
                        data, aspeed_smc_test_write_page_qpi);
}

static void test_ast1030_evb(AspeedSMCTestData *data)
{
    int ret;
    int fd;

    fd = g_file_open_tmp("qtest.m25p80.w25q80bl.XXXXXX",
                         &data->tmp_path, NULL);
    g_assert(fd >= 0);
    ret = ftruncate(fd, 1 * 1024 * 1024);
    g_assert(ret == 0);
    close(fd);

    data->s = qtest_initf("-machine ast1030-evb "
                          "-drive file=%s,format=raw,if=mtd",
                          data->tmp_path);

    /* fmc cs0 with w25q80bl flash */
    data->flash_base = 0x80000000;
    data->spi_base = 0x7E620000;
    data->jedec_id = 0xef4014;
    data->cs = 0;
    data->node = "/machine/soc/fmc/ssi.0/child[0]";
    /* beyond 512KB */
    data->page_addr = 0x800 * FLASH_PAGE_SIZE;

    qtest_add_data_func("/ast1030/smc/read_jedec",
                        data, aspeed_smc_test_read_jedec);
    qtest_add_data_func("/ast1030/smc/erase_sector",
                        data, aspeed_smc_test_erase_sector);
    qtest_add_data_func("/ast1030/smc/erase_all",
                        data, aspeed_smc_test_erase_all);
    qtest_add_data_func("/ast1030/smc/write_page",
                        data, aspeed_smc_test_write_page);
    qtest_add_data_func("/ast1030/smc/read_page_mem",
                        data, aspeed_smc_test_read_page_mem);
    qtest_add_data_func("/ast1030/smc/write_page_mem",
                        data, aspeed_smc_test_write_page_mem);
    qtest_add_data_func("/ast1030/smc/read_status_reg",
                        data, aspeed_smc_test_read_status_reg);
    qtest_add_data_func("/ast1030/smc/write_page_qpi",
                        data, aspeed_smc_test_write_page_qpi);
}

int main(int argc, char **argv)
{
    AspeedSMCTestData palmetto_data;
    AspeedSMCTestData ast2500_evb_data;
    AspeedSMCTestData ast2600_evb_data;
    AspeedSMCTestData ast1030_evb_data;
    int ret;

    g_test_init(&argc, &argv, NULL);

    test_palmetto_bmc(&palmetto_data);
    test_ast2500_evb(&ast2500_evb_data);
    test_ast2600_evb(&ast2600_evb_data);
    test_ast1030_evb(&ast1030_evb_data);
    ret = g_test_run();

    qtest_quit(palmetto_data.s);
    qtest_quit(ast2500_evb_data.s);
    qtest_quit(ast2600_evb_data.s);
    qtest_quit(ast1030_evb_data.s);
    unlink(palmetto_data.tmp_path);
    unlink(ast2500_evb_data.tmp_path);
    unlink(ast2600_evb_data.tmp_path);
    unlink(ast1030_evb_data.tmp_path);
    return ret;
}
