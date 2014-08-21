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
#define PCI_MSI_FLAGS_RESERVED         (0xFF00)
#define PCI_PM_CTRL_RESERVED             (0xFC)
#define PCI_BCC(REG32)          ((REG32) >> 24)
#define PCI_PI(REG32)   (((REG32) >> 8) & 0xFF)
#define PCI_SCC(REG32) (((REG32) >> 16) & 0xFF)

/*** Recognized AHCI Device Types ***/
#define AHCI_INTEL_ICH9 (PCI_DEVICE_ID_INTEL_Q35_AHCI << 16 | \
                         PCI_VENDOR_ID_INTEL)

/*** AHCI/HBA Register Offsets and Bitmasks ***/
#define AHCI_CAP                          (0)
#define AHCI_CAP_NP                    (0x1F)
#define AHCI_CAP_SXS                   (0x20)
#define AHCI_CAP_EMS                   (0x40)
#define AHCI_CAP_CCCS                  (0x80)
#define AHCI_CAP_NCS                 (0x1F00)
#define AHCI_CAP_PSC                 (0x2000)
#define AHCI_CAP_SSC                 (0x4000)
#define AHCI_CAP_PMD                 (0x8000)
#define AHCI_CAP_FBSS               (0x10000)
#define AHCI_CAP_SPM                (0x20000)
#define AHCI_CAP_SAM                (0x40000)
#define AHCI_CAP_RESERVED           (0x80000)
#define AHCI_CAP_ISS               (0xF00000)
#define AHCI_CAP_SCLO             (0x1000000)
#define AHCI_CAP_SAL              (0x2000000)
#define AHCI_CAP_SALP             (0x4000000)
#define AHCI_CAP_SSS              (0x8000000)
#define AHCI_CAP_SMPS            (0x10000000)
#define AHCI_CAP_SSNTF           (0x20000000)
#define AHCI_CAP_SNCQ            (0x40000000)
#define AHCI_CAP_S64A            (0x80000000)

#define AHCI_GHC                          (1)
#define AHCI_GHC_HR                    (0x01)
#define AHCI_GHC_IE                    (0x02)
#define AHCI_GHC_MRSM                  (0x04)
#define AHCI_GHC_RESERVED        (0x7FFFFFF8)
#define AHCI_GHC_AE              (0x80000000)

#define AHCI_IS                           (2)
#define AHCI_PI                           (3)
#define AHCI_VS                           (4)

#define AHCI_CCCCTL                       (5)
#define AHCI_CCCCTL_EN                 (0x01)
#define AHCI_CCCCTL_RESERVED           (0x06)
#define AHCI_CCCCTL_CC               (0xFF00)
#define AHCI_CCCCTL_TV           (0xFFFF0000)

#define AHCI_CCCPORTS                     (6)
#define AHCI_EMLOC                        (7)

#define AHCI_EMCTL                        (8)
#define AHCI_EMCTL_STSMR               (0x01)
#define AHCI_EMCTL_CTLTM              (0x100)
#define AHCI_EMCTL_CTLRST             (0x200)
#define AHCI_EMCTL_RESERVED      (0xF0F0FCFE)

#define AHCI_CAP2                         (9)
#define AHCI_CAP2_BOH                  (0x01)
#define AHCI_CAP2_NVMP                 (0x02)
#define AHCI_CAP2_APST                 (0x04)
#define AHCI_CAP2_RESERVED       (0xFFFFFFF8)

#define AHCI_BOHC                        (10)
#define AHCI_RESERVED                    (11)
#define AHCI_NVMHCI                      (24)
#define AHCI_VENDOR                      (40)
#define AHCI_PORTS                       (64)

/*** Port Memory Offsets & Bitmasks ***/
#define AHCI_PX_CLB                       (0)
#define AHCI_PX_CLB_RESERVED          (0x1FF)

#define AHCI_PX_CLBU                      (1)

#define AHCI_PX_FB                        (2)
#define AHCI_PX_FB_RESERVED            (0xFF)

#define AHCI_PX_FBU                       (3)

#define AHCI_PX_IS                        (4)
#define AHCI_PX_IS_DHRS                 (0x1)
#define AHCI_PX_IS_PSS                  (0x2)
#define AHCI_PX_IS_DSS                  (0x4)
#define AHCI_PX_IS_SDBS                 (0x8)
#define AHCI_PX_IS_UFS                 (0x10)
#define AHCI_PX_IS_DPS                 (0x20)
#define AHCI_PX_IS_PCS                 (0x40)
#define AHCI_PX_IS_DMPS                (0x80)
#define AHCI_PX_IS_RESERVED       (0x23FFF00)
#define AHCI_PX_IS_PRCS            (0x400000)
#define AHCI_PX_IS_IPMS            (0x800000)
#define AHCI_PX_IS_OFS            (0x1000000)
#define AHCI_PX_IS_INFS           (0x4000000)
#define AHCI_PX_IS_IFS            (0x8000000)
#define AHCI_PX_IS_HBDS          (0x10000000)
#define AHCI_PX_IS_HBFS          (0x20000000)
#define AHCI_PX_IS_TFES          (0x40000000)
#define AHCI_PX_IS_CPDS          (0x80000000)

#define AHCI_PX_IE                        (5)
#define AHCI_PX_IE_DHRE                 (0x1)
#define AHCI_PX_IE_PSE                  (0x2)
#define AHCI_PX_IE_DSE                  (0x4)
#define AHCI_PX_IE_SDBE                 (0x8)
#define AHCI_PX_IE_UFE                 (0x10)
#define AHCI_PX_IE_DPE                 (0x20)
#define AHCI_PX_IE_PCE                 (0x40)
#define AHCI_PX_IE_DMPE                (0x80)
#define AHCI_PX_IE_RESERVED       (0x23FFF00)
#define AHCI_PX_IE_PRCE            (0x400000)
#define AHCI_PX_IE_IPME            (0x800000)
#define AHCI_PX_IE_OFE            (0x1000000)
#define AHCI_PX_IE_INFE           (0x4000000)
#define AHCI_PX_IE_IFE            (0x8000000)
#define AHCI_PX_IE_HBDE          (0x10000000)
#define AHCI_PX_IE_HBFE          (0x20000000)
#define AHCI_PX_IE_TFEE          (0x40000000)
#define AHCI_PX_IE_CPDE          (0x80000000)

