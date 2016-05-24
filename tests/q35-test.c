/*
 * QTest testcase for Q35 northbridge
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * Author: Gerd Hoffmann <kraxel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"
#include "hw/pci-host/q35.h"

static void smram_set_bit(QPCIDevice *pcidev, uint8_t mask, bool enabled)
{
    uint8_t smram;

    smram = qpci_config_readb(pcidev, MCH_HOST_BRIDGE_SMRAM);
    if (enabled) {
        smram |= mask;
    } else {
        smram &= ~mask;
    }
    qpci_config_writeb(pcidev, MCH_HOST_BRIDGE_SMRAM, smram);
}

static bool smram_test_bit(QPCIDevice *pcidev, uint8_t mask)
{
    uint8_t smram;

    smram = qpci_config_readb(pcidev, MCH_HOST_BRIDGE_SMRAM);
    return smram & mask;
}

static void test_smram_lock(void)
{
    QPCIBus *pcibus;
    QPCIDevice *pcidev;
    QDict *response;

    pcibus = qpci_init_pc();
    g_assert(pcibus != NULL);

    pcidev = qpci_device_find(pcibus, 0);
    g_assert(pcidev != NULL);

    /* check open is settable */
    smram_set_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN, false);
    g_assert(smram_test_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN) == false);
    smram_set_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN, true);
    g_assert(smram_test_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN) == true);

    /* lock, check open is cleared & not settable */
    smram_set_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_LCK, true);
    g_assert(smram_test_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN) == false);
    smram_set_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN, true);
    g_assert(smram_test_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN) == false);

    /* reset */
    response = qmp("{'execute': 'system_reset', 'arguments': {} }");
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    /* check open is settable again */
    smram_set_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN, false);
    g_assert(smram_test_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN) == false);
    smram_set_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN, true);
    g_assert(smram_test_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN) == true);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/q35/smram/lock", test_smram_lock);

    qtest_start("-M q35");
    ret = g_test_run();
    qtest_end();

    return ret;
}
