/*
 * SD Memory Card emulation as defined in the "SD Memory Card Physical
 * layer specification, Version 2.00."
 *
 * Copyright (c) 2006 Andrzej Zaborowski  <balrog@zabor.org>
 * Copyright (c) 2007 CodeSourcery
 * Copyright (c) 2018 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "hw/irq.h"
#include "hw/registerfields.h"
#include "sysemu/block-backend.h"
#include "hw/sd/sd.h"
#include "hw/sd/sdcard_legacy.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitmap.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "sdmmc-internal.h"
#include "trace.h"

//#define DEBUG_SD 1

#define SDSC_MAX_CAPACITY   (2 * GiB)

#define INVALID_ADDRESS     UINT32_MAX

typedef enum {
    sd_r0 = 0,    /* no response */
    sd_r1,        /* normal response command */
    sd_r2_i,      /* CID register */
    sd_r2_s,      /* CSD register */
    sd_r3,        /* OCR register */
    sd_r6 = 6,    /* Published RCA response */
    sd_r7,        /* Operating voltage */
    sd_r1b = -1,
    sd_illegal = -2,
} sd_rsp_type_t;

enum SDCardModes {
    sd_inactive,
    sd_card_identification_mode,
    sd_data_transfer_mode,
};

enum SDCardStates {
    sd_inactive_state = -1,
    sd_idle_state = 0,
    sd_ready_state,
    sd_identification_state,
    sd_standby_state,
    sd_transfer_state,
    sd_sendingdata_state,
    sd_receivingdata_state,
    sd_programming_state,
    sd_disconnect_state,
};

typedef sd_rsp_type_t (*sd_cmd_handler)(SDState *sd, SDRequest req);

typedef struct SDProto {
    const char *name;
    sd_cmd_handler cmd[SDMMC_CMD_MAX];
    sd_cmd_handler acmd[SDMMC_CMD_MAX];
} SDProto;

struct SDState {
    DeviceState parent_obj;

    /* If true, created by sd_init() for a non-qdevified caller */
    /* TODO purge them with fire */
    bool me_no_qdev_me_kill_mammoth_with_rocks;

    /* SD Memory Card Registers */
    uint32_t ocr;
    uint8_t scr[8];
    uint8_t cid[16];
    uint8_t csd[16];
    uint16_t rca;
    uint32_t card_status;
    uint8_t sd_status[64];

    /* Static properties */

    uint8_t spec_version;
    BlockBackend *blk;

    /* Runtime changeables */

    uint32_t mode;    /* current card mode, one of SDCardModes */
    int32_t state;    /* current card state, one of SDCardStates */
    uint32_t vhs;
    bool wp_switch;
    unsigned long *wp_group_bmap;
    int32_t wp_group_bits;
    uint64_t size;
    uint32_t blk_len;
    uint32_t multi_blk_cnt;
    uint32_t erase_start;
    uint32_t erase_end;
    uint8_t pwd[16];
    uint32_t pwd_len;
    uint8_t function_group[6];
    uint8_t current_cmd;
    /* True if we will handle the next command as an ACMD. Note that this does
     * *not* track the APP_CMD status bit!
     */
    bool expecting_acmd;
    uint32_t blk_written;
    uint64_t data_start;
    uint32_t data_offset;
    uint8_t data[512];
    qemu_irq readonly_cb;
    qemu_irq inserted_cb;
    QEMUTimer *ocr_power_timer;
    bool enable;
    uint8_t dat_lines;
    bool cmd_line;
};

static void sd_realize(DeviceState *dev, Error **errp);

static const struct SDProto *sd_proto(SDState *sd)
{
    SDCardClass *sc = SD_CARD_GET_CLASS(sd);

    return sc->proto;
}

static const SDProto sd_proto_spi;

static bool sd_is_spi(SDState *sd)
{
    return sd_proto(sd) == &sd_proto_spi;
}

static const char *sd_version_str(enum SDPhySpecificationVersion version)
{
    static const char *sdphy_version[] = {
        [SD_PHY_SPECv1_10_VERS]     = "v1.10",
        [SD_PHY_SPECv2_00_VERS]     = "v2.00",
        [SD_PHY_SPECv3_01_VERS]     = "v3.01",
    };
    if (version >= ARRAY_SIZE(sdphy_version)) {
        return "unsupported version";
    }
    return sdphy_version[version];
}

static const char *sd_state_name(enum SDCardStates state)
{
    static const char *state_name[] = {
        [sd_idle_state]             = "idle",
        [sd_ready_state]            = "ready",
        [sd_identification_state]   = "identification",
        [sd_standby_state]          = "standby",
        [sd_transfer_state]         = "transfer",
        [sd_sendingdata_state]      = "sendingdata",
        [sd_receivingdata_state]    = "receivingdata",
        [sd_programming_state]      = "programming",
        [sd_disconnect_state]       = "disconnect",
    };
    if (state == sd_inactive_state) {
        return "inactive";
    }
    assert(state < ARRAY_SIZE(state_name));
    return state_name[state];
}

static const char *sd_response_name(sd_rsp_type_t rsp)
{
    static const char *response_name[] = {
        [sd_r0]     = "RESP#0 (no response)",
        [sd_r1]     = "RESP#1 (normal cmd)",
        [sd_r2_i]   = "RESP#2 (CID reg)",
        [sd_r2_s]   = "RESP#2 (CSD reg)",
        [sd_r3]     = "RESP#3 (OCR reg)",
        [sd_r6]     = "RESP#6 (RCA)",
        [sd_r7]     = "RESP#7 (operating voltage)",
    };
    if (rsp == sd_illegal) {
        return "ILLEGAL RESP";
    }
    if (rsp == sd_r1b) {
        rsp = sd_r1;
    }
    assert(rsp < ARRAY_SIZE(response_name));
    return response_name[rsp];
}

static uint8_t sd_get_dat_lines(SDState *sd)
{
    return sd->enable ? sd->dat_lines : 0;
}

static bool sd_get_cmd_line(SDState *sd)
{
    return sd->enable ? sd->cmd_line : false;
}

static void sd_set_voltage(SDState *sd, uint16_t millivolts)
{
    trace_sdcard_set_voltage(millivolts);

    switch (millivolts) {
    case 3001 ... 3600: /* SD_VOLTAGE_3_3V */
    case 2001 ... 3000: /* SD_VOLTAGE_3_0V */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "SD card voltage not supported: %.3fV",
                      millivolts / 1000.f);
    }
}

static void sd_set_mode(SDState *sd)
{
    switch (sd->state) {
    case sd_inactive_state:
        sd->mode = sd_inactive;
        break;

    case sd_idle_state:
    case sd_ready_state:
    case sd_identification_state:
        sd->mode = sd_card_identification_mode;
        break;

    case sd_standby_state:
    case sd_transfer_state:
    case sd_sendingdata_state:
    case sd_receivingdata_state:
    case sd_programming_state:
    case sd_disconnect_state:
        sd->mode = sd_data_transfer_mode;
        break;
    }
}

static const sd_cmd_type_t sd_cmd_type[SDMMC_CMD_MAX] = {
    sd_bc,   sd_none, sd_bcr,  sd_bcr,  sd_none, sd_none, sd_none, sd_ac,
    sd_bcr,  sd_ac,   sd_ac,   sd_adtc, sd_ac,   sd_ac,   sd_none, sd_ac,
    /* 16 */
    sd_ac,   sd_adtc, sd_adtc, sd_none, sd_none, sd_none, sd_none, sd_none,
    sd_adtc, sd_adtc, sd_adtc, sd_adtc, sd_ac,   sd_ac,   sd_adtc, sd_none,
    /* 32 */
    sd_ac,   sd_ac,   sd_none, sd_none, sd_none, sd_none, sd_ac,   sd_none,
    sd_none, sd_none, sd_bc,   sd_none, sd_none, sd_none, sd_none, sd_none,
    /* 48 */
    sd_none, sd_none, sd_none, sd_none, sd_none, sd_none, sd_none, sd_ac,
    sd_adtc, sd_none, sd_none, sd_none, sd_none, sd_none, sd_none, sd_none,
};

static const int sd_cmd_class[SDMMC_CMD_MAX] = {
    0,  0,  0,  0,  0,  9, 10,  0,  0,  0,  0,  1,  0,  0,  0,  0,
    2,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,  4,  6,  6,  6,  6,
    5,  5, 10, 10, 10, 10,  5,  9,  9,  9,  7,  7,  7,  7,  7,  7,
    7,  7, 10,  7,  9,  9,  9,  8,  8, 10,  8,  8,  8,  8,  8,  8,
};

static uint8_t sd_crc7(const void *message, size_t width)
{
    int i, bit;
    uint8_t shift_reg = 0x00;
    const uint8_t *msg = (const uint8_t *)message;

    for (i = 0; i < width; i ++, msg ++)
        for (bit = 7; bit >= 0; bit --) {
            shift_reg <<= 1;
            if ((shift_reg >> 7) ^ ((*msg >> bit) & 1))
                shift_reg ^= 0x89;
        }

    return shift_reg;
}

#define OCR_POWER_DELAY_NS      500000 /* 0.5ms */