#define AHCI_PX_CMD                       (6)
#define AHCI_PX_CMD_ST                  (0x1)
#define AHCI_PX_CMD_SUD                 (0x2)
#define AHCI_PX_CMD_POD                 (0x4)
#define AHCI_PX_CMD_CLO                 (0x8)
#define AHCI_PX_CMD_FRE                (0x10)
#define AHCI_PX_CMD_RESERVED           (0xE0)
#define AHCI_PX_CMD_CCS              (0x1F00)
#define AHCI_PX_CMD_MPSS             (0x2000)
#define AHCI_PX_CMD_FR               (0x4000)
#define AHCI_PX_CMD_CR               (0x8000)
#define AHCI_PX_CMD_CPS             (0x10000)
#define AHCI_PX_CMD_PMA             (0x20000)
#define AHCI_PX_CMD_HPCP            (0x40000)
#define AHCI_PX_CMD_MPSP            (0x80000)
#define AHCI_PX_CMD_CPD            (0x100000)
#define AHCI_PX_CMD_ESP            (0x200000)
#define AHCI_PX_CMD_FBSCP          (0x400000)
#define AHCI_PX_CMD_APSTE          (0x800000)
#define AHCI_PX_CMD_ATAPI         (0x1000000)
#define AHCI_PX_CMD_DLAE          (0x2000000)
#define AHCI_PX_CMD_ALPE          (0x4000000)
#define AHCI_PX_CMD_ASP           (0x8000000)
#define AHCI_PX_CMD_ICC          (0xF0000000)

#define AHCI_PX_RES1                      (7)

#define AHCI_PX_TFD                       (8)
#define AHCI_PX_TFD_STS                (0xFF)
#define AHCI_PX_TFD_STS_ERR            (0x01)
#define AHCI_PX_TFD_STS_CS1            (0x06)
#define AHCI_PX_TFD_STS_DRQ            (0x08)
#define AHCI_PX_TFD_STS_CS2            (0x70)
#define AHCI_PX_TFD_STS_BSY            (0x80)
#define AHCI_PX_TFD_ERR              (0xFF00)
#define AHCI_PX_TFD_RESERVED     (0xFFFF0000)

#define AHCI_PX_SIG                       (9)
#define AHCI_PX_SIG_SECTOR_COUNT       (0xFF)
#define AHCI_PX_SIG_LBA_LOW          (0xFF00)
#define AHCI_PX_SIG_LBA_MID        (0xFF0000)
#define AHCI_PX_SIG_LBA_HIGH     (0xFF000000)

#define AHCI_PX_SSTS                     (10)
#define AHCI_PX_SSTS_DET               (0x0F)
#define AHCI_PX_SSTS_SPD               (0xF0)
#define AHCI_PX_SSTS_IPM              (0xF00)
#define AHCI_PX_SSTS_RESERVED    (0xFFFFF000)
#define SSTS_DET_NO_DEVICE             (0x00)
#define SSTS_DET_PRESENT               (0x01)
#define SSTS_DET_ESTABLISHED           (0x03)
#define SSTS_DET_OFFLINE               (0x04)

#define AHCI_PX_SCTL                     (11)

#define AHCI_PX_SERR                     (12)
#define AHCI_PX_SERR_ERR             (0xFFFF)
#define AHCI_PX_SERR_DIAG        (0xFFFF0000)
#define AHCI_PX_SERR_DIAG_X      (0x04000000)

#define AHCI_PX_SACT                     (13)
#define AHCI_PX_CI                       (14)
#define AHCI_PX_SNTF                     (15)

#define AHCI_PX_FBS                      (16)
#define AHCI_PX_FBS_EN                  (0x1)
#define AHCI_PX_FBS_DEC                 (0x2)
#define AHCI_PX_FBS_SDE                 (0x4)
#define AHCI_PX_FBS_DEV               (0xF00)
#define AHCI_PX_FBS_ADO              (0xF000)
#define AHCI_PX_FBS_DWE             (0xF0000)
#define AHCI_PX_FBS_RESERVED     (0xFFF000F8)

#define AHCI_PX_RES2                     (17)
#define AHCI_PX_VS                       (28)

#define HBA_DATA_REGION_SIZE            (256)
#define HBA_PORT_DATA_SIZE              (128)
#define HBA_PORT_NUM_REG (HBA_PORT_DATA_SIZE/4)

#define AHCI_VERSION_0_95        (0x00000905)
#define AHCI_VERSION_1_0         (0x00010000)
#define AHCI_VERSION_1_1         (0x00010100)
#define AHCI_VERSION_1_2         (0x00010200)
#define AHCI_VERSION_1_3         (0x00010300)

/*** Structures ***/

/**
 * Generic FIS structure.
 */
typedef struct FIS {
    uint8_t fis_type;
    uint8_t flags;
    char data[0];
} __attribute__((__packed__)) FIS;

/**
 * Register device-to-host FIS structure.
 */
typedef struct RegD2HFIS {
    /* DW0 */
    uint8_t fis_type;
    uint8_t flags;
    uint8_t status;
    uint8_t error;
    /* DW1 */
    uint8_t lba_low;
    uint8_t lba_mid;
    uint8_t lba_high;
    uint8_t device;
    /* DW2 */
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t res1;
    /* DW3 */
    uint16_t count;
    uint8_t res2;
    uint8_t res3;
    /* DW4 */
    uint16_t res4;
    uint16_t res5;
} __attribute__((__packed__)) RegD2HFIS;

/**
 * Register host-to-device FIS structure.
 */
typedef struct RegH2DFIS {
    /* DW0 */
    uint8_t fis_type;
    uint8_t flags;
    uint8_t command;
    uint8_t feature_low;
    /* DW1 */
    uint8_t lba_low;
    uint8_t lba_mid;
    uint8_t lba_high;
    uint8_t device;
    /* DW2 */
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t feature_high;
    /* DW3 */
    uint16_t count;
    uint8_t icc;
    uint8_t control;
    /* DW4 */
    uint32_t aux;
} __attribute__((__packed__)) RegH2DFIS;

/**
 * Command List entry structure.
 * The command list contains between 1-32 of these structures.
 */
