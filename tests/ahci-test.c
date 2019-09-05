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

#include "qemu/osdep.h"
#include <getopt.h>

#include "libqtest.h"
#include "libqos/libqos-pc.h"
#include "libqos/ahci.h"
#include "libqos/pci-pc.h"

#include "qemu-common.h"
#include "qapi/qmp/qdict.h"
#include "qemu/host-utils.h"

#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_regs.h"

/* TODO actually test the results and get rid of this */
#define qmp_discard_response(s, ...) qobject_unref(qtest_qmp(s, __VA_ARGS__))

/* Test images sizes in MB */
#define TEST_IMAGE_SIZE_MB_LARGE (200 * 1024)
#define TEST_IMAGE_SIZE_MB_SMALL 64

/*** Globals ***/
static char tmp_path[] = "/tmp/qtest.XXXXXX";
static char debug_path[] = "/tmp/qtest-blkdebug.XXXXXX";
static char mig_socket[] = "/tmp/qtest-migration.XXXXXX";
static bool ahci_pedantic;
static const char *imgfmt;
static unsigned test_image_size_mb;

/*** Function Declarations ***/
static void ahci_test_port_spec(AHCIQState *ahci, uint8_t port);
static void ahci_test_pci_spec(AHCIQState *ahci);
static void ahci_test_pci_caps(AHCIQState *ahci, uint16_t header,
                               uint8_t offset);
static void ahci_test_satacap(AHCIQState *ahci, uint8_t offset);
static void ahci_test_msicap(AHCIQState *ahci, uint8_t offset);
static void ahci_test_pmcap(AHCIQState *ahci, uint8_t offset);

/*** Utilities ***/

static uint64_t mb_to_sectors(uint64_t image_size_mb)
{
    return (image_size_mb * 1024 * 1024) / AHCI_SECTOR_SIZE;
}

static void string_bswap16(uint16_t *s, size_t bytes)
{
    g_assert_cmphex((bytes & 1), ==, 0);
    bytes /= 2;

    while (bytes--) {
        *s = bswap16(*s);
        s++;
    }
}

/**
 * Verify that the transfer did not corrupt our state at all.
 */
static void verify_state(AHCIQState *ahci, uint64_t hba_old)
{
    int i, j;
    uint32_t ahci_fingerprint;
    uint64_t hba_base;
    AHCICommandHeader cmd;

    ahci_fingerprint = qpci_config_readl(ahci->dev, PCI_VENDOR_ID);
    g_assert_cmphex(ahci_fingerprint, ==, ahci->fingerprint);

    /* If we haven't initialized, this is as much as can be validated. */
    if (!ahci->enabled) {
        return;
    }

    hba_base = (uint64_t)qpci_config_readl(ahci->dev, PCI_BASE_ADDRESS_5);
    g_assert_cmphex(hba_base, ==, hba_old);

    g_assert_cmphex(ahci_rreg(ahci, AHCI_CAP), ==, ahci->cap);
    g_assert_cmphex(ahci_rreg(ahci, AHCI_CAP2), ==, ahci->cap2);

    for (i = 0; i < 32; i++) {
        g_assert_cmphex(ahci_px_rreg(ahci, i, AHCI_PX_FB), ==,
                        ahci->port[i].fb);
        g_assert_cmphex(ahci_px_rreg(ahci, i, AHCI_PX_CLB), ==,
                        ahci->port[i].clb);
        for (j = 0; j < 32; j++) {
            ahci_get_command_header(ahci, i, j, &cmd);
            g_assert_cmphex(cmd.prdtl, ==, ahci->port[i].prdtl[j]);
            g_assert_cmphex(cmd.ctba, ==, ahci->port[i].ctba[j]);
        }
    }
}

static void ahci_migrate(AHCIQState *from, AHCIQState *to, const char *uri)
{
    QOSState *tmp = to->parent;
    QPCIDevice *dev = to->dev;
    char *uri_local = NULL;
    uint64_t hba_old;

    if (uri == NULL) {
        uri_local = g_strdup_printf("%s%s", "unix:", mig_socket);
        uri = uri_local;
    }

    hba_old = (uint64_t)qpci_config_readl(from->dev, PCI_BASE_ADDRESS_5);

    /* context will be 'to' after completion. */
    migrate(from->parent, to->parent, uri);

    /* We'd like for the AHCIState objects to still point
     * to information specific to its specific parent
     * instance, but otherwise just inherit the new data. */
    memcpy(to, from, sizeof(AHCIQState));
    to->parent = tmp;
    to->dev = dev;

    tmp = from->parent;
    dev = from->dev;
    memset(from, 0x00, sizeof(AHCIQState));
    from->parent = tmp;
    from->dev = dev;

    verify_state(to, hba_old);
    g_free(uri_local);
}

/*** Test Setup & Teardown ***/

/**
 * Start a Q35 machine and bookmark a handle to the AHCI device.
 */
static AHCIQState *ahci_vboot(const char *cli, va_list ap)
{
    AHCIQState *s;

    s = g_new0(AHCIQState, 1);
    s->parent = qtest_pc_vboot(cli, ap);
    alloc_set_flags(&s->parent->alloc, ALLOC_LEAK_ASSERT);

    /* Verify that we have an AHCI device present. */
    s->dev = get_ahci_device(s->parent->qts, &s->fingerprint);

    return s;
}

/**
 * Start a Q35 machine and bookmark a handle to the AHCI device.
 */
static AHCIQState *ahci_boot(const char *cli, ...)
{
    AHCIQState *s;
    va_list ap;

    if (cli) {
        va_start(ap, cli);
        s = ahci_vboot(cli, ap);
        va_end(ap);
    } else {
        cli = "-drive if=none,id=drive0,file=%s,cache=writeback,format=%s"
            " -M q35 "
            "-device ide-hd,drive=drive0 "
            "-global ide-hd.serial=%s "
            "-global ide-hd.ver=%s";
        s = ahci_boot(cli, tmp_path, imgfmt, "testdisk", "version");
    }

    return s;
}

/**
 * Clean up the PCI device, then terminate the QEMU instance.
 */
static void ahci_shutdown(AHCIQState *ahci)
{
    QOSState *qs = ahci->parent;

    ahci_clean_mem(ahci);
    free_ahci_device(ahci->dev);
    g_free(ahci);
    qtest_shutdown(qs);
}

/**
 * Boot and fully enable the HBA device.
 * @see ahci_boot, ahci_pci_enable and ahci_hba_enable.
 */
static AHCIQState *ahci_boot_and_enable(const char *cli, ...)
{
    AHCIQState *ahci;
    va_list ap;
    uint16_t buff[256];
    uint8_t port;
    uint8_t hello;

    if (cli) {
        va_start(ap, cli);
        ahci = ahci_vboot(cli, ap);
        va_end(ap);
    } else {
        ahci = ahci_boot(NULL);
    }

    ahci_pci_enable(ahci);
    ahci_hba_enable(ahci);
    /* Initialize test device */
    port = ahci_port_select(ahci);
    ahci_port_clear(ahci, port);
    if (is_atapi(ahci, port)) {
        hello = CMD_PACKET_ID;
    } else {
        hello = CMD_IDENTIFY;
    }
    ahci_io(ahci, port, hello, &buff, sizeof(buff), 0);

    return ahci;
}

/*** Specification Adherence Tests ***/

/**
 * Implementation for test_pci_spec. Ensures PCI configuration space is sane.
 */
