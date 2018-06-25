#ifndef LIBQOS_AHCI_H
#define LIBQOS_AHCI_H

/*
 * AHCI qtest library functions and definitions
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

#include "libqos/libqos.h"
#include "libqos/pci.h"
#include "libqos/malloc-pc.h"

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

#define AHCI_SECTOR_SIZE                (512)
#define ATAPI_SECTOR_SIZE              (2048)

#define AHCI_SIGNATURE_CDROM     (0xeb140101)
#define AHCI_SIGNATURE_DISK      (0x00000101)

/* FIS types */
enum {
    REG_H2D_FIS = 0x27,
    REG_D2H_FIS = 0x34,
    DMA_ACTIVATE_FIS = 0x39,
    DMA_SETUP_FIS = 0x41,
    DATA_FIS = 0x46,
    BIST_ACTIVATE_FIS = 0x58,
    PIO_SETUP_FIS = 0x5F,
    SDB_FIS = 0xA1
};

/* FIS flags */
#define REG_H2D_FIS_CMD  0x80

/* ATA Commands */
enum {
    /* DMA */
    CMD_READ_DMA       = 0xC8,
    CMD_READ_DMA_EXT   = 0x25,
    CMD_WRITE_DMA      = 0xCA,
    CMD_WRITE_DMA_EXT  = 0x35,
    /* PIO */
    CMD_READ_PIO       = 0x20,
    CMD_READ_PIO_EXT   = 0x24,
    CMD_WRITE_PIO      = 0x30,
    CMD_WRITE_PIO_EXT  = 0x34,
    /* Misc */
    CMD_READ_MAX       = 0xF8,
    CMD_READ_MAX_EXT   = 0x27,
    CMD_FLUSH_CACHE    = 0xE7,
    CMD_IDENTIFY       = 0xEC,
    CMD_PACKET         = 0xA0,
    CMD_PACKET_ID      = 0xA1,
    /* NCQ */
    READ_FPDMA_QUEUED  = 0x60,
    WRITE_FPDMA_QUEUED = 0x61,
};

/* ATAPI Commands */
enum {
    CMD_ATAPI_TEST_UNIT_READY = 0x00,
    CMD_ATAPI_REQUEST_SENSE   = 0x03,
    CMD_ATAPI_START_STOP_UNIT = 0x1b,
    CMD_ATAPI_READ_10         = 0x28,
    CMD_ATAPI_READ_CD         = 0xbe,
};

enum {
    SENSE_NO_SENSE       = 0x00,
    SENSE_NOT_READY      = 0x02,
    SENSE_UNIT_ATTENTION = 0x06,
};

enum {
    ASC_MEDIUM_MAY_HAVE_CHANGED = 0x28,
    ASC_MEDIUM_NOT_PRESENT      = 0x3a,
};

/* AHCI Command Header Flags & Masks*/
#define CMDH_CFL        (0x1F)
#define CMDH_ATAPI      (0x20)
#define CMDH_WRITE      (0x40)
#define CMDH_PREFETCH   (0x80)
#define CMDH_RESET     (0x100)
#define CMDH_BIST      (0x200)
#define CMDH_CLR_BSY   (0x400)
#define CMDH_RES       (0x800)
#define CMDH_PMP      (0xF000)

/* ATA device register masks */
#define ATA_DEVICE_MAGIC 0xA0 /* used in ata1-3 */
#define ATA_DEVICE_LBA   0x40
#define NCQ_DEVICE_MAGIC 0x40 /* for ncq device registers */
#define ATA_DEVICE_DRIVE 0x10
#define ATA_DEVICE_HEAD  0x0F

/*** Structures ***/

typedef struct AHCIPortQState {
    uint64_t fb;
    uint64_t clb;
    uint64_t ctba[32];
    uint16_t prdtl[32];
    uint8_t next; /** Next Command Slot to Use **/
} AHCIPortQState;