FIELD(OCR, VDD_VOLTAGE_WINDOW,          0, 24)
FIELD(OCR, VDD_VOLTAGE_WIN_LO,          0,  8)
FIELD(OCR, DUAL_VOLTAGE_CARD,           7,  1)
FIELD(OCR, VDD_VOLTAGE_WIN_HI,          8, 16)
FIELD(OCR, ACCEPT_SWITCH_1V8,          24,  1) /* Only UHS-I */
FIELD(OCR, UHS_II_CARD,                29,  1) /* Only UHS-II */
FIELD(OCR, CARD_CAPACITY,              30,  1) /* 0:SDSC, 1:SDHC/SDXC */
FIELD(OCR, CARD_POWER_UP,              31,  1)

#define ACMD41_ENQUIRY_MASK     0x00ffffff
#define ACMD41_R3_MASK          (R_OCR_VDD_VOLTAGE_WIN_HI_MASK \
                               | R_OCR_ACCEPT_SWITCH_1V8_MASK \
                               | R_OCR_UHS_II_CARD_MASK \
                               | R_OCR_CARD_CAPACITY_MASK \
                               | R_OCR_CARD_POWER_UP_MASK)

static void sd_ocr_powerup(void *opaque)
{
    SDState *sd = opaque;

    trace_sdcard_powerup();
    assert(!FIELD_EX32(sd->ocr, OCR, CARD_POWER_UP));

    /* card power-up OK */
    sd->ocr = FIELD_DP32(sd->ocr, OCR, CARD_POWER_UP, 1);

    if (sd->size > SDSC_MAX_CAPACITY) {
        sd->ocr = FIELD_DP32(sd->ocr, OCR, CARD_CAPACITY, 1);
    }
}

static void sd_set_ocr(SDState *sd)
{
    /* All voltages OK */
    sd->ocr = R_OCR_VDD_VOLTAGE_WIN_HI_MASK;

    if (sd_is_spi(sd)) {
        /*
         * We don't need to emulate power up sequence in SPI-mode.
         * Thus, the card's power up status bit should be set to 1 when reset.
         * The card's capacity status bit should also be set if SD card size
         * is larger than 2GB for SDHC support.
         */
        sd_ocr_powerup(sd);
    }
}

static void sd_set_scr(SDState *sd)
{
    sd->scr[0] = 0 << 4;        /* SCR structure version 1.0 */
    if (sd->spec_version == SD_PHY_SPECv1_10_VERS) {
        sd->scr[0] |= 1;        /* Spec Version 1.10 */
    } else {
        sd->scr[0] |= 2;        /* Spec Version 2.00 or Version 3.0X */
    }
    sd->scr[1] = (2 << 4)       /* SDSC Card (Security Version 1.01) */
                 | 0b0101;      /* 1-bit or 4-bit width bus modes */
    sd->scr[2] = 0x00;          /* Extended Security is not supported. */
    if (sd->spec_version >= SD_PHY_SPECv3_01_VERS) {
        sd->scr[2] |= 1 << 7;   /* Spec Version 3.0X */
    }
    sd->scr[3] = 0x00;
    /* reserved for manufacturer usage */
    sd->scr[4] = 0x00;
    sd->scr[5] = 0x00;
    sd->scr[6] = 0x00;
    sd->scr[7] = 0x00;
}

#define MID     0xaa
#define OID     "XY"
#define PNM     "QEMU!"
#define PRV     0x01
#define MDT_YR  2006
#define MDT_MON 2

static void sd_set_cid(SDState *sd)
{
    sd->cid[0] = MID;       /* Fake card manufacturer ID (MID) */
    sd->cid[1] = OID[0];    /* OEM/Application ID (OID) */
    sd->cid[2] = OID[1];
    sd->cid[3] = PNM[0];    /* Fake product name (PNM) */
    sd->cid[4] = PNM[1];
    sd->cid[5] = PNM[2];
    sd->cid[6] = PNM[3];
    sd->cid[7] = PNM[4];
    sd->cid[8] = PRV;       /* Fake product revision (PRV) */
    sd->cid[9] = 0xde;      /* Fake serial number (PSN) */
    sd->cid[10] = 0xad;
    sd->cid[11] = 0xbe;
    sd->cid[12] = 0xef;
    sd->cid[13] = 0x00 |    /* Manufacture date (MDT) */
        ((MDT_YR - 2000) / 10);
    sd->cid[14] = ((MDT_YR % 10) << 4) | MDT_MON;
    sd->cid[15] = (sd_crc7(sd->cid, 15) << 1) | 1;
}

#define HWBLOCK_SHIFT   9        /* 512 bytes */
#define SECTOR_SHIFT    5        /* 16 kilobytes */
#define WPGROUP_SHIFT   7        /* 2 megs */
#define CMULT_SHIFT     9        /* 512 times HWBLOCK_SIZE */
#define WPGROUP_SIZE    (1 << (HWBLOCK_SHIFT + SECTOR_SHIFT + WPGROUP_SHIFT))

static const uint8_t sd_csd_rw_mask[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0xfe,
};

static void sd_set_csd(SDState *sd, uint64_t size)
{
    int hwblock_shift = HWBLOCK_SHIFT;
    uint32_t csize;
    uint32_t sectsize = (1 << (SECTOR_SHIFT + 1)) - 1;
    uint32_t wpsize = (1 << (WPGROUP_SHIFT + 1)) - 1;

    /* To indicate 2 GiB card, BLOCK_LEN shall be 1024 bytes */
    if (size == SDSC_MAX_CAPACITY) {
        hwblock_shift += 1;
    }
    csize = (size >> (CMULT_SHIFT + hwblock_shift)) - 1;

    if (size <= SDSC_MAX_CAPACITY) { /* Standard Capacity SD */
        sd->csd[0] = 0x00;      /* CSD structure */
        sd->csd[1] = 0x26;      /* Data read access-time-1 */
        sd->csd[2] = 0x00;      /* Data read access-time-2 */
        sd->csd[3] = 0x32;      /* Max. data transfer rate: 25 MHz */
        sd->csd[4] = 0x5f;      /* Card Command Classes */
        sd->csd[5] = 0x50 |     /* Max. read data block length */
            hwblock_shift;
        sd->csd[6] = 0xe0 |     /* Partial block for read allowed */
            ((csize >> 10) & 0x03);
        sd->csd[7] = 0x00 |     /* Device size */
            ((csize >> 2) & 0xff);
        sd->csd[8] = 0x3f |     /* Max. read current */
            ((csize << 6) & 0xc0);
        sd->csd[9] = 0xfc |     /* Max. write current */
            ((CMULT_SHIFT - 2) >> 1);
        sd->csd[10] = 0x40 |    /* Erase sector size */
            (((CMULT_SHIFT - 2) << 7) & 0x80) | (sectsize >> 1);
        sd->csd[11] = 0x00 |    /* Write protect group size */
            ((sectsize << 7) & 0x80) | wpsize;
        sd->csd[12] = 0x90 |    /* Write speed factor */
            (hwblock_shift >> 2);
        sd->csd[13] = 0x20 |    /* Max. write data block length */
            ((hwblock_shift << 6) & 0xc0);
        sd->csd[14] = 0x00;     /* File format group */
    } else {                    /* SDHC */
        size /= 512 * KiB;
        size -= 1;
        sd->csd[0] = 0x40;
        sd->csd[1] = 0x0e;
        sd->csd[2] = 0x00;
        sd->csd[3] = 0x32;
        sd->csd[4] = 0x5b;
        sd->csd[5] = 0x59;
        sd->csd[6] = 0x00;
        sd->csd[7] = (size >> 16) & 0xff;
        sd->csd[8] = (size >> 8) & 0xff;
        sd->csd[9] = (size & 0xff);
        sd->csd[10] = 0x7f;
        sd->csd[11] = 0x80;
        sd->csd[12] = 0x0a;
        sd->csd[13] = 0x40;
        sd->csd[14] = 0x00;
    }
    sd->csd[15] = (sd_crc7(sd->csd, 15) << 1) | 1;
}

static void sd_set_rca(SDState *sd)
{
    sd->rca += 0x4567;
}

FIELD(CSR, AKE_SEQ_ERROR,               3,  1)
FIELD(CSR, APP_CMD,                     5,  1)
FIELD(CSR, FX_EVENT,                    6,  1)
FIELD(CSR, READY_FOR_DATA,              8,  1)
FIELD(CSR, CURRENT_STATE,               9,  4)
FIELD(CSR, ERASE_RESET,                13,  1)
FIELD(CSR, CARD_ECC_DISABLED,          14,  1)
FIELD(CSR, WP_ERASE_SKIP,              15,  1)
FIELD(CSR, CSD_OVERWRITE,              16,  1)
FIELD(CSR, DEFERRED_RESPONSE,          17,  1)
FIELD(CSR, ERROR,                      19,  1)
FIELD(CSR, CC_ERROR,                   20,  1)
FIELD(CSR, CARD_ECC_FAILED,            21,  1)
FIELD(CSR, ILLEGAL_COMMAND,            22,  1)
FIELD(CSR, COM_CRC_ERROR,              23,  1)
FIELD(CSR, LOCK_UNLOCK_FAILED,         24,  1)
FIELD(CSR, CARD_IS_LOCKED,             25,  1)
FIELD(CSR, WP_VIOLATION,               26,  1)
FIELD(CSR, ERASE_PARAM,                27,  1)
FIELD(CSR, ERASE_SEQ_ERROR,            28,  1)
FIELD(CSR, BLOCK_LEN_ERROR,            29,  1)
FIELD(CSR, ADDRESS_ERROR,              30,  1)
FIELD(CSR, OUT_OF_RANGE,               31,  1)