static void ahci_test_pci_spec(AHCIQState *ahci)
{
    uint8_t datab;
    uint16_t data;
    uint32_t datal;

    /* Most of these bits should start cleared until we turn them on. */
    data = qpci_config_readw(ahci->dev, PCI_COMMAND);
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_MEMORY);
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_MASTER);
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_SPECIAL);     /* Reserved */
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_VGA_PALETTE); /* Reserved */
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_PARITY);
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_WAIT);        /* Reserved */
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_SERR);
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_FAST_BACK);
    ASSERT_BIT_CLEAR(data, PCI_COMMAND_INTX_DISABLE);
    ASSERT_BIT_CLEAR(data, 0xF800);                  /* Reserved */

    data = qpci_config_readw(ahci->dev, PCI_STATUS);
    ASSERT_BIT_CLEAR(data, 0x01 | 0x02 | 0x04);     /* Reserved */
    ASSERT_BIT_CLEAR(data, PCI_STATUS_INTERRUPT);
    ASSERT_BIT_SET(data, PCI_STATUS_CAP_LIST);      /* must be set */
    ASSERT_BIT_CLEAR(data, PCI_STATUS_UDF);         /* Reserved */
    ASSERT_BIT_CLEAR(data, PCI_STATUS_PARITY);
    ASSERT_BIT_CLEAR(data, PCI_STATUS_SIG_TARGET_ABORT);
    ASSERT_BIT_CLEAR(data, PCI_STATUS_REC_TARGET_ABORT);
    ASSERT_BIT_CLEAR(data, PCI_STATUS_REC_MASTER_ABORT);
    ASSERT_BIT_CLEAR(data, PCI_STATUS_SIG_SYSTEM_ERROR);
    ASSERT_BIT_CLEAR(data, PCI_STATUS_DETECTED_PARITY);

    /* RID occupies the low byte, CCs occupy the high three. */
    datal = qpci_config_readl(ahci->dev, PCI_CLASS_REVISION);
    if (ahci_pedantic) {
        /* AHCI 1.3 specifies that at-boot, the RID should reset to 0x00,
         * Though in practice this is likely seldom true. */
        ASSERT_BIT_CLEAR(datal, 0xFF);
    }

    /* BCC *must* equal 0x01. */
    g_assert_cmphex(PCI_BCC(datal), ==, 0x01);
    if (PCI_SCC(datal) == 0x01) {
        /* IDE */
        ASSERT_BIT_SET(0x80000000, datal);
        ASSERT_BIT_CLEAR(0x60000000, datal);
    } else if (PCI_SCC(datal) == 0x04) {
        /* RAID */
        g_assert_cmphex(PCI_PI(datal), ==, 0);
    } else if (PCI_SCC(datal) == 0x06) {
        /* AHCI */
        g_assert_cmphex(PCI_PI(datal), ==, 0x01);
    } else {
        g_assert_not_reached();
    }

    datab = qpci_config_readb(ahci->dev, PCI_CACHE_LINE_SIZE);
    g_assert_cmphex(datab, ==, 0);

    datab = qpci_config_readb(ahci->dev, PCI_LATENCY_TIMER);
    g_assert_cmphex(datab, ==, 0);

    /* Only the bottom 7 bits must be off. */
    datab = qpci_config_readb(ahci->dev, PCI_HEADER_TYPE);
    ASSERT_BIT_CLEAR(datab, 0x7F);

    /* BIST is optional, but the low 7 bits must always start off regardless. */
    datab = qpci_config_readb(ahci->dev, PCI_BIST);
    ASSERT_BIT_CLEAR(datab, 0x7F);

    /* BARS 0-4 do not have a boot spec, but ABAR/BAR5 must be clean. */
    datal = qpci_config_readl(ahci->dev, PCI_BASE_ADDRESS_5);
    g_assert_cmphex(datal, ==, 0);

    qpci_config_writel(ahci->dev, PCI_BASE_ADDRESS_5, 0xFFFFFFFF);
    datal = qpci_config_readl(ahci->dev, PCI_BASE_ADDRESS_5);
    /* ABAR must be 32-bit, memory mapped, non-prefetchable and
     * must be >= 512 bytes. To that end, bits 0-8 must be off. */
    ASSERT_BIT_CLEAR(datal, 0xFF);

    /* Capability list MUST be present, */
    datal = qpci_config_readl(ahci->dev, PCI_CAPABILITY_LIST);
    /* But these bits are reserved. */
    ASSERT_BIT_CLEAR(datal, ~0xFF);
    g_assert_cmphex(datal, !=, 0);

    /* Check specification adherence for capability extenstions. */
    data = qpci_config_readw(ahci->dev, datal);

    switch (ahci->fingerprint) {
    case AHCI_INTEL_ICH9:
        /* Intel ICH9 Family Datasheet 14.1.19 p.550 */
        g_assert_cmphex((data & 0xFF), ==, PCI_CAP_ID_MSI);
        break;
    default:
        /* AHCI 1.3, Section 2.1.14 -- CAP must point to PMCAP. */
        g_assert_cmphex((data & 0xFF), ==, PCI_CAP_ID_PM);
    }

    ahci_test_pci_caps(ahci, data, (uint8_t)datal);

    /* Reserved. */
    datal = qpci_config_readl(ahci->dev, PCI_CAPABILITY_LIST + 4);
    g_assert_cmphex(datal, ==, 0);

    /* IPIN might vary, but ILINE must be off. */
    datab = qpci_config_readb(ahci->dev, PCI_INTERRUPT_LINE);
    g_assert_cmphex(datab, ==, 0);
}

/**
 * Test PCI capabilities for AHCI specification adherence.
 */
static void ahci_test_pci_caps(AHCIQState *ahci, uint16_t header,
                               uint8_t offset)
{
    uint8_t cid = header & 0xFF;
    uint8_t next = header >> 8;

    g_test_message("CID: %02x; next: %02x", cid, next);

    switch (cid) {
    case PCI_CAP_ID_PM:
        ahci_test_pmcap(ahci, offset);
        break;
    case PCI_CAP_ID_MSI:
        ahci_test_msicap(ahci, offset);
        break;
    case PCI_CAP_ID_SATA:
        ahci_test_satacap(ahci, offset);
        break;

    default:
        g_test_message("Unknown CAP 0x%02x", cid);
    }

    if (next) {
        ahci_test_pci_caps(ahci, qpci_config_readw(ahci->dev, next), next);
    }
}

/**
 * Test SATA PCI capabilitity for AHCI specification adherence.
 */
static void ahci_test_satacap(AHCIQState *ahci, uint8_t offset)
{
    uint16_t dataw;
    uint32_t datal;

    g_test_message("Verifying SATACAP");

    /* Assert that the SATACAP version is 1.0, And reserved bits are empty. */
    dataw = qpci_config_readw(ahci->dev, offset + 2);
    g_assert_cmphex(dataw, ==, 0x10);

    /* Grab the SATACR1 register. */
    datal = qpci_config_readw(ahci->dev, offset + 4);

    switch (datal & 0x0F) {
    case 0x04: /* BAR0 */
    case 0x05: /* BAR1 */
    case 0x06:
    case 0x07:
    case 0x08:
    case 0x09: /* BAR5 */
    case 0x0F: /* Immediately following SATACR1 in PCI config space. */
        break;
    default:
        /* Invalid BARLOC for the Index Data Pair. */
        g_assert_not_reached();
    }

    /* Reserved. */
    g_assert_cmphex((datal >> 24), ==, 0x00);
}

/**
 * Test MSI PCI capability for AHCI specification adherence.
 */
static void ahci_test_msicap(AHCIQState *ahci, uint8_t offset)
{
    uint16_t dataw;
    uint32_t datal;

    g_test_message("Verifying MSICAP");

    dataw = qpci_config_readw(ahci->dev, offset + PCI_MSI_FLAGS);
    ASSERT_BIT_CLEAR(dataw, PCI_MSI_FLAGS_ENABLE);
    ASSERT_BIT_CLEAR(dataw, PCI_MSI_FLAGS_QSIZE);
    ASSERT_BIT_CLEAR(dataw, PCI_MSI_FLAGS_RESERVED);

    datal = qpci_config_readl(ahci->dev, offset + PCI_MSI_ADDRESS_LO);
    g_assert_cmphex(datal, ==, 0);

    if (dataw & PCI_MSI_FLAGS_64BIT) {
        g_test_message("MSICAP is 64bit");
        datal = qpci_config_readl(ahci->dev, offset + PCI_MSI_ADDRESS_HI);
        g_assert_cmphex(datal, ==, 0);
        dataw = qpci_config_readw(ahci->dev, offset + PCI_MSI_DATA_64);
        g_assert_cmphex(dataw, ==, 0);
    } else {
        g_test_message("MSICAP is 32bit");
        dataw = qpci_config_readw(ahci->dev, offset + PCI_MSI_DATA_32);
        g_assert_cmphex(dataw, ==, 0);
    }
}

/**
 * Test Power Management PCI capability for AHCI specification adherence.
 */
static void ahci_test_pmcap(AHCIQState *ahci, uint8_t offset)
{
    uint16_t dataw;

    g_test_message("Verifying PMCAP");

    dataw = qpci_config_readw(ahci->dev, offset + PCI_PM_PMC);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CAP_PME_CLOCK);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CAP_RESERVED);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CAP_D1);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CAP_D2);

    dataw = qpci_config_readw(ahci->dev, offset + PCI_PM_CTRL);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CTRL_STATE_MASK);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CTRL_RESERVED);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CTRL_DATA_SEL_MASK);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CTRL_DATA_SCALE_MASK);
}