typedef struct AHCICommand {
    uint8_t b1;
    uint8_t b2;
    uint16_t prdtl; /* Phys Region Desc. Table Length */
    uint32_t prdbc; /* Phys Region Desc. Byte Count */
    uint32_t ctba;  /* Command Table Descriptor Base Address */
    uint32_t ctbau; /*                                    '' Upper */
    uint32_t res[4];
} __attribute__((__packed__)) AHCICommand;

/**
 * Physical Region Descriptor; pointed to by the Command List Header,
 * struct ahci_command.
 */
typedef struct PRD {
    uint32_t dba;  /* Data Base Address */
    uint32_t dbau; /* Data Base Address Upper */
    uint32_t res;  /* Reserved */
    uint32_t dbc;  /* Data Byte Count (0-indexed) & Interrupt Flag (bit 2^31) */
} PRD;

typedef struct HBACap {
    uint32_t cap;
    uint32_t cap2;
} HBACap;

/*** Globals ***/
static QGuestAllocator *guest_malloc;
static QPCIBus *pcibus;
static uint64_t barsize;
static char tmp_path[] = "/tmp/qtest.XXXXXX";
static bool ahci_pedantic;
static uint32_t ahci_fingerprint;

/*** Macro Utilities ***/
#define BITANY(data, mask) (((data) & (mask)) != 0)
#define BITSET(data, mask) (((data) & (mask)) == (mask))
#define BITCLR(data, mask) (((data) & (mask)) == 0)
#define ASSERT_BIT_SET(data, mask) g_assert_cmphex((data) & (mask), ==, (mask))
#define ASSERT_BIT_CLEAR(data, mask) g_assert_cmphex((data) & (mask), ==, 0)

/*** IO macros for the AHCI memory registers. ***/
#define AHCI_READ(OFST) qpci_io_readl(ahci, hba_base + (OFST))
#define AHCI_WRITE(OFST, VAL) qpci_io_writel(ahci, hba_base + (OFST), (VAL))
#define AHCI_RREG(regno)      AHCI_READ(4 * (regno))
#define AHCI_WREG(regno, val) AHCI_WRITE(4 * (regno), (val))
#define AHCI_SET(regno, mask) AHCI_WREG((regno), AHCI_RREG(regno) | (mask))
#define AHCI_CLR(regno, mask) AHCI_WREG((regno), AHCI_RREG(regno) & ~(mask))

/*** IO macros for port-specific offsets inside of AHCI memory. ***/
#define PX_OFST(port, regno) (HBA_PORT_NUM_REG * (port) + AHCI_PORTS + (regno))
#define PX_RREG(port, regno)      AHCI_RREG(PX_OFST((port), (regno)))
#define PX_WREG(port, regno, val) AHCI_WREG(PX_OFST((port), (regno)), (val))
#define PX_SET(port, reg, mask)   PX_WREG((port), (reg),                \
                                          PX_RREG((port), (reg)) | (mask));
#define PX_CLR(port, reg, mask)   PX_WREG((port), (reg),                \
                                          PX_RREG((port), (reg)) & ~(mask));

/* For calculating how big the PRD table needs to be: */
#define CMD_TBL_SIZ(n) ((0x80 + ((n) * sizeof(PRD)) + 0x7F) & ~0x7F)


/*** Function Declarations ***/
static QPCIDevice *get_ahci_device(void);
static QPCIDevice *start_ahci_device(QPCIDevice *dev, void **hba_base);
static void free_ahci_device(QPCIDevice *dev);
static void ahci_test_port_spec(QPCIDevice *ahci, void *hba_base,
                                HBACap *hcap, uint8_t port);
static void ahci_test_pci_spec(QPCIDevice *ahci);
static void ahci_test_pci_caps(QPCIDevice *ahci, uint16_t header,
                               uint8_t offset);
static void ahci_test_satacap(QPCIDevice *ahci, uint8_t offset);
static void ahci_test_msicap(QPCIDevice *ahci, uint8_t offset);
static void ahci_test_pmcap(QPCIDevice *ahci, uint8_t offset);

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

/**
 * Locate, verify, and return a handle to the AHCI device.
 */