/* Card status bits, split by clear condition:
 * A : According to the card current state
 * B : Always related to the previous command
 * C : Cleared by read
 */
#define CARD_STATUS_A           (R_CSR_READY_FOR_DATA_MASK \
                               | R_CSR_CARD_ECC_DISABLED_MASK \
                               | R_CSR_CARD_IS_LOCKED_MASK)
#define CARD_STATUS_B           (R_CSR_CURRENT_STATE_MASK \
                               | R_CSR_ILLEGAL_COMMAND_MASK \
                               | R_CSR_COM_CRC_ERROR_MASK)
#define CARD_STATUS_C           (R_CSR_AKE_SEQ_ERROR_MASK \
                               | R_CSR_APP_CMD_MASK \
                               | R_CSR_ERASE_RESET_MASK \
                               | R_CSR_WP_ERASE_SKIP_MASK \
                               | R_CSR_CSD_OVERWRITE_MASK \
                               | R_CSR_ERROR_MASK \
                               | R_CSR_CC_ERROR_MASK \
                               | R_CSR_CARD_ECC_FAILED_MASK \
                               | R_CSR_LOCK_UNLOCK_FAILED_MASK \
                               | R_CSR_WP_VIOLATION_MASK \
                               | R_CSR_ERASE_PARAM_MASK \
                               | R_CSR_ERASE_SEQ_ERROR_MASK \
                               | R_CSR_BLOCK_LEN_ERROR_MASK \
                               | R_CSR_ADDRESS_ERROR_MASK \
                               | R_CSR_OUT_OF_RANGE_MASK)

static void sd_set_cardstatus(SDState *sd)
{
    sd->card_status = 0x00000100;
}

static void sd_set_sdstatus(SDState *sd)
{
    memset(sd->sd_status, 0, 64);
}

static int sd_req_crc_validate(SDRequest *req)
{
    uint8_t buffer[5];
    buffer[0] = 0x40 | req->cmd;
    stl_be_p(&buffer[1], req->arg);
    return 0;
    return sd_crc7(buffer, 5) != req->crc;  /* TODO */
}

static void sd_response_r1_make(SDState *sd, uint8_t *response)
{
    stl_be_p(response, sd->card_status);

    /* Clear the "clear on read" status bits */
    sd->card_status &= ~CARD_STATUS_C;
}

static void sd_response_r3_make(SDState *sd, uint8_t *response)
{
    stl_be_p(response, sd->ocr & ACMD41_R3_MASK);
}

static void sd_response_r6_make(SDState *sd, uint8_t *response)
{
    uint16_t status;

    status = ((sd->card_status >> 8) & 0xc000) |
             ((sd->card_status >> 6) & 0x2000) |
              (sd->card_status & 0x1fff);
    sd->card_status &= ~(CARD_STATUS_C & 0xc81fff);
    stw_be_p(response + 0, sd->rca);
    stw_be_p(response + 2, status);
}

static void sd_response_r7_make(SDState *sd, uint8_t *response)
{
    stl_be_p(response, sd->vhs);
}

static inline uint64_t sd_addr_to_wpnum(uint64_t addr)
{
    return addr >> (HWBLOCK_SHIFT + SECTOR_SHIFT + WPGROUP_SHIFT);
}

static void sd_reset(DeviceState *dev)
{
    SDState *sd = SD_CARD(dev);
    uint64_t size;
    uint64_t sect;

    trace_sdcard_reset();
    if (sd->blk) {
        blk_get_geometry(sd->blk, &sect);
    } else {
        sect = 0;
    }
    size = sect << 9;

    sect = sd_addr_to_wpnum(size) + 1;

    sd->state = sd_idle_state;
    sd->rca = 0x0000;
    sd->size = size;
    sd_set_ocr(sd);
    sd_set_scr(sd);
    sd_set_cid(sd);
    sd_set_csd(sd, size);
    sd_set_cardstatus(sd);
    sd_set_sdstatus(sd);

    g_free(sd->wp_group_bmap);
    sd->wp_switch = sd->blk ? !blk_is_writable(sd->blk) : false;
    sd->wp_group_bits = sect;
    sd->wp_group_bmap = bitmap_new(sd->wp_group_bits);
    memset(sd->function_group, 0, sizeof(sd->function_group));
    sd->erase_start = INVALID_ADDRESS;
    sd->erase_end = INVALID_ADDRESS;
    sd->blk_len = 0x200;
    sd->pwd_len = 0;
    sd->expecting_acmd = false;
    sd->dat_lines = 0xf;
    sd->cmd_line = true;
    sd->multi_blk_cnt = 0;
}

static bool sd_get_inserted(SDState *sd)
{
    return sd->blk && blk_is_inserted(sd->blk);
}

static bool sd_get_readonly(SDState *sd)
{
    return sd->wp_switch;
}

static void sd_cardchange(void *opaque, bool load, Error **errp)
{
    SDState *sd = opaque;
    DeviceState *dev = DEVICE(sd);
    SDBus *sdbus;
    bool inserted = sd_get_inserted(sd);
    bool readonly = sd_get_readonly(sd);

    if (inserted) {
        trace_sdcard_inserted(readonly);
        sd_reset(dev);
    } else {
        trace_sdcard_ejected();
    }

    if (sd->me_no_qdev_me_kill_mammoth_with_rocks) {
        qemu_set_irq(sd->inserted_cb, inserted);
        if (inserted) {
            qemu_set_irq(sd->readonly_cb, readonly);
        }
    } else {
        sdbus = SD_BUS(qdev_get_parent_bus(dev));
        sdbus_set_inserted(sdbus, inserted);
        if (inserted) {
            sdbus_set_readonly(sdbus, readonly);
        }
    }
}

static const BlockDevOps sd_block_ops = {
    .change_media_cb = sd_cardchange,
};

static bool sd_ocr_vmstate_needed(void *opaque)
{
    SDState *sd = opaque;

    /* Include the OCR state (and timer) if it is not yet powered up */
    return !FIELD_EX32(sd->ocr, OCR, CARD_POWER_UP);
}

static const VMStateDescription sd_ocr_vmstate = {
    .name = "sd-card/ocr-state",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = sd_ocr_vmstate_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ocr, SDState),
        VMSTATE_TIMER_PTR(ocr_power_timer, SDState),
        VMSTATE_END_OF_LIST()
    },
};

static int sd_vmstate_pre_load(void *opaque)
{
    SDState *sd = opaque;

    /* If the OCR state is not included (prior versions, or not
     * needed), then the OCR must be set as powered up. If the OCR state
     * is included, this will be replaced by the state restore.
     */
    sd_ocr_powerup(sd);

    return 0;
}

static const VMStateDescription sd_vmstate = {
    .name = "sd-card",
    .version_id = 2,
    .minimum_version_id = 2,
    .pre_load = sd_vmstate_pre_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(mode, SDState),
        VMSTATE_INT32(state, SDState),
        VMSTATE_UINT8_ARRAY(cid, SDState, 16),
        VMSTATE_UINT8_ARRAY(csd, SDState, 16),
        VMSTATE_UINT16(rca, SDState),
        VMSTATE_UINT32(card_status, SDState),
        VMSTATE_PARTIAL_BUFFER(sd_status, SDState, 1),
        VMSTATE_UINT32(vhs, SDState),
        VMSTATE_BITMAP(wp_group_bmap, SDState, 0, wp_group_bits),
        VMSTATE_UINT32(blk_len, SDState),
        VMSTATE_UINT32(multi_blk_cnt, SDState),
        VMSTATE_UINT32(erase_start, SDState),
        VMSTATE_UINT32(erase_end, SDState),
        VMSTATE_UINT8_ARRAY(pwd, SDState, 16),
        VMSTATE_UINT32(pwd_len, SDState),
        VMSTATE_UINT8_ARRAY(function_group, SDState, 6),
        VMSTATE_UINT8(current_cmd, SDState),
        VMSTATE_BOOL(expecting_acmd, SDState),
        VMSTATE_UINT32(blk_written, SDState),
        VMSTATE_UINT64(data_start, SDState),
        VMSTATE_UINT32(data_offset, SDState),
        VMSTATE_UINT8_ARRAY(data, SDState, 512),
        VMSTATE_UNUSED_V(1, 512),
        VMSTATE_BOOL(enable, SDState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &sd_ocr_vmstate,
        NULL
    },
};

/* Legacy initialization function for use by non-qdevified callers */
SDState *sd_init(BlockBackend *blk, bool is_spi)
{
    Object *obj;
    DeviceState *dev;
    SDState *sd;
    Error *err = NULL;

    obj = object_new(is_spi ? TYPE_SD_CARD_SPI : TYPE_SD_CARD);
    dev = DEVICE(obj);
    if (!qdev_prop_set_drive_err(dev, "drive", blk, &err)) {
        error_reportf_err(err, "sd_init failed: ");
        return NULL;
    }

    /*
     * Realizing the device properly would put it into the QOM
     * composition tree even though it is not plugged into an
     * appropriate bus.  That's a no-no.  Hide the device from
     * QOM/qdev, and call its qdev realize callback directly.
     */
    object_ref(obj);
    object_unparent(obj);
    sd_realize(dev, &err);
    if (err) {
        error_reportf_err(err, "sd_init failed: ");
        return NULL;
    }

    sd = SD_CARD(dev);
    sd->me_no_qdev_me_kill_mammoth_with_rocks = true;
    return sd;
}