static void ahci_test_hba_spec(AHCIQState *ahci)
{
    unsigned i;
    uint32_t reg;
    uint32_t ports;
    uint8_t nports_impl;
    uint8_t maxports;

    g_assert(ahci != NULL);

    /*
     * Note that the AHCI spec does expect the BIOS to set up a few things:
     * CAP.SSS    - Support for staggered spin-up            (t/f)
     * CAP.SMPS   - Support for mechanical presence switches (t/f)
     * PI         - Ports Implemented                        (1-32)
     * PxCMD.HPCP - Hot Plug Capable Port
     * PxCMD.MPSP - Mechanical Presence Switch Present
     * PxCMD.CPD  - Cold Presence Detection support
     *
     * Additional items are touched if CAP.SSS is on, see AHCI 10.1.1 p.97:
     * Foreach Port Implemented:
     * -PxCMD.ST, PxCMD.CR, PxCMD.FRE, PxCMD.FR, PxSCTL.DET are 0
     * -PxCLB/U and PxFB/U are set to valid regions in memory
     * -PxSUD is set to 1.
     * -PxSSTS.DET is polled for presence; if detected, we continue:
     * -PxSERR is cleared with 1's.
     * -If PxTFD.STS.BSY, PxTFD.STS.DRQ, and PxTFD.STS.ERR are all zero,
     *  the device is ready.
     */

    /* 1 CAP - Capabilities Register */
    ahci->cap = ahci_rreg(ahci, AHCI_CAP);
    ASSERT_BIT_CLEAR(ahci->cap, AHCI_CAP_RESERVED);

    /* 2 GHC - Global Host Control */
    reg = ahci_rreg(ahci, AHCI_GHC);
    ASSERT_BIT_CLEAR(reg, AHCI_GHC_HR);
    ASSERT_BIT_CLEAR(reg, AHCI_GHC_IE);
    ASSERT_BIT_CLEAR(reg, AHCI_GHC_MRSM);
    if (BITSET(ahci->cap, AHCI_CAP_SAM)) {
        g_test_message("Supports AHCI-Only Mode: GHC_AE is Read-Only.");
        ASSERT_BIT_SET(reg, AHCI_GHC_AE);
    } else {
        g_test_message("Supports AHCI/Legacy mix.");
        ASSERT_BIT_CLEAR(reg, AHCI_GHC_AE);
    }

    /* 3 IS - Interrupt Status */
    reg = ahci_rreg(ahci, AHCI_IS);
    g_assert_cmphex(reg, ==, 0);

    /* 4 PI - Ports Implemented */
    ports = ahci_rreg(ahci, AHCI_PI);
    /* Ports Implemented must be non-zero. */
    g_assert_cmphex(ports, !=, 0);
    /* Ports Implemented must be <= Number of Ports. */
    nports_impl = ctpopl(ports);
    g_assert_cmpuint(((AHCI_CAP_NP & ahci->cap) + 1), >=, nports_impl);

    /* Ports must be within the proper range. Given a mapping of SIZE,
     * 256 bytes are used for global HBA control, and the rest is used
     * for ports data, at 0x80 bytes each. */
    g_assert_cmphex(ahci->barsize, >, 0);
    maxports = (ahci->barsize - HBA_DATA_REGION_SIZE) / HBA_PORT_DATA_SIZE;
    /* e.g, 30 ports for 4K of memory. (4096 - 256) / 128 = 30 */
    g_assert_cmphex((reg >> maxports), ==, 0);

    /* 5 AHCI Version */
    reg = ahci_rreg(ahci, AHCI_VS);
    switch (reg) {
    case AHCI_VERSION_0_95:
    case AHCI_VERSION_1_0:
    case AHCI_VERSION_1_1:
    case AHCI_VERSION_1_2:
    case AHCI_VERSION_1_3:
        break;
    default:
        g_assert_not_reached();
    }

    /* 6 Command Completion Coalescing Control: depends on CAP.CCCS. */
    reg = ahci_rreg(ahci, AHCI_CCCCTL);
    if (BITSET(ahci->cap, AHCI_CAP_CCCS)) {
        ASSERT_BIT_CLEAR(reg, AHCI_CCCCTL_EN);
        ASSERT_BIT_CLEAR(reg, AHCI_CCCCTL_RESERVED);
        ASSERT_BIT_SET(reg, AHCI_CCCCTL_CC);
        ASSERT_BIT_SET(reg, AHCI_CCCCTL_TV);
    } else {
        g_assert_cmphex(reg, ==, 0);
    }

    /* 7 CCC_PORTS */
    reg = ahci_rreg(ahci, AHCI_CCCPORTS);
    /* Must be zeroes initially regardless of CAP.CCCS */
    g_assert_cmphex(reg, ==, 0);

    /* 8 EM_LOC */
    reg = ahci_rreg(ahci, AHCI_EMLOC);
    if (BITCLR(ahci->cap, AHCI_CAP_EMS)) {
        g_assert_cmphex(reg, ==, 0);
    }

    /* 9 EM_CTL */
    reg = ahci_rreg(ahci, AHCI_EMCTL);
    if (BITSET(ahci->cap, AHCI_CAP_EMS)) {
        ASSERT_BIT_CLEAR(reg, AHCI_EMCTL_STSMR);
        ASSERT_BIT_CLEAR(reg, AHCI_EMCTL_CTLTM);
        ASSERT_BIT_CLEAR(reg, AHCI_EMCTL_CTLRST);
        ASSERT_BIT_CLEAR(reg, AHCI_EMCTL_RESERVED);
    } else {
        g_assert_cmphex(reg, ==, 0);
    }

    /* 10 CAP2 -- Capabilities Extended */
    ahci->cap2 = ahci_rreg(ahci, AHCI_CAP2);
    ASSERT_BIT_CLEAR(ahci->cap2, AHCI_CAP2_RESERVED);

    /* 11 BOHC -- Bios/OS Handoff Control */
    reg = ahci_rreg(ahci, AHCI_BOHC);
    g_assert_cmphex(reg, ==, 0);

    /* 12 -- 23: Reserved */
    g_test_message("Verifying HBA reserved area is empty.");
    for (i = AHCI_RESERVED; i < AHCI_NVMHCI; ++i) {
        reg = ahci_rreg(ahci, i);
        g_assert_cmphex(reg, ==, 0);
    }

    /* 24 -- 39: NVMHCI */
    if (BITCLR(ahci->cap2, AHCI_CAP2_NVMP)) {
        g_test_message("Verifying HBA/NVMHCI area is empty.");
        for (i = AHCI_NVMHCI; i < AHCI_VENDOR; ++i) {
            reg = ahci_rreg(ahci, i);
            g_assert_cmphex(reg, ==, 0);
        }
    }

    /* 40 -- 63: Vendor */
    g_test_message("Verifying HBA/Vendor area is empty.");
    for (i = AHCI_VENDOR; i < AHCI_PORTS; ++i) {
        reg = ahci_rreg(ahci, i);
        g_assert_cmphex(reg, ==, 0);
    }

    /* 64 -- XX: Port Space */
    for (i = 0; ports || (i < maxports); ports >>= 1, ++i) {
        if (BITSET(ports, 0x1)) {
            g_test_message("Testing port %u for spec", i);
            ahci_test_port_spec(ahci, i);
        } else {
            uint16_t j;
            uint16_t low = AHCI_PORTS + (32 * i);
            uint16_t high = AHCI_PORTS + (32 * (i + 1));
            g_test_message("Asserting unimplemented port %u "
                           "(reg [%u-%u]) is empty.",
                           i, low, high - 1);
            for (j = low; j < high; ++j) {
                reg = ahci_rreg(ahci, j);
                g_assert_cmphex(reg, ==, 0);
            }
        }
    }
}

/**
 * Test the memory space for one port for specification adherence.
 */
