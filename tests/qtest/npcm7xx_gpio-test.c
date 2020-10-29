/*
 * QTest testcase for the Nuvoton NPCM7xx GPIO modules.
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define NR_GPIO_DEVICES (8)
#define GPIO(x)         (0xf0010000 + (x) * 0x1000)
#define GPIO_IRQ(x)     (116 + (x))

/* GPIO registers */
#define GP_N_TLOCK1     0x00
#define GP_N_DIN        0x04 /* Data IN */
#define GP_N_POL        0x08 /* Polarity */
#define GP_N_DOUT       0x0c /* Data OUT */
#define GP_N_OE         0x10 /* Output Enable */
#define GP_N_OTYP       0x14
#define GP_N_MP         0x18
#define GP_N_PU         0x1c /* Pull-up */
#define GP_N_PD         0x20 /* Pull-down */
#define GP_N_DBNC       0x24 /* Debounce */
#define GP_N_EVTYP      0x28 /* Event Type */
#define GP_N_EVBE       0x2c /* Event Both Edge */
#define GP_N_OBL0       0x30
#define GP_N_OBL1       0x34
#define GP_N_OBL2       0x38
#define GP_N_OBL3       0x3c
#define GP_N_EVEN       0x40 /* Event Enable */
#define GP_N_EVENS      0x44 /* Event Set (enable) */
#define GP_N_EVENC      0x48 /* Event Clear (disable) */
#define GP_N_EVST       0x4c /* Event Status */
#define GP_N_SPLCK      0x50
#define GP_N_MPLCK      0x54
#define GP_N_IEM        0x58 /* Input Enable */
#define GP_N_OSRC       0x5c
#define GP_N_ODSC       0x60
#define GP_N_DOS        0x68 /* Data OUT Set */
#define GP_N_DOC        0x6c /* Data OUT Clear */
#define GP_N_OES        0x70 /* Output Enable Set */
#define GP_N_OEC        0x74 /* Output Enable Clear */
#define GP_N_TLOCK2     0x7c

static void gpio_unlock(int n)
{
    if (readl(GPIO(n) + GP_N_TLOCK1) != 0) {
        writel(GPIO(n) + GP_N_TLOCK2, 0xc0de1248);
        writel(GPIO(n) + GP_N_TLOCK1, 0xc0defa73);
    }
}

/* Restore the GPIO controller to a sensible default state. */
static void gpio_reset(int n)
{
    gpio_unlock(0);

    writel(GPIO(n) + GP_N_EVEN, 0x00000000);
    writel(GPIO(n) + GP_N_EVST, 0xffffffff);
    writel(GPIO(n) + GP_N_POL, 0x00000000);
    writel(GPIO(n) + GP_N_DOUT, 0x00000000);
    writel(GPIO(n) + GP_N_OE, 0x00000000);
    writel(GPIO(n) + GP_N_OTYP, 0x00000000);
    writel(GPIO(n) + GP_N_PU, 0xffffffff);
    writel(GPIO(n) + GP_N_PD, 0x00000000);
    writel(GPIO(n) + GP_N_IEM, 0xffffffff);
}

static void test_dout_to_din(void)
{
    gpio_reset(0);

    /* When output is enabled, DOUT should be reflected on DIN. */
    writel(GPIO(0) + GP_N_OE, 0xffffffff);
    /* PU and PD shouldn't have any impact on DIN. */
    writel(GPIO(0) + GP_N_PU, 0xffff0000);
    writel(GPIO(0) + GP_N_PD, 0x0000ffff);
    writel(GPIO(0) + GP_N_DOUT, 0x12345678);
    g_assert_cmphex(readl(GPIO(0) + GP_N_DOUT), ==, 0x12345678);
    g_assert_cmphex(readl(GPIO(0) + GP_N_DIN), ==, 0x12345678);
}

static void test_pullup_pulldown(void)
{
    gpio_reset(0);

    /*
     * When output is disabled, and PD is the inverse of PU, PU should be
     * reflected on DIN. If PD is not the inverse of PU, the state of DIN is
     * undefined, so we don't test that.
     */
    writel(GPIO(0) + GP_N_OE, 0x00000000);
    /* DOUT shouldn't have any impact on DIN. */
    writel(GPIO(0) + GP_N_DOUT, 0xffff0000);
    writel(GPIO(0) + GP_N_PU, 0x23456789);
    writel(GPIO(0) + GP_N_PD, ~0x23456789U);
    g_assert_cmphex(readl(GPIO(0) + GP_N_PU), ==, 0x23456789);
    g_assert_cmphex(readl(GPIO(0) + GP_N_PD), ==, ~0x23456789U);
    g_assert_cmphex(readl(GPIO(0) + GP_N_DIN), ==, 0x23456789);
}

