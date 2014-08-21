/*
 * AHCI test cases
 *
 * Copyright (c) 2014 John Snow <jsnow@redhat.com>
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

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <glib.h>

#include "libqtest.h"
#include "libqos/pci-pc.h"
#include "libqos/malloc-pc.h"

#include "qemu-common.h"
#include "qemu/host-utils.h"

#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_regs.h"

/* Test-specific defines. */
#define TEST_IMAGE_SIZE    (64 * 1024 * 1024)

/*** Supplementary PCI Config Space IDs & Masks ***/
#define PCI_DEVICE_ID_INTEL_Q35_AHCI   (0x2922)

/*** Globals ***/
static QGuestAllocator *guest_malloc;
static QPCIBus *pcibus;
static char tmp_path[] = "/tmp/qtest.XXXXXX";

/*** Function Declarations ***/
static QPCIDevice *get_ahci_device(void);
static void free_ahci_device(QPCIDevice *dev);

/*** Utilities ***/

/**
 * Locate, verify, and return a handle to the AHCI device.
 */
static QPCIDevice *get_ahci_device(void)
{
    QPCIDevice *ahci;
    uint16_t vendor_id, device_id;

    pcibus = qpci_init_pc();

    /* Find the AHCI PCI device and verify it's the right one. */
    ahci = qpci_device_find(pcibus, QPCI_DEVFN(0x1F, 0x02));
    g_assert(ahci != NULL);

    vendor_id = qpci_config_readw(ahci, PCI_VENDOR_ID);
    device_id = qpci_config_readw(ahci, PCI_DEVICE_ID);

    g_assert_cmphex(vendor_id, ==, PCI_VENDOR_ID_INTEL);
    g_assert_cmphex(device_id, ==, PCI_DEVICE_ID_INTEL_Q35_AHCI);

    return ahci;
}

static void free_ahci_device(QPCIDevice *ahci)
{
    /* libqos doesn't have a function for this, so free it manually */
    g_free(ahci);

    if (pcibus) {
        qpci_free_pc(pcibus);
        pcibus = NULL;
    }
}

/*** Test Setup & Teardown ***/

/**
 * Launch QEMU with the given command line,
 * and then set up interrupts and our guest malloc interface.
 */
static void qtest_boot(const char *cmdline_fmt, ...)
{
    va_list ap;
    char *cmdline;

    va_start(ap, cmdline_fmt);
    cmdline = g_strdup_vprintf(cmdline_fmt, ap);
    va_end(ap);

    qtest_start(cmdline);
    qtest_irq_intercept_in(global_qtest, "ioapic");
    guest_malloc = pc_alloc_init();

    g_free(cmdline);
}

/**
 * Tear down the QEMU instance.
 */
static void qtest_shutdown(void)
{
    g_free(guest_malloc);
    guest_malloc = NULL;
    qtest_end();
}

/**
 * Start a Q35 machine and bookmark a handle to the AHCI device.
 */
static QPCIDevice *ahci_boot(void)
{
    qtest_boot("-drive if=none,id=drive0,file=%s,cache=writeback,serial=%s"
               " -M q35 "
               "-device ide-hd,drive=drive0 "
               "-global ide-hd.ver=%s",
               tmp_path, "testdisk", "version");

    /* Verify that we have an AHCI device present. */
    return get_ahci_device();
}

/**
 * Clean up the PCI device, then terminate the QEMU instance.
 */
static void ahci_shutdown(QPCIDevice *ahci)
{
    free_ahci_device(ahci);
    qtest_shutdown();
}

/******************************************************************************/
/* Test Interfaces                                                            */
/******************************************************************************/

/**
 * Basic sanity test to boot a machine, find an AHCI device, and shutdown.
 */
static void test_sanity(void)
{
    QPCIDevice *ahci;
    ahci = ahci_boot();
    ahci_shutdown(ahci);
}

/******************************************************************************/

int main(int argc, char **argv)
{
    const char *arch;
    int fd;
    int ret;

    /* Should be first to utilize g_test functionality, So we can see errors. */
    g_test_init(&argc, &argv, NULL);

    /* Check architecture */
    arch = qtest_get_arch();
    if (strcmp(arch, "i386") && strcmp(arch, "x86_64")) {
        g_test_message("Skipping test for non-x86");
        return 0;
    }

    /* Create a temporary raw image */
    fd = mkstemp(tmp_path);
    g_assert(fd >= 0);
    ret = ftruncate(fd, TEST_IMAGE_SIZE);
    g_assert(ret == 0);
    close(fd);

    /* Run the tests */
    qtest_add_func("/ahci/sanity",     test_sanity);

    ret = g_test_run();

    /* Cleanup */
    unlink(tmp_path);

    return ret;
}