static void ahci_test_port_spec(AHCIQState *ahci, uint8_t port)
{
    uint32_t reg;
    unsigned i;

    /* (0) CLB */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_CLB);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CLB_RESERVED);

    /* (1) CLBU */
    if (BITCLR(ahci->cap, AHCI_CAP_S64A)) {
        reg = ahci_px_rreg(ahci, port, AHCI_PX_CLBU);
        g_assert_cmphex(reg, ==, 0);
    }

    /* (2) FB */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_FB);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FB_RESERVED);

    /* (3) FBU */
    if (BITCLR(ahci->cap, AHCI_CAP_S64A)) {
        reg = ahci_px_rreg(ahci, port, AHCI_PX_FBU);
        g_assert_cmphex(reg, ==, 0);
    }

    /* (4) IS */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_IS);
    g_assert_cmphex(reg, ==, 0);

    /* (5) IE */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_IE);
    g_assert_cmphex(reg, ==, 0);

    /* (6) CMD */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_CMD);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_FRE);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_RESERVED);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_CCS);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_FR);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_CR);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_PMA); /* And RW only if CAP.SPM */
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_APSTE); /* RW only if CAP2.APST */
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_ATAPI);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_DLAE);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_ALPE);  /* RW only if CAP.SALP */
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_ASP);   /* RW only if CAP.SALP */
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_ICC);
    /* If CPDetect support does not exist, CPState must be off. */
    if (BITCLR(reg, AHCI_PX_CMD_CPD)) {
        ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_CPS);
    }
    /* If MPSPresence is not set, MPSState must be off. */
    if (BITCLR(reg, AHCI_PX_CMD_MPSP)) {
        ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_MPSS);
    }
    /* If we do not support MPS, MPSS and MPSP must be off. */
    if (BITCLR(ahci->cap, AHCI_CAP_SMPS)) {
        ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_MPSS);
        ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_MPSP);
    }
    /* If, via CPD or MPSP we detect a drive, HPCP must be on. */
    if (BITANY(reg, AHCI_PX_CMD_CPD | AHCI_PX_CMD_MPSP)) {
        ASSERT_BIT_SET(reg, AHCI_PX_CMD_HPCP);
    }
    /* HPCP and ESP cannot both be active. */
    g_assert(!BITSET(reg, AHCI_PX_CMD_HPCP | AHCI_PX_CMD_ESP));
    /* If CAP.FBSS is not set, FBSCP must not be set. */
    if (BITCLR(ahci->cap, AHCI_CAP_FBSS)) {
        ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_FBSCP);
    }

    /* (7) RESERVED */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_RES1);
    g_assert_cmphex(reg, ==, 0);

    /* (8) TFD */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_TFD);
    /* At boot, prior to an FIS being received, the TFD register should be 0x7F,
     * which breaks down as follows, as seen in AHCI 1.3 sec 3.3.8, p. 27. */
    ASSERT_BIT_SET(reg, AHCI_PX_TFD_STS_ERR);
    ASSERT_BIT_SET(reg, AHCI_PX_TFD_STS_CS1);
    ASSERT_BIT_SET(reg, AHCI_PX_TFD_STS_DRQ);
    ASSERT_BIT_SET(reg, AHCI_PX_TFD_STS_CS2);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_TFD_STS_BSY);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_TFD_ERR);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_TFD_RESERVED);

    /* (9) SIG */
    /* Though AHCI specifies the boot value should be 0xFFFFFFFF,
     * Even when GHC.ST is zero, the AHCI HBA may receive the initial
     * D2H register FIS and update the signature asynchronously,
     * so we cannot expect a value here. AHCI 1.3, sec 3.3.9, pp 27-28 */

    /* (10) SSTS / SCR0: SStatus */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_SSTS);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_SSTS_RESERVED);
    /* Even though the register should be 0 at boot, it is asynchronous and
     * prone to change, so we cannot test any well known value. */

    /* (11) SCTL / SCR2: SControl */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_SCTL);
    g_assert_cmphex(reg, ==, 0);

    /* (12) SERR / SCR1: SError */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_SERR);
    g_assert_cmphex(reg, ==, 0);

    /* (13) SACT / SCR3: SActive */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_SACT);
    g_assert_cmphex(reg, ==, 0);

    /* (14) CI */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_CI);
    g_assert_cmphex(reg, ==, 0);

    /* (15) SNTF */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_SNTF);
    g_assert_cmphex(reg, ==, 0);

    /* (16) FBS */
    reg = ahci_px_rreg(ahci, port, AHCI_PX_FBS);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_EN);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_DEC);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_SDE);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_DEV);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_DWE);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_RESERVED);
    if (BITSET(ahci->cap, AHCI_CAP_FBSS)) {
        /* if Port-Multiplier FIS-based switching avail, ADO must >= 2 */
        g_assert((reg & AHCI_PX_FBS_ADO) >> ctzl(AHCI_PX_FBS_ADO) >= 2);
    }

    /* [17 -- 27] RESERVED */
    for (i = AHCI_PX_RES2; i < AHCI_PX_VS; ++i) {
        reg = ahci_px_rreg(ahci, port, i);
        g_assert_cmphex(reg, ==, 0);
    }

    /* [28 -- 31] Vendor-Specific */
    for (i = AHCI_PX_VS; i < 32; ++i) {
        reg = ahci_px_rreg(ahci, port, i);
        if (reg) {
            g_test_message("INFO: Vendor register %u non-empty", i);
        }
    }
}

/**
 * Utilizing an initialized AHCI HBA, issue an IDENTIFY command to the first
 * device we see, then read and check the response.
 */
static void ahci_test_identify(AHCIQState *ahci)
{
    uint16_t buff[256];
    unsigned px;
    int rc;
    uint16_t sect_size;
    const size_t buffsize = 512;

    g_assert(ahci != NULL);

    /**
     * This serves as a bit of a tutorial on AHCI device programming:
     *
     * (1) Create a data buffer for the IDENTIFY response to be sent to
     * (2) Create a Command Table buffer, where we will store the
     *     command and PRDT (Physical Region Descriptor Table)
     * (3) Construct an FIS host-to-device command structure, and write it to
     *     the top of the Command Table buffer.
     * (4) Create one or more Physical Region Descriptors (PRDs) that describe
     *     a location in memory where data may be stored/retrieved.
     * (5) Write these PRDTs to the bottom (offset 0x80) of the Command Table.
     * (6) Each AHCI port has up to 32 command slots. Each slot contains a
     *     header that points to a Command Table buffer. Pick an unused slot
     *     and update it to point to the Command Table we have built.
     * (7) Now: Command #n points to our Command Table, and our Command Table
     *     contains the FIS (that describes our command) and the PRDTL, which
     *     describes our buffer.
     * (8) We inform the HBA via PxCI (Command Issue) that the command in slot
     *     #n is ready for processing.
     */

    /* Pick the first implemented and running port */
    px = ahci_port_select(ahci);
    g_test_message("Selected port %u for test", px);

    /* Clear out the FIS Receive area and any pending interrupts. */
    ahci_port_clear(ahci, px);

    /* "Read" 512 bytes using CMD_IDENTIFY into the host buffer. */
    ahci_io(ahci, px, CMD_IDENTIFY, &buff, buffsize, 0);

    /* Check serial number/version in the buffer */
    /* NB: IDENTIFY strings are packed in 16bit little endian chunks.
     * Since we copy byte-for-byte in ahci-test, on both LE and BE, we need to
     * unchunk this data. By contrast, ide-test copies 2 bytes at a time, and
     * as a consequence, only needs to unchunk the data on LE machines. */
    string_bswap16(&buff[10], 20);
    rc = memcmp(&buff[10], "testdisk            ", 20);
    g_assert_cmphex(rc, ==, 0);

    string_bswap16(&buff[23], 8);
    rc = memcmp(&buff[23], "version ", 8);
    g_assert_cmphex(rc, ==, 0);

    sect_size = le16_to_cpu(*((uint16_t *)(&buff[5])));
    g_assert_cmphex(sect_size, ==, AHCI_SECTOR_SIZE);
}

static void ahci_test_io_rw_simple(AHCIQState *ahci, unsigned bufsize,
                                   uint64_t sector, uint8_t read_cmd,
                                   uint8_t write_cmd)
{
    uint64_t ptr;
    uint8_t port;
    unsigned char *tx = g_malloc(bufsize);
    unsigned char *rx = g_malloc0(bufsize);

    g_assert(ahci != NULL);

    /* Pick the first running port and clear it. */
    port = ahci_port_select(ahci);
    ahci_port_clear(ahci, port);

    /*** Create pattern and transfer to guest ***/
    /* Data buffer in the guest */
    ptr = ahci_alloc(ahci, bufsize);
    g_assert(ptr);

    /* Write some indicative pattern to our buffer. */
    generate_pattern(tx, bufsize, AHCI_SECTOR_SIZE);
    qtest_bufwrite(ahci->parent->qts, ptr, tx, bufsize);

    /* Write this buffer to disk, then read it back to the DMA buffer. */
    ahci_guest_io(ahci, port, write_cmd, ptr, bufsize, sector);
    qtest_memset(ahci->parent->qts, ptr, 0x00, bufsize);
    ahci_guest_io(ahci, port, read_cmd, ptr, bufsize, sector);

    /*** Read back the Data ***/
    qtest_bufread(ahci->parent->qts, ptr, rx, bufsize);
    g_assert_cmphex(memcmp(tx, rx, bufsize), ==, 0);

    ahci_free(ahci, ptr);
    g_free(tx);
    g_free(rx);
}

static uint8_t ahci_test_nondata(AHCIQState *ahci, uint8_t ide_cmd)
{
    uint8_t port;

    /* Sanitize */
    port = ahci_port_select(ahci);
    ahci_port_clear(ahci, port);

    ahci_io(ahci, port, ide_cmd, NULL, 0, 0);

    return port;
}

static void ahci_test_flush(AHCIQState *ahci)
{
    ahci_test_nondata(ahci, CMD_FLUSH_CACHE);
}