void sd_set_cb(SDState *sd, qemu_irq readonly, qemu_irq insert)
{
    sd->readonly_cb = readonly;
    sd->inserted_cb = insert;
    qemu_set_irq(readonly, sd->blk ? !blk_is_writable(sd->blk) : 0);
    qemu_set_irq(insert, sd->blk ? blk_is_inserted(sd->blk) : 0);
}

static void sd_blk_read(SDState *sd, uint64_t addr, uint32_t len)
{
    trace_sdcard_read_block(addr, len);
    if (!sd->blk || blk_pread(sd->blk, addr, len, sd->data, 0) < 0) {
        fprintf(stderr, "sd_blk_read: read error on host side\n");
    }
}

static void sd_blk_write(SDState *sd, uint64_t addr, uint32_t len)
{
    trace_sdcard_write_block(addr, len);
    if (!sd->blk || blk_pwrite(sd->blk, addr, len, sd->data, 0) < 0) {
        fprintf(stderr, "sd_blk_write: write error on host side\n");
    }
}

#define BLK_READ_BLOCK(a, len)  sd_blk_read(sd, a, len)
#define BLK_WRITE_BLOCK(a, len) sd_blk_write(sd, a, len)
#define APP_READ_BLOCK(a, len)  memset(sd->data, 0xec, len)
#define APP_WRITE_BLOCK(a, len)

static void sd_erase(SDState *sd)
{
    uint64_t erase_start = sd->erase_start;
    uint64_t erase_end = sd->erase_end;
    bool sdsc = true;
    uint64_t wpnum;
    uint64_t erase_addr;
    int erase_len = 1 << HWBLOCK_SHIFT;

    trace_sdcard_erase(sd->erase_start, sd->erase_end);
    if (sd->erase_start == INVALID_ADDRESS
            || sd->erase_end == INVALID_ADDRESS) {
        sd->card_status |= ERASE_SEQ_ERROR;
        sd->erase_start = INVALID_ADDRESS;
        sd->erase_end = INVALID_ADDRESS;
        return;
    }

    if (FIELD_EX32(sd->ocr, OCR, CARD_CAPACITY)) {
        /* High capacity memory card: erase units are 512 byte blocks */
        erase_start *= 512;
        erase_end *= 512;
        sdsc = false;
    }

    if (erase_start > sd->size || erase_end > sd->size) {
        sd->card_status |= OUT_OF_RANGE;
        sd->erase_start = INVALID_ADDRESS;
        sd->erase_end = INVALID_ADDRESS;
        return;
    }

    sd->erase_start = INVALID_ADDRESS;
    sd->erase_end = INVALID_ADDRESS;
    sd->csd[14] |= 0x40;

    memset(sd->data, 0xff, erase_len);
    for (erase_addr = erase_start; erase_addr <= erase_end;
         erase_addr += erase_len) {
        if (sdsc) {
            /* Only SDSC cards support write protect groups */
            wpnum = sd_addr_to_wpnum(erase_addr);
            assert(wpnum < sd->wp_group_bits);
            if (test_bit(wpnum, sd->wp_group_bmap)) {
                sd->card_status |= WP_ERASE_SKIP;
                continue;
            }
        }
        BLK_WRITE_BLOCK(erase_addr, erase_len);
    }
}

static uint32_t sd_wpbits(SDState *sd, uint64_t addr)
{
    uint32_t i, wpnum;
    uint32_t ret = 0;

    wpnum = sd_addr_to_wpnum(addr);

    for (i = 0; i < 32; i++, wpnum++, addr += WPGROUP_SIZE) {
        if (addr >= sd->size) {
            /*
             * If the addresses of the last groups are outside the valid range,
             * then the corresponding write protection bits shall be set to 0.
             */
            continue;
        }
        assert(wpnum < sd->wp_group_bits);
        if (test_bit(wpnum, sd->wp_group_bmap)) {
            ret |= (1 << i);
        }
    }

    return ret;
}

static void sd_function_switch(SDState *sd, uint32_t arg)
{
    int i, mode, new_func;
    mode = !!(arg & 0x80000000);

    sd->data[0] = 0x00;     /* Maximum current consumption */
    sd->data[1] = 0x01;
    sd->data[2] = 0x80;     /* Supported group 6 functions */
    sd->data[3] = 0x01;
    sd->data[4] = 0x80;     /* Supported group 5 functions */
    sd->data[5] = 0x01;
    sd->data[6] = 0x80;     /* Supported group 4 functions */
    sd->data[7] = 0x01;
    sd->data[8] = 0x80;     /* Supported group 3 functions */
    sd->data[9] = 0x01;
    sd->data[10] = 0x80;    /* Supported group 2 functions */
    sd->data[11] = 0x43;
    sd->data[12] = 0x80;    /* Supported group 1 functions */
    sd->data[13] = 0x03;

    memset(&sd->data[14], 0, 3);
    for (i = 0; i < 6; i ++) {
        new_func = (arg >> (i * 4)) & 0x0f;
        if (mode && new_func != 0x0f)
            sd->function_group[i] = new_func;
        sd->data[16 - (i >> 1)] |= new_func << ((i % 2) * 4);
    }
    memset(&sd->data[17], 0, 47);
}

static inline bool sd_wp_addr(SDState *sd, uint64_t addr)
{
    return test_bit(sd_addr_to_wpnum(addr), sd->wp_group_bmap);
}

static void sd_lock_command(SDState *sd)
{
    int erase, lock, clr_pwd, set_pwd, pwd_len;
    erase = !!(sd->data[0] & 0x08);
    lock = sd->data[0] & 0x04;
    clr_pwd = sd->data[0] & 0x02;
    set_pwd = sd->data[0] & 0x01;

    if (sd->blk_len > 1)
        pwd_len = sd->data[1];
    else
        pwd_len = 0;

    if (lock) {
        trace_sdcard_lock();
    } else {
        trace_sdcard_unlock();
    }
    if (erase) {
        if (!(sd->card_status & CARD_IS_LOCKED) || sd->blk_len > 1 ||
                        set_pwd || clr_pwd || lock || sd->wp_switch ||
                        (sd->csd[14] & 0x20)) {
            sd->card_status |= LOCK_UNLOCK_FAILED;
            return;
        }
        bitmap_zero(sd->wp_group_bmap, sd->wp_group_bits);
        sd->csd[14] &= ~0x10;
        sd->card_status &= ~CARD_IS_LOCKED;
        sd->pwd_len = 0;
        /* Erasing the entire card here! */
        fprintf(stderr, "SD: Card force-erased by CMD42\n");
        return;
    }

    if (sd->blk_len < 2 + pwd_len ||
                    pwd_len <= sd->pwd_len ||
                    pwd_len > sd->pwd_len + 16) {
        sd->card_status |= LOCK_UNLOCK_FAILED;
        return;
    }

    if (sd->pwd_len && memcmp(sd->pwd, sd->data + 2, sd->pwd_len)) {
        sd->card_status |= LOCK_UNLOCK_FAILED;
        return;
    }

    pwd_len -= sd->pwd_len;
    if ((pwd_len && !set_pwd) ||
                    (clr_pwd && (set_pwd || lock)) ||
                    (lock && !sd->pwd_len && !set_pwd) ||
                    (!set_pwd && !clr_pwd &&
                     (((sd->card_status & CARD_IS_LOCKED) && lock) ||
                      (!(sd->card_status & CARD_IS_LOCKED) && !lock)))) {
        sd->card_status |= LOCK_UNLOCK_FAILED;
        return;
    }

    if (set_pwd) {
        memcpy(sd->pwd, sd->data + 2 + sd->pwd_len, pwd_len);
        sd->pwd_len = pwd_len;
    }

    if (clr_pwd) {
        sd->pwd_len = 0;
    }

    if (lock)
        sd->card_status |= CARD_IS_LOCKED;
    else
        sd->card_status &= ~CARD_IS_LOCKED;
}

static bool address_in_range(SDState *sd, const char *desc,
                             uint64_t addr, uint32_t length)
{
    if (addr + length > sd->size) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s offset %"PRIu64" > card %"PRIu64" [%%%u]\n",
                      desc, addr, sd->size, length);
        sd->card_status |= ADDRESS_ERROR;
        return false;
    }
    return true;
}

static sd_rsp_type_t sd_invalid_state_for_cmd(SDState *sd, SDRequest req)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: CMD%i in a wrong state: %s (spec %s)\n",
                  sd_proto(sd)->name, req.cmd, sd_state_name(sd->state),
                  sd_version_str(sd->spec_version));

    return sd_illegal;
}

static sd_rsp_type_t sd_cmd_illegal(SDState *sd, SDRequest req)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown CMD%i for spec %s\n",
                  sd_proto(sd)->name, req.cmd,
                  sd_version_str(sd->spec_version));

    return sd_illegal;
}

/* Commands that are recognised but not yet implemented. */
static sd_rsp_type_t sd_cmd_unimplemented(SDState *sd, SDRequest req)
{
    qemu_log_mask(LOG_UNIMP, "%s: CMD%i not implemented\n",
                  sd_proto(sd)->name, req.cmd);

    return sd_illegal;
}

