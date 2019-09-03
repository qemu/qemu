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

/*
 * ASPEED SPI Controller registers
 */
#define R_CONF              0x00
#define   CONF_ENABLE_W0       (1 << 16)
#define R_CE_CTRL           0x04
#define   CRTL_EXTENDED0       0  /* 32 bit addressing for SPI */
#define R_CTRL0             0x10
#define   CTRL_CE_STOP_ACTIVE  (1 << 2)
#define   CTRL_READMODE        0x0
#define   CTRL_FREADMODE       0x1
#define   CTRL_WRITEMODE       0x2
#define   CTRL_USERMODE        0x3

#define ASPEED_FMC_BASE    0x1E620000
#define ASPEED_FLASH_BASE  0x20000000

/*
 * Flash commands
 */
enum {
    JEDEC_READ = 0x9f,
    BULK_ERASE = 0xc7,
    READ = 0x03,
    PP = 0x02,
    WREN = 0x6,
    RESET_ENABLE = 0x66,
    RESET_MEMORY = 0x99,
    EN_4BYTE_ADDR = 0xB7,
    ERASE_SECTOR = 0xd8,
};

#define FLASH_JEDEC         0x20ba19  /* n25q256a */
#define FLASH_SIZE          (32 * 1024 * 1024)

#define PAGE_SIZE           256

/*
 * Use an explicit bswap for the values read/wrote to the flash region
 * as they are BE and the Aspeed CPU is LE.
 */
static inline uint32_t make_be32(uint32_t data)
{
    return bswap32(data);
}

static void spi_conf(uint32_t value)
{
    uint32_t conf = readl(ASPEED_FMC_BASE + R_CONF);

    conf |= value;
    writel(ASPEED_FMC_BASE + R_CONF, conf);
}

static void spi_conf_remove(uint32_t value)
{
    uint32_t conf = readl(ASPEED_FMC_BASE + R_CONF);

    conf &= ~value;
    writel(ASPEED_FMC_BASE + R_CONF, conf);
}

static void spi_ce_ctrl(uint32_t value)
{
    uint32_t conf = readl(ASPEED_FMC_BASE + R_CE_CTRL);

    conf |= value;
    writel(ASPEED_FMC_BASE + R_CE_CTRL, conf);
}

static void spi_ctrl_setmode(uint8_t mode, uint8_t cmd)
{
    uint32_t ctrl = readl(ASPEED_FMC_BASE + R_CTRL0);
    ctrl &= ~(CTRL_USERMODE | 0xff << 16);
    ctrl |= mode | (cmd << 16);
    writel(ASPEED_FMC_BASE + R_CTRL0, ctrl);
}

static void spi_ctrl_start_user(void)
{
    uint32_t ctrl = readl(ASPEED_FMC_BASE + R_CTRL0);

    ctrl |= CTRL_USERMODE | CTRL_CE_STOP_ACTIVE;
    writel(ASPEED_FMC_BASE + R_CTRL0, ctrl);

    ctrl &= ~CTRL_CE_STOP_ACTIVE;
    writel(ASPEED_FMC_BASE + R_CTRL0, ctrl);
}

static void spi_ctrl_stop_user(void)
{
    uint32_t ctrl = readl(ASPEED_FMC_BASE + R_CTRL0);

    ctrl |= CTRL_USERMODE | CTRL_CE_STOP_ACTIVE;
    writel(ASPEED_FMC_BASE + R_CTRL0, ctrl);
}

static void flash_reset(void)
{
    spi_conf(CONF_ENABLE_W0);

    spi_ctrl_start_user();
    writeb(ASPEED_FLASH_BASE, RESET_ENABLE);
    writeb(ASPEED_FLASH_BASE, RESET_MEMORY);
    spi_ctrl_stop_user();

    spi_conf_remove(CONF_ENABLE_W0);
}