static void ahci_test_max(AHCIQState *ahci)
{
    RegD2HFIS *d2h = g_malloc0(0x20);
    uint64_t nsect;
    uint8_t port;
    uint8_t cmd;
    uint64_t config_sect = mb_to_sectors(test_image_size_mb) - 1;

    if (config_sect > 0xFFFFFF) {
        cmd = CMD_READ_MAX_EXT;
    } else {
        cmd = CMD_READ_MAX;
    }

    port = ahci_test_nondata(ahci, cmd);
    qtest_memread(ahci->parent->qts, ahci->port[port].fb + 0x40, d2h, 0x20);
    nsect = (uint64_t)d2h->lba_hi[2] << 40 |
        (uint64_t)d2h->lba_hi[1] << 32 |
        (uint64_t)d2h->lba_hi[0] << 24 |
        (uint64_t)d2h->lba_lo[2] << 16 |
        (uint64_t)d2h->lba_lo[1] << 8 |
        (uint64_t)d2h->lba_lo[0];

    g_assert_cmphex(nsect, ==, config_sect);
    g_free(d2h);
}


/******************************************************************************/
/* Test Interfaces                                                            */
/******************************************************************************/

/**
 * Basic sanity test to boot a machine, find an AHCI device, and shutdown.
 */
static void test_sanity(void)
{
    AHCIQState *ahci;
    ahci = ahci_boot(NULL);
    ahci_shutdown(ahci);
}

/**
 * Ensure that the PCI configuration space for the AHCI device is in-line with
 * the AHCI 1.3 specification for initial values.
 */
static void test_pci_spec(void)
{
    AHCIQState *ahci;
    ahci = ahci_boot(NULL);
    ahci_test_pci_spec(ahci);
    ahci_shutdown(ahci);
}

/**
 * Engage the PCI AHCI device and sanity check the response.
 * Perform additional PCI config space bringup for the HBA.
 */
static void test_pci_enable(void)
{
    AHCIQState *ahci;
    ahci = ahci_boot(NULL);
    ahci_pci_enable(ahci);
    ahci_shutdown(ahci);
}

/**
 * Investigate the memory mapped regions of the HBA,
 * and test them for AHCI specification adherence.
 */
static void test_hba_spec(void)
{
    AHCIQState *ahci;

    ahci = ahci_boot(NULL);
    ahci_pci_enable(ahci);
    ahci_test_hba_spec(ahci);
    ahci_shutdown(ahci);
}

/**
 * Engage the HBA functionality of the AHCI PCI device,
 * and bring it into a functional idle state.
 */
static void test_hba_enable(void)
{
    AHCIQState *ahci;

    ahci = ahci_boot(NULL);
    ahci_pci_enable(ahci);
    ahci_hba_enable(ahci);
    ahci_shutdown(ahci);
}

/**
 * Bring up the device and issue an IDENTIFY command.
 * Inspect the state of the HBA device and the data returned.
 */
static void test_identify(void)
{
    AHCIQState *ahci;

    ahci = ahci_boot_and_enable(NULL);
    ahci_test_identify(ahci);
    ahci_shutdown(ahci);
}

/**
 * Fragmented DMA test: Perform a standard 4K DMA read/write
 * test, but make sure the physical regions are fragmented to
 * be very small, each just 32 bytes, to see how AHCI performs
 * with chunks defined to be much less than a sector.
 */
static void test_dma_fragmented(void)
{
    AHCIQState *ahci;
    AHCICommand *cmd;
    uint8_t px;
    size_t bufsize = 4096;
    unsigned char *tx = g_malloc(bufsize);
    unsigned char *rx = g_malloc0(bufsize);
    uint64_t ptr;

    ahci = ahci_boot_and_enable(NULL);
    px = ahci_port_select(ahci);
    ahci_port_clear(ahci, px);

    /* create pattern */
    generate_pattern(tx, bufsize, AHCI_SECTOR_SIZE);

    /* Create a DMA buffer in guest memory, and write our pattern to it. */
    ptr = guest_alloc(&ahci->parent->alloc, bufsize);
    g_assert(ptr);
    qtest_bufwrite(ahci->parent->qts, ptr, tx, bufsize);

    cmd = ahci_command_create(CMD_WRITE_DMA);
    ahci_command_adjust(cmd, 0, ptr, bufsize, 32);
    ahci_command_commit(ahci, cmd, px);
    ahci_command_issue(ahci, cmd);
    ahci_command_verify(ahci, cmd);
    ahci_command_free(cmd);

    cmd = ahci_command_create(CMD_READ_DMA);
    ahci_command_adjust(cmd, 0, ptr, bufsize, 32);
    ahci_command_commit(ahci, cmd, px);
    ahci_command_issue(ahci, cmd);
    ahci_command_verify(ahci, cmd);
    ahci_command_free(cmd);

    /* Read back the guest's receive buffer into local memory */
    qtest_bufread(ahci->parent->qts, ptr, rx, bufsize);
    guest_free(&ahci->parent->alloc, ptr);

    g_assert_cmphex(memcmp(tx, rx, bufsize), ==, 0);

    ahci_shutdown(ahci);

    g_free(rx);
    g_free(tx);
}

/*
 * Write sector 1 with random data to make AHCI storage dirty
 * Needed for flush tests so that flushes actually go though the block layer
 */
static void make_dirty(AHCIQState* ahci, uint8_t port)
{
    uint64_t ptr;
    unsigned bufsize = 512;

    ptr = ahci_alloc(ahci, bufsize);
    g_assert(ptr);

    ahci_guest_io(ahci, port, CMD_WRITE_DMA, ptr, bufsize, 1);
    ahci_free(ahci, ptr);
}

static void test_flush(void)
{
    AHCIQState *ahci;
    uint8_t port;

    ahci = ahci_boot_and_enable(NULL);

    port = ahci_port_select(ahci);
    ahci_port_clear(ahci, port);

    make_dirty(ahci, port);

    ahci_test_flush(ahci);
    ahci_shutdown(ahci);
}

static void test_flush_retry(void)
{
    AHCIQState *ahci;
    AHCICommand *cmd;
    uint8_t port;

    prepare_blkdebug_script(debug_path, "flush_to_disk");
    ahci = ahci_boot_and_enable("-drive file=blkdebug:%s:%s,if=none,id=drive0,"
                                "format=%s,cache=writeback,"
                                "rerror=stop,werror=stop "
                                "-M q35 "
                                "-device ide-hd,drive=drive0 ",
                                debug_path,
                                tmp_path, imgfmt);

    port = ahci_port_select(ahci);
    ahci_port_clear(ahci, port);

    /* Issue write so that flush actually goes to disk */
    make_dirty(ahci, port);

    /* Issue Flush Command and wait for error */
    cmd = ahci_guest_io_halt(ahci, port, CMD_FLUSH_CACHE, 0, 0, 0);
    ahci_guest_io_resume(ahci, cmd);

    ahci_shutdown(ahci);
}

/**
 * Basic sanity test to boot a machine, find an AHCI device, and shutdown.
 */
static void test_migrate_sanity(void)
{
    AHCIQState *src, *dst;
    char *uri = g_strdup_printf("unix:%s", mig_socket);

    src = ahci_boot("-m 384 -M q35 "
                    "-drive if=ide,file=%s,format=%s ", tmp_path, imgfmt);
    dst = ahci_boot("-m 384 -M q35 "
                    "-drive if=ide,file=%s,format=%s "
                    "-incoming %s", tmp_path, imgfmt, uri);

    ahci_migrate(src, dst, uri);

    ahci_shutdown(src);
    ahci_shutdown(dst);
    g_free(uri);
}

/**
 * Simple migration test: Write a pattern, migrate, then read.
 */
static void ahci_migrate_simple(uint8_t cmd_read, uint8_t cmd_write)
{
    AHCIQState *src, *dst;
    uint8_t px;
    size_t bufsize = 4096;
    unsigned char *tx = g_malloc(bufsize);
    unsigned char *rx = g_malloc0(bufsize);
    char *uri = g_strdup_printf("unix:%s", mig_socket);

    src = ahci_boot_and_enable("-m 384 -M q35 "
                               "-drive if=ide,format=%s,file=%s ",
                               imgfmt, tmp_path);
    dst = ahci_boot("-m 384 -M q35 "
                    "-drive if=ide,format=%s,file=%s "
                    "-incoming %s", imgfmt, tmp_path, uri);

    /* initialize */
    px = ahci_port_select(src);
    ahci_port_clear(src, px);

    /* create pattern */
    generate_pattern(tx, bufsize, AHCI_SECTOR_SIZE);

    /* Write, migrate, then read. */
    ahci_io(src, px, cmd_write, tx, bufsize, 0);
    ahci_migrate(src, dst, uri);
    ahci_io(dst, px, cmd_read, rx, bufsize, 0);

    /* Verify pattern */
    g_assert_cmphex(memcmp(tx, rx, bufsize), ==, 0);

    ahci_shutdown(src);
    ahci_shutdown(dst);
    g_free(rx);
    g_free(tx);
    g_free(uri);
}