static QPCIDevice *get_ahci_device(void)
{
    QPCIDevice *ahci;

    pcibus = qpci_init_pc();

    /* Find the AHCI PCI device and verify it's the right one. */
    ahci = qpci_device_find(pcibus, QPCI_DEVFN(0x1F, 0x02));
    g_assert(ahci != NULL);

    ahci_fingerprint = qpci_config_readl(ahci, PCI_VENDOR_ID);

    switch (ahci_fingerprint) {
    case AHCI_INTEL_ICH9:
        break;
    default:
        /* Unknown device. */
        g_assert_not_reached();
    }

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

    /* Clear our cached barsize information. */
    barsize = 0;
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

/*** Logical Device Initialization ***/

/**
 * Start the PCI device and sanity-check default operation.
 */
static void ahci_pci_enable(QPCIDevice *ahci, void **hba_base)
{
    uint8_t reg;

    start_ahci_device(ahci, hba_base);

    switch (ahci_fingerprint) {
    case AHCI_INTEL_ICH9:
        /* ICH9 has a register at PCI 0x92 that
         * acts as a master port enabler mask. */
        reg = qpci_config_readb(ahci, 0x92);
        reg |= 0x3F;
        qpci_config_writeb(ahci, 0x92, reg);
        /* 0...0111111b -- bit significant, ports 0-5 enabled. */
        ASSERT_BIT_SET(qpci_config_readb(ahci, 0x92), 0x3F);
        break;
    }

}

/**
 * Map BAR5/ABAR, and engage the PCI device.
 */
static QPCIDevice *start_ahci_device(QPCIDevice *ahci, void **hba_base)
{
    /* Map AHCI's ABAR (BAR5) */
    *hba_base = qpci_iomap(ahci, 5, &barsize);

    /* turns on pci.cmd.iose, pci.cmd.mse and pci.cmd.bme */
    qpci_device_enable(ahci);

    return ahci;
}

/**
 * Test and initialize the AHCI's HBA memory areas.
 * Initialize and start any ports with devices attached.
 * Bring the HBA into the idle state.
 */
static void ahci_hba_enable(QPCIDevice *ahci, void *hba_base)
{
    /* Bits of interest in this section:
     * GHC.AE     Global Host Control / AHCI Enable
     * PxCMD.ST   Port Command: Start
     * PxCMD.SUD  "Spin Up Device"
     * PxCMD.POD  "Power On Device"
     * PxCMD.FRE  "FIS Receive Enable"
     * PxCMD.FR   "FIS Receive Running"
     * PxCMD.CR   "Command List Running"
     */

    g_assert(ahci != NULL);
    g_assert(hba_base != NULL);

    uint32_t reg, ports_impl, clb, fb;
    uint16_t i;
    uint8_t num_cmd_slots;

    g_assert(hba_base != 0);

    /* Set GHC.AE to 1 */
    AHCI_SET(AHCI_GHC, AHCI_GHC_AE);
    reg = AHCI_RREG(AHCI_GHC);
    ASSERT_BIT_SET(reg, AHCI_GHC_AE);

    /* Read CAP.NCS, how many command slots do we have? */
    reg = AHCI_RREG(AHCI_CAP);
    num_cmd_slots = ((reg & AHCI_CAP_NCS) >> ctzl(AHCI_CAP_NCS)) + 1;
    g_test_message("Number of Command Slots: %u", num_cmd_slots);

    /* Determine which ports are implemented. */
    ports_impl = AHCI_RREG(AHCI_PI);

    for (i = 0; ports_impl; ports_impl >>= 1, ++i) {
        if (!(ports_impl & 0x01)) {
            continue;
        }

        g_test_message("Initializing port %u", i);

        reg = PX_RREG(i, AHCI_PX_CMD);
        if (BITCLR(reg, AHCI_PX_CMD_ST | AHCI_PX_CMD_CR |
                   AHCI_PX_CMD_FRE | AHCI_PX_CMD_FR)) {
            g_test_message("port is idle");
        } else {
            g_test_message("port needs to be idled");
            PX_CLR(i, AHCI_PX_CMD, (AHCI_PX_CMD_ST | AHCI_PX_CMD_FRE));
            /* The port has 500ms to disengage. */
            usleep(500000);
            reg = PX_RREG(i, AHCI_PX_CMD);
            ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_CR);
            ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_FR);
            g_test_message("port is now idle");
            /* The spec does allow for possibly needing a PORT RESET
             * or HBA reset if we fail to idle the port. */
        }

        /* Allocate Memory for the Command List Buffer & FIS Buffer */
        /* PxCLB space ... 0x20 per command, as in 4.2.2 p 36 */
        clb = guest_alloc(guest_malloc, num_cmd_slots * 0x20);
        g_test_message("CLB: 0x%08x", clb);
        PX_WREG(i, AHCI_PX_CLB, clb);
        g_assert_cmphex(clb, ==, PX_RREG(i, AHCI_PX_CLB));

        /* PxFB space ... 0x100, as in 4.2.1 p 35 */
        fb = guest_alloc(guest_malloc, 0x100);
        g_test_message("FB: 0x%08x", fb);
        PX_WREG(i, AHCI_PX_FB, fb);
        g_assert_cmphex(fb, ==, PX_RREG(i, AHCI_PX_FB));

        /* Clear PxSERR, PxIS, then IS.IPS[x] by writing '1's. */
        PX_WREG(i, AHCI_PX_SERR, 0xFFFFFFFF);
        PX_WREG(i, AHCI_PX_IS, 0xFFFFFFFF);
        AHCI_WREG(AHCI_IS, (1 << i));

        /* Verify Interrupts Cleared */
        reg = PX_RREG(i, AHCI_PX_SERR);
        g_assert_cmphex(reg, ==, 0);

        reg = PX_RREG(i, AHCI_PX_IS);
        g_assert_cmphex(reg, ==, 0);

        reg = AHCI_RREG(AHCI_IS);
        ASSERT_BIT_CLEAR(reg, (1 << i));

        /* Enable All Interrupts: */
        PX_WREG(i, AHCI_PX_IE, 0xFFFFFFFF);
        reg = PX_RREG(i, AHCI_PX_IE);
        g_assert_cmphex(reg, ==, ~((uint32_t)AHCI_PX_IE_RESERVED));

        /* Enable the FIS Receive Engine. */
        PX_SET(i, AHCI_PX_CMD, AHCI_PX_CMD_FRE);
        reg = PX_RREG(i, AHCI_PX_CMD);
        ASSERT_BIT_SET(reg, AHCI_PX_CMD_FR);

        /* AHCI 1.3 spec: if !STS.BSY, !STS.DRQ and PxSSTS.DET indicates
         * physical presence, a device is present and may be started. However,
         * PxSERR.DIAG.X /may/ need to be cleared a priori. */
        reg = PX_RREG(i, AHCI_PX_SERR);
        if (BITSET(reg, AHCI_PX_SERR_DIAG_X)) {
            PX_SET(i, AHCI_PX_SERR, AHCI_PX_SERR_DIAG_X);
        }

        reg = PX_RREG(i, AHCI_PX_TFD);
        if (BITCLR(reg, AHCI_PX_TFD_STS_BSY | AHCI_PX_TFD_STS_DRQ)) {
            reg = PX_RREG(i, AHCI_PX_SSTS);
            if ((reg & AHCI_PX_SSTS_DET) == SSTS_DET_ESTABLISHED) {
                /* Device Found: set PxCMD.ST := 1 */
                PX_SET(i, AHCI_PX_CMD, AHCI_PX_CMD_ST);
                ASSERT_BIT_SET(PX_RREG(i, AHCI_PX_CMD), AHCI_PX_CMD_CR);
                g_test_message("Started Device %u", i);
            } else if ((reg & AHCI_PX_SSTS_DET)) {
                /* Device present, but in some unknown state. */
                g_assert_not_reached();
            }
        }
    }

    /* Enable GHC.IE */
    AHCI_SET(AHCI_GHC, AHCI_GHC_IE);
    reg = AHCI_RREG(AHCI_GHC);
    ASSERT_BIT_SET(reg, AHCI_GHC_IE);

    /* TODO: The device should now be idling and waiting for commands.
     * In the future, a small test-case to inspect the Register D2H FIS
     * and clear the initial interrupts might be good. */
}

/*** Specification Adherence Tests ***/

/**
 * Implementation for test_pci_spec. Ensures PCI configuration space is sane.
 */