static void test_read_jedec(void)
{
    uint32_t jedec = 0x0;

    spi_conf(CONF_ENABLE_W0);

    spi_ctrl_start_user();
    writeb(ASPEED_FLASH_BASE, JEDEC_READ);
    jedec |= readb(ASPEED_FLASH_BASE) << 16;
    jedec |= readb(ASPEED_FLASH_BASE) << 8;
    jedec |= readb(ASPEED_FLASH_BASE);
    spi_ctrl_stop_user();

    flash_reset();

    g_assert_cmphex(jedec, ==, FLASH_JEDEC);
}

static void read_page(uint32_t addr, uint32_t *page)
{
    int i;

    spi_ctrl_start_user();

    writeb(ASPEED_FLASH_BASE, EN_4BYTE_ADDR);
    writeb(ASPEED_FLASH_BASE, READ);
    writel(ASPEED_FLASH_BASE, make_be32(addr));

    /* Continuous read are supported */
    for (i = 0; i < PAGE_SIZE / 4; i++) {
        page[i] = make_be32(readl(ASPEED_FLASH_BASE));
    }
    spi_ctrl_stop_user();
}

static void read_page_mem(uint32_t addr, uint32_t *page)
{
    int i;

    /* move out USER mode to use direct reads from the AHB bus */
    spi_ctrl_setmode(CTRL_READMODE, READ);

    for (i = 0; i < PAGE_SIZE / 4; i++) {
        page[i] = make_be32(readl(ASPEED_FLASH_BASE + addr + i * 4));
    }
}

static void test_erase_sector(void)
{
    uint32_t some_page_addr = 0x600 * PAGE_SIZE;
    uint32_t page[PAGE_SIZE / 4];
    int i;

    spi_conf(CONF_ENABLE_W0);

    spi_ctrl_start_user();
    writeb(ASPEED_FLASH_BASE, WREN);
    writeb(ASPEED_FLASH_BASE, EN_4BYTE_ADDR);
    writeb(ASPEED_FLASH_BASE, ERASE_SECTOR);
    writel(ASPEED_FLASH_BASE, make_be32(some_page_addr));
    spi_ctrl_stop_user();

    /* Previous page should be full of zeroes as backend is not
     * initialized */
    read_page(some_page_addr - PAGE_SIZE, page);
    for (i = 0; i < PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, 0x0);
    }

    /* But this one was erased */
    read_page(some_page_addr, page);
    for (i = 0; i < PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, 0xffffffff);
    }

    flash_reset();
}

static void test_erase_all(void)
{
    uint32_t some_page_addr = 0x15000 * PAGE_SIZE;
    uint32_t page[PAGE_SIZE / 4];
    int i;

    spi_conf(CONF_ENABLE_W0);

    /* Check some random page. Should be full of zeroes as backend is
     * not initialized */
    read_page(some_page_addr, page);
    for (i = 0; i < PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, 0x0);
    }

    spi_ctrl_start_user();
    writeb(ASPEED_FLASH_BASE, WREN);
    writeb(ASPEED_FLASH_BASE, BULK_ERASE);
    spi_ctrl_stop_user();

    /* Recheck that some random page */
    read_page(some_page_addr, page);
    for (i = 0; i < PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, 0xffffffff);
    }

    flash_reset();
}

static void test_write_page(void)
{
    uint32_t my_page_addr = 0x14000 * PAGE_SIZE; /* beyond 16MB */
    uint32_t some_page_addr = 0x15000 * PAGE_SIZE;
    uint32_t page[PAGE_SIZE / 4];
    int i;

    spi_conf(CONF_ENABLE_W0);

    spi_ctrl_start_user();
    writeb(ASPEED_FLASH_BASE, EN_4BYTE_ADDR);
    writeb(ASPEED_FLASH_BASE, WREN);
    writeb(ASPEED_FLASH_BASE, PP);
    writel(ASPEED_FLASH_BASE, make_be32(my_page_addr));

    /* Fill the page with its own addresses */
    for (i = 0; i < PAGE_SIZE / 4; i++) {
        writel(ASPEED_FLASH_BASE, make_be32(my_page_addr + i * 4));
    }
    spi_ctrl_stop_user();

    /* Check what was written */
    read_page(my_page_addr, page);
    for (i = 0; i < PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, my_page_addr + i * 4);
    }

    /* Check some other page. It should be full of 0xff */
    read_page(some_page_addr, page);
    for (i = 0; i < PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, 0xffffffff);
    }

    flash_reset();
}