static void test_migrate_dma(void)
{
    ahci_migrate_simple(CMD_READ_DMA, CMD_WRITE_DMA);
}

static void test_migrate_ncq(void)
{
    ahci_migrate_simple(READ_FPDMA_QUEUED, WRITE_FPDMA_QUEUED);
}

/**
 * Halted IO Error Test
 *
 * Simulate an error on first write, Try to write a pattern,
 * Confirm the VM has stopped, resume the VM, verify command
 * has completed, then read back the data and verify.
 */
static void ahci_halted_io_test(uint8_t cmd_read, uint8_t cmd_write)
{
    AHCIQState *ahci;
    uint8_t port;
    size_t bufsize = 4096;
    unsigned char *tx = g_malloc(bufsize);
    unsigned char *rx = g_malloc0(bufsize);
    uint64_t ptr;
    AHCICommand *cmd;

    prepare_blkdebug_script(debug_path, "write_aio");

    ahci = ahci_boot_and_enable("-drive file=blkdebug:%s:%s,if=none,id=drive0,"
                                "format=%s,cache=writeback,"
                                "rerror=stop,werror=stop "
                                "-M q35 "
                                "-device ide-hd,drive=drive0 ",
                                debug_path,
                                tmp_path, imgfmt);

    /* Initialize and prepare */
    port = ahci_port_select(ahci);
    ahci_port_clear(ahci, port);

    /* create DMA source buffer and write pattern */
    generate_pattern(tx, bufsize, AHCI_SECTOR_SIZE);
    ptr = ahci_alloc(ahci, bufsize);
    g_assert(ptr);
    qtest_memwrite(ahci->parent->qts, ptr, tx, bufsize);

    /* Attempt to write (and fail) */
    cmd = ahci_guest_io_halt(ahci, port, cmd_write,
                             ptr, bufsize, 0);

    /* Attempt to resume the command */
    ahci_guest_io_resume(ahci, cmd);
    ahci_free(ahci, ptr);

    /* Read back and verify */
    ahci_io(ahci, port, cmd_read, rx, bufsize, 0);
    g_assert_cmphex(memcmp(tx, rx, bufsize), ==, 0);

    /* Cleanup and go home */
    ahci_shutdown(ahci);
    g_free(rx);
    g_free(tx);
}

static void test_halted_dma(void)
{
    ahci_halted_io_test(CMD_READ_DMA, CMD_WRITE_DMA);
}

static void test_halted_ncq(void)
{
    ahci_halted_io_test(READ_FPDMA_QUEUED, WRITE_FPDMA_QUEUED);
}

/**
 * IO Error Migration Test
 *
 * Simulate an error on first write, Try to write a pattern,
 * Confirm the VM has stopped, migrate, resume the VM,
 * verify command has completed, then read back the data and verify.
 */
static void ahci_migrate_halted_io(uint8_t cmd_read, uint8_t cmd_write)
{
    AHCIQState *src, *dst;
    uint8_t port;
    size_t bufsize = 4096;
    unsigned char *tx = g_malloc(bufsize);
    unsigned char *rx = g_malloc0(bufsize);
    uint64_t ptr;
    AHCICommand *cmd;
    char *uri = g_strdup_printf("unix:%s", mig_socket);

    prepare_blkdebug_script(debug_path, "write_aio");

    src = ahci_boot_and_enable("-drive file=blkdebug:%s:%s,if=none,id=drive0,"
                               "format=%s,cache=writeback,"
                               "rerror=stop,werror=stop "
                               "-M q35 "
                               "-device ide-hd,drive=drive0 ",
                               debug_path,
                               tmp_path, imgfmt);

    dst = ahci_boot("-drive file=%s,if=none,id=drive0,"
                    "format=%s,cache=writeback,"
                    "rerror=stop,werror=stop "
                    "-M q35 "
                    "-device ide-hd,drive=drive0 "
                    "-incoming %s",
                    tmp_path, imgfmt, uri);

    /* Initialize and prepare */
    port = ahci_port_select(src);
    ahci_port_clear(src, port);
    generate_pattern(tx, bufsize, AHCI_SECTOR_SIZE);

    /* create DMA source buffer and write pattern */
    ptr = ahci_alloc(src, bufsize);
    g_assert(ptr);
    qtest_memwrite(src->parent->qts, ptr, tx, bufsize);

    /* Write, trigger the VM to stop, migrate, then resume. */
    cmd = ahci_guest_io_halt(src, port, cmd_write,
                             ptr, bufsize, 0);
    ahci_migrate(src, dst, uri);
    ahci_guest_io_resume(dst, cmd);
    ahci_free(dst, ptr);

    /* Read back */
    ahci_io(dst, port, cmd_read, rx, bufsize, 0);

    /* Verify TX and RX are identical */
    g_assert_cmphex(memcmp(tx, rx, bufsize), ==, 0);

    /* Cleanup and go home. */
    ahci_shutdown(src);
    ahci_shutdown(dst);
    g_free(rx);
    g_free(tx);
    g_free(uri);
}

static void test_migrate_halted_dma(void)
{
    ahci_migrate_halted_io(CMD_READ_DMA, CMD_WRITE_DMA);
}

static void test_migrate_halted_ncq(void)
{
    ahci_migrate_halted_io(READ_FPDMA_QUEUED, WRITE_FPDMA_QUEUED);
}

/**
 * Migration test: Try to flush, migrate, then resume.
 */
static void test_flush_migrate(void)
{
    AHCIQState *src, *dst;
    AHCICommand *cmd;
    uint8_t px;
    char *uri = g_strdup_printf("unix:%s", mig_socket);

    prepare_blkdebug_script(debug_path, "flush_to_disk");

    src = ahci_boot_and_enable("-drive file=blkdebug:%s:%s,if=none,id=drive0,"
                               "cache=writeback,rerror=stop,werror=stop,"
                               "format=%s "
                               "-M q35 "
                               "-device ide-hd,drive=drive0 ",
                               debug_path, tmp_path, imgfmt);
    dst = ahci_boot("-drive file=%s,if=none,id=drive0,"
                    "cache=writeback,rerror=stop,werror=stop,"
                    "format=%s "
                    "-M q35 "
                    "-device ide-hd,drive=drive0 "
                    "-incoming %s", tmp_path, imgfmt, uri);

    px = ahci_port_select(src);
    ahci_port_clear(src, px);

    /* Dirty device so that flush reaches disk */
    make_dirty(src, px);

    /* Issue Flush Command */
    cmd = ahci_command_create(CMD_FLUSH_CACHE);
    ahci_command_commit(src, cmd, px);
    ahci_command_issue_async(src, cmd);
    qtest_qmp_eventwait(src->parent->qts, "STOP");

    /* Migrate over */
    ahci_migrate(src, dst, uri);

    /* Complete the command */
    qtest_qmp_send(dst->parent->qts, "{'execute':'cont' }");
    qtest_qmp_eventwait(dst->parent->qts, "RESUME");
    ahci_command_wait(dst, cmd);
    ahci_command_verify(dst, cmd);

    ahci_command_free(cmd);
    ahci_shutdown(src);
    ahci_shutdown(dst);
    g_free(uri);
}

static void test_max(void)
{
    AHCIQState *ahci;

    ahci = ahci_boot_and_enable(NULL);
    ahci_test_max(ahci);
    ahci_shutdown(ahci);
}

static void test_reset(void)
{
    AHCIQState *ahci;
    int i;

    ahci = ahci_boot(NULL);
    ahci_test_pci_spec(ahci);
    ahci_pci_enable(ahci);

    for (i = 0; i < 2; i++) {
        ahci_test_hba_spec(ahci);
        ahci_hba_enable(ahci);
        ahci_test_identify(ahci);
        ahci_test_io_rw_simple(ahci, 4096, 0,
                               CMD_READ_DMA_EXT,
                               CMD_WRITE_DMA_EXT);
        ahci_set(ahci, AHCI_GHC, AHCI_GHC_HR);
        ahci_clean_mem(ahci);
    }

    ahci_shutdown(ahci);
}

static void test_ncq_simple(void)
{
    AHCIQState *ahci;

    ahci = ahci_boot_and_enable(NULL);
    ahci_test_io_rw_simple(ahci, 4096, 0,
                           READ_FPDMA_QUEUED,
                           WRITE_FPDMA_QUEUED);
    ahci_shutdown(ahci);
}