static sd_rsp_type_t sd_cmd_GO_IDLE_STATE(SDState *sd, SDRequest req)
{
    if (sd->state != sd_inactive_state) {
        sd->state = sd_idle_state;
        sd_reset(DEVICE(sd));
    }

    return sd_is_spi(sd) ? sd_r1 : sd_r0;
}

static sd_rsp_type_t sd_cmd_SEND_OP_CMD(SDState *sd, SDRequest req)
{
    sd->state = sd_transfer_state;

    return sd_r1;
}

static sd_rsp_type_t sd_cmd_ALL_SEND_CID(SDState *sd, SDRequest req)
{
    if (sd->state != sd_ready_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }

    sd->state = sd_identification_state;

    return sd_r2_i;
}

static sd_rsp_type_t sd_cmd_SEND_RELATIVE_ADDR(SDState *sd, SDRequest req)
{
    switch (sd->state) {
    case sd_identification_state:
    case sd_standby_state:
        sd->state = sd_standby_state;
        sd_set_rca(sd);
        return sd_r6;

    default:
        return sd_invalid_state_for_cmd(sd, req);
    }
}

static sd_rsp_type_t sd_cmd_SEND_TUNING_BLOCK(SDState *sd, SDRequest req)
{
        if (sd->spec_version < SD_PHY_SPECv3_01_VERS) {
            return sd_cmd_illegal(sd, req);
        }

        if (sd->state != sd_transfer_state) {
            return sd_invalid_state_for_cmd(sd, req);
        }

        sd->state = sd_sendingdata_state;
        sd->data_offset = 0;

        return sd_r1;
}

static sd_rsp_type_t sd_cmd_SET_BLOCK_COUNT(SDState *sd, SDRequest req)
{
        if (sd->spec_version < SD_PHY_SPECv3_01_VERS) {
            return sd_cmd_illegal(sd, req);
        }

        if (sd->state != sd_transfer_state) {
            return sd_invalid_state_for_cmd(sd, req);
        }

        sd->multi_blk_cnt = req.arg;

        return sd_r1;
}

static sd_rsp_type_t sd_normal_command(SDState *sd, SDRequest req)
{
    uint32_t rca = 0x0000;
    uint64_t addr = (sd->ocr & (1 << 30)) ? (uint64_t) req.arg << 9 : req.arg;

    /* CMD55 precedes an ACMD, so we are not interested in tracing it.
     * However there is no ACMD55, so we want to trace this particular case.
     */
    if (req.cmd != 55 || sd->expecting_acmd) {
        trace_sdcard_normal_command(sd_proto(sd)->name,
                                    sd_cmd_name(req.cmd), req.cmd,
                                    req.arg, sd_state_name(sd->state));
    }

    /* Not interpreting this as an app command */
    sd->card_status &= ~APP_CMD;

    if (sd_cmd_type[req.cmd] == sd_ac
        || sd_cmd_type[req.cmd] == sd_adtc) {
        rca = req.arg >> 16;
    }

    /* CMD23 (set block count) must be immediately followed by CMD18 or CMD25
     * if not, its effects are cancelled */
    if (sd->multi_blk_cnt != 0 && !(req.cmd == 18 || req.cmd == 25)) {
        sd->multi_blk_cnt = 0;
    }

    if (sd_cmd_class[req.cmd] == 6 && FIELD_EX32(sd->ocr, OCR, CARD_CAPACITY)) {
        /* Only Standard Capacity cards support class 6 commands */
        return sd_illegal;
    }

    if (sd_proto(sd)->cmd[req.cmd]) {
        return sd_proto(sd)->cmd[req.cmd](sd, req);
    }

    switch (req.cmd) {
    /* Basic commands (Class 0 and Class 1) */
    case 4:  /* CMD4:   SEND_DSR */
        switch (sd->state) {
        case sd_standby_state:
            break;

        default:
            break;
        }
        break;

    case 6:  /* CMD6:   SWITCH_FUNCTION */
        switch (sd->mode) {
        case sd_data_transfer_mode:
            sd_function_switch(sd, req.arg);
            sd->state = sd_sendingdata_state;
            sd->data_start = 0;
            sd->data_offset = 0;
            return sd_r1;

        default:
            break;
        }
        break;

    case 7:  /* CMD7:   SELECT/DESELECT_CARD */
        switch (sd->state) {
        case sd_standby_state:
            if (sd->rca != rca)
                return sd_r0;

            sd->state = sd_transfer_state;
            return sd_r1b;

        case sd_transfer_state:
        case sd_sendingdata_state:
            if (sd->rca == rca)
                break;

            sd->state = sd_standby_state;
            return sd_r1b;

        case sd_disconnect_state:
            if (sd->rca != rca)
                return sd_r0;

            sd->state = sd_programming_state;
            return sd_r1b;

        case sd_programming_state:
            if (sd->rca == rca)
                break;

            sd->state = sd_disconnect_state;
            return sd_r1b;

        default:
            break;
        }
        break;

    case 8:  /* CMD8:   SEND_IF_COND */
        if (sd->spec_version < SD_PHY_SPECv2_00_VERS) {
            break;
        }
        if (sd->state != sd_idle_state) {
            break;
        }
        sd->vhs = 0;

        /* No response if not exactly one VHS bit is set.  */
        if (!(req.arg >> 8) || (req.arg >> (ctz32(req.arg & ~0xff) + 1))) {
            return sd_is_spi(sd) ? sd_r7 : sd_r0;
        }

        /* Accept.  */
        sd->vhs = req.arg;
        return sd_r7;

    case 9:  /* CMD9:   SEND_CSD */
        switch (sd->state) {
        case sd_standby_state:
            if (sd->rca != rca)
                return sd_r0;

            return sd_r2_s;

        case sd_transfer_state:
            if (!sd_is_spi(sd)) {
                break;
            }
            sd->state = sd_sendingdata_state;
            memcpy(sd->data, sd->csd, 16);
            sd->data_start = addr;
            sd->data_offset = 0;
            return sd_r1;

        default:
            break;
        }
        break;

    case 10:  /* CMD10:  SEND_CID */
        switch (sd->state) {
        case sd_standby_state:
            if (sd->rca != rca)
                return sd_r0;

            return sd_r2_i;

        case sd_transfer_state:
            if (!sd_is_spi(sd)) {
                break;
            }
            sd->state = sd_sendingdata_state;
            memcpy(sd->data, sd->cid, 16);
            sd->data_start = addr;
            sd->data_offset = 0;
            return sd_r1;

        default:
            break;
        }
        break;

    case 12:  /* CMD12:  STOP_TRANSMISSION */
        switch (sd->state) {
        case sd_sendingdata_state:
            sd->state = sd_transfer_state;
            return sd_r1b;

        case sd_receivingdata_state:
            sd->state = sd_programming_state;
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
            return sd_r1b;

        default:
            break;
        }
        break;

    case 13:  /* CMD13:  SEND_STATUS */
        switch (sd->mode) {
        case sd_data_transfer_mode:
            if (!sd_is_spi(sd) && sd->rca != rca) {
                return sd_r0;
            }

            return sd_r1;

        default:
            break;
        }
        break;

    case 15:  /* CMD15:  GO_INACTIVE_STATE */
        switch (sd->mode) {
        case sd_data_transfer_mode:
            if (sd->rca != rca)
                return sd_r0;

            sd->state = sd_inactive_state;
            return sd_r0;

        default:
            break;
        }
        break;

    /* Block read commands (Class 2) */
    case 16:  /* CMD16:  SET_BLOCKLEN */
        switch (sd->state) {
        case sd_transfer_state:
            if (req.arg > (1 << HWBLOCK_SHIFT)) {
                sd->card_status |= BLOCK_LEN_ERROR;
            } else {
                trace_sdcard_set_blocklen(req.arg);
                sd->blk_len = req.arg;
            }

            return sd_r1;

        default:
            break;
        }
        break;

    case 17:  /* CMD17:  READ_SINGLE_BLOCK */
    case 18:  /* CMD18:  READ_MULTIPLE_BLOCK */
        switch (sd->state) {
        case sd_transfer_state:

            if (!address_in_range(sd, "READ_BLOCK", addr, sd->blk_len)) {
                return sd_r1;
            }

            sd->state = sd_sendingdata_state;
            sd->data_start = addr;
            sd->data_offset = 0;
            return sd_r1;

        default:
            break;
        }
        break;

    /* Block write commands (Class 4) */
    case 24:  /* CMD24:  WRITE_SINGLE_BLOCK */
    case 25:  /* CMD25:  WRITE_MULTIPLE_BLOCK */
        switch (sd->state) {
        case sd_transfer_state:

            if (!address_in_range(sd, "WRITE_BLOCK", addr, sd->blk_len)) {
                return sd_r1;
            }

            sd->state = sd_receivingdata_state;
            sd->data_start = addr;
            sd->data_offset = 0;
            sd->blk_written = 0;

            if (sd->size <= SDSC_MAX_CAPACITY) {
                if (sd_wp_addr(sd, sd->data_start)) {
                    sd->card_status |= WP_VIOLATION;
                }
            }
            if (sd->csd[14] & 0x30) {
                sd->card_status |= WP_VIOLATION;
            }
            return sd_r1;

        default:
            break;
        }
        break;

    case 26:  /* CMD26:  PROGRAM_CID */
        switch (sd->state) {
        case sd_transfer_state:
            sd->state = sd_receivingdata_state;
            sd->data_start = 0;
            sd->data_offset = 0;
            return sd_r1;

        default:
            break;
        }
        break;

    case 27:  /* CMD27:  PROGRAM_CSD */
        switch (sd->state) {
        case sd_transfer_state:
            sd->state = sd_receivingdata_state;
            sd->data_start = 0;
            sd->data_offset = 0;
            return sd_r1;

        default:
            break;
        }
        break;

    /* Write protection (Class 6) */
    case 28:  /* CMD28:  SET_WRITE_PROT */
        if (sd->size > SDSC_MAX_CAPACITY) {
            return sd_illegal;
        }

        switch (sd->state) {
        case sd_transfer_state:
            if (!address_in_range(sd, "SET_WRITE_PROT", addr, 1)) {
                return sd_r1b;
            }

            sd->state = sd_programming_state;
            set_bit(sd_addr_to_wpnum(addr), sd->wp_group_bmap);
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
            return sd_r1b;

        default:
            break;
        }
        break;

    case 29:  /* CMD29:  CLR_WRITE_PROT */
        if (sd->size > SDSC_MAX_CAPACITY) {
            return sd_illegal;
        }

        switch (sd->state) {
        case sd_transfer_state:
            if (!address_in_range(sd, "CLR_WRITE_PROT", addr, 1)) {
                return sd_r1b;
            }

            sd->state = sd_programming_state;
            clear_bit(sd_addr_to_wpnum(addr), sd->wp_group_bmap);
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
            return sd_r1b;

        default:
            break;
        }
        break;

    case 30:  /* CMD30:  SEND_WRITE_PROT */
        if (sd->size > SDSC_MAX_CAPACITY) {
            return sd_illegal;
        }

        switch (sd->state) {
        case sd_transfer_state:
            if (!address_in_range(sd, "SEND_WRITE_PROT",
                                  req.arg, sd->blk_len)) {
                return sd_r1;
            }

            sd->state = sd_sendingdata_state;
            *(uint32_t *) sd->data = sd_wpbits(sd, req.arg);
            sd->data_start = addr;
            sd->data_offset = 0;
            return sd_r1;

        default:
            break;
        }
        break;

    /* Erase commands (Class 5) */
    case 32:  /* CMD32:  ERASE_WR_BLK_START */
        switch (sd->state) {
        case sd_transfer_state:
            sd->erase_start = req.arg;
            return sd_r1;

        default:
            break;
        }
        break;

    case 33:  /* CMD33:  ERASE_WR_BLK_END */
        switch (sd->state) {
        case sd_transfer_state:
            sd->erase_end = req.arg;
            return sd_r1;

        default:
            break;
        }
        break;

    case 38:  /* CMD38:  ERASE */
        switch (sd->state) {
        case sd_transfer_state:
            if (sd->csd[14] & 0x30) {
                sd->card_status |= WP_VIOLATION;
                return sd_r1b;
            }

            sd->state = sd_programming_state;
            sd_erase(sd);
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
            return sd_r1b;

        default:
            break;
        }
        break;

    /* Lock card commands (Class 7) */
    case 42:  /* CMD42:  LOCK_UNLOCK */
        switch (sd->state) {
        case sd_transfer_state:
            sd->state = sd_receivingdata_state;
            sd->data_start = 0;
            sd->data_offset = 0;
            return sd_r1;

        default:
            break;
        }
        break;

    /* Application specific commands (Class 8) */
    case 55:  /* CMD55:  APP_CMD */
        switch (sd->state) {
        case sd_ready_state:
        case sd_identification_state:
        case sd_inactive_state:
            return sd_illegal;
        case sd_idle_state:
            if (rca) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "SD: illegal RCA 0x%04x for APP_CMD\n", req.cmd);
            }
        default:
            break;
        }
        if (!sd_is_spi(sd)) {
            if (sd->rca != rca) {
                return sd_r0;
            }
        }
        sd->expecting_acmd = true;
        sd->card_status |= APP_CMD;
        return sd_r1;

    case 56:  /* CMD56:  GEN_CMD */
        switch (sd->state) {
        case sd_transfer_state:
            sd->data_offset = 0;
            if (req.arg & 1)
                sd->state = sd_sendingdata_state;
            else
                sd->state = sd_receivingdata_state;
            return sd_r1;

        default:
            break;
        }
        break;

    case 58:    /* CMD58:   READ_OCR (SPI) */
        return sd_r3;

    case 59:    /* CMD59:   CRC_ON_OFF (SPI) */
        return sd_r1;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "SD: Unknown CMD%i\n", req.cmd);
        return sd_illegal;
    }

    return sd_invalid_state_for_cmd(sd, req);
}