static void test_read_page_mem(void)
{
    uint32_t my_page_addr = 0x14000 * PAGE_SIZE; /* beyond 16MB */
    uint32_t some_page_addr = 0x15000 * PAGE_SIZE;
    uint32_t page[PAGE_SIZE / 4];
    int i;

    /* Enable 4BYTE mode for controller. This is should be strapped by
     * HW for CE0 anyhow.
     */
    spi_ce_ctrl(1 << CRTL_EXTENDED0);

    /* Enable 4BYTE mode for flash. */
    spi_conf(CONF_ENABLE_W0);
    spi_ctrl_start_user();
    writeb(ASPEED_FLASH_BASE, EN_4BYTE_ADDR);
    spi_ctrl_stop_user();
    spi_conf_remove(CONF_ENABLE_W0);

    /* Check what was written */
    read_page_mem(my_page_addr, page);
    for (i = 0; i < PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, my_page_addr + i * 4);
    }

    /* Check some other page. It should be full of 0xff */
    read_page_mem(some_page_addr, page);
    for (i = 0; i < PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, 0xffffffff);
    }

    flash_reset();
}

static void test_write_page_mem(void)
{
    uint32_t my_page_addr = 0x15000 * PAGE_SIZE;
    uint32_t page[PAGE_SIZE / 4];
    int i;

    /* Enable 4BYTE mode for controller. This is should be strapped by
     * HW for CE0 anyhow.
     */
    spi_ce_ctrl(1 << CRTL_EXTENDED0);

    /* Enable 4BYTE mode for flash. */
    spi_conf(CONF_ENABLE_W0);
    spi_ctrl_start_user();
    writeb(ASPEED_FLASH_BASE, EN_4BYTE_ADDR);
    writeb(ASPEED_FLASH_BASE, WREN);
    spi_ctrl_stop_user();

    /* move out USER mode to use direct writes to the AHB bus */
    spi_ctrl_setmode(CTRL_WRITEMODE, PP);

    for (i = 0; i < PAGE_SIZE / 4; i++) {
        writel(ASPEED_FLASH_BASE + my_page_addr + i * 4,
               make_be32(my_page_addr + i * 4));
    }

    /* Check what was written */
    read_page_mem(my_page_addr, page);
    for (i = 0; i < PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, my_page_addr + i * 4);
    }

    flash_reset();
}

static char tmp_path[] = "/tmp/qtest.m25p80.XXXXXX";

int main(int argc, char **argv)
{
    int ret;
    int fd;

    g_test_init(&argc, &argv, NULL);

    fd = mkstemp(tmp_path);
    g_assert(fd >= 0);
    ret = ftruncate(fd, FLASH_SIZE);
    g_assert(ret == 0);
    close(fd);

    global_qtest = qtest_initf("-m 256 -machine palmetto-bmc "
                               "-drive file=%s,format=raw,if=mtd",
                               tmp_path);

    qtest_add_func("/m25p80/read_jedec", test_read_jedec);
    qtest_add_func("/m25p80/erase_sector", test_erase_sector);
    qtest_add_func("/m25p80/erase_all",  test_erase_all);
    qtest_add_func("/m25p80/write_page", test_write_page);
    qtest_add_func("/m25p80/read_page_mem", test_read_page_mem);
    qtest_add_func("/m25p80/write_page_mem", test_write_page_mem);

    ret = g_test_run();

    qtest_quit(global_qtest);
    unlink(tmp_path);
    return ret;
}