static void ahci_test_pci_spec(QPCIDevice *ahci)
{
    uint8_t datab;
    uint16_t data;
    uint32_t datal;

    /* Most of these bits should start cleared until we turn them on. */
    data = qpci_config_readw(ahci, PCI_COMMAND);
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

    data = qpci_config_readw(ahci, PCI_STATUS);
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
    datal = qpci_config_readl(ahci, PCI_CLASS_REVISION);
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

    datab = qpci_config_readb(ahci, PCI_CACHE_LINE_SIZE);
    g_assert_cmphex(datab, ==, 0);

    datab = qpci_config_readb(ahci, PCI_LATENCY_TIMER);
    g_assert_cmphex(datab, ==, 0);

    /* Only the bottom 7 bits must be off. */
    datab = qpci_config_readb(ahci, PCI_HEADER_TYPE);
    ASSERT_BIT_CLEAR(datab, 0x7F);

    /* BIST is optional, but the low 7 bits must always start off regardless. */
    datab = qpci_config_readb(ahci, PCI_BIST);
    ASSERT_BIT_CLEAR(datab, 0x7F);

    /* BARS 0-4 do not have a boot spec, but ABAR/BAR5 must be clean. */
    datal = qpci_config_readl(ahci, PCI_BASE_ADDRESS_5);
    g_assert_cmphex(datal, ==, 0);

    qpci_config_writel(ahci, PCI_BASE_ADDRESS_5, 0xFFFFFFFF);
    datal = qpci_config_readl(ahci, PCI_BASE_ADDRESS_5);
    /* ABAR must be 32-bit, memory mapped, non-prefetchable and
     * must be >= 512 bytes. To that end, bits 0-8 must be off. */
    ASSERT_BIT_CLEAR(datal, 0xFF);

    /* Capability list MUST be present, */
    datal = qpci_config_readl(ahci, PCI_CAPABILITY_LIST);
    /* But these bits are reserved. */
    ASSERT_BIT_CLEAR(datal, ~0xFF);
    g_assert_cmphex(datal, !=, 0);

    /* Check specification adherence for capability extenstions. */
    data = qpci_config_readw(ahci, datal);

    switch (ahci_fingerprint) {
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
    datal = qpci_config_readl(ahci, PCI_CAPABILITY_LIST + 4);
    g_assert_cmphex(datal, ==, 0);

    /* IPIN might vary, but ILINE must be off. */
    datab = qpci_config_readb(ahci, PCI_INTERRUPT_LINE);
    g_assert_cmphex(datab, ==, 0);
}

/**
 * Test PCI capabilities for AHCI specification adherence.
 */
static void ahci_test_pci_caps(QPCIDevice *ahci, uint16_t header,
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
        ahci_test_pci_caps(ahci, qpci_config_readw(ahci, next), next);
    }
}

/**
 * Test SATA PCI capabilitity for AHCI specification adherence.
 */
static void ahci_test_satacap(QPCIDevice *ahci, uint8_t offset)
{
    uint16_t dataw;
    uint32_t datal;

    g_test_message("Verifying SATACAP");

    /* Assert that the SATACAP version is 1.0, And reserved bits are empty. */
    dataw = qpci_config_readw(ahci, offset + 2);
    g_assert_cmphex(dataw, ==, 0x10);

    /* Grab the SATACR1 register. */
    datal = qpci_config_readw(ahci, offset + 4);

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
static void ahci_test_msicap(QPCIDevice *ahci, uint8_t offset)
{
    uint16_t dataw;
    uint32_t datal;

    g_test_message("Verifying MSICAP");

    dataw = qpci_config_readw(ahci, offset + PCI_MSI_FLAGS);
    ASSERT_BIT_CLEAR(dataw, PCI_MSI_FLAGS_ENABLE);
    ASSERT_BIT_CLEAR(dataw, PCI_MSI_FLAGS_QSIZE);
    ASSERT_BIT_CLEAR(dataw, PCI_MSI_FLAGS_RESERVED);

    datal = qpci_config_readl(ahci, offset + PCI_MSI_ADDRESS_LO);
    g_assert_cmphex(datal, ==, 0);

    if (dataw & PCI_MSI_FLAGS_64BIT) {
        g_test_message("MSICAP is 64bit");
        datal = qpci_config_readl(ahci, offset + PCI_MSI_ADDRESS_HI);
        g_assert_cmphex(datal, ==, 0);
        dataw = qpci_config_readw(ahci, offset + PCI_MSI_DATA_64);
        g_assert_cmphex(dataw, ==, 0);
    } else {
        g_test_message("MSICAP is 32bit");
        dataw = qpci_config_readw(ahci, offset + PCI_MSI_DATA_32);
        g_assert_cmphex(dataw, ==, 0);
    }
}

/**
 * Test Power Management PCI capability for AHCI specification adherence.
 */
static void ahci_test_pmcap(QPCIDevice *ahci, uint8_t offset)
{
    uint16_t dataw;

    g_test_message("Verifying PMCAP");

    dataw = qpci_config_readw(ahci, offset + PCI_PM_PMC);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CAP_PME_CLOCK);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CAP_RESERVED);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CAP_D1);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CAP_D2);

    dataw = qpci_config_readw(ahci, offset + PCI_PM_CTRL);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CTRL_STATE_MASK);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CTRL_RESERVED);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CTRL_DATA_SEL_MASK);
    ASSERT_BIT_CLEAR(dataw, PCI_PM_CTRL_DATA_SCALE_MASK);
}