static sd_rsp_type_t sd_app_command(SDState *sd,
                                    SDRequest req)
{
    trace_sdcard_app_command(sd_proto(sd)->name, sd_acmd_name(req.cmd),
                             req.cmd, req.arg, sd_state_name(sd->state));
    sd->card_status |= APP_CMD;

    if (sd_proto(sd)->acmd[req.cmd]) {
        return sd_proto(sd)->acmd[req.cmd](sd, req);
    }

    switch (req.cmd) {
    case 6:  /* ACMD6:  SET_BUS_WIDTH */
        switch (sd->state) {
        case sd_transfer_state:
            sd->sd_status[0] &= 0x3f;
            sd->sd_status[0] |= (req.arg & 0x03) << 6;
            return sd_r1;

        default:
            break;
        }
        break;

    case 13:  /* ACMD13: SD_STATUS */
        switch (sd->state) {
        case sd_transfer_state:
            sd->state = sd_sendingdata_state;
            sd->data_start = 0;
            sd->data_offset = 0;
            return sd_r1;

        default:
            break;
        }
        break;

    case 22:  /* ACMD22: SEND_NUM_WR_BLOCKS */
        switch (sd->state) {
        case sd_transfer_state:
            *(uint32_t *) sd->data = sd->blk_written;

            sd->state = sd_sendingdata_state;
            sd->data_start = 0;
            sd->data_offset = 0;
            return sd_r1;

        default:
            break;
        }
        break;

    case 23:  /* ACMD23: SET_WR_BLK_ERASE_COUNT */
        switch (sd->state) {
        case sd_transfer_state:
            return sd_r1;

        default:
            break;
        }
        break;

    case 41:  /* ACMD41: SD_APP_OP_COND */
        if (sd->state != sd_idle_state) {
            break;
        }
        /* If it's the first ACMD41 since reset, we need to decide
         * whether to power up. If this is not an enquiry ACMD41,
         * we immediately report power on and proceed below to the
         * ready state, but if it is, we set a timer to model a
         * delay for power up. This works around a bug in EDK2
         * UEFI, which sends an initial enquiry ACMD41, but
         * assumes that the card is in ready state as soon as it
         * sees the power up bit set. */
        if (!FIELD_EX32(sd->ocr, OCR, CARD_POWER_UP)) {
            if ((req.arg & ACMD41_ENQUIRY_MASK) != 0) {
                timer_del(sd->ocr_power_timer);
                sd_ocr_powerup(sd);
            } else {
                trace_sdcard_inquiry_cmd41();
                if (!timer_pending(sd->ocr_power_timer)) {
                    timer_mod_ns(sd->ocr_power_timer,
                                 (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                                  + OCR_POWER_DELAY_NS));
                }
            }
        }

        if (FIELD_EX32(sd->ocr & req.arg, OCR, VDD_VOLTAGE_WINDOW)) {
            /* We accept any voltage.  10000 V is nothing.
             *
             * Once we're powered up, we advance straight to ready state
             * unless it's an enquiry ACMD41 (bits 23:0 == 0).
             */
            sd->state = sd_ready_state;
        }

        return sd_r3;

    case 42:  /* ACMD42: SET_CLR_CARD_DETECT */
        switch (sd->state) {
        case sd_transfer_state:
            /* Bringing in the 50KOhm pull-up resistor... Done.  */
            return sd_r1;

        default:
            break;
        }
        break;

    case 51:  /* ACMD51: SEND_SCR */
        switch (sd->state) {
        case sd_transfer_state:
            sd->state = sd_sendingdata_state;
            sd->data_start = 0;
            sd->data_offset = 0;
            return sd_r1;

        default:
            break;
        }
        break;

    case 18:    /* Reserved for SD security applications */
    case 25:
    case 26:
    case 38:
    case 43 ... 49:
        /* Refer to the "SD Specifications Part3 Security Specification" for
         * information about the SD Security Features.
         */
        qemu_log_mask(LOG_UNIMP, "SD: CMD%i Security not implemented\n",
                      req.cmd);
        return sd_illegal;

    default:
        /* Fall back to standard commands.  */
        return sd_normal_command(sd, req);
    }

    qemu_log_mask(LOG_GUEST_ERROR, "SD: ACMD%i in a wrong state\n", req.cmd);
    return sd_illegal;
}

