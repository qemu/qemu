/*
 * SD Association Host Standard Specification v2.0 controller emulation
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Mitsyanko Igor <i.mitsyanko@samsung.com>
 * Peter A.G. Crosthwaite <peter.crosthwaite@petalogix.com>
 *
 * Based on MMC controller for Samsung S5PC1xx-based board emulation
 * by Alexey Merkulov and Vladimir Monakhov.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SDHCI_H
#define SDHCI_H

#include "qemu-common.h"
#include "hw/sysbus.h"
#include "hw/sd.h"

/* R/W SDMA System Address register 0x0 */
#define SDHC_SYSAD                     0x00

/* R/W Host DMA Buffer Boundary and Transfer Block Size Register 0x0 */
#define SDHC_BLKSIZE                   0x04

/* R/W Blocks count for current transfer 0x0 */
#define SDHC_BLKCNT                    0x06

/* R/W Command Argument Register 0x0 */
#define SDHC_ARGUMENT                  0x08

/* R/W Transfer Mode Setting Register 0x0 */
#define SDHC_TRNMOD                    0x0C
#define SDHC_TRNS_DMA                  0x0001
#define SDHC_TRNS_BLK_CNT_EN           0x0002
#define SDHC_TRNS_ACMD12               0x0004
#define SDHC_TRNS_READ                 0x0010
#define SDHC_TRNS_MULTI                0x0020

/* R/W Command Register 0x0 */
#define SDHC_CMDREG                    0x0E
#define SDHC_CMD_RSP_WITH_BUSY         (3 << 0)
#define SDHC_CMD_DATA_PRESENT          (1 << 5)
#define SDHC_CMD_SUSPEND               (1 << 6)
#define SDHC_CMD_RESUME                (1 << 7)
#define SDHC_CMD_ABORT                 ((1 << 6)|(1 << 7))
#define SDHC_CMD_TYPE_MASK             ((1 << 6)|(1 << 7))
#define SDHC_COMMAND_TYPE(x)           ((x) & SDHC_CMD_TYPE_MASK)

/* ROC Response Register 0 0x0 */
#define SDHC_RSPREG0                   0x10
/* ROC Response Register 1 0x0 */
#define SDHC_RSPREG1                   0x14
/* ROC Response Register 2 0x0 */
#define SDHC_RSPREG2                   0x18
/* ROC Response Register 3 0x0 */
#define SDHC_RSPREG3                   0x1C

/* R/W Buffer Data Register 0x0 */
#define SDHC_BDATA                     0x20

/* R/ROC Present State Register 0x000A0000 */
#define SDHC_PRNSTS                    0x24
#define SDHC_CMD_INHIBIT               0x00000001
#define SDHC_DATA_INHIBIT              0x00000002
#define SDHC_DAT_LINE_ACTIVE           0x00000004
#define SDHC_DOING_WRITE               0x00000100
#define SDHC_DOING_READ                0x00000200
#define SDHC_SPACE_AVAILABLE           0x00000400
#define SDHC_DATA_AVAILABLE            0x00000800
#define SDHC_CARD_PRESENT              0x00010000
#define SDHC_CARD_DETECT               0x00040000
#define SDHC_WRITE_PROTECT             0x00080000
#define TRANSFERRING_DATA(x)           \
    ((x) & (SDHC_DOING_READ | SDHC_DOING_WRITE))

/* R/W Host control Register 0x0 */
#define SDHC_HOSTCTL                   0x28
#define SDHC_CTRL_DMA_CHECK_MASK       0x18
#define SDHC_CTRL_SDMA                 0x00
#define SDHC_CTRL_ADMA1_32             0x08
#define SDHC_CTRL_ADMA2_32             0x10
#define SDHC_CTRL_ADMA2_64             0x18
#define SDHC_DMA_TYPE(x)               ((x) & SDHC_CTRL_DMA_CHECK_MASK)

/* R/W Power Control Register 0x0 */
#define SDHC_PWRCON                    0x29
#define SDHC_POWER_ON                  (1 << 0)

/* R/W Block Gap Control Register 0x0 */
#define SDHC_BLKGAP                    0x2A
#define SDHC_STOP_AT_GAP_REQ           0x01
#define SDHC_CONTINUE_REQ              0x02

/* R/W WakeUp Control Register 0x0 */
#define SDHC_WAKCON                    0x2B
#define SDHC_WKUP_ON_INS               (1 << 1)
#define SDHC_WKUP_ON_RMV               (1 << 2)

/* CLKCON */
#define SDHC_CLKCON                    0x2C
#define SDHC_CLOCK_INT_STABLE          0x0002
#define SDHC_CLOCK_INT_EN              0x0001
#define SDHC_CLOCK_SDCLK_EN            (1 << 2)
#define SDHC_CLOCK_CHK_MASK            0x0007
#define SDHC_CLOCK_IS_ON(x)            \
    (((x) & SDHC_CLOCK_CHK_MASK) == SDHC_CLOCK_CHK_MASK)