static void test_output_enable(void)
{
    gpio_reset(0);

    /*
     * With all pins weakly pulled down, and DOUT all-ones, OE should be
     * reflected on DIN.
     */
    writel(GPIO(0) + GP_N_DOUT, 0xffffffff);
    writel(GPIO(0) + GP_N_PU, 0x00000000);
    writel(GPIO(0) + GP_N_PD, 0xffffffff);
    writel(GPIO(0) + GP_N_OE, 0x3456789a);
    g_assert_cmphex(readl(GPIO(0) + GP_N_OE), ==, 0x3456789a);
    g_assert_cmphex(readl(GPIO(0) + GP_N_DIN), ==, 0x3456789a);

    writel(GPIO(0) + GP_N_OEC, 0x00030002);
    g_assert_cmphex(readl(GPIO(0) + GP_N_OE), ==, 0x34547898);
    g_assert_cmphex(readl(GPIO(0) + GP_N_DIN), ==, 0x34547898);

    writel(GPIO(0) + GP_N_OES, 0x0000f001);
    g_assert_cmphex(readl(GPIO(0) + GP_N_OE), ==, 0x3454f899);
    g_assert_cmphex(readl(GPIO(0) + GP_N_DIN), ==, 0x3454f899);
}

static void test_open_drain(void)
{
    gpio_reset(0);

    /*
     * Upper half of DOUT drives a 1 only if the corresponding bit in OTYP is
     * not set. If OTYP is set, DIN is determined by PU/PD. Lower half of
     * DOUT always drives a 0 regardless of OTYP; PU/PD have no effect.  When
     * OE is 0, output is determined by PU/PD; OTYP has no effect.
     */
    writel(GPIO(0) + GP_N_OTYP, 0x456789ab);
    writel(GPIO(0) + GP_N_OE, 0xf0f0f0f0);
    writel(GPIO(0) + GP_N_DOUT, 0xffff0000);
    writel(GPIO(0) + GP_N_PU, 0xff00ff00);
    writel(GPIO(0) + GP_N_PD, 0x00ff00ff);
    g_assert_cmphex(readl(GPIO(0) + GP_N_OTYP), ==, 0x456789ab);
    g_assert_cmphex(readl(GPIO(0) + GP_N_DIN), ==, 0xff900f00);
}

static void test_polarity(void)
{
    gpio_reset(0);

    /*
     * In push-pull mode, DIN should reflect DOUT because the signal is
     * inverted in both directions.
     */
    writel(GPIO(0) + GP_N_OTYP, 0x00000000);
    writel(GPIO(0) + GP_N_OE, 0xffffffff);
    writel(GPIO(0) + GP_N_DOUT, 0x56789abc);
    writel(GPIO(0) + GP_N_POL, 0x6789abcd);
    g_assert_cmphex(readl(GPIO(0) + GP_N_POL), ==, 0x6789abcd);
    g_assert_cmphex(readl(GPIO(0) + GP_N_DIN), ==, 0x56789abc);

    /*
     * When turning off the drivers, DIN should reflect the inverse of the
     * pulled-up lines.
     */
    writel(GPIO(0) + GP_N_OE, 0x00000000);
    writel(GPIO(0) + GP_N_POL, 0xffffffff);
    writel(GPIO(0) + GP_N_PU, 0x789abcde);
    writel(GPIO(0) + GP_N_PD, ~0x789abcdeU);
    g_assert_cmphex(readl(GPIO(0) + GP_N_DIN), ==, ~0x789abcdeU);

    /*
     * In open-drain mode, DOUT=1 will appear to drive the pin high (since DIN
     * is inverted), while DOUT=0 will leave the pin floating.
     */
    writel(GPIO(0) + GP_N_OTYP, 0xffffffff);
    writel(GPIO(0) + GP_N_OE, 0xffffffff);
    writel(GPIO(0) + GP_N_PU, 0xffff0000);
    writel(GPIO(0) + GP_N_PD, 0x0000ffff);
    writel(GPIO(0) + GP_N_DOUT, 0xff00ff00);
    g_assert_cmphex(readl(GPIO(0) + GP_N_DIN), ==, 0xff00ffff);
}

static void test_input_mask(void)
{
    gpio_reset(0);

    /* IEM=0 forces the input to zero before polarity inversion. */
    writel(GPIO(0) + GP_N_OE, 0xffffffff);
    writel(GPIO(0) + GP_N_DOUT, 0xff00ff00);
    writel(GPIO(0) + GP_N_POL, 0xffff0000);
    writel(GPIO(0) + GP_N_IEM, 0x87654321);
    g_assert_cmphex(readl(GPIO(0) + GP_N_DIN), ==, 0xff9a4300);
}