static void ahci_test_hba_spec(QPCIDevice *ahci, void *hba_base)
{
    HBACap hcap;
    unsigned i;
    uint32_t cap, cap2, reg;
    uint32_t ports;
    uint8_t nports_impl;
    uint8_t maxports;

    g_assert(ahci != 0);
    g_assert(hba_base != 0);

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
    cap = AHCI_RREG(AHCI_CAP);
    ASSERT_BIT_CLEAR(cap, AHCI_CAP_RESERVED);

    /* 2 GHC - Global Host Control */
    reg = AHCI_RREG(AHCI_GHC);
    ASSERT_BIT_CLEAR(reg, AHCI_GHC_HR);
    ASSERT_BIT_CLEAR(reg, AHCI_GHC_IE);
    ASSERT_BIT_CLEAR(reg, AHCI_GHC_MRSM);
    if (BITSET(cap, AHCI_CAP_SAM)) {
        g_test_message("Supports AHCI-Only Mode: GHC_AE is Read-Only.");
        ASSERT_BIT_SET(reg, AHCI_GHC_AE);
    } else {
        g_test_message("Supports AHCI/Legacy mix.");
        ASSERT_BIT_CLEAR(reg, AHCI_GHC_AE);
    }

    /* 3 IS - Interrupt Status */
    reg = AHCI_RREG(AHCI_IS);
    g_assert_cmphex(reg, ==, 0);

    /* 4 PI - Ports Implemented */
    ports = AHCI_RREG(AHCI_PI);
    /* Ports Implemented must be non-zero. */
    g_assert_cmphex(ports, !=, 0);
    /* Ports Implemented must be <= Number of Ports. */
    nports_impl = ctpopl(ports);
    g_assert_cmpuint(((AHCI_CAP_NP & cap) + 1), >=, nports_impl);

    g_assert_cmphex(barsize, >, 0);
    /* Ports must be within the proper range. Given a mapping of SIZE,
     * 256 bytes are used for global HBA control, and the rest is used
     * for ports data, at 0x80 bytes each. */
    maxports = (barsize - HBA_DATA_REGION_SIZE) / HBA_PORT_DATA_SIZE;
    /* e.g, 30 ports for 4K of memory. (4096 - 256) / 128 = 30 */
    g_assert_cmphex((reg >> maxports), ==, 0);

    /* 5 AHCI Version */
    reg = AHCI_RREG(AHCI_VS);
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
    reg = AHCI_RREG(AHCI_CCCCTL);
    if (BITSET(cap, AHCI_CAP_CCCS)) {
        ASSERT_BIT_CLEAR(reg, AHCI_CCCCTL_EN);
        ASSERT_BIT_CLEAR(reg, AHCI_CCCCTL_RESERVED);
        ASSERT_BIT_SET(reg, AHCI_CCCCTL_CC);
        ASSERT_BIT_SET(reg, AHCI_CCCCTL_TV);
    } else {
        g_assert_cmphex(reg, ==, 0);
    }

    /* 7 CCC_PORTS */
    reg = AHCI_RREG(AHCI_CCCPORTS);
    /* Must be zeroes initially regardless of CAP.CCCS */
    g_assert_cmphex(reg, ==, 0);

    /* 8 EM_LOC */
    reg = AHCI_RREG(AHCI_EMLOC);
    if (BITCLR(cap, AHCI_CAP_EMS)) {
        g_assert_cmphex(reg, ==, 0);
    }

    /* 9 EM_CTL */
    reg = AHCI_RREG(AHCI_EMCTL);
    if (BITSET(cap, AHCI_CAP_EMS)) {
        ASSERT_BIT_CLEAR(reg, AHCI_EMCTL_STSMR);
        ASSERT_BIT_CLEAR(reg, AHCI_EMCTL_CTLTM);
        ASSERT_BIT_CLEAR(reg, AHCI_EMCTL_CTLRST);
        ASSERT_BIT_CLEAR(reg, AHCI_EMCTL_RESERVED);
    } else {
        g_assert_cmphex(reg, ==, 0);
    }

    /* 10 CAP2 -- Capabilities Extended */
    cap2 = AHCI_RREG(AHCI_CAP2);
    ASSERT_BIT_CLEAR(cap2, AHCI_CAP2_RESERVED);

    /* 11 BOHC -- Bios/OS Handoff Control */
    reg = AHCI_RREG(AHCI_BOHC);
    g_assert_cmphex(reg, ==, 0);

    /* 12 -- 23: Reserved */
    g_test_message("Verifying HBA reserved area is empty.");
    for (i = AHCI_RESERVED; i < AHCI_NVMHCI; ++i) {
        reg = AHCI_RREG(i);
        g_assert_cmphex(reg, ==, 0);
    }

    /* 24 -- 39: NVMHCI */
    if (BITCLR(cap2, AHCI_CAP2_NVMP)) {
        g_test_message("Verifying HBA/NVMHCI area is empty.");
        for (i = AHCI_NVMHCI; i < AHCI_VENDOR; ++i) {
            reg = AHCI_RREG(i);
            g_assert_cmphex(reg, ==, 0);
        }
    }

    /* 40 -- 63: Vendor */
    g_test_message("Verifying HBA/Vendor area is empty.");
    for (i = AHCI_VENDOR; i < AHCI_PORTS; ++i) {
        reg = AHCI_RREG(i);
        g_assert_cmphex(reg, ==, 0);
    }

    /* 64 -- XX: Port Space */
    hcap.cap = cap;
    hcap.cap2 = cap2;
    for (i = 0; ports || (i < maxports); ports >>= 1, ++i) {
        if (BITSET(ports, 0x1)) {
            g_test_message("Testing port %u for spec", i);
            ahci_test_port_spec(ahci, hba_base, &hcap, i);
        } else {
            uint16_t j;
            uint16_t low = AHCI_PORTS + (32 * i);
            uint16_t high = AHCI_PORTS + (32 * (i + 1));
            g_test_message("Asserting unimplemented port %u "
                           "(reg [%u-%u]) is empty.",
                           i, low, high - 1);
            for (j = low; j < high; ++j) {
                reg = AHCI_RREG(j);
                g_assert_cmphex(reg, ==, 0);
            }
        }
    }
}

/**
 * Test the memory space for one port for specification adherence.
 */