/* R/W Timeout Control Register 0x0 */
#define SDHC_TIMEOUTCON                0x2E

/* R/W Software Reset Register 0x0 */
#define SDHC_SWRST                     0x2F
#define SDHC_RESET_ALL                 0x01
#define SDHC_RESET_CMD                 0x02
#define SDHC_RESET_DATA                0x04

/* ROC/RW1C Normal Interrupt Status Register 0x0 */
#define SDHC_NORINTSTS                 0x30
#define SDHC_NIS_ERR                   0x8000
#define SDHC_NIS_CMDCMP                0x0001
#define SDHC_NIS_TRSCMP                0x0002
#define SDHC_NIS_BLKGAP                0x0004
#define SDHC_NIS_DMA                   0x0008
#define SDHC_NIS_WBUFRDY               0x0010
#define SDHC_NIS_RBUFRDY               0x0020
#define SDHC_NIS_INSERT                0x0040
#define SDHC_NIS_REMOVE                0x0080
#define SDHC_NIS_CARDINT               0x0100

/* ROC/RW1C Error Interrupt Status Register 0x0 */
#define SDHC_ERRINTSTS                 0x32
#define SDHC_EIS_CMDTIMEOUT            0x0001
#define SDHC_EIS_BLKGAP                0x0004
#define SDHC_EIS_CMDIDX                0x0008
#define SDHC_EIS_CMD12ERR              0x0100
#define SDHC_EIS_ADMAERR               0x0200

/* R/W Normal Interrupt Status Enable Register 0x0 */
#define SDHC_NORINTSTSEN               0x34
#define SDHC_NISEN_CMDCMP              0x0001
#define SDHC_NISEN_TRSCMP              0x0002
#define SDHC_NISEN_DMA                 0x0008
#define SDHC_NISEN_WBUFRDY             0x0010
#define SDHC_NISEN_RBUFRDY             0x0020
#define SDHC_NISEN_INSERT              0x0040
#define SDHC_NISEN_REMOVE              0x0080
#define SDHC_NISEN_CARDINT             0x0100

/* R/W Error Interrupt Status Enable Register 0x0 */
#define SDHC_ERRINTSTSEN               0x36
#define SDHC_EISEN_CMDTIMEOUT          0x0001
#define SDHC_EISEN_BLKGAP              0x0004
#define SDHC_EISEN_CMDIDX              0x0008
#define SDHC_EISEN_ADMAERR             0x0200

/* R/W Normal Interrupt Signal Enable Register 0x0 */
#define SDHC_NORINTSIGEN               0x38
#define SDHC_NORINTSIG_INSERT          (1 << 6)
#define SDHC_NORINTSIG_REMOVE          (1 << 7)

/* R/W Error Interrupt Signal Enable Register 0x0 */
#define SDHC_ERRINTSIGEN               0x3A

/* ROC Auto CMD12 error status register 0x0 */
#define SDHC_ACMD12ERRSTS              0x3C

/* HWInit Capabilities Register 0x05E80080 */
#define SDHC_CAPAREG                   0x40
#define SDHC_CAN_DO_DMA                0x00400000
#define SDHC_CAN_DO_ADMA2              0x00080000
#define SDHC_CAN_DO_ADMA1              0x00100000
#define SDHC_64_BIT_BUS_SUPPORT        (1 << 28)
#define SDHC_CAPAB_BLOCKSIZE(x)        (((x) >> 16) & 0x3)

/* HWInit Maximum Current Capabilities Register 0x0 */
#define SDHC_MAXCURR                   0x48

/* W Force Event Auto CMD12 Error Interrupt Register 0x0000 */
#define SDHC_FEAER                     0x50
/* W Force Event Error Interrupt Register Error Interrupt 0x0000 */
#define SDHC_FEERR                     0x52

/* R/W ADMA Error Status Register 0x00 */
#define SDHC_ADMAERR                   0x54
#define SDHC_ADMAERR_LENGTH_MISMATCH   (1 << 2)
#define SDHC_ADMAERR_STATE_ST_STOP     (0 << 0)
#define SDHC_ADMAERR_STATE_ST_FDS      (1 << 0)
#define SDHC_ADMAERR_STATE_ST_TFR      (3 << 0)
#define SDHC_ADMAERR_STATE_MASK        (3 << 0)

/* R/W ADMA System Address Register 0x00 */
#define SDHC_ADMASYSADDR               0x58
#define SDHC_ADMA_ATTR_SET_LEN         (1 << 4)
#define SDHC_ADMA_ATTR_ACT_TRAN        (1 << 5)
#define SDHC_ADMA_ATTR_ACT_LINK        (3 << 4)
#define SDHC_ADMA_ATTR_INT             (1 << 2)
#define SDHC_ADMA_ATTR_END             (1 << 1)
#define SDHC_ADMA_ATTR_VALID           (1 << 0)
#define SDHC_ADMA_ATTR_ACT_MASK        ((1 << 4)|(1 << 5))

/* Slot interrupt status */
#define SDHC_SLOT_INT_STATUS            0xFC