static void test_temp_lock(void)
{
    gpio_reset(0);

    writel(GPIO(0) + GP_N_DOUT, 0x98765432);

    /* Make sure we're unlocked initially. */
    g_assert_cmphex(readl(GPIO(0) + GP_N_TLOCK1), ==, 0);
    /* Writing any value to TLOCK1 will lock. */
    writel(GPIO(0) + GP_N_TLOCK1, 0);
    g_assert_cmphex(readl(GPIO(0) + GP_N_TLOCK1), ==, 1);
    writel(GPIO(0) + GP_N_DOUT, 0xa9876543);
    g_assert_cmphex(readl(GPIO(0) + GP_N_DOUT), ==, 0x98765432);
    /* Now, try to unlock. */
    gpio_unlock(0);
    g_assert_cmphex(readl(GPIO(0) + GP_N_TLOCK1), ==, 0);
    writel(GPIO(0) + GP_N_DOUT, 0xa9876543);
    g_assert_cmphex(readl(GPIO(0) + GP_N_DOUT), ==, 0xa9876543);

    /* Try it again, but write TLOCK2 to lock. */
    writel(GPIO(0) + GP_N_TLOCK2, 0);
    g_assert_cmphex(readl(GPIO(0) + GP_N_TLOCK1), ==, 1);
    writel(GPIO(0) + GP_N_DOUT, 0x98765432);
    g_assert_cmphex(readl(GPIO(0) + GP_N_DOUT), ==, 0xa9876543);
    /* Now, try to unlock. */
    gpio_unlock(0);
    g_assert_cmphex(readl(GPIO(0) + GP_N_TLOCK1), ==, 0);
    writel(GPIO(0) + GP_N_DOUT, 0x98765432);
    g_assert_cmphex(readl(GPIO(0) + GP_N_DOUT), ==, 0x98765432);
}

static void test_events_level(void)
{
    gpio_reset(0);

    writel(GPIO(0) + GP_N_EVTYP, 0x00000000);
    writel(GPIO(0) + GP_N_DOUT, 0xba987654);
    writel(GPIO(0) + GP_N_OE, 0xffffffff);
    writel(GPIO(0) + GP_N_EVST, 0xffffffff);

    g_assert_cmphex(readl(GPIO(0) + GP_N_EVST), ==, 0xba987654);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(0)));
    writel(GPIO(0) + GP_N_DOUT, 0x00000000);
    g_assert_cmphex(readl(GPIO(0) + GP_N_EVST), ==, 0xba987654);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(0)));
    writel(GPIO(0) + GP_N_EVST, 0x00007654);
    g_assert_cmphex(readl(GPIO(0) + GP_N_EVST), ==, 0xba980000);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(0)));
    writel(GPIO(0) + GP_N_EVST, 0xba980000);
    g_assert_cmphex(readl(GPIO(0) + GP_N_EVST), ==, 0x00000000);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(0)));
}

static void test_events_rising_edge(void)
{
    gpio_reset(0);

    writel(GPIO(0) + GP_N_EVTYP, 0xffffffff);
    writel(GPIO(0) + GP_N_EVBE, 0x00000000);
    writel(GPIO(0) + GP_N_DOUT, 0xffff0000);
    writel(GPIO(0) + GP_N_OE, 0xffffffff);
    writel(GPIO(0) + GP_N_EVST, 0xffffffff);

    g_assert_cmphex(readl(GPIO(0) + GP_N_EVST), ==, 0x00000000);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(0)));
    writel(GPIO(0) + GP_N_DOUT, 0xff00ff00);
    g_assert_cmphex(readl(GPIO(0) + GP_N_EVST), ==, 0x0000ff00);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(0)));
    writel(GPIO(0) + GP_N_DOUT, 0x00ff0000);
    g_assert_cmphex(readl(GPIO(0) + GP_N_EVST), ==, 0x00ffff00);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(0)));
    writel(GPIO(0) + GP_N_EVST, 0x0000f000);
    g_assert_cmphex(readl(GPIO(0) + GP_N_EVST), ==, 0x00ff0f00);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(0)));
    writel(GPIO(0) + GP_N_EVST, 0x00ff0f00);
    g_assert_cmphex(readl(GPIO(0) + GP_N_EVST), ==, 0x00000000);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(0)));
}