static void ahci_test_port_spec(QPCIDevice *ahci, void *hba_base,
                                HBACap *hcap, uint8_t port)
{
    uint32_t reg;
    unsigned i;

    /* (0) CLB */
    reg = PX_RREG(port, AHCI_PX_CLB);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_CLB_RESERVED);

    /* (1) CLBU */
    if (BITCLR(hcap->cap, AHCI_CAP_S64A)) {
        reg = PX_RREG(port, AHCI_PX_CLBU);
        g_assert_cmphex(reg, ==, 0);
    }

    /* (2) FB */
    reg = PX_RREG(port, AHCI_PX_FB);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FB_RESERVED);

    /* (3) FBU */
    if (BITCLR(hcap->cap, AHCI_CAP_S64A)) {
        reg = PX_RREG(port, AHCI_PX_FBU);
        g_assert_cmphex(reg, ==, 0);
    }

    /* (4) IS */
    reg = PX_RREG(port, AHCI_PX_IS);
    g_assert_cmphex(reg, ==, 0);

    /* (5) IE */
    reg = PX_RREG(port, AHCI_PX_IE);
    g_assert_cmphex(reg, ==, 0);

    /* (6) CMD */
    reg = PX_RREG(port, AHCI_PX_CMD);
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
    if (BITCLR(hcap->cap, AHCI_CAP_SMPS)) {
        ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_MPSS);
        ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_MPSP);
    }
    /* If, via CPD or MPSP we detect a drive, HPCP must be on. */
    if (BITANY(reg, AHCI_PX_CMD_CPD || AHCI_PX_CMD_MPSP)) {
        ASSERT_BIT_SET(reg, AHCI_PX_CMD_HPCP);
    }
    /* HPCP and ESP cannot both be active. */
    g_assert(!BITSET(reg, AHCI_PX_CMD_HPCP | AHCI_PX_CMD_ESP));
    /* If CAP.FBSS is not set, FBSCP must not be set. */
    if (BITCLR(hcap->cap, AHCI_CAP_FBSS)) {
        ASSERT_BIT_CLEAR(reg, AHCI_PX_CMD_FBSCP);
    }

    /* (7) RESERVED */
    reg = PX_RREG(port, AHCI_PX_RES1);
    g_assert_cmphex(reg, ==, 0);

    /* (8) TFD */
    reg = PX_RREG(port, AHCI_PX_TFD);
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
    reg = PX_RREG(port, AHCI_PX_SSTS);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_SSTS_RESERVED);
    /* Even though the register should be 0 at boot, it is asynchronous and
     * prone to change, so we cannot test any well known value. */

    /* (11) SCTL / SCR2: SControl */
    reg = PX_RREG(port, AHCI_PX_SCTL);
    g_assert_cmphex(reg, ==, 0);

    /* (12) SERR / SCR1: SError */
    reg = PX_RREG(port, AHCI_PX_SERR);
    g_assert_cmphex(reg, ==, 0);

    /* (13) SACT / SCR3: SActive */
    reg = PX_RREG(port, AHCI_PX_SACT);
    g_assert_cmphex(reg, ==, 0);

    /* (14) CI */
    reg = PX_RREG(port, AHCI_PX_CI);
    g_assert_cmphex(reg, ==, 0);

    /* (15) SNTF */
    reg = PX_RREG(port, AHCI_PX_SNTF);
    g_assert_cmphex(reg, ==, 0);

    /* (16) FBS */
    reg = PX_RREG(port, AHCI_PX_FBS);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_EN);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_DEC);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_SDE);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_DEV);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_DWE);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_FBS_RESERVED);
    if (BITSET(hcap->cap, AHCI_CAP_FBSS)) {
        /* if Port-Multiplier FIS-based switching avail, ADO must >= 2 */
        g_assert((reg & AHCI_PX_FBS_ADO) >> ctzl(AHCI_PX_FBS_ADO) >= 2);
    }

    /* [17 -- 27] RESERVED */
    for (i = AHCI_PX_RES2; i < AHCI_PX_VS; ++i) {
        reg = PX_RREG(port, i);
        g_assert_cmphex(reg, ==, 0);
    }

    /* [28 -- 31] Vendor-Specific */
    for (i = AHCI_PX_VS; i < 32; ++i) {
        reg = PX_RREG(port, i);
        if (reg) {
            g_test_message("INFO: Vendor register %u non-empty", i);
        }
    }
}

/**
 * Utilizing an initialized AHCI HBA, issue an IDENTIFY command to the first
 * device we see, then read and check the response.
 */