typedef struct AHCIQState {
    QOSState *parent;
    QPCIDevice *dev;
    QPCIBar hba_bar;
    uint64_t barsize;
    uint32_t fingerprint;
    uint32_t cap;
    uint32_t cap2;
    AHCIPortQState port[32];
    bool enabled;
} AHCIQState;

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
    uint8_t lba_lo[3];
    uint8_t device;
    /* DW2 */
    uint8_t lba_hi[3];
    uint8_t res0;
    /* DW3 */
    uint16_t count;
    uint16_t res1;
    /* DW4 */
    uint32_t res2;
} __attribute__((__packed__)) RegD2HFIS;

/**
 * Register device-to-host FIS structure;
 * PIO Setup variety.
 */
typedef struct PIOSetupFIS {
    /* DW0 */
    uint8_t fis_type;
    uint8_t flags;
    uint8_t status;
    uint8_t error;
    /* DW1 */
    uint8_t lba_lo[3];
    uint8_t device;
    /* DW2 */
    uint8_t lba_hi[3];
    uint8_t res0;
    /* DW3 */
    uint16_t count;
    uint8_t res1;
    uint8_t e_status;
    /* DW4 */
    uint16_t tx_count;
    uint16_t res2;
} __attribute__((__packed__)) PIOSetupFIS;

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
    uint8_t lba_lo[3];
    uint8_t device;
    /* DW2 */
    uint8_t lba_hi[3];
    uint8_t feature_high;
    /* DW3 */
    uint16_t count;
    uint8_t icc;
    uint8_t control;
    /* DW4 */
    uint8_t aux[4];
} __attribute__((__packed__)) RegH2DFIS;

/**
 * Register host-to-device FIS structure, for NCQ commands.
 * Actually just a RegH2DFIS, but with fields repurposed.
 * Repurposed fields are annotated below.
 */
typedef struct NCQFIS {
    /* DW0 */
    uint8_t fis_type;
    uint8_t flags;
    uint8_t command;
    uint8_t sector_low; /* H2D: Feature 7:0 */
    /* DW1 */
    uint8_t lba_lo[3];
    uint8_t device;
    /* DW2 */
    uint8_t lba_hi[3];
    uint8_t sector_hi; /* H2D: Feature 15:8 */
    /* DW3 */
    uint8_t tag;       /* H2D: Count 0:7 */
    uint8_t prio;      /* H2D: Count 15:8 */
    uint8_t icc;
    uint8_t control;
    /* DW4 */
    uint8_t aux[4];
} __attribute__((__packed__)) NCQFIS;

/**
 * Command List entry structure.
 * The command list contains between 1-32 of these structures.
 */
typedef struct AHCICommandHeader {
    uint16_t flags; /* Cmd-Fis-Len, PMP#, and flags. */
    uint16_t prdtl; /* Phys Region Desc. Table Length */
    uint32_t prdbc; /* Phys Region Desc. Byte Count */
    uint64_t ctba;  /* Command Table Descriptor Base Address */
    uint32_t res[4];
} __attribute__((__packed__)) AHCICommandHeader;

/**
 * Physical Region Descriptor; pointed to by the Command List Header,
 * struct ahci_command.
 */
typedef struct PRD {
    uint64_t dba;  /* Data Base Address */
    uint32_t res;  /* Reserved */
    uint32_t dbc;  /* Data Byte Count (0-indexed) & Interrupt Flag (bit 2^31) */
} __attribute__((__packed__)) PRD;

/* Opaque, defined within ahci.c */
typedef struct AHCICommand AHCICommand;

/* Options to ahci_exec */
typedef struct AHCIOpts {
    size_t size;        /* Size of transfer */
    unsigned prd_size;  /* Size per-each PRD */
    bool set_bcl;       /* Override the default BCL of ATAPI_SECTOR_SIZE */
    unsigned bcl;       /* Byte Count Limit, for ATAPI PIO */
    uint64_t lba;       /* Starting LBA offset */
    uint64_t buffer;    /* Pointer to source or destination guest buffer */
    bool atapi;         /* ATAPI command? */
    bool atapi_dma;     /* Use DMA for ATAPI? */
    bool error;
    int (*pre_cb)(AHCIQState*, AHCICommand*, const struct AHCIOpts *);
    int (*mid_cb)(AHCIQState*, AHCICommand*, const struct AHCIOpts *);
    int (*post_cb)(AHCIQState*, AHCICommand*, const struct AHCIOpts *);
    void *opaque;
} AHCIOpts;