static int cmd_valid_while_locked(SDState *sd, const uint8_t cmd)
{
    /* Valid commands in locked state:
     * basic class (0)
     * lock card class (7)
     * CMD16
     * implicitly, the ACMD prefix CMD55
     * ACMD41 and ACMD42
     * Anything else provokes an "illegal command" response.
     */
    if (sd->expecting_acmd) {
        return cmd == 41 || cmd == 42;
    }
    if (cmd == 16 || cmd == 55) {
        return 1;
    }
    return sd_cmd_class[cmd] == 0 || sd_cmd_class[cmd] == 7;
}

int sd_do_command(SDState *sd, SDRequest *req,
                  uint8_t *response) {
    int last_state;
    sd_rsp_type_t rtype;
    int rsplen;

    if (!sd->blk || !blk_is_inserted(sd->blk) || !sd->enable) {
        return 0;
    }

    if (sd_req_crc_validate(req)) {
        sd->card_status |= COM_CRC_ERROR;
        rtype = sd_illegal;
        goto send_response;
    }

    if (req->cmd >= SDMMC_CMD_MAX) {
        qemu_log_mask(LOG_GUEST_ERROR, "SD: incorrect command 0x%02x\n",
                      req->cmd);
        req->cmd &= 0x3f;
    }

    if (sd->card_status & CARD_IS_LOCKED) {
        if (!cmd_valid_while_locked(sd, req->cmd)) {
            sd->card_status |= ILLEGAL_COMMAND;
            sd->expecting_acmd = false;
            qemu_log_mask(LOG_GUEST_ERROR, "SD: Card is locked\n");
            rtype = sd_illegal;
            goto send_response;
        }
    }

    last_state = sd->state;
    sd_set_mode(sd);

    if (sd->expecting_acmd) {
        sd->expecting_acmd = false;
        rtype = sd_app_command(sd, *req);
    } else {
        rtype = sd_normal_command(sd, *req);
    }

    if (rtype == sd_illegal) {
        sd->card_status |= ILLEGAL_COMMAND;
    } else {
        /* Valid command, we can update the 'state before command' bits.
         * (Do this now so they appear in r1 responses.)
         */
        sd->current_cmd = req->cmd;
        sd->card_status &= ~CURRENT_STATE;
        sd->card_status |= (last_state << 9);
    }

send_response:
    switch (rtype) {
    case sd_r1:
    case sd_r1b:
        sd_response_r1_make(sd, response);
        rsplen = 4;
        break;

    case sd_r2_i:
        memcpy(response, sd->cid, sizeof(sd->cid));
        rsplen = 16;
        break;

    case sd_r2_s:
        memcpy(response, sd->csd, sizeof(sd->csd));
        rsplen = 16;
        break;

    case sd_r3:
        sd_response_r3_make(sd, response);
        rsplen = 4;
        break;

    case sd_r6:
        sd_response_r6_make(sd, response);
        rsplen = 4;
        break;

    case sd_r7:
        sd_response_r7_make(sd, response);
        rsplen = 4;
        break;

    case sd_r0:
    case sd_illegal:
        rsplen = 0;
        break;
    default:
        g_assert_not_reached();
    }
    trace_sdcard_response(sd_response_name(rtype), rsplen);

    if (rtype != sd_illegal) {
        /* Clear the "clear on valid command" status bits now we've
         * sent any response
         */
        sd->card_status &= ~CARD_STATUS_B;
    }

#ifdef DEBUG_SD
    qemu_hexdump(stderr, "Response", response, rsplen);
#endif

    return rsplen;
}

void sd_write_byte(SDState *sd, uint8_t value)
{
    int i;

    if (!sd->blk || !blk_is_inserted(sd->blk) || !sd->enable)
        return;

    if (sd->state != sd_receivingdata_state) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: not in Receiving-Data state\n", __func__);
        return;
    }

    if (sd->card_status & (ADDRESS_ERROR | WP_VIOLATION))
        return;

    trace_sdcard_write_data(sd_proto(sd)->name,
                            sd_acmd_name(sd->current_cmd),
                            sd->current_cmd, value);
    switch (sd->current_cmd) {
    case 24:  /* CMD24:  WRITE_SINGLE_BLOCK */
        sd->data[sd->data_offset ++] = value;
        if (sd->data_offset >= sd->blk_len) {
            /* TODO: Check CRC before committing */
            sd->state = sd_programming_state;
            BLK_WRITE_BLOCK(sd->data_start, sd->data_offset);
            sd->blk_written ++;
            sd->csd[14] |= 0x40;
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
        }
        break;

    case 25:  /* CMD25:  WRITE_MULTIPLE_BLOCK */
        if (sd->data_offset == 0) {
            /* Start of the block - let's check the address is valid */
            if (!address_in_range(sd, "WRITE_MULTIPLE_BLOCK",
                                  sd->data_start, sd->blk_len)) {
                break;
            }
            if (sd->size <= SDSC_MAX_CAPACITY) {
                if (sd_wp_addr(sd, sd->data_start)) {
                    sd->card_status |= WP_VIOLATION;
                    break;
                }
            }
        }
        sd->data[sd->data_offset++] = value;
        if (sd->data_offset >= sd->blk_len) {
            /* TODO: Check CRC before committing */
            sd->state = sd_programming_state;
            BLK_WRITE_BLOCK(sd->data_start, sd->data_offset);
            sd->blk_written++;
            sd->data_start += sd->blk_len;
            sd->data_offset = 0;
            sd->csd[14] |= 0x40;

            /* Bzzzzzzztt .... Operation complete.  */
            if (sd->multi_blk_cnt != 0) {
                if (--sd->multi_blk_cnt == 0) {
                    /* Stop! */
                    sd->state = sd_transfer_state;
                    break;
                }
            }

            sd->state = sd_receivingdata_state;
        }
        break;

    case 26:  /* CMD26:  PROGRAM_CID */
        sd->data[sd->data_offset ++] = value;
        if (sd->data_offset >= sizeof(sd->cid)) {
            /* TODO: Check CRC before committing */
            sd->state = sd_programming_state;
            for (i = 0; i < sizeof(sd->cid); i ++)
                if ((sd->cid[i] | 0x00) != sd->data[i])
                    sd->card_status |= CID_CSD_OVERWRITE;

            if (!(sd->card_status & CID_CSD_OVERWRITE))
                for (i = 0; i < sizeof(sd->cid); i ++) {
                    sd->cid[i] |= 0x00;
                    sd->cid[i] &= sd->data[i];
                }
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
        }
        break;

    case 27:  /* CMD27:  PROGRAM_CSD */
        sd->data[sd->data_offset ++] = value;
        if (sd->data_offset >= sizeof(sd->csd)) {
            /* TODO: Check CRC before committing */
            sd->state = sd_programming_state;
            for (i = 0; i < sizeof(sd->csd); i ++)
                if ((sd->csd[i] | sd_csd_rw_mask[i]) !=
                    (sd->data[i] | sd_csd_rw_mask[i]))
                    sd->card_status |= CID_CSD_OVERWRITE;

            /* Copy flag (OTP) & Permanent write protect */
            if (sd->csd[14] & ~sd->data[14] & 0x60)
                sd->card_status |= CID_CSD_OVERWRITE;

            if (!(sd->card_status & CID_CSD_OVERWRITE))
                for (i = 0; i < sizeof(sd->csd); i ++) {
                    sd->csd[i] |= sd_csd_rw_mask[i];
                    sd->csd[i] &= sd->data[i];
                }
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
        }
        break;

    case 42:  /* CMD42:  LOCK_UNLOCK */
        sd->data[sd->data_offset ++] = value;
        if (sd->data_offset >= sd->blk_len) {
            /* TODO: Check CRC before committing */
            sd->state = sd_programming_state;
            sd_lock_command(sd);
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
        }
        break;

    case 56:  /* CMD56:  GEN_CMD */
        sd->data[sd->data_offset ++] = value;
        if (sd->data_offset >= sd->blk_len) {
            APP_WRITE_BLOCK(sd->data_start, sd->data_offset);
            sd->state = sd_transfer_state;
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown command\n", __func__);
        break;
    }
}

#define SD_TUNING_BLOCK_SIZE    64

static const uint8_t sd_tuning_block_pattern[SD_TUNING_BLOCK_SIZE] = {
    /* See: Physical Layer Simplified Specification Version 3.01, Table 4-2 */
    0xff, 0x0f, 0xff, 0x00,         0x0f, 0xfc, 0xc3, 0xcc,
    0xc3, 0x3c, 0xcc, 0xff,         0xfe, 0xff, 0xfe, 0xef,
    0xff, 0xdf, 0xff, 0xdd,         0xff, 0xfb, 0xff, 0xfb,
    0xbf, 0xff, 0x7f, 0xff,         0x77, 0xf7, 0xbd, 0xef,
    0xff, 0xf0, 0xff, 0xf0,         0x0f, 0xfc, 0xcc, 0x3c,
    0xcc, 0x33, 0xcc, 0xcf,         0xff, 0xef, 0xff, 0xee,
    0xff, 0xfd, 0xff, 0xfd,         0xdf, 0xff, 0xbf, 0xff,
    0xbb, 0xff, 0xf7, 0xff,         0xf7, 0x7f, 0x7b, 0xde,
};

