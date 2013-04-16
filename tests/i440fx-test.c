/*
 * qtest I440FX test case
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "libqos/pci.h"
#include "libqos/pci-pc.h"
#include "libqtest.h"

#include "hw/pci/pci_regs.h"

#include <glib.h>
#include <stdio.h>

#define BROKEN 1

typedef struct TestData
{
    int num_cpus;
    QPCIBus *bus;
} TestData;

static void test_i440fx_defaults(gconstpointer opaque)
{
    const TestData *s = opaque;
    QPCIDevice *dev;
    uint32_t value;

    dev = qpci_device_find(s->bus, QPCI_DEVFN(0, 0));
    g_assert(dev != NULL);

    /* 3.2.2 */
    g_assert_cmpint(qpci_config_readw(dev, PCI_VENDOR_ID), ==, 0x8086);
    /* 3.2.3 */
    g_assert_cmpint(qpci_config_readw(dev, PCI_DEVICE_ID), ==, 0x1237);
#ifndef BROKEN
    /* 3.2.4 */
    g_assert_cmpint(qpci_config_readw(dev, PCI_COMMAND), ==, 0x0006);
    /* 3.2.5 */
    g_assert_cmpint(qpci_config_readw(dev, PCI_STATUS), ==, 0x0280);
#endif
    /* 3.2.7 */
    g_assert_cmpint(qpci_config_readb(dev, PCI_CLASS_PROG), ==, 0x00);
    g_assert_cmpint(qpci_config_readw(dev, PCI_CLASS_DEVICE), ==, 0x0600);
    /* 3.2.8 */
    g_assert_cmpint(qpci_config_readb(dev, PCI_LATENCY_TIMER), ==, 0x00);
    /* 3.2.9 */
    g_assert_cmpint(qpci_config_readb(dev, PCI_HEADER_TYPE), ==, 0x00);
    /* 3.2.10 */
    g_assert_cmpint(qpci_config_readb(dev, PCI_BIST), ==, 0x00);

    /* 3.2.11 */
    value = qpci_config_readw(dev, 0x50); /* PMCCFG */
    if (s->num_cpus == 1) { /* WPE */
        g_assert(!(value & (1 << 15)));
    } else {
        g_assert((value & (1 << 15)));
    }

    g_assert(!(value & (1 << 6))); /* EPTE */

    /* 3.2.12 */
    g_assert_cmpint(qpci_config_readb(dev, 0x52), ==, 0x00); /* DETURBO */
    /* 3.2.13 */
#ifndef BROKEN
    g_assert_cmpint(qpci_config_readb(dev, 0x53), ==, 0x80); /* DBC */
#endif
    /* 3.2.14 */
    g_assert_cmpint(qpci_config_readb(dev, 0x54), ==, 0x00); /* AXC */
    /* 3.2.15 */
    g_assert_cmpint(qpci_config_readw(dev, 0x55), ==, 0x0000); /* DRT */
#ifndef BROKEN
    /* 3.2.16 */
    g_assert_cmpint(qpci_config_readb(dev, 0x57), ==, 0x01); /* DRAMC */
    /* 3.2.17 */
    g_assert_cmpint(qpci_config_readb(dev, 0x58), ==, 0x10); /* DRAMT */
#endif
    /* 3.2.18 */
    g_assert_cmpint(qpci_config_readb(dev, 0x59), ==, 0x00); /* PAM0 */
    g_assert_cmpint(qpci_config_readb(dev, 0x5A), ==, 0x00); /* PAM1 */
    g_assert_cmpint(qpci_config_readb(dev, 0x5B), ==, 0x00); /* PAM2 */
    g_assert_cmpint(qpci_config_readb(dev, 0x5C), ==, 0x00); /* PAM3 */
    g_assert_cmpint(qpci_config_readb(dev, 0x5D), ==, 0x00); /* PAM4 */
    g_assert_cmpint(qpci_config_readb(dev, 0x5E), ==, 0x00); /* PAM5 */
    g_assert_cmpint(qpci_config_readb(dev, 0x5F), ==, 0x00); /* PAM6 */
#ifndef BROKEN
    /* 3.2.19 */
    g_assert_cmpint(qpci_config_readb(dev, 0x60), ==, 0x01); /* DRB0 */
    g_assert_cmpint(qpci_config_readb(dev, 0x61), ==, 0x01); /* DRB1 */
    g_assert_cmpint(qpci_config_readb(dev, 0x62), ==, 0x01); /* DRB2 */
    g_assert_cmpint(qpci_config_readb(dev, 0x63), ==, 0x01); /* DRB3 */
    g_assert_cmpint(qpci_config_readb(dev, 0x64), ==, 0x01); /* DRB4 */
    g_assert_cmpint(qpci_config_readb(dev, 0x65), ==, 0x01); /* DRB5 */
    g_assert_cmpint(qpci_config_readb(dev, 0x66), ==, 0x01); /* DRB6 */
    g_assert_cmpint(qpci_config_readb(dev, 0x67), ==, 0x01); /* DRB7 */
#endif
    /* 3.2.20 */
    g_assert_cmpint(qpci_config_readb(dev, 0x68), ==, 0x00); /* FDHC */
    /* 3.2.21 */
    g_assert_cmpint(qpci_config_readb(dev, 0x70), ==, 0x00); /* MTT */
#ifndef BROKEN
    /* 3.2.22 */
    g_assert_cmpint(qpci_config_readb(dev, 0x71), ==, 0x10); /* CLT */
#endif
    /* 3.2.23 */
    g_assert_cmpint(qpci_config_readb(dev, 0x72), ==, 0x02); /* SMRAM */
    /* 3.2.24 */
    g_assert_cmpint(qpci_config_readb(dev, 0x90), ==, 0x00); /* ERRCMD */
    /* 3.2.25 */
    g_assert_cmpint(qpci_config_readb(dev, 0x91), ==, 0x00); /* ERRSTS */
    /* 3.2.26 */
    g_assert_cmpint(qpci_config_readb(dev, 0x93), ==, 0x00); /* TRC */
}

int main(int argc, char **argv)
{
    QTestState *s;
    TestData data;
    char *cmdline;
    int ret;

    g_test_init(&argc, &argv, NULL);

    data.num_cpus = 1;

    cmdline = g_strdup_printf("-display none -smp %d", data.num_cpus);
    s = qtest_start(cmdline);
    g_free(cmdline);

    data.bus = qpci_init_pc();

    g_test_add_data_func("/i440fx/defaults", &data, test_i440fx_defaults);

    ret = g_test_run();

    if (s) {
        qtest_quit(s);
    }

    return ret;
}