/*** Macro Utilities ***/
#define BITANY(data, mask) (((data) & (mask)) != 0)
#define BITSET(data, mask) (((data) & (mask)) == (mask))
#define BITCLR(data, mask) (((data) & (mask)) == 0)
#define ASSERT_BIT_SET(data, mask) g_assert_cmphex((data) & (mask), ==, (mask))
#define ASSERT_BIT_CLEAR(data, mask) g_assert_cmphex((data) & (mask), ==, 0)

/* For calculating how big the PRD table needs to be: */
#define CMD_TBL_SIZ(n) ((0x80 + ((n) * sizeof(PRD)) + 0x7F) & ~0x7F)

/* Helpers for reading/writing AHCI HBA register values */

static inline uint32_t ahci_mread(AHCIQState *ahci, size_t offset)
{
    return qpci_io_readl(ahci->dev, ahci->hba_bar, offset);
}

static inline void ahci_mwrite(AHCIQState *ahci, size_t offset, uint32_t value)
{
    qpci_io_writel(ahci->dev, ahci->hba_bar, offset, value);
}

static inline uint32_t ahci_rreg(AHCIQState *ahci, uint32_t reg_num)
{
    return ahci_mread(ahci, 4 * reg_num);
}

static inline void ahci_wreg(AHCIQState *ahci, uint32_t reg_num, uint32_t value)
{
    ahci_mwrite(ahci, 4 * reg_num, value);
}

static inline void ahci_set(AHCIQState *ahci, uint32_t reg_num, uint32_t mask)
{
    ahci_wreg(ahci, reg_num, ahci_rreg(ahci, reg_num) | mask);
}

static inline void ahci_clr(AHCIQState *ahci, uint32_t reg_num, uint32_t mask)
{
    ahci_wreg(ahci, reg_num, ahci_rreg(ahci, reg_num) & ~mask);
}

static inline size_t ahci_px_offset(uint8_t port, uint32_t reg_num)
{
    return AHCI_PORTS + (HBA_PORT_NUM_REG * port) + reg_num;
}

static inline uint32_t ahci_px_rreg(AHCIQState *ahci, uint8_t port,
                                    uint32_t reg_num)
{
    return ahci_rreg(ahci, ahci_px_offset(port, reg_num));
}

static inline void ahci_px_wreg(AHCIQState *ahci, uint8_t port,
                                uint32_t reg_num, uint32_t value)
{
    ahci_wreg(ahci, ahci_px_offset(port, reg_num), value);
}

static inline void ahci_px_set(AHCIQState *ahci, uint8_t port,
                               uint32_t reg_num, uint32_t mask)
{
    ahci_px_wreg(ahci, port, reg_num,
                 ahci_px_rreg(ahci, port, reg_num) | mask);
}

static inline void ahci_px_clr(AHCIQState *ahci, uint8_t port,
                               uint32_t reg_num, uint32_t mask)
{
    ahci_px_wreg(ahci, port, reg_num,
                 ahci_px_rreg(ahci, port, reg_num) & ~mask);
}

/*** Prototypes ***/
uint64_t ahci_alloc(AHCIQState *ahci, size_t bytes);
void ahci_free(AHCIQState *ahci, uint64_t addr);
void ahci_clean_mem(AHCIQState *ahci);

/* Device management */
QPCIDevice *get_ahci_device(QTestState *qts, uint32_t *fingerprint);
void free_ahci_device(QPCIDevice *dev);
void ahci_pci_enable(AHCIQState *ahci);
void start_ahci_device(AHCIQState *ahci);
void ahci_hba_enable(AHCIQState *ahci);

/* Port Management */
unsigned ahci_port_select(AHCIQState *ahci);
void ahci_port_clear(AHCIQState *ahci, uint8_t port);

/* Command header / table management */
unsigned ahci_pick_cmd(AHCIQState *ahci, uint8_t port);
void ahci_get_command_header(AHCIQState *ahci, uint8_t port,
                             uint8_t slot, AHCICommandHeader *cmd);
