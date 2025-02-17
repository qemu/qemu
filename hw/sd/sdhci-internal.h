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
#ifndef SDHCI_INTERNAL_H
#define SDHCI_INTERNAL_H

#include "hw/registerfields.h"

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
#define SDHC_TRNS_ACMD23               0x0008 /* since v3 */
#define SDHC_TRNS_READ                 0x0010
#define SDHC_TRNS_MULTI                0x0020
#define SDHC_TRNMOD_MASK               0x0037

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
#define SDHC_IMX_CLOCK_GATE_OFF        0x00000080
#define SDHC_DOING_WRITE               0x00000100
#define SDHC_DOING_READ                0x00000200
#define SDHC_SPACE_AVAILABLE           0x00000400
#define SDHC_DATA_AVAILABLE            0x00000800
#define SDHC_CARD_PRESENT              0x00010000
#define SDHC_CARD_DETECT               0x00040000
#define SDHC_WRITE_PROTECT             0x00080000
FIELD(SDHC_PRNSTS, DAT_LVL,            20, 4);
FIELD(SDHC_PRNSTS, CMD_LVL,            24, 1);
#define TRANSFERRING_DATA(x)           \
    ((x) & (SDHC_DOING_READ | SDHC_DOING_WRITE))

/* R/W Host control Register 0x0 */
#define SDHC_HOSTCTL                   0x28
#define SDHC_CTRL_LED                  0x01
#define SDHC_CTRL_DATATRANSFERWIDTH    0x02 /* SD mode only */
#define SDHC_CTRL_HIGH_SPEED           0x04
#define SDHC_CTRL_DMA_CHECK_MASK       0x18
#define SDHC_CTRL_SDMA                 0x00
#define SDHC_CTRL_ADMA1_32             0x08 /* NOT ALLOWED since v2 */
#define SDHC_CTRL_ADMA2_32             0x10
#define SDHC_CTRL_ADMA2_64             0x18
#define SDHC_DMA_TYPE(x)               ((x) & SDHC_CTRL_DMA_CHECK_MASK)
#define SDHC_CTRL_4BITBUS              0x02
#define SDHC_CTRL_8BITBUS              0x20
#define SDHC_CTRL_CDTEST_INS           0x40
#define SDHC_CTRL_CDTEST_EN            0x80

/* R/W Power Control Register 0x0 */
#define SDHC_PWRCON                    0x29
#define SDHC_POWER_ON                  (1 << 0)
FIELD(SDHC_PWRCON, BUS_VOLTAGE,        1, 3);

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
FIELD(SDHC_TIMEOUTCON, COUNTER,        0, 4);

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
FIELD(SDHC_ACMD12ERRSTS, TIMEOUT_ERR,  1, 1);
FIELD(SDHC_ACMD12ERRSTS, CRC_ERR,      2, 1);
FIELD(SDHC_ACMD12ERRSTS, INDEX_ERR,    4, 1);

/* Host Control Register 2 (since v3) */
#define SDHC_HOSTCTL2                  0x3E
FIELD(SDHC_HOSTCTL2, UHS_MODE_SEL,     0, 3);
FIELD(SDHC_HOSTCTL2, V18_ENA,          3, 1); /* UHS-I only */
FIELD(SDHC_HOSTCTL2, DRIVER_STRENGTH,  4, 2); /* UHS-I only */
FIELD(SDHC_HOSTCTL2, EXECUTE_TUNING,   6, 1); /* UHS-I only */
FIELD(SDHC_HOSTCTL2, SAMPLING_CLKSEL,  7, 1); /* UHS-I only */
FIELD(SDHC_HOSTCTL2, UHS_II_ENA,       8, 1); /* since v4 */
FIELD(SDHC_HOSTCTL2, ADMA2_LENGTH,    10, 1); /* since v4 */
FIELD(SDHC_HOSTCTL2, CMD23_ENA,       11, 1); /* since v4 */
FIELD(SDHC_HOSTCTL2, VERSION4,        12, 1); /* since v4 */
FIELD(SDHC_HOSTCTL2, ASYNC_INT,       14, 1);
FIELD(SDHC_HOSTCTL2, PRESET_ENA,      15, 1);

