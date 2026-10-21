/*
 * STM32F2XX SPI controller tests
 *
 * Copyright (c) 2026 Manuele Conti <conti.manuele@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/* The first SSI bus found by -device belongs to SPI3. */
#define SPI3_BASE 0x40003c00
#define SPI_CR1   (SPI3_BASE + 0x00)
#define SPI_SR    (SPI3_BASE + 0x08)
#define SPI_DR    (SPI3_BASE + 0x0c)
#define SPI_RXNE  (1U << 0)

static void test_jedec_id(void)
{
    static const uint8_t jedec_id[] = { 0x20, 0x20, 0x14 };
    QTestState *qts = qtest_init("-machine netduino2 "
                               "-device m25p80,bus=ssi");

    /* Enable SPI in master mode, with software slave management. */
    qtest_writel(qts, SPI_CR1, (1U << 9) | (1U << 8) |
                             (1U << 6) | (1U << 2));

    qtest_writeb(qts, SPI_DR, 0x9f);
    qtest_readb(qts, SPI_DR);

    for (unsigned i = 0; i < ARRAY_SIZE(jedec_id); i++) {
        qtest_writeb(qts, SPI_DR, 0);
        g_assert_cmphex(qtest_readl(qts, SPI_SR) & SPI_RXNE, ==, SPI_RXNE);
        g_assert_cmphex(qtest_readb(qts, SPI_DR), ==, jedec_id[i]);
        g_assert_cmphex(qtest_readl(qts, SPI_SR) & SPI_RXNE, ==, 0);

        /* Reading DR again must not clock another byte out of the flash. */
        g_assert_cmphex(qtest_readb(qts, SPI_DR), ==, jedec_id[i]);
        g_assert_cmphex(qtest_readl(qts, SPI_SR) & SPI_RXNE, ==, 0);
    }

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/stm32f2xx/spi/jedec-id", test_jedec_id);
    return g_test_run();
}