void ahci_set_command_header(AHCIQState *ahci, uint8_t port,
                             uint8_t slot, AHCICommandHeader *cmd);
void ahci_destroy_command(AHCIQState *ahci, uint8_t port, uint8_t slot);

/* AHCI sanity check routines */
void ahci_port_check_error(AHCIQState *ahci, uint8_t port,
                           uint32_t imask, uint8_t emask);
void ahci_port_check_interrupts(AHCIQState *ahci, uint8_t port,
                                uint32_t intr_mask);
void ahci_port_check_nonbusy(AHCIQState *ahci, uint8_t port, uint8_t slot);
void ahci_port_check_d2h_sanity(AHCIQState *ahci, uint8_t port, uint8_t slot);
void ahci_port_check_pio_sanity(AHCIQState *ahci, AHCICommand *cmd);
void ahci_port_check_cmd_sanity(AHCIQState *ahci, AHCICommand *cmd);

/* Misc */
bool is_atapi(AHCIQState *ahci, uint8_t port);
unsigned size_to_prdtl(unsigned bytes, unsigned bytes_per_prd);

/* Command: Macro level execution */
void ahci_guest_io(AHCIQState *ahci, uint8_t port, uint8_t ide_cmd,
                   uint64_t gbuffer, size_t size, uint64_t sector);
AHCICommand *ahci_guest_io_halt(AHCIQState *ahci, uint8_t port, uint8_t ide_cmd,
                                uint64_t gbuffer, size_t size, uint64_t sector);
void ahci_guest_io_resume(AHCIQState *ahci, AHCICommand *cmd);
void ahci_io(AHCIQState *ahci, uint8_t port, uint8_t ide_cmd,
             void *buffer, size_t bufsize, uint64_t sector);
void ahci_exec(AHCIQState *ahci, uint8_t port,
               uint8_t op, const AHCIOpts *opts);
void ahci_atapi_test_ready(AHCIQState *ahci, uint8_t port, bool ready,
                           uint8_t expected_sense);
void ahci_atapi_get_sense(AHCIQState *ahci, uint8_t port,
                          uint8_t *sense, uint8_t *asc);
void ahci_atapi_eject(AHCIQState *ahci, uint8_t port);
void ahci_atapi_load(AHCIQState *ahci, uint8_t port);

/* Command: Fine-grained lifecycle */
AHCICommand *ahci_command_create(uint8_t command_name);
AHCICommand *ahci_atapi_command_create(uint8_t scsi_cmd, uint16_t bcl, bool dma);
void ahci_command_commit(AHCIQState *ahci, AHCICommand *cmd, uint8_t port);
void ahci_command_issue(AHCIQState *ahci, AHCICommand *cmd);
void ahci_command_issue_async(AHCIQState *ahci, AHCICommand *cmd);
void ahci_command_wait(AHCIQState *ahci, AHCICommand *cmd);
void ahci_command_verify(AHCIQState *ahci, AHCICommand *cmd);
void ahci_command_free(AHCICommand *cmd);

/* Command: adjustments */
void ahci_command_set_flags(AHCICommand *cmd, uint16_t cmdh_flags);
void ahci_command_clr_flags(AHCICommand *cmd, uint16_t cmdh_flags);
void ahci_command_set_offset(AHCICommand *cmd, uint64_t lba_sect);
void ahci_command_set_buffer(AHCICommand *cmd, uint64_t buffer);
void ahci_command_set_size(AHCICommand *cmd, uint64_t xbytes);
void ahci_command_set_prd_size(AHCICommand *cmd, unsigned prd_size);
void ahci_command_set_sizes(AHCICommand *cmd, uint64_t xbytes,
                            unsigned prd_size);
void ahci_command_set_acmd(AHCICommand *cmd, void *acmd);
void ahci_command_enable_atapi_dma(AHCICommand *cmd);
void ahci_command_adjust(AHCICommand *cmd, uint64_t lba_sect, uint64_t gbuffer,
                         uint64_t xbytes, unsigned prd_size);

/* Command: Misc */
uint8_t ahci_command_slot(AHCICommand *cmd);
void ahci_write_fis(AHCIQState *ahci, AHCICommand *cmd);

#endif