static void test_events_both_edges(void)
{
    gpio_reset(0);

    writel(GPIO(0) + GP_N_EVTYP, 0xffffffff);
    writel(GPIO(0) + GP_N_EVBE, 0xffffffff);
    writel(GPIO(0) + GP_N_DOUT, 0xffff0000);
    writel(GPIO(0) + GP_N_OE, 0xffffffff);
    writel(GPIO(0) + GP_N_EVST, 0xffffffff);

    g_assert_cmphex(readl(GPIO(0) + GP_N_EVST), ==, 0x00000000);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(0)));
    writel(GPIO(0) + GP_N_DOUT, 0xff00ff00);
    g_assert_cmphex(readl(GPIO(0) + GP_N_EVST), ==, 0x00ffff00);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(0)));
    writel(GPIO(0) + GP_N_DOUT, 0xef00ff08);
    g_assert_cmphex(readl(GPIO(0) + GP_N_EVST), ==, 0x10ffff08);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(0)));
    writel(GPIO(0) + GP_N_EVST, 0x0000f000);
    g_assert_cmphex(readl(GPIO(0) + GP_N_EVST), ==, 0x10ff0f08);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(0)));
    writel(GPIO(0) + GP_N_EVST, 0x10ff0f08);
    g_assert_cmphex(readl(GPIO(0) + GP_N_EVST), ==, 0x00000000);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(0)));
}

static void test_gpion_irq(gconstpointer test_data)
{
    intptr_t n = (intptr_t)test_data;

    gpio_reset(n);

    writel(GPIO(n) + GP_N_EVTYP, 0x00000000);
    writel(GPIO(n) + GP_N_DOUT, 0x00000000);
    writel(GPIO(n) + GP_N_OE, 0xffffffff);
    writel(GPIO(n) + GP_N_EVST, 0xffffffff);
    writel(GPIO(n) + GP_N_EVEN, 0x00000000);

    /* Trigger an event; interrupts are masked. */
    g_assert_cmphex(readl(GPIO(n) + GP_N_EVST), ==, 0x00000000);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(n)));
    writel(GPIO(n) + GP_N_DOS, 0x00008000);
    g_assert_cmphex(readl(GPIO(n) + GP_N_EVST), ==, 0x00008000);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(n)));

    /* Unmask all event interrupts; verify that the interrupt fired. */
    writel(GPIO(n) + GP_N_EVEN, 0xffffffff);
    g_assert_true(qtest_get_irq(global_qtest, GPIO_IRQ(n)));

    /* Clear the current bit, set a new bit, irq stays asserted. */
    writel(GPIO(n) + GP_N_DOC, 0x00008000);
    g_assert_true(qtest_get_irq(global_qtest, GPIO_IRQ(n)));
    writel(GPIO(n) + GP_N_DOS, 0x00000200);
    g_assert_true(qtest_get_irq(global_qtest, GPIO_IRQ(n)));
    writel(GPIO(n) + GP_N_EVST, 0x00008000);
    g_assert_true(qtest_get_irq(global_qtest, GPIO_IRQ(n)));

    /* Mask/unmask the event that's currently active. */
    writel(GPIO(n) + GP_N_EVENC, 0x00000200);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(n)));
    writel(GPIO(n) + GP_N_EVENS, 0x00000200);
    g_assert_true(qtest_get_irq(global_qtest, GPIO_IRQ(n)));

    /* Clear the input and the status bit, irq is deasserted. */
    writel(GPIO(n) + GP_N_DOC, 0x00000200);
    g_assert_true(qtest_get_irq(global_qtest, GPIO_IRQ(n)));
    writel(GPIO(n) + GP_N_EVST, 0x00000200);
    g_assert_false(qtest_get_irq(global_qtest, GPIO_IRQ(n)));
}

int main(int argc, char **argv)
{
    int ret;
    int i;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    qtest_add_func("/npcm7xx_gpio/dout_to_din", test_dout_to_din);
    qtest_add_func("/npcm7xx_gpio/pullup_pulldown", test_pullup_pulldown);
    qtest_add_func("/npcm7xx_gpio/output_enable", test_output_enable);
    qtest_add_func("/npcm7xx_gpio/open_drain", test_open_drain);
    qtest_add_func("/npcm7xx_gpio/polarity", test_polarity);
    qtest_add_func("/npcm7xx_gpio/input_mask", test_input_mask);
    qtest_add_func("/npcm7xx_gpio/temp_lock", test_temp_lock);
    qtest_add_func("/npcm7xx_gpio/events/level", test_events_level);
    qtest_add_func("/npcm7xx_gpio/events/rising_edge", test_events_rising_edge);
    qtest_add_func("/npcm7xx_gpio/events/both_edges", test_events_both_edges);

    for (i = 0; i < NR_GPIO_DEVICES; i++) {
        g_autofree char *test_name =
            g_strdup_printf("/npcm7xx_gpio/gpio[%d]/irq", i);
        qtest_add_data_func(test_name, (void *)(intptr_t)i, test_gpion_irq);
    }

    qtest_start("-machine npcm750-evb");
    qtest_irq_intercept_in(global_qtest, "/machine/soc/a9mpcore/gic");
    ret = g_test_run();
    qtest_end();

    return ret;
}