uint8_t sd_read_byte(SDState *sd)
{
    /* TODO: Append CRCs */
    uint8_t ret;
    uint32_t io_len;

    if (!sd->blk || !blk_is_inserted(sd->blk) || !sd->enable)
        return 0x00;

    if (sd->state != sd_sendingdata_state) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: not in Sending-Data state\n", __func__);
        return 0x00;
    }

    if (sd->card_status & (ADDRESS_ERROR | WP_VIOLATION))
        return 0x00;

    io_len = (sd->ocr & (1 << 30)) ? 512 : sd->blk_len;

    trace_sdcard_read_data(sd_proto(sd)->name,
                           sd_acmd_name(sd->current_cmd),
                           sd->current_cmd, io_len);
    switch (sd->current_cmd) {
    case 6:  /* CMD6:   SWITCH_FUNCTION */
        ret = sd->data[sd->data_offset ++];

        if (sd->data_offset >= 64)
            sd->state = sd_transfer_state;
        break;

    case 9:  /* CMD9:   SEND_CSD */
    case 10:  /* CMD10:  SEND_CID */
        ret = sd->data[sd->data_offset ++];

        if (sd->data_offset >= 16)
            sd->state = sd_transfer_state;
        break;

    case 13:  /* ACMD13: SD_STATUS */
        ret = sd->sd_status[sd->data_offset ++];

        if (sd->data_offset >= sizeof(sd->sd_status))
            sd->state = sd_transfer_state;
        break;

    case 17:  /* CMD17:  READ_SINGLE_BLOCK */
        if (sd->data_offset == 0)
            BLK_READ_BLOCK(sd->data_start, io_len);
        ret = sd->data[sd->data_offset ++];

        if (sd->data_offset >= io_len)
            sd->state = sd_transfer_state;
        break;

    case 18:  /* CMD18:  READ_MULTIPLE_BLOCK */
        if (sd->data_offset == 0) {
            if (!address_in_range(sd, "READ_MULTIPLE_BLOCK",
                                  sd->data_start, io_len)) {
                return 0x00;
            }
            BLK_READ_BLOCK(sd->data_start, io_len);
        }
        ret = sd->data[sd->data_offset ++];

        if (sd->data_offset >= io_len) {
            sd->data_start += io_len;
            sd->data_offset = 0;

            if (sd->multi_blk_cnt != 0) {
                if (--sd->multi_blk_cnt == 0) {
                    /* Stop! */
                    sd->state = sd_transfer_state;
                    break;
                }
            }
        }
        break;

    case 19:    /* CMD19:  SEND_TUNING_BLOCK (SD) */
        if (sd->data_offset >= SD_TUNING_BLOCK_SIZE - 1) {
            sd->state = sd_transfer_state;
        }
        ret = sd_tuning_block_pattern[sd->data_offset++];
        break;

    case 22:  /* ACMD22: SEND_NUM_WR_BLOCKS */
        ret = sd->data[sd->data_offset ++];

        if (sd->data_offset >= 4)
            sd->state = sd_transfer_state;
        break;

    case 30:  /* CMD30:  SEND_WRITE_PROT */
        ret = sd->data[sd->data_offset ++];

        if (sd->data_offset >= 4)
            sd->state = sd_transfer_state;
        break;

    case 51:  /* ACMD51: SEND_SCR */
        ret = sd->scr[sd->data_offset ++];

        if (sd->data_offset >= sizeof(sd->scr))
            sd->state = sd_transfer_state;
        break;

    case 56:  /* CMD56:  GEN_CMD */
        if (sd->data_offset == 0)
            APP_READ_BLOCK(sd->data_start, sd->blk_len);
        ret = sd->data[sd->data_offset ++];

        if (sd->data_offset >= sd->blk_len)
            sd->state = sd_transfer_state;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown command\n", __func__);
        return 0x00;
    }

    return ret;
}

static bool sd_receive_ready(SDState *sd)
{
    return sd->state == sd_receivingdata_state;
}

static bool sd_data_ready(SDState *sd)
{
    return sd->state == sd_sendingdata_state;
}

void sd_enable(SDState *sd, bool enable)
{
    sd->enable = enable;
}

static const SDProto sd_proto_spi = {
    .name = "SPI",
    .cmd = {
        [0]         = sd_cmd_GO_IDLE_STATE,
        [1]         = sd_cmd_SEND_OP_CMD,
        [2 ... 4]   = sd_cmd_illegal,
        [5]         = sd_cmd_illegal,
        [7]         = sd_cmd_illegal,
        [15]        = sd_cmd_illegal,
        [26]        = sd_cmd_illegal,
        [52 ... 54] = sd_cmd_illegal,
    },
    .acmd = {
        [6]         = sd_cmd_unimplemented,
        [41]        = sd_cmd_SEND_OP_CMD,
    },
};

static const SDProto sd_proto_sd = {
    .name = "SD",
    .cmd = {
        [0]         = sd_cmd_GO_IDLE_STATE,
        [1]         = sd_cmd_illegal,
        [2]         = sd_cmd_ALL_SEND_CID,
        [3]         = sd_cmd_SEND_RELATIVE_ADDR,
        [5]         = sd_cmd_illegal,
        [19]        = sd_cmd_SEND_TUNING_BLOCK,
        [23]        = sd_cmd_SET_BLOCK_COUNT,
        [52 ... 54] = sd_cmd_illegal,
        [58]        = sd_cmd_illegal,
        [59]        = sd_cmd_illegal,
    },
};

static void sd_instance_init(Object *obj)
{
    SDState *sd = SD_CARD(obj);

    sd->enable = true;
    sd->ocr_power_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sd_ocr_powerup, sd);
}

static void sd_instance_finalize(Object *obj)
{
    SDState *sd = SD_CARD(obj);

    timer_free(sd->ocr_power_timer);
}

static void sd_realize(DeviceState *dev, Error **errp)
{
    SDState *sd = SD_CARD(dev);
    int ret;

    switch (sd->spec_version) {
    case SD_PHY_SPECv1_10_VERS
     ... SD_PHY_SPECv3_01_VERS:
        break;
    default:
        error_setg(errp, "Invalid SD card Spec version: %u", sd->spec_version);
        return;
    }

    if (sd->blk) {
        int64_t blk_size;

        if (!blk_supports_write_perm(sd->blk)) {
            error_setg(errp, "Cannot use read-only drive as SD card");
            return;
        }

        blk_size = blk_getlength(sd->blk);
        if (blk_size > 0 && !is_power_of_2(blk_size)) {
            int64_t blk_size_aligned = pow2ceil(blk_size);
            char *blk_size_str;

            blk_size_str = size_to_str(blk_size);
            error_setg(errp, "Invalid SD card size: %s", blk_size_str);
            g_free(blk_size_str);

            blk_size_str = size_to_str(blk_size_aligned);
            error_append_hint(errp,
                              "SD card size has to be a power of 2, e.g. %s.\n"
                              "You can resize disk images with"
                              " 'qemu-img resize <imagefile> <new-size>'\n"
                              "(note that this will lose data if you make the"
                              " image smaller than it currently is).\n",
                              blk_size_str);
            g_free(blk_size_str);

            return;
        }

        ret = blk_set_perm(sd->blk, BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                           BLK_PERM_ALL, errp);
        if (ret < 0) {
            return;
        }
        blk_set_dev_ops(sd->blk, &sd_block_ops, sd);
    }
}

static Property sd_properties[] = {
    DEFINE_PROP_UINT8("spec_version", SDState,
                      spec_version, SD_PHY_SPECv2_00_VERS),
    DEFINE_PROP_DRIVE("drive", SDState, blk),
    /* We do not model the chip select pin, so allow the board to select
     * whether card should be in SSI or MMC/SD mode.  It is also up to the
     * board to ensure that ssi transfers only occur when the chip select
     * is asserted.  */
    DEFINE_PROP_END_OF_LIST()
};

static void sd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SDCardClass *sc = SD_CARD_CLASS(klass);

    dc->realize = sd_realize;
    device_class_set_props(dc, sd_properties);
    dc->vmsd = &sd_vmstate;
    dc->reset = sd_reset;
    dc->bus_type = TYPE_SD_BUS;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

    sc->set_voltage = sd_set_voltage;
    sc->get_dat_lines = sd_get_dat_lines;
    sc->get_cmd_line = sd_get_cmd_line;
    sc->do_command = sd_do_command;
    sc->write_byte = sd_write_byte;
    sc->read_byte = sd_read_byte;
    sc->receive_ready = sd_receive_ready;
    sc->data_ready = sd_data_ready;
    sc->enable = sd_enable;
    sc->get_inserted = sd_get_inserted;
    sc->get_readonly = sd_get_readonly;
    sc->proto = &sd_proto_sd;
}

/*
 * We do not model the chip select pin, so allow the board to select
 * whether card should be in SSI or MMC/SD mode.  It is also up to the
 * board to ensure that ssi transfers only occur when the chip select
 * is asserted.
 */
static void sd_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SDCardClass *sc = SD_CARD_CLASS(klass);

    dc->desc = "SD SPI";
    sc->proto = &sd_proto_spi;
}

static const TypeInfo sd_types[] = {
    {
        .name           = TYPE_SD_CARD,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(SDState),
        .class_size     = sizeof(SDCardClass),
        .class_init     = sd_class_init,
        .instance_init  = sd_instance_init,
        .instance_finalize = sd_instance_finalize,
    },
    {
        .name           = TYPE_SD_CARD_SPI,
        .parent         = TYPE_SD_CARD,
        .class_init     = sd_spi_class_init,
    },
};

DEFINE_TYPES(sd_types)