/* HWInit Host Controller Version Register 0x0401 */
#define SDHC_HCVER                      0xFE
#define SD_HOST_SPECv2_VERS             0x2401

#define SDHC_REGISTERS_MAP_SIZE         0x100
#define SDHC_INSERTION_DELAY            (get_ticks_per_sec())
#define SDHC_TRANSFER_DELAY             100
#define SDHC_ADMA_DESCS_PER_DELAY       5
#define SDHC_CMD_RESPONSE               (3 << 0)

enum {
    sdhc_not_stopped = 0, /* normal SDHC state */
    sdhc_gap_read   = 1,  /* SDHC stopped at block gap during read operation */
    sdhc_gap_write  = 2   /* SDHC stopped at block gap during write operation */
};

/* SD/MMC host controller state */
typedef struct SDHCIState {
    SysBusDevice busdev;
    SDState *card;
    MemoryRegion iomem;

    QEMUTimer *insert_timer;       /* timer for 'changing' sd card. */
    QEMUTimer *transfer_timer;
    qemu_irq eject_cb;
    qemu_irq ro_cb;
    qemu_irq irq;

    uint32_t sdmasysad;    /* SDMA System Address register */
    uint16_t blksize;      /* Host DMA Buff Boundary and Transfer BlkSize Reg */
    uint16_t blkcnt;       /* Blocks count for current transfer */
    uint32_t argument;     /* Command Argument Register */
    uint16_t trnmod;       /* Transfer Mode Setting Register */
    uint16_t cmdreg;       /* Command Register */
    uint32_t rspreg[4];    /* Response Registers 0-3 */
    uint32_t prnsts;       /* Present State Register */
    uint8_t  hostctl;      /* Host Control Register */
    uint8_t  pwrcon;       /* Power control Register */
    uint8_t  blkgap;       /* Block Gap Control Register */
    uint8_t  wakcon;       /* WakeUp Control Register */
    uint16_t clkcon;       /* Clock control Register */
    uint8_t  timeoutcon;   /* Timeout Control Register */
    uint8_t  admaerr;      /* ADMA Error Status Register */
    uint16_t norintsts;    /* Normal Interrupt Status Register */
    uint16_t errintsts;    /* Error Interrupt Status Register */
    uint16_t norintstsen;  /* Normal Interrupt Status Enable Register */
    uint16_t errintstsen;  /* Error Interrupt Status Enable Register */
    uint16_t norintsigen;  /* Normal Interrupt Signal Enable Register */
    uint16_t errintsigen;  /* Error Interrupt Signal Enable Register */
    uint16_t acmd12errsts; /* Auto CMD12 error status register */
    uint64_t admasysaddr;  /* ADMA System Address Register */

    uint32_t capareg;      /* Capabilities Register */
    uint32_t maxcurr;      /* Maximum Current Capabilities Register */
    uint8_t  *fifo_buffer; /* SD host i/o FIFO buffer */
    uint32_t buf_maxsz;
    uint16_t data_count;   /* current element in FIFO buffer */
    uint8_t  stopped_state;/* Current SDHC state */
    /* Buffer Data Port Register - virtual access point to R and W buffers */
    /* Software Reset Register - always reads as 0 */
    /* Force Event Auto CMD12 Error Interrupt Reg - write only */
    /* Force Event Error Interrupt Register- write only */
    /* RO Host Controller Version Register always reads as 0x2401 */
} SDHCIState;

typedef struct SDHCIClass {
    SysBusDeviceClass busdev_class;

    void (*reset)(SDHCIState *s);
    uint32_t (*mem_read)(SDHCIState *s, unsigned int offset, unsigned size);
    void (*mem_write)(SDHCIState *s, unsigned int offset, uint32_t value,
            unsigned size);
    void (*send_command)(SDHCIState *s);
    bool (*can_issue_command)(SDHCIState *s);
    void (*data_transfer)(SDHCIState *s);
    void (*end_data_transfer)(SDHCIState *s);
    void (*do_sdma_single)(SDHCIState *s);
    void (*do_sdma_multi)(SDHCIState *s);
    void (*do_adma)(SDHCIState *s);
    void (*read_block_from_card)(SDHCIState *s);
    void (*write_block_to_card)(SDHCIState *s);
    uint32_t (*bdata_read)(SDHCIState *s, unsigned size);
    void (*bdata_write)(SDHCIState *s, uint32_t value, unsigned size);
} SDHCIClass;

extern const VMStateDescription sdhci_vmstate;

#define TYPE_SDHCI            "generic-sdhci"
#define SDHCI(obj)            \
     OBJECT_CHECK(SDHCIState, (obj), TYPE_SDHCI)
#define SDHCI_CLASS(klass)    \
     OBJECT_CLASS_CHECK(SDHCIClass, (klass), TYPE_SDHCI)
#define SDHCI_GET_CLASS(obj)  \
     OBJECT_GET_CLASS(SDHCIClass, (obj), TYPE_SDHCI)

#endif /* SDHCI_H */
