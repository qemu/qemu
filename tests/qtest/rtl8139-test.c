/*
 * QTest testcase for Realtek 8139 NIC
 *
 * Copyright (c) 2013-2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "libqos/pci-pc.h"
#include "qemu/timer.h"

/* Tests only initialization so far. TODO: Replace with functional tests */
static void nop(void)
{
}

#define CLK 33333333

static QPCIBus *pcibus;
static QPCIDevice *dev;
static QPCIBar dev_bar;

static void save_fn(QPCIDevice *dev, int devfn, void *data)
{
    QPCIDevice **pdev = (QPCIDevice **) data;

    *pdev = dev;
}

static QPCIDevice *get_device(void)
{
    QPCIDevice *dev;

    pcibus = qpci_new_pc(global_qtest, NULL);
    qpci_device_foreach(pcibus, 0x10ec, 0x8139, save_fn, &dev);
    g_assert(dev != NULL);

    return dev;
}

#define PORT(name, len, val) \
static unsigned __attribute__((unused)) in_##name(void) \
{ \
    unsigned res = qpci_io_read##len(dev, dev_bar, (val));     \
    g_test_message("*%s -> %x", #name, res); \
    return res; \
} \
static void out_##name(unsigned v) \
{ \
    g_test_message("%x -> *%s", v, #name); \
    qpci_io_write##len(dev, dev_bar, (val), v);        \
}

PORT(Timer, l, 0x48)
PORT(IntrMask, w, 0x3c)
PORT(IntrStatus, w, 0x3E)
PORT(TimerInt, l, 0x54)

#define fatal(...) do { g_test_message(__VA_ARGS__); g_assert(0); } while (0)

static void test_timer(void)
{
    const unsigned from = 0.95 * CLK;
    const unsigned to = 1.6 * CLK;
    unsigned prev, curr, next;
    unsigned cnt, diff;

    out_IntrMask(0);

    in_IntrStatus();
    in_Timer();
    in_Timer();

    /* Test 1. test counter continue and continue */
    out_TimerInt(0); /* disable timer */
    out_IntrStatus(0x4000);
    out_Timer(12345); /* reset timer to 0 */
    curr = in_Timer();
    if (curr > 0.1 * CLK) {
        fatal("time too big %u\n", curr);
    }
    for (cnt = 0; ; ) {
        clock_step(1 * NANOSECONDS_PER_SECOND);
        prev = curr;
        curr = in_Timer();

        /* test skip is in a specific range */
        diff = (curr-prev) & 0xffffffffu;
        if (diff < from || diff > to) {
            fatal("Invalid diff %u (%u-%u)\n", diff, from, to);
        }
        if (curr < prev && ++cnt == 3) {
            break;
        }
    }

    /* Test 2. Check we didn't get an interrupt with TimerInt == 0 */
    if (in_IntrStatus() & 0x4000) {
        fatal("got an interrupt\n");
    }

    /* Test 3. Setting TimerInt to 1 and Timer to 0 get interrupt */
    out_TimerInt(1);
    out_Timer(0);
    clock_step(40);
    if ((in_IntrStatus() & 0x4000) == 0) {
        fatal("we should have an interrupt here!\n");
    }

    /* Test 3. Check acknowledge */
    out_IntrStatus(0x4000);
    if (in_IntrStatus() & 0x4000) {
        fatal("got an interrupt\n");
    }

    /* Test. Status set after Timer reset */
    out_Timer(0);
    out_TimerInt(0);
    out_IntrStatus(0x4000);
    curr = in_Timer();
    out_TimerInt(curr + 0.5 * CLK);
    clock_step(1 * NANOSECONDS_PER_SECOND);
    out_Timer(0);
    if ((in_IntrStatus() & 0x4000) == 0) {
        fatal("we should have an interrupt here!\n");
    }

    /* Test. Status set after TimerInt reset */
    out_Timer(0);
    out_TimerInt(0);
    out_IntrStatus(0x4000);
    curr = in_Timer();
    out_TimerInt(curr + 0.5 * CLK);
    clock_step(1 * NANOSECONDS_PER_SECOND);
    out_TimerInt(0);
    if ((in_IntrStatus() & 0x4000) == 0) {
        fatal("we should have an interrupt here!\n");
    }

    /* Test 4. Increment TimerInt we should see an interrupt */
    curr = in_Timer();
    next = curr + 5.0 * CLK;
    out_TimerInt(next);
    for (cnt = 0; ; ) {
        clock_step(1 * NANOSECONDS_PER_SECOND);
        prev = curr;
        curr = in_Timer();
        diff = (curr-prev) & 0xffffffffu;
        if (diff < from || diff > to) {
            fatal("Invalid diff %u (%u-%u)\n", diff, from, to);
        }
        if (cnt < 3 && curr > next) {
            if ((in_IntrStatus() & 0x4000) == 0) {
                fatal("we should have an interrupt here!\n");
            }
            out_IntrStatus(0x4000);
            next = curr + 5.0 * CLK;
            out_TimerInt(next);
            if (++cnt == 3) {
                out_TimerInt(1);
            }
        /* Test 5. Second time we pass from 0 should see an interrupt */
        } else if (cnt >= 3 && curr < prev) {
            /* here we should have an interrupt */
            if ((in_IntrStatus() & 0x4000) == 0) {
                fatal("we should have an interrupt here!\n");
            }
            out_IntrStatus(0x4000);
            if (++cnt == 5) {
                break;
            }
        }
    }

    g_test_message("Everythink is ok!");
}


static void test_init(void)
{
    uint64_t barsize;

    dev = get_device();

    dev_bar = qpci_iomap(dev, 0, &barsize);

    qpci_device_enable(dev);

    test_timer();
}

int main(int argc, char **argv)
{
    int ret;

    qtest_start("-device rtl8139");

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/rtl8139/nop", nop);
    qtest_add_func("/rtl8139/timer", test_init);

    ret = g_test_run();

    qtest_end();

    return ret;
}
