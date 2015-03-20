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
#include <getopt.h>
#include <glib.h>

#include "libqtest.h"
#include "libqos/libqos-pc.h"
#include "libqos/ahci.h"
#include "libqos/pci-pc.h"

#include "qemu-common.h"
#include "qemu/host-utils.h"

#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_regs.h"

/* Test-specific defines. */
#define TEST_IMAGE_SIZE    (64 * 1024 * 1024)

/*** Globals ***/
static char tmp_path[] = "/tmp/qtest.XXXXXX";
static bool ahci_pedantic;

/*** Function Declarations ***/
static void ahci_test_port_spec(AHCIQState *ahci, uint8_t port);
static void ahci_test_pci_spec(AHCIQState *ahci);
static void ahci_test_pci_caps(AHCIQState *ahci, uint16_t header,
                               uint8_t offset);
static void ahci_test_satacap(AHCIQState *ahci, uint8_t offset);
static void ahci_test_msicap(AHCIQState *ahci, uint8_t offset);
static void ahci_test_pmcap(AHCIQState *ahci, uint8_t offset);

/*** Utilities ***/

static void string_bswap16(uint16_t *s, size_t bytes)
{
    g_assert_cmphex((bytes & 1), ==, 0);
    bytes /= 2;

    while (bytes--) {
        *s = bswap16(*s);
        s++;
    }
}

static void generate_pattern(void *buffer, size_t len, size_t cycle_len)
{
    int i, j;
    unsigned char *tx = (unsigned char *)buffer;
    unsigned char p;
    size_t *sx;

    /* Write an indicative pattern that varies and is unique per-cycle */
    p = rand() % 256;
    for (i = j = 0; i < len; i++, j++) {
        tx[i] = p;
        if (j % cycle_len == 0) {
            p = rand() % 256;
        }
    }

    /* force uniqueness by writing an id per-cycle */
    for (i = 0; i < len / cycle_len; i++) {
        j = i * cycle_len;
        if (j + sizeof(*sx) <= len) {
            sx = (size_t *)&tx[j];
            *sx = i;
        }
    }
}

/*** Test Setup & Teardown ***/

/**
 * Start a Q35 machine and bookmark a handle to the AHCI device.
 */
static AHCIQState *ahci_boot(void)
{
    AHCIQState *s;
    const char *cli;

    s = g_malloc0(sizeof(AHCIQState));

    cli = "-drive if=none,id=drive0,file=%s,cache=writeback,serial=%s"
        ",format=raw"
        " -M q35 "
        "-device ide-hd,drive=drive0 "
        "-global ide-hd.ver=%s";
    s->parent = qtest_pc_boot(cli, tmp_path, "testdisk", "version");
    alloc_set_flags(s->parent->alloc, ALLOC_LEAK_ASSERT);

    /* Verify that we have an AHCI device present. */
    s->dev = get_ahci_device(&s->fingerprint);

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
static AHCIQState *ahci_boot_and_enable(void)
{
    AHCIQState *ahci;
    ahci = ahci_boot();

    ahci_pci_enable(ahci);
    ahci_hba_enable(ahci);

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
    ahci_io(ahci, px, CMD_IDENTIFY, &buff, buffsize);

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
    g_assert_cmphex(sect_size, ==, 0x200);
}

static void ahci_test_io_rw_simple(AHCIQState *ahci, unsigned bufsize,
                                   uint8_t read_cmd, uint8_t write_cmd)
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
    memwrite(ptr, tx, bufsize);

    /* Write this buffer to disk, then read it back to the DMA buffer. */
    ahci_guest_io(ahci, port, write_cmd, ptr, bufsize);
    qmemset(ptr, 0x00, bufsize);
    ahci_guest_io(ahci, port, read_cmd, ptr, bufsize);

    /*** Read back the Data ***/
    memread(ptr, rx, bufsize);
    g_assert_cmphex(memcmp(tx, rx, bufsize), ==, 0);

    ahci_free(ahci, ptr);
    g_free(tx);
    g_free(rx);
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
    ahci = ahci_boot();
    ahci_shutdown(ahci);
}

/**
 * Ensure that the PCI configuration space for the AHCI device is in-line with
 * the AHCI 1.3 specification for initial values.
 */
static void test_pci_spec(void)
{
    AHCIQState *ahci;
    ahci = ahci_boot();
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

    ahci = ahci_boot();
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

    ahci = ahci_boot();
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

    ahci = ahci_boot();
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

    ahci = ahci_boot_and_enable();
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

    ahci = ahci_boot_and_enable();
    px = ahci_port_select(ahci);
    ahci_port_clear(ahci, px);

    /* create pattern */
    generate_pattern(tx, bufsize, AHCI_SECTOR_SIZE);

    /* Create a DMA buffer in guest memory, and write our pattern to it. */
    ptr = guest_alloc(ahci->parent->alloc, bufsize);
    g_assert(ptr);
    memwrite(ptr, tx, bufsize);

    cmd = ahci_command_create(CMD_WRITE_DMA);
    ahci_command_adjust(cmd, 0, ptr, bufsize, 32);
    ahci_command_commit(ahci, cmd, px);
    ahci_command_issue(ahci, cmd);
    ahci_command_verify(ahci, cmd);
    g_free(cmd);

    cmd = ahci_command_create(CMD_READ_DMA);
    ahci_command_adjust(cmd, 0, ptr, bufsize, 32);
    ahci_command_commit(ahci, cmd, px);
    ahci_command_issue(ahci, cmd);
    ahci_command_verify(ahci, cmd);
    g_free(cmd);

    /* Read back the guest's receive buffer into local memory */
    memread(ptr, rx, bufsize);
    guest_free(ahci->parent->alloc, ptr);

    g_assert_cmphex(memcmp(tx, rx, bufsize), ==, 0);

    ahci_shutdown(ahci);

    g_free(rx);
    g_free(tx);
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

typedef struct AHCIIOTestOptions {
    enum BuffLen length;
    enum AddrMode address_type;
    enum IOMode io_type;
} AHCIIOTestOptions;

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
                                 unsigned bufsize)
{
    AHCIQState *ahci;

    ahci = ahci_boot_and_enable();
    ahci_test_io_rw_simple(ahci, bufsize,
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

    test_io_rw_interface(opts->address_type, opts->io_type, bufsize);
    g_free(opts);
    return;
}

static void create_ahci_io_test(enum IOMode type, enum AddrMode addr,
                                enum BuffLen len)
{
    static const char *arch;
    char *name;
    AHCIIOTestOptions *opts = g_malloc(sizeof(AHCIIOTestOptions));

    opts->length = len;
    opts->address_type = addr;
    opts->io_type = type;

    if (!arch) {
        arch = qtest_get_arch();
    }

    name = g_strdup_printf("/%s/ahci/io/%s/%s/%s", arch,
                           io_mode_str[type],
                           addr_mode_str[addr],
                           buff_len_str[len]);

    g_test_add_data_func(name, opts, test_io_interface);
    g_free(name);
}

/******************************************************************************/

int main(int argc, char **argv)
{
    const char *arch;
    int fd;
    int ret;
    int c;
    int i, j, k;

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

    /* Create a temporary raw image */
    fd = mkstemp(tmp_path);
    g_assert(fd >= 0);
    ret = ftruncate(fd, TEST_IMAGE_SIZE);
    g_assert(ret == 0);
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
                create_ahci_io_test(i, j, k);
            }
        }
    }

    qtest_add_func("/ahci/io/dma/lba28/fragmented", test_dma_fragmented);

    ret = g_test_run();

    /* Cleanup */
    unlink(tmp_path);

    return ret;
}
