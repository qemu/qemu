/*
 * QTest testcase for Broadcom Serial Controller (BSC)
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

#include "qemu/osdep.h"
#include "libqtest-single.h"

#include "hw/i2c/bcm2835_i2c.h"
#include "hw/sensor/tmp105_regs.h"

static const uint32_t bsc_base_addrs[] = {
    0x3f205000,                         /* I2C0 */
    0x3f804000,                         /* I2C1 */
    0x3f805000,                         /* I2C2 */
};

static void bcm2835_i2c_init_transfer(uint32_t base_addr, bool read)
{
    /* read flag is bit 0 so we can write it directly */
    int interrupt = read ? BCM2835_I2C_C_INTR : BCM2835_I2C_C_INTT;

    writel(base_addr + BCM2835_I2C_C,
           BCM2835_I2C_C_I2CEN | BCM2835_I2C_C_INTD |
           BCM2835_I2C_C_ST | BCM2835_I2C_C_CLEAR | interrupt | read);
}

static void test_i2c_read_write(gconstpointer data)
{
    uint32_t i2cdata;
    intptr_t index = (intptr_t) data;
    uint32_t base_addr = bsc_base_addrs[index];

    /* Write to TMP105 register */
    writel(base_addr + BCM2835_I2C_A, 0x50);
    writel(base_addr + BCM2835_I2C_DLEN, 3);

    bcm2835_i2c_init_transfer(base_addr, 0);

    writel(base_addr + BCM2835_I2C_FIFO, TMP105_REG_T_HIGH);
    writel(base_addr + BCM2835_I2C_FIFO, 0xde);
    writel(base_addr + BCM2835_I2C_FIFO, 0xad);

    /* Clear flags */
    writel(base_addr + BCM2835_I2C_S, BCM2835_I2C_S_DONE | BCM2835_I2C_S_ERR |
                                      BCM2835_I2C_S_CLKT);

    /* Read from TMP105 register */
    writel(base_addr + BCM2835_I2C_A, 0x50);
    writel(base_addr + BCM2835_I2C_DLEN, 1);

    bcm2835_i2c_init_transfer(base_addr, 0);

    writel(base_addr + BCM2835_I2C_FIFO, TMP105_REG_T_HIGH);

    writel(base_addr + BCM2835_I2C_DLEN, 2);
    bcm2835_i2c_init_transfer(base_addr, 1);

    i2cdata = readl(base_addr + BCM2835_I2C_FIFO);
    g_assert_cmpint(i2cdata, ==, 0xde);

    i2cdata = readl(base_addr + BCM2835_I2C_FIFO);
    g_assert_cmpint(i2cdata, ==, 0xa0);

    /* Clear flags */
    writel(base_addr + BCM2835_I2C_S, BCM2835_I2C_S_DONE | BCM2835_I2C_S_ERR |
                                      BCM2835_I2C_S_CLKT);

}

int main(int argc, char **argv)
{
    int ret;
    int i;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; i < 3; i++) {
        g_autofree char *test_name =
        g_strdup_printf("/bcm2835/bcm2835-i2c%d/read_write", i);
        qtest_add_data_func(test_name, (void *)(intptr_t) i,
                            test_i2c_read_write);
    }

    /* Run I2C tests with TMP105 slaves on all three buses */
    qtest_start("-M raspi3b "
                "-device tmp105,address=0x50,bus=i2c-bus.0 "
                "-device tmp105,address=0x50,bus=i2c-bus.1 "
                "-device tmp105,address=0x50,bus=i2c-bus.2");
    ret = g_test_run();
    qtest_end();

    return ret;
}