static void ahci_test_identify(QPCIDevice *ahci, void *hba_base)
{
    RegD2HFIS *d2h = g_malloc0(0x20);
    RegD2HFIS *pio = g_malloc0(0x20);
    RegH2DFIS fis;
    AHCICommand cmd;
    PRD prd;
    uint32_t ports, reg, clb, table, fb, data_ptr;
    uint16_t buff[256];
    unsigned i;
    int rc;

    g_assert(ahci != NULL);
    g_assert(hba_base != NULL);

    /* We need to:
     * (1) Create a Command Table Buffer and update the Command List Slot #0
     *     to point to this buffer.
     * (2) Construct an FIS host-to-device command structure, and write it to
     *     the top of the command table buffer.
     * (3) Create a data buffer for the IDENTIFY response to be sent to
     * (4) Create a Physical Region Descriptor that points to the data buffer,
     *     and write it to the bottom (offset 0x80) of the command table.
     * (5) Now, PxCLB points to the command list, command 0 points to
     *     our table, and our table contains an FIS instruction and a
     *     PRD that points to our rx buffer.
     * (6) We inform the HBA via PxCI that there is a command ready in slot #0.
     */

    /* Pick the first implemented and running port */
    ports = AHCI_RREG(AHCI_PI);
    for (i = 0; i < 32; ports >>= 1, ++i) {
        if (ports == 0) {
            i = 32;
        }

        if (!(ports & 0x01)) {
            continue;
        }

        reg = PX_RREG(i, AHCI_PX_CMD);
        if (BITSET(reg, AHCI_PX_CMD_ST)) {
            break;
        }
    }
    g_assert_cmphex(i, <, 32);
    g_test_message("Selected port %u for test", i);

    /* Clear out this port's interrupts (ignore the init register d2h fis) */
    reg = PX_RREG(i, AHCI_PX_IS);
    PX_WREG(i, AHCI_PX_IS, reg);
    g_assert_cmphex(PX_RREG(i, AHCI_PX_IS), ==, 0);

    /* Wipe the FIS-Recieve Buffer */
    fb = PX_RREG(i, AHCI_PX_FB);
    g_assert_cmphex(fb, !=, 0);
    qmemset(fb, 0x00, 0x100);

    /* Create a Command Table buffer. 0x80 is the smallest with a PRDTL of 0. */
    /* We need at least one PRD, so round up to the nearest 0x80 multiple.    */
    table = guest_alloc(guest_malloc, CMD_TBL_SIZ(1));
    g_assert(table);
    ASSERT_BIT_CLEAR(table, 0x7F);

    /* Create a data buffer ... where we will dump the IDENTIFY data to. */
    data_ptr = guest_alloc(guest_malloc, 512);
    g_assert(data_ptr);

    /* Grab the Command List Buffer pointer */
    clb = PX_RREG(i, AHCI_PX_CLB);
    g_assert(clb);

    /* Copy the existing Command #0 structure from the CLB into local memory,
     * and build a new command #0. */
    memread(clb, &cmd, sizeof(cmd));
    cmd.b1 = 5;    /* reg_h2d_fis is 5 double-words long */
    cmd.b2 = 0x04; /* clear PxTFD.STS.BSY when done */
    cmd.prdtl = cpu_to_le16(1); /* One PRD table entry. */
    cmd.prdbc = 0;
    cmd.ctba = cpu_to_le32(table);
    cmd.ctbau = 0;

    /* Construct our PRD, noting that DBC is 0-indexed. */
    prd.dba = cpu_to_le32(data_ptr);
    prd.dbau = 0;
    prd.res = 0;
    /* 511+1 bytes, request DPS interrupt */
    prd.dbc = cpu_to_le32(511 | 0x80000000);

    /* Construct our Command FIS, Based on http://wiki.osdev.org/AHCI */
    memset(&fis, 0x00, sizeof(fis));
    fis.fis_type = 0x27; /* Register Host-to-Device FIS */
    fis.command = 0xEC;  /* IDENTIFY */
    fis.device = 0;
    fis.flags = 0x80;    /* Indicate this is a command FIS */

    /* We've committed nothing yet, no interrupts should be posted yet. */
    g_assert_cmphex(PX_RREG(i, AHCI_PX_IS), ==, 0);

    /* Commit the Command FIS to the Command Table */
    memwrite(table, &fis, sizeof(fis));

    /* Commit the PRD entry to the Command Table */
    memwrite(table + 0x80, &prd, sizeof(prd));

    /* Commit Command #0, pointing to the Table, to the Command List Buffer. */
    memwrite(clb, &cmd, sizeof(cmd));

    /* Everything is in place, but we haven't given the go-ahead yet. */
    g_assert_cmphex(PX_RREG(i, AHCI_PX_IS), ==, 0);

    /* Issue Command #0 via PxCI */
    PX_WREG(i, AHCI_PX_CI, (1 << 0));
    while (BITSET(PX_RREG(i, AHCI_PX_TFD), AHCI_PX_TFD_STS_BSY)) {
        usleep(50);
    }

    /* Check for expected interrupts */
    reg = PX_RREG(i, AHCI_PX_IS);
    ASSERT_BIT_SET(reg, AHCI_PX_IS_DHRS);
    ASSERT_BIT_SET(reg, AHCI_PX_IS_PSS);
    /* BUG: we expect AHCI_PX_IS_DPS to be set. */
    ASSERT_BIT_CLEAR(reg, AHCI_PX_IS_DPS);

    /* Clear expected interrupts and assert all interrupts now cleared. */
    PX_WREG(i, AHCI_PX_IS, AHCI_PX_IS_DHRS | AHCI_PX_IS_PSS | AHCI_PX_IS_DPS);
    g_assert_cmphex(PX_RREG(i, AHCI_PX_IS), ==, 0);

    /* Check for errors. */
    reg = PX_RREG(i, AHCI_PX_SERR);
    g_assert_cmphex(reg, ==, 0);
    reg = PX_RREG(i, AHCI_PX_TFD);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_TFD_STS_ERR);
    ASSERT_BIT_CLEAR(reg, AHCI_PX_TFD_ERR);

    /* Investigate CMD #0, assert that we read 512 bytes */
    memread(clb, &cmd, sizeof(cmd));
    g_assert_cmphex(512, ==, le32_to_cpu(cmd.prdbc));

    /* Investigate FIS responses */
    memread(fb + 0x20, pio, 0x20);
    memread(fb + 0x40, d2h, 0x20);
    g_assert_cmphex(pio->fis_type, ==, 0x5f);
    g_assert_cmphex(d2h->fis_type, ==, 0x34);
    g_assert_cmphex(pio->flags, ==, d2h->flags);
    g_assert_cmphex(pio->status, ==, d2h->status);
    g_assert_cmphex(pio->error, ==, d2h->error);

    reg = PX_RREG(i, AHCI_PX_TFD);
    g_assert_cmphex((reg & AHCI_PX_TFD_ERR), ==, pio->error);
    g_assert_cmphex((reg & AHCI_PX_TFD_STS), ==, pio->status);
    /* The PIO Setup FIS contains a "bytes read" field, which is a
     * 16-bit value. The Physical Region Descriptor Byte Count is
     * 32-bit, but for small transfers using one PRD, it should match. */
    g_assert_cmphex(le16_to_cpu(pio->res4), ==, le32_to_cpu(cmd.prdbc));

    /* Last, but not least: Investigate the IDENTIFY response data. */
    memread(data_ptr, &buff, 512);

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

    g_free(d2h);
    g_free(pio);
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

/**
 * Ensure that the PCI configuration space for the AHCI device is in-line with
 * the AHCI 1.3 specification for initial values.
 */
static void test_pci_spec(void)
{
    QPCIDevice *ahci;
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
    QPCIDevice *ahci;
    void *hba_base;
    ahci = ahci_boot();
    ahci_pci_enable(ahci, &hba_base);
    ahci_shutdown(ahci);
}

/**
 * Investigate the memory mapped regions of the HBA,
 * and test them for AHCI specification adherence.
 */
static void test_hba_spec(void)
{
    QPCIDevice *ahci;
    void *hba_base;

    ahci = ahci_boot();
    ahci_pci_enable(ahci, &hba_base);
    ahci_test_hba_spec(ahci, hba_base);
    ahci_shutdown(ahci);
}

/**
 * Engage the HBA functionality of the AHCI PCI device,
 * and bring it into a functional idle state.
 */
static void test_hba_enable(void)
{
    QPCIDevice *ahci;
    void *hba_base;

    ahci = ahci_boot();
    ahci_pci_enable(ahci, &hba_base);
    ahci_hba_enable(ahci, hba_base);
    ahci_shutdown(ahci);
}

/**
 * Bring up the device and issue an IDENTIFY command.
 * Inspect the state of the HBA device and the data returned.
 */
static void test_identify(void)
{
    QPCIDevice *ahci;
    void *hba_base;

    ahci = ahci_boot();
    ahci_pci_enable(ahci, &hba_base);
    ahci_hba_enable(ahci, hba_base);
    ahci_test_identify(ahci, hba_base);
    ahci_shutdown(ahci);
}

/******************************************************************************/

int main(int argc, char **argv)
{
    const char *arch;
    int fd;
    int ret;
    int c;

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

    ret = g_test_run();

    /* Cleanup */
    unlink(tmp_path);

    return ret;
}