static int prepare_iso(size_t size, unsigned char **buf, char **name)
{
    char cdrom_path[] = "/tmp/qtest.iso.XXXXXX";
    unsigned char *patt;
    ssize_t ret;
    int fd = mkstemp(cdrom_path);

    g_assert(buf);
    g_assert(name);
    patt = g_malloc(size);

    /* Generate a pattern and build a CDROM image to read from */
    generate_pattern(patt, size, ATAPI_SECTOR_SIZE);
    ret = write(fd, patt, size);
    g_assert(ret == size);

    *name = g_strdup(cdrom_path);
    *buf = patt;
    return fd;
}

static void remove_iso(int fd, char *name)
{
    unlink(name);
    g_free(name);
    close(fd);
}

static int ahci_cb_cmp_buff(AHCIQState *ahci, AHCICommand *cmd,
                            const AHCIOpts *opts)
{
    unsigned char *tx = opts->opaque;
    unsigned char *rx;

    if (!opts->size) {
        return 0;
    }

    rx = g_malloc0(opts->size);
    qtest_bufread(ahci->parent->qts, opts->buffer, rx, opts->size);
    g_assert_cmphex(memcmp(tx, rx, opts->size), ==, 0);
    g_free(rx);

    return 0;
}

static void ahci_test_cdrom(int nsectors, bool dma, uint8_t cmd,
                            bool override_bcl, uint16_t bcl)
{
    AHCIQState *ahci;
    unsigned char *tx;
    char *iso;
    int fd;
    AHCIOpts opts = {
        .size = (ATAPI_SECTOR_SIZE * nsectors),
        .atapi = true,
        .atapi_dma = dma,
        .post_cb = ahci_cb_cmp_buff,
        .set_bcl = override_bcl,
        .bcl = bcl,
    };
    uint64_t iso_size = ATAPI_SECTOR_SIZE * (nsectors + 1);

    /* Prepare ISO and fill 'tx' buffer */
    fd = prepare_iso(iso_size, &tx, &iso);
    opts.opaque = tx;

    /* Standard startup wonkery, but use ide-cd and our special iso file */
    ahci = ahci_boot_and_enable("-drive if=none,id=drive0,file=%s,format=raw "
                                "-M q35 "
                                "-device ide-cd,drive=drive0 ", iso);

    /* Build & Send AHCI command */
    ahci_exec(ahci, ahci_port_select(ahci), cmd, &opts);

    /* Cleanup */
    g_free(tx);
    ahci_shutdown(ahci);
    remove_iso(fd, iso);
}

static void ahci_test_cdrom_read10(int nsectors, bool dma)
{
    ahci_test_cdrom(nsectors, dma, CMD_ATAPI_READ_10, false, 0);
}

static void test_cdrom_dma(void)
{
    ahci_test_cdrom_read10(1, true);
}

static void test_cdrom_dma_multi(void)
{
    ahci_test_cdrom_read10(3, true);
}

static void test_cdrom_pio(void)
{
    ahci_test_cdrom_read10(1, false);
}

static void test_cdrom_pio_multi(void)
{
    ahci_test_cdrom_read10(3, false);
}

/* Regression test: Test that a READ_CD command with a BCL of 0 but a size of 0
 * completes as a NOP instead of erroring out. */
static void test_atapi_bcl(void)
{
    ahci_test_cdrom(0, false, CMD_ATAPI_READ_CD, true, 0);
}


static void atapi_wait_tray(AHCIQState *ahci, bool open)
{
    QDict *rsp = qtest_qmp_eventwait_ref(ahci->parent->qts,
                                         "DEVICE_TRAY_MOVED");
    QDict *data = qdict_get_qdict(rsp, "data");
    if (open) {
        g_assert(qdict_get_bool(data, "tray-open"));
    } else {
        g_assert(!qdict_get_bool(data, "tray-open"));
    }
    qobject_unref(rsp);
}

static void test_atapi_tray(void)
{
    AHCIQState *ahci;
    unsigned char *tx;
    char *iso;
    int fd;
    uint8_t port, sense, asc;
    uint64_t iso_size = ATAPI_SECTOR_SIZE;
    QDict *rsp;

    fd = prepare_iso(iso_size, &tx, &iso);
    ahci = ahci_boot_and_enable("-blockdev node-name=drive0,driver=file,filename=%s "
                                "-M q35 "
                                "-device ide-cd,id=cd0,drive=drive0 ", iso);
    port = ahci_port_select(ahci);

    ahci_atapi_eject(ahci, port);
    atapi_wait_tray(ahci, true);

    ahci_atapi_load(ahci, port);
    atapi_wait_tray(ahci, false);

    /* Remove media */
    qtest_qmp_send(ahci->parent->qts, "{'execute': 'blockdev-open-tray', "
                    "'arguments': {'id': 'cd0'}}");
    atapi_wait_tray(ahci, true);
    rsp = qtest_qmp_receive(ahci->parent->qts);
    qobject_unref(rsp);

    qmp_discard_response(ahci->parent->qts,
                         "{'execute': 'blockdev-remove-medium', "
                         "'arguments': {'id': 'cd0'}}");

    /* Test the tray without a medium */
    ahci_atapi_load(ahci, port);
    atapi_wait_tray(ahci, false);

    ahci_atapi_eject(ahci, port);
    atapi_wait_tray(ahci, true);

    /* Re-insert media */
    qmp_discard_response(ahci->parent->qts,
                         "{'execute': 'blockdev-add', "
                         "'arguments': {'node-name': 'node0', "
                                        "'driver': 'raw', "
                                        "'file': { 'driver': 'file', "
                                                  "'filename': %s }}}", iso);
    qmp_discard_response(ahci->parent->qts,
                         "{'execute': 'blockdev-insert-medium',"
                         "'arguments': { 'id': 'cd0', "
                                         "'node-name': 'node0' }}");

    /* Again, the event shows up first */
    qtest_qmp_send(ahci->parent->qts, "{'execute': 'blockdev-close-tray', "
                   "'arguments': {'id': 'cd0'}}");
    atapi_wait_tray(ahci, false);
    rsp = qtest_qmp_receive(ahci->parent->qts);
    qobject_unref(rsp);

    /* Now, to convince ATAPI we understand the media has changed... */
    ahci_atapi_test_ready(ahci, port, false, SENSE_NOT_READY);
    ahci_atapi_get_sense(ahci, port, &sense, &asc);
    g_assert_cmpuint(sense, ==, SENSE_NOT_READY);
    g_assert_cmpuint(asc, ==, ASC_MEDIUM_NOT_PRESENT);

    ahci_atapi_test_ready(ahci, port, false, SENSE_UNIT_ATTENTION);
    ahci_atapi_get_sense(ahci, port, &sense, &asc);
    g_assert_cmpuint(sense, ==, SENSE_UNIT_ATTENTION);
    g_assert_cmpuint(asc, ==, ASC_MEDIUM_MAY_HAVE_CHANGED);

    ahci_atapi_test_ready(ahci, port, true, SENSE_NO_SENSE);
    ahci_atapi_get_sense(ahci, port, &sense, &asc);
    g_assert_cmpuint(sense, ==, SENSE_NO_SENSE);

    /* Final tray test. */
    ahci_atapi_eject(ahci, port);
    atapi_wait_tray(ahci, true);

    ahci_atapi_load(ahci, port);
    atapi_wait_tray(ahci, false);

    /* Cleanup */
    g_free(tx);
    ahci_shutdown(ahci);
    remove_iso(fd, iso);
}

/******************************************************************************/
/* AHCI I/O Test Matrix Definitions                                           */

enum BuffLen {
    LEN_BEGIN = 0,
    LEN_SIMPLE = LEN_BEGIN,
    LEN_DOUBLE,
    LEN_LONG,
    LEN_SHORT,
    NUM_LENGTHS
};

static const char *buff_len_str[NUM_LENGTHS] = { "simple", "double",
                                                 "long", "short" };

enum AddrMode {
    ADDR_MODE_BEGIN = 0,
    ADDR_MODE_LBA28 = ADDR_MODE_BEGIN,
    ADDR_MODE_LBA48,
    NUM_ADDR_MODES
};

static const char *addr_mode_str[NUM_ADDR_MODES] = { "lba28", "lba48" };

enum IOMode {
    MODE_BEGIN = 0,
    MODE_PIO = MODE_BEGIN,
    MODE_DMA,
    NUM_MODES
};

static const char *io_mode_str[NUM_MODES] = { "pio", "dma" };

enum IOOps {
    IO_BEGIN = 0,
    IO_READ = IO_BEGIN,
    IO_WRITE,
    NUM_IO_OPS
};

enum OffsetType {
    OFFSET_BEGIN = 0,
    OFFSET_ZERO = OFFSET_BEGIN,
    OFFSET_LOW,
    OFFSET_HIGH,
    NUM_OFFSETS
};

static const char *offset_str[NUM_OFFSETS] = { "zero", "low", "high" };

typedef struct AHCIIOTestOptions {
    enum BuffLen length;
    enum AddrMode address_type;
    enum IOMode io_type;
    enum OffsetType offset;
} AHCIIOTestOptions;