/* HWInit Capabilities Register 0x05E80080 */
#define SDHC_CAPAB                     0x40
FIELD(SDHC_CAPAB, TOCLKFREQ,           0, 6);
FIELD(SDHC_CAPAB, TOUNIT,              7, 1);
FIELD(SDHC_CAPAB, BASECLKFREQ,         8, 8);
FIELD(SDHC_CAPAB, MAXBLOCKLENGTH,     16, 2);
FIELD(SDHC_CAPAB, EMBEDDED_8BIT,      18, 1); /* since v3 */
FIELD(SDHC_CAPAB, ADMA2,              19, 1); /* since v2 */
FIELD(SDHC_CAPAB, ADMA1,              20, 1); /* v1 only? */
FIELD(SDHC_CAPAB, HIGHSPEED,          21, 1);
FIELD(SDHC_CAPAB, SDMA,               22, 1);
FIELD(SDHC_CAPAB, SUSPRESUME,         23, 1);
FIELD(SDHC_CAPAB, V33,                24, 1);
FIELD(SDHC_CAPAB, V30,                25, 1);
FIELD(SDHC_CAPAB, V18,                26, 1);
FIELD(SDHC_CAPAB, BUS64BIT_V4,        27, 1); /* since v4.10 */
FIELD(SDHC_CAPAB, BUS64BIT,           28, 1); /* since v2 */
FIELD(SDHC_CAPAB, ASYNC_INT,          29, 1); /* since v3 */
FIELD(SDHC_CAPAB, SLOT_TYPE,          30, 2); /* since v3 */
FIELD(SDHC_CAPAB, BUS_SPEED,          32, 3); /* since v3 */
FIELD(SDHC_CAPAB, UHS_II,             35, 8); /* since v4.20 */
FIELD(SDHC_CAPAB, DRIVER_STRENGTH,    36, 3); /* since v3 */
FIELD(SDHC_CAPAB, DRIVER_TYPE_A,      36, 1); /* since v3 */
FIELD(SDHC_CAPAB, DRIVER_TYPE_C,      37, 1); /* since v3 */
FIELD(SDHC_CAPAB, DRIVER_TYPE_D,      38, 1); /* since v3 */
FIELD(SDHC_CAPAB, TIMER_RETUNING,     40, 4); /* since v3 */
FIELD(SDHC_CAPAB, SDR50_TUNING,       45, 1); /* since v3 */
FIELD(SDHC_CAPAB, RETUNING_MODE,      46, 2); /* since v3 */
FIELD(SDHC_CAPAB, CLOCK_MULT,         48, 8); /* since v3 */
FIELD(SDHC_CAPAB, ADMA3,              59, 1); /* since v4.20 */
FIELD(SDHC_CAPAB, V18_VDD2,           60, 1); /* since v4.20 */

/* HWInit Maximum Current Capabilities Register 0x0 */
#define SDHC_MAXCURR                   0x48
FIELD(SDHC_MAXCURR, V33_VDD1,          0, 8);
FIELD(SDHC_MAXCURR, V30_VDD1,          8, 8);
FIELD(SDHC_MAXCURR, V18_VDD1,         16, 8);
FIELD(SDHC_MAXCURR, V18_VDD2,         32, 8); /* since v4.20 */

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

/* HWInit Host Controller Version Register */
#define SDHC_HCVER                      0xFE
#define SDHC_HCVER_VENDOR               0x24

#define SDHC_REGISTERS_MAP_SIZE         0x100
#define SDHC_INSERTION_DELAY            (NANOSECONDS_PER_SECOND)
#define SDHC_TRANSFER_DELAY             100
#define SDHC_ADMA_DESCS_PER_DELAY       5
#define SDHC_CMD_RESPONSE               (3 << 0)

enum {
    sdhc_not_stopped = 0, /* normal SDHC state */
    sdhc_gap_read   = 1,  /* SDHC stopped at block gap during read operation */
    sdhc_gap_write  = 2   /* SDHC stopped at block gap during write operation */
};

extern const VMStateDescription sdhci_vmstate;

/*
 * Default SD/MMC host controller features information, which will be
 * presented in CAPABILITIES register of generic SD host controller at reset.
 *
 * support:
 * - 3.3v and 1.8v voltages
 * - SDMA/ADMA1/ADMA2
 * - high-speed
 * max host controller R/W buffers size: 512B
 * max clock frequency for SDclock: 52 MHz
 * timeout clock frequency: 52 MHz
 *
 * does not support:
 * - 3.0v voltage
 * - 64-bit system bus
 * - suspend/resume
 */
#define SDHC_CAPAB_REG_DEFAULT 0x057834b4

#define DEFINE_SDHCI_COMMON_PROPERTIES(_state) \
    DEFINE_PROP_UINT8("endianness", _state, endianness, DEVICE_LITTLE_ENDIAN), \
    DEFINE_PROP_UINT8("sd-spec-version", _state, sd_spec_version, 2), \
    DEFINE_PROP_UINT8("uhs", _state, uhs_mode, UHS_NOT_SUPPORTED), \
    DEFINE_PROP_UINT8("vendor", _state, vendor, SDHCI_VENDOR_NONE), \
    \
    /* Capabilities registers provide information on supported
     * features of this specific host controller implementation */ \
    DEFINE_PROP_UINT64("capareg", _state, capareg, SDHC_CAPAB_REG_DEFAULT), \
    DEFINE_PROP_UINT64("maxcurr", _state, maxcurr, 0)

void sdhci_initfn(SDHCIState *s);
void sdhci_uninitfn(SDHCIState *s);
void sdhci_common_realize(SDHCIState *s, Error **errp);
void sdhci_common_unrealize(SDHCIState *s);
void sdhci_common_class_init(ObjectClass *klass, const void *data);

#endif