static uint64_t offset_sector(enum OffsetType ofst,
                              enum AddrMode addr_type,
                              uint64_t buffsize)
{
    uint64_t ceil;
    uint64_t nsectors;

    switch (ofst) {
    case OFFSET_ZERO:
        return 0;
    case OFFSET_LOW:
        return 1;
    case OFFSET_HIGH:
        ceil = (addr_type == ADDR_MODE_LBA28) ? 0xfffffff : 0xffffffffffff;
        ceil = MIN(ceil, mb_to_sectors(test_image_size_mb) - 1);
        nsectors = buffsize / AHCI_SECTOR_SIZE;
        return ceil - nsectors + 1;
    default:
        g_assert_not_reached();
    }
}

/**
 * Table of possible I/O ATA commands given a set of enumerations.
 */
static const uint8_t io_cmds[NUM_MODES][NUM_ADDR_MODES][NUM_IO_OPS] = {
    [MODE_PIO] = {
        [ADDR_MODE_LBA28] = {
            [IO_READ] = CMD_READ_PIO,
            [IO_WRITE] = CMD_WRITE_PIO },
        [ADDR_MODE_LBA48] = {
            [IO_READ] = CMD_READ_PIO_EXT,
            [IO_WRITE] = CMD_WRITE_PIO_EXT }
    },
    [MODE_DMA] = {
        [ADDR_MODE_LBA28] = {
            [IO_READ] = CMD_READ_DMA,
            [IO_WRITE] = CMD_WRITE_DMA },
        [ADDR_MODE_LBA48] = {
            [IO_READ] = CMD_READ_DMA_EXT,
            [IO_WRITE] = CMD_WRITE_DMA_EXT }
    }
};

/**
 * Test a Read/Write pattern using various commands, addressing modes,
 * transfer modes, and buffer sizes.
 */
static void test_io_rw_interface(enum AddrMode lba48, enum IOMode dma,
                                 unsigned bufsize, uint64_t sector)
{
    AHCIQState *ahci;

    ahci = ahci_boot_and_enable(NULL);
    ahci_test_io_rw_simple(ahci, bufsize, sector,
                           io_cmds[dma][lba48][IO_READ],
                           io_cmds[dma][lba48][IO_WRITE]);
    ahci_shutdown(ahci);
}

/**
 * Demultiplex the test data and invoke the actual test routine.
 */
static void test_io_interface(gconstpointer opaque)
{
    AHCIIOTestOptions *opts = (AHCIIOTestOptions *)opaque;
    unsigned bufsize;
    uint64_t sector;

    switch (opts->length) {
    case LEN_SIMPLE:
        bufsize = 4096;
        break;
    case LEN_DOUBLE:
        bufsize = 8192;
        break;
    case LEN_LONG:
        bufsize = 4096 * 64;
        break;
    case LEN_SHORT:
        bufsize = 512;
        break;
    default:
        g_assert_not_reached();
    }

    sector = offset_sector(opts->offset, opts->address_type, bufsize);
    test_io_rw_interface(opts->address_type, opts->io_type, bufsize, sector);
    g_free(opts);
    return;
}

static void create_ahci_io_test(enum IOMode type, enum AddrMode addr,
                                enum BuffLen len, enum OffsetType offset)
{
    char *name;
    AHCIIOTestOptions *opts;

    opts = g_new(AHCIIOTestOptions, 1);
    opts->length = len;
    opts->address_type = addr;
    opts->io_type = type;
    opts->offset = offset;

    name = g_strdup_printf("ahci/io/%s/%s/%s/%s",
                           io_mode_str[type],
                           addr_mode_str[addr],
                           buff_len_str[len],
                           offset_str[offset]);

    if ((addr == ADDR_MODE_LBA48) && (offset == OFFSET_HIGH) &&
        (mb_to_sectors(test_image_size_mb) <= 0xFFFFFFF)) {
        g_test_message("%s: skipped; test image too small", name);
        g_free(opts);
        g_free(name);
        return;
    }

    qtest_add_data_func(name, opts, test_io_interface);
    g_free(name);
}

/******************************************************************************/

int main(int argc, char **argv)
{
    const char *arch;
    int ret;
    int fd;
    int c;
    int i, j, k, m;

    static struct option long_options[] = {
        {"pedantic", no_argument, 0, 'p' },
        {0, 0, 0, 0},
    };

    /* Should be first to utilize g_test functionality, So we can see errors. */
    g_test_init(&argc, &argv, NULL);

    while (1) {
        c = getopt_long(argc, argv, "", long_options, NULL);
        if (c == -1) {
            break;
        }
        switch (c) {
        case -1:
            break;
        case 'p':
            ahci_pedantic = 1;
            break;
        default:
            fprintf(stderr, "Unrecognized ahci_test option.\n");
            g_assert_not_reached();
        }
    }

    /* Check architecture */
    arch = qtest_get_arch();
    if (strcmp(arch, "i386") && strcmp(arch, "x86_64")) {
        g_test_message("Skipping test for non-x86");
        return 0;
    }

    /* Create a temporary image */
    fd = mkstemp(tmp_path);
    g_assert(fd >= 0);
    if (have_qemu_img()) {
        imgfmt = "qcow2";
        test_image_size_mb = TEST_IMAGE_SIZE_MB_LARGE;
        mkqcow2(tmp_path, TEST_IMAGE_SIZE_MB_LARGE);
    } else {
        g_test_message("QTEST_QEMU_IMG not set or qemu-img missing; "
                       "skipping LBA48 high-sector tests");
        imgfmt = "raw";
        test_image_size_mb = TEST_IMAGE_SIZE_MB_SMALL;
        ret = ftruncate(fd, test_image_size_mb * 1024 * 1024);
        g_assert(ret == 0);
    }
    close(fd);

    /* Create temporary blkdebug instructions */
    fd = mkstemp(debug_path);
    g_assert(fd >= 0);
    close(fd);

    /* Reserve a hollow file to use as a socket for migration tests */
    fd = mkstemp(mig_socket);
    g_assert(fd >= 0);
    close(fd);

    /* Run the tests */
    qtest_add_func("/ahci/sanity",     test_sanity);
    qtest_add_func("/ahci/pci_spec",   test_pci_spec);
    qtest_add_func("/ahci/pci_enable", test_pci_enable);
    qtest_add_func("/ahci/hba_spec",   test_hba_spec);
    qtest_add_func("/ahci/hba_enable", test_hba_enable);
    qtest_add_func("/ahci/identify",   test_identify);

    for (i = MODE_BEGIN; i < NUM_MODES; i++) {
        for (j = ADDR_MODE_BEGIN; j < NUM_ADDR_MODES; j++) {
            for (k = LEN_BEGIN; k < NUM_LENGTHS; k++) {
                for (m = OFFSET_BEGIN; m < NUM_OFFSETS; m++) {
                    create_ahci_io_test(i, j, k, m);
                }
            }
        }
    }

    qtest_add_func("/ahci/io/dma/lba28/fragmented", test_dma_fragmented);

    qtest_add_func("/ahci/flush/simple", test_flush);
    qtest_add_func("/ahci/flush/retry", test_flush_retry);
    qtest_add_func("/ahci/flush/migrate", test_flush_migrate);

    qtest_add_func("/ahci/migrate/sanity", test_migrate_sanity);
    qtest_add_func("/ahci/migrate/dma/simple", test_migrate_dma);
    qtest_add_func("/ahci/io/dma/lba28/retry", test_halted_dma);
    qtest_add_func("/ahci/migrate/dma/halted", test_migrate_halted_dma);

    qtest_add_func("/ahci/max", test_max);
    qtest_add_func("/ahci/reset", test_reset);

    qtest_add_func("/ahci/io/ncq/simple", test_ncq_simple);
    qtest_add_func("/ahci/migrate/ncq/simple", test_migrate_ncq);
    qtest_add_func("/ahci/io/ncq/retry", test_halted_ncq);
    qtest_add_func("/ahci/migrate/ncq/halted", test_migrate_halted_ncq);

    qtest_add_func("/ahci/cdrom/dma/single", test_cdrom_dma);
    qtest_add_func("/ahci/cdrom/dma/multi", test_cdrom_dma_multi);
    qtest_add_func("/ahci/cdrom/pio/single", test_cdrom_pio);
    qtest_add_func("/ahci/cdrom/pio/multi", test_cdrom_pio_multi);

    qtest_add_func("/ahci/cdrom/pio/bcl", test_atapi_bcl);
    qtest_add_func("/ahci/cdrom/eject", test_atapi_tray);

    ret = g_test_run();

    /* Cleanup */
    unlink(tmp_path);
    unlink(debug_path);
    unlink(mig_socket);

    return ret;
}
