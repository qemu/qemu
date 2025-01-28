/*
 * SD Memory Card emulation as defined in the "SD Memory Card Physical
 * layer specification, Version 2.00."
 *
 * eMMC emulation defined in "JEDEC Standard No. 84-A43"
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
#include "system/block-backend.h"
#include "hw/sd/sd.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitmap.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/guest-random.h"
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

typedef enum {
    sd_spi,
    sd_bc,     /* broadcast -- no response */
    sd_bcr,    /* broadcast with response */
    sd_ac,     /* addressed -- no data transfer */
    sd_adtc,   /* addressed with data transfer */
} sd_cmd_type_t;

enum SDCardModes {
    sd_inactive,
    sd_card_identification_mode,
    sd_data_transfer_mode,
};

enum SDCardStates {
    sd_waitirq_state        = -2, /* emmc */
    sd_inactive_state       = -1,

    sd_idle_state           = 0,
    sd_ready_state          = 1,
    sd_identification_state = 2,
    sd_standby_state        = 3,
    sd_transfer_state       = 4,
    sd_sendingdata_state    = 5,
    sd_receivingdata_state  = 6,
    sd_programming_state    = 7,
    sd_disconnect_state     = 8,
    sd_bus_test_state       = 9,  /* emmc */
    sd_sleep_state          = 10, /* emmc */
    sd_io_state             = 15  /* sd */
};

#define SDMMC_CMD_MAX 64

typedef sd_rsp_type_t (*sd_cmd_handler)(SDState *sd, SDRequest req);

typedef struct SDProto {
    const char *name;
    struct {
        const unsigned class;
        const sd_cmd_type_t type;
        const char *name;
        sd_cmd_handler handler;
    } cmd[SDMMC_CMD_MAX], acmd[SDMMC_CMD_MAX];
} SDProto;

struct SDState {
    DeviceState parent_obj;

    /* SD Memory Card Registers */
    uint32_t ocr;
    uint8_t scr[8];
    uint8_t cid[16];
    uint8_t csd[16];
    uint16_t rca;
    uint32_t card_status;
    uint8_t sd_status[64];
    union {
        uint8_t ext_csd[512];
        struct {
            uint8_t ext_csd_rw[192]; /* Modes segment */
            uint8_t ext_csd_ro[320]; /* Properties segment */
        };
    };

    /* Static properties */

    uint8_t spec_version;
    uint64_t boot_part_size;
    BlockBackend *blk;
    uint8_t boot_config;

    const SDProto *proto;

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
    const char *last_cmd_name;
    /* True if we will handle the next command as an ACMD. Note that this does
     * *not* track the APP_CMD status bit!
     */
    bool expecting_acmd;
    uint32_t blk_written;

    uint64_t data_start;
    uint32_t data_offset;
    size_t data_size;
    uint8_t data[512];
    QEMUTimer *ocr_power_timer;
    uint8_t dat_lines;
    bool cmd_line;
};

static void sd_realize(DeviceState *dev, Error **errp);

static const SDProto sd_proto_spi;
static const SDProto sd_proto_emmc;

static bool sd_is_spi(SDState *sd)
{
    return sd->proto == &sd_proto_spi;
}

static bool sd_is_emmc(SDState *sd)
{
    return sd->proto == &sd_proto_emmc;
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

static const char *sd_mode_name(enum SDCardModes mode)
{
    static const char *mode_name[] = {
        [sd_inactive]                   = "inactive",
        [sd_card_identification_mode]   = "identification",
        [sd_data_transfer_mode]         = "transfer",
    };
    assert(mode < ARRAY_SIZE(mode_name));
    return mode_name[mode];
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
        [sd_bus_test_state]         = "bus-test",
        [sd_receivingdata_state]    = "receivingdata",
        [sd_programming_state]      = "programming",
        [sd_disconnect_state]       = "disconnect",
        [sd_sleep_state]            = "sleep",
        [sd_io_state]               = "i/o"
    };
    if (state == sd_inactive_state) {
        return "inactive";
    }
    if (state == sd_waitirq_state) {
        return "wait-irq";
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

static const char *sd_cmd_name(SDState *sd, uint8_t cmd)
{
    static const char *cmd_abbrev[SDMMC_CMD_MAX] = {
        [18]    = "READ_MULTIPLE_BLOCK",
                                            [25]    = "WRITE_MULTIPLE_BLOCK",
    };
    const SDProto *sdp = sd->proto;

    if (sdp->cmd[cmd].handler) {
        assert(!cmd_abbrev[cmd]);
        return sdp->cmd[cmd].name;
    }
    return cmd_abbrev[cmd] ? cmd_abbrev[cmd] : "UNKNOWN_CMD";
}

static const char *sd_acmd_name(SDState *sd, uint8_t cmd)
{
    const SDProto *sdp = sd->proto;

    if (sdp->acmd[cmd].handler) {
        return sdp->acmd[cmd].name;
    }

    return "UNKNOWN_ACMD";
}

static uint8_t sd_get_dat_lines(SDState *sd)
{
    return sd->dat_lines;
}

static bool sd_get_cmd_line(SDState *sd)
{
    return sd->cmd_line;
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

/* Operation Conditions register */

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

/* SD Configuration register */

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

/* Card IDentification register */

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
    stl_be_p(&sd->cid[9], 0xdeadbeef); /* Fake serial number (PSN) */
    sd->cid[13] = 0x00 |    /* Manufacture date (MDT) */
        ((MDT_YR - 2000) / 10);
    sd->cid[14] = ((MDT_YR % 10) << 4) | MDT_MON;
    sd->cid[15] = (sd_crc7(sd->cid, 15) << 1) | 1;
}

static void emmc_set_cid(SDState *sd)
{
    sd->cid[0] = MID;       /* Fake card manufacturer ID (MID) */
    sd->cid[1] = 0b01;      /* CBX: soldered BGA */
    sd->cid[2] = OID[0];    /* OEM/Application ID (OID) */
    sd->cid[3] = PNM[0];    /* Fake product name (PNM) */
    sd->cid[4] = PNM[1];
    sd->cid[5] = PNM[2];
    sd->cid[6] = PNM[3];
    sd->cid[7] = PNM[4];
    sd->cid[8] = PNM[4];
    sd->cid[9] = PRV;       /* Fake product revision (PRV) */
    stl_be_p(&sd->cid[10], 0xdeadbeef); /* Fake serial number (PSN) */
    sd->cid[14] = (MDT_MON << 4) | (MDT_YR - 1997); /* Manufacture date (MDT) */
    sd->cid[15] = (sd_crc7(sd->cid, 15) << 1) | 1;
}

/* Card-Specific Data register */

#define HWBLOCK_SHIFT   9        /* 512 bytes */
#define SECTOR_SHIFT    5        /* 16 kilobytes */
#define WPGROUP_SHIFT   7        /* 2 megs */
#define CMULT_SHIFT     9        /* 512 times HWBLOCK_SIZE */
#define WPGROUP_SIZE    (1 << (HWBLOCK_SHIFT + SECTOR_SHIFT + WPGROUP_SHIFT))

static const uint8_t sd_csd_rw_mask[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0xfe,
};

static void emmc_set_ext_csd(SDState *sd, uint64_t size)
{
    uint32_t sectcount = size >> HWBLOCK_SHIFT;

    memset(sd->ext_csd, 0, sizeof(sd->ext_csd)); /* FIXME only RW at reset */

    /* Properties segment (RO) */
    sd->ext_csd[EXT_CSD_S_CMD_SET] = 0b1; /* supported command sets */
    sd->ext_csd[EXT_CSD_BOOT_INFO] = 0x0; /* Boot information */
                                     /* Boot partition size. 128KB unit */
    sd->ext_csd[EXT_CSD_BOOT_MULT] = sd->boot_part_size / (128 * KiB);
    sd->ext_csd[EXT_CSD_ACC_SIZE] = 0x1; /* Access size */
    sd->ext_csd[EXT_CSD_HC_ERASE_GRP_SIZE] = 0x01; /* HC Erase unit size */
    sd->ext_csd[EXT_CSD_ERASE_TIMEOUT_MULT] = 0x01; /* HC erase timeout */
    sd->ext_csd[EXT_CSD_REL_WR_SEC_C] = 0x1; /* Reliable write sector count */
    sd->ext_csd[EXT_CSD_HC_WP_GRP_SIZE] = 0x01; /* HC write protect group size */
    sd->ext_csd[EXT_CSD_S_C_VCC] = 0x01; /* Sleep current VCC  */
    sd->ext_csd[EXT_CSD_S_C_VCCQ] = 0x01; /* Sleep current VCCQ */
    sd->ext_csd[EXT_CSD_S_A_TIMEOUT] = 0x01; /* Sleep/Awake timeout */
    stl_le_p(&sd->ext_csd[EXT_CSD_SEC_CNT], sectcount); /* Sector count */
    sd->ext_csd[210] = 0x46; /* Min write perf for 8bit@52Mhz */
    sd->ext_csd[209] = 0x46; /* Min read perf for 8bit@52Mhz  */
    sd->ext_csd[208] = 0x46; /* Min write perf for 4bit@52Mhz */
    sd->ext_csd[207] = 0x46; /* Min read perf for 4bit@52Mhz */
    sd->ext_csd[206] = 0x46; /* Min write perf for 4bit@26Mhz */
    sd->ext_csd[205] = 0x46; /* Min read perf for 4bit@26Mhz */
    sd->ext_csd[EXT_CSD_CARD_TYPE] = 0b11;
    sd->ext_csd[EXT_CSD_STRUCTURE] = 2;
    sd->ext_csd[EXT_CSD_REV] = 3;

    /* Mode segment (RW) */
    sd->ext_csd[EXT_CSD_PART_CONFIG] = sd->boot_config;
}

static void emmc_set_csd(SDState *sd, uint64_t size)
{
    int hwblock_shift = HWBLOCK_SHIFT;
    uint32_t sectsize = (1 << (SECTOR_SHIFT + 1)) - 1;
    uint32_t wpsize = (1 << (WPGROUP_SHIFT + 1)) - 1;

    sd->csd[0] = (3 << 6) | (4 << 2); /* Spec v4.3 with EXT_CSD */
    sd->csd[1] = (1 << 3) | 6; /* Asynchronous data access time: 1ms */
    sd->csd[2] = 0x00;
    sd->csd[3] = (1 << 3) | 3;; /* Maximum bus clock frequency: 100MHz */
    sd->csd[4] = 0x0f;
    if (size <= 2 * GiB) {
        /* use 1k blocks */
        uint32_t csize1k = (size >> (CMULT_SHIFT + 10)) - 1;
        sd->csd[5] = 0x5a;
        sd->csd[6] = 0x80 | ((csize1k >> 10) & 0xf);
        sd->csd[7] = (csize1k >> 2) & 0xff;
    } else { /* >= 2GB : size stored in ext CSD, block addressing */
        sd->csd[5] = 0x59;
        sd->csd[6] = 0x8f;
        sd->csd[7] = 0xff;
        sd->ocr = FIELD_DP32(sd->ocr, OCR, CARD_CAPACITY, 1);
    }
    sd->csd[8] = 0xff;
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
    sd->csd[14] = 0x00;
    sd->csd[15] = (sd_crc7(sd->csd, 15) << 1) | 1;
    emmc_set_ext_csd(sd, size);
}

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
        st24_be_p(&sd->csd[7], size);
        sd->csd[10] = 0x7f;
        sd->csd[11] = 0x80;
        sd->csd[12] = 0x0a;
        sd->csd[13] = 0x40;
        sd->csd[14] = 0x00;
    }
    sd->csd[15] = (sd_crc7(sd->csd, 15) << 1) | 1;
}

/* Relative Card Address register */

static void sd_set_rca(SDState *sd, uint16_t value)
{
    trace_sdcard_set_rca(value);
    sd->rca = value;
}

static uint16_t sd_req_get_rca(SDState *s, SDRequest req)
{
    switch (s->proto->cmd[req.cmd].type) {
    case sd_ac:
    case sd_adtc:
        return req.arg >> 16;
    case sd_spi:
    default:
        g_assert_not_reached();
    }
}

static bool sd_req_rca_same(SDState *s, SDRequest req)
{
    return sd_req_get_rca(s, req) == s->rca;
}

/* Card Status register */

FIELD(CSR, AKE_SEQ_ERROR,               3,  1)
FIELD(CSR, APP_CMD,                     5,  1)
FIELD(CSR, FX_EVENT,                    6,  1)
FIELD(CSR, SWITCH_ERROR,                7,  1)
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
    sd->card_status = READY_FOR_DATA;
}

static void sd_set_sdstatus(SDState *sd)
{
    memset(sd->sd_status, 0, 64);
}

static const uint8_t sd_tuning_block_pattern4[64] = {
    /*
     * See: Physical Layer Simplified Specification Version 3.01,
     * Table 4-2.
     */
    0xff, 0x0f, 0xff, 0x00,     0x0f, 0xfc, 0xc3, 0xcc,
    0xc3, 0x3c, 0xcc, 0xff,     0xfe, 0xff, 0xfe, 0xef,
    0xff, 0xdf, 0xff, 0xdd,     0xff, 0xfb, 0xff, 0xfb,
    0xbf, 0xff, 0x7f, 0xff,     0x77, 0xf7, 0xbd, 0xef,
    0xff, 0xf0, 0xff, 0xf0,     0x0f, 0xfc, 0xcc, 0x3c,
    0xcc, 0x33, 0xcc, 0xcf,     0xff, 0xef, 0xff, 0xee,
    0xff, 0xfd, 0xff, 0xfd,     0xdf, 0xff, 0xbf, 0xff,
    0xbb, 0xff, 0xf7, 0xff,     0xf7, 0x7f, 0x7b, 0xde
};

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

static uint32_t sd_blk_len(SDState *sd)
{
    if (FIELD_EX32(sd->ocr, OCR, CARD_CAPACITY)) {
        return 1 << HWBLOCK_SHIFT;
    }
    return sd->blk_len;
}

/*
 * This requires a disk image that has two boot partitions inserted at the
 * beginning of it. The size of the boot partitions is the "boot-size"
 * property.
 */
static uint32_t sd_bootpart_offset(SDState *sd)
{
    unsigned partition_access;

    if (!sd->boot_part_size || !sd_is_emmc(sd)) {
        return 0;
    }

    partition_access = sd->ext_csd[EXT_CSD_PART_CONFIG]
                                 & EXT_CSD_PART_CONFIG_ACC_MASK;
    switch (partition_access) {
    case EXT_CSD_PART_CONFIG_ACC_DEFAULT:
        return sd->boot_part_size * 2;
    case EXT_CSD_PART_CONFIG_ACC_BOOT0:
        return 0;
    case EXT_CSD_PART_CONFIG_ACC_BOOT0 + 1:
        return sd->boot_part_size * 1;
    default:
         g_assert_not_reached();
    }
}

static uint64_t sd_req_get_address(SDState *sd, SDRequest req)
{
    uint64_t addr;

    if (FIELD_EX32(sd->ocr, OCR, CARD_CAPACITY)) {
        addr = (uint64_t) req.arg << HWBLOCK_SHIFT;
    } else {
        addr = req.arg;
    }
    trace_sdcard_req_addr(req.arg, addr);
    return addr;
}

static inline uint64_t sd_addr_to_wpnum(uint64_t addr)
{
    return addr >> (HWBLOCK_SHIFT + SECTOR_SHIFT + WPGROUP_SHIFT);
}

static void sd_reset(DeviceState *dev)
{
    SDState *sd = SDMMC_COMMON(dev);
    SDCardClass *sc = SDMMC_COMMON_GET_CLASS(sd);
    uint64_t size;
    uint64_t sect;

    trace_sdcard_reset();
    if (sd->blk) {
        blk_get_geometry(sd->blk, &sect);
    } else {
        sect = 0;
    }
    size = sect << HWBLOCK_SHIFT;
    if (sd_is_emmc(sd)) {
        size -= sd->boot_part_size * 2;
    }

    sect = sd_addr_to_wpnum(size) + 1;

    sd->state = sd_idle_state;

    /* card registers */
    sd->rca = sd_is_emmc(sd) ? 0x0001 : 0x0000;
    sd->size = size;
    sd_set_ocr(sd);
    sd_set_scr(sd);
    sc->set_cid(sd);
    sc->set_csd(sd, size);
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

    sdbus = SD_BUS(qdev_get_parent_bus(dev));
    sdbus_set_inserted(sdbus, inserted);
    if (inserted) {
        sdbus_set_readonly(sdbus, readonly);
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
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ocr, SDState),
        VMSTATE_TIMER_PTR(ocr_power_timer, SDState),
        VMSTATE_END_OF_LIST()
    },
};

static bool vmstate_needed_for_emmc(void *opaque)
{
    SDState *sd = opaque;

    return sd_is_emmc(sd);
}

static const VMStateDescription emmc_extcsd_vmstate = {
    .name = "sd-card/ext_csd_modes-state",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vmstate_needed_for_emmc,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(ext_csd_rw, SDState, 192),
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
    .fields = (const VMStateField[]) {
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
        VMSTATE_UNUSED(1),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &sd_ocr_vmstate,
        &emmc_extcsd_vmstate,
        NULL
    },
};

static void sd_blk_read(SDState *sd, uint64_t addr, uint32_t len)
{
    trace_sdcard_read_block(addr, len);
    addr += sd_bootpart_offset(sd);
    if (!sd->blk || blk_pread(sd->blk, addr, len, sd->data, 0) < 0) {
        fprintf(stderr, "sd_blk_read: read error on host side\n");
    }
}

static void sd_blk_write(SDState *sd, uint64_t addr, uint32_t len)
{
    trace_sdcard_write_block(addr, len);
    addr += sd_bootpart_offset(sd);
    if (!sd->blk || blk_pwrite(sd->blk, addr, len, sd->data, 0) < 0) {
        fprintf(stderr, "sd_blk_write: write error on host side\n");
    }
}

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
        erase_start <<= HWBLOCK_SHIFT;
        erase_end <<= HWBLOCK_SHIFT;
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
        sd_blk_write(sd, erase_addr, erase_len);
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

enum ExtCsdAccessMode {
    EXT_CSD_ACCESS_MODE_COMMAND_SET = 0,
    EXT_CSD_ACCESS_MODE_SET_BITS    = 1,
    EXT_CSD_ACCESS_MODE_CLEAR_BITS  = 2,
    EXT_CSD_ACCESS_MODE_WRITE_BYTE  = 3
};

static void emmc_function_switch(SDState *sd, uint32_t arg)
{
    uint8_t access = extract32(arg, 24, 2);
    uint8_t index = extract32(arg, 16, 8);
    uint8_t value = extract32(arg, 8, 8);
    uint8_t b = sd->ext_csd[index];

    trace_sdcard_switch(access, index, value, extract32(arg, 0, 2));

    if (index >= 192) {
        qemu_log_mask(LOG_GUEST_ERROR, "MMC switching illegal offset\n");
        sd->card_status |= R_CSR_SWITCH_ERROR_MASK;
        return;
    }

    switch (access) {
    case EXT_CSD_ACCESS_MODE_COMMAND_SET:
        qemu_log_mask(LOG_UNIMP, "MMC Command set switching not supported\n");
        return;
    case EXT_CSD_ACCESS_MODE_SET_BITS:
        b |= value;
        break;
    case EXT_CSD_ACCESS_MODE_CLEAR_BITS:
        b &= ~value;
        break;
    case EXT_CSD_ACCESS_MODE_WRITE_BYTE:
        b = value;
        break;
    }

    trace_sdcard_ext_csd_update(index, sd->ext_csd[index], b);
    sd->ext_csd[index] = b;
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
                  sd->proto->name, req.cmd, sd_state_name(sd->state),
                  sd_version_str(sd->spec_version));

    return sd_illegal;
}

static sd_rsp_type_t sd_invalid_mode_for_cmd(SDState *sd, SDRequest req)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: CMD%i in a wrong mode: %s (spec %s)\n",
                  sd->proto->name, req.cmd, sd_mode_name(sd->mode),
                  sd_version_str(sd->spec_version));

    return sd_illegal;
}

static sd_rsp_type_t sd_cmd_illegal(SDState *sd, SDRequest req)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown CMD%i for spec %s\n",
                  sd->proto->name, req.cmd,
                  sd_version_str(sd->spec_version));

    return sd_illegal;
}

/* Commands that are recognised but not yet implemented. */
static sd_rsp_type_t sd_cmd_unimplemented(SDState *sd, SDRequest req)
{
    qemu_log_mask(LOG_UNIMP, "%s: CMD%i not implemented\n",
                  sd->proto->name, req.cmd);

    return sd_illegal;
}

static sd_rsp_type_t sd_cmd_optional(SDState *sd, SDRequest req)
{
    qemu_log_mask(LOG_UNIMP, "%s: Optional CMD%i not implemented\n",
                  sd->proto->name, req.cmd);

    return sd_illegal;
}

/* Configure fields for following sd_generic_write_byte() calls */
static sd_rsp_type_t sd_cmd_to_receivingdata(SDState *sd, SDRequest req,
                                             uint64_t start, size_t size)
{
    if (sd->state != sd_transfer_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }
    sd->state = sd_receivingdata_state;
    sd->data_start = start;
    sd->data_offset = 0;
    /* sd->data[] used as receive buffer */
    sd->data_size = size ?: sizeof(sd->data);
    return sd_r1;
}

/* Configure fields for following sd_generic_read_byte() calls */
static sd_rsp_type_t sd_cmd_to_sendingdata(SDState *sd, SDRequest req,
                                           uint64_t start,
                                           const void *data, size_t size)
{
    if (sd->state != sd_transfer_state) {
        sd_invalid_state_for_cmd(sd, req);
    }

    sd->state = sd_sendingdata_state;
    sd->data_start = start;
    sd->data_offset = 0;
    if (data) {
        assert(size > 0 && size <= sizeof(sd->data));
        memcpy(sd->data, data, size);
    }
    if (size) {
        sd->data_size = size;
    }
    return sd_r1;
}

/* CMD0 */
static sd_rsp_type_t sd_cmd_GO_IDLE_STATE(SDState *sd, SDRequest req)
{
    if (sd->state == sd_sleep_state) {
        switch (req.arg) {
        case 0x00000000:
        case 0xf0f0f0f0:
            break;
        default:
            return sd_r0;
        }
    }
    if (sd->state != sd_inactive_state) {
        sd->state = sd_idle_state;
        sd_reset(DEVICE(sd));
    }

    return sd_is_spi(sd) ? sd_r1 : sd_r0;
}

/* CMD1 */
static sd_rsp_type_t spi_cmd_SEND_OP_COND(SDState *sd, SDRequest req)
{
    sd->state = sd_transfer_state;

    return sd_r1;
}

/* CMD2 */
static sd_rsp_type_t sd_cmd_ALL_SEND_CID(SDState *sd, SDRequest req)
{
    switch (sd->state) {
    case sd_ready_state:
        sd->state = sd_identification_state;
        return sd_r2_i;
    default:
        return sd_invalid_state_for_cmd(sd, req);
    }
}

/* CMD3 */
static sd_rsp_type_t sd_cmd_SEND_RELATIVE_ADDR(SDState *sd, SDRequest req)
{
    uint16_t random_rca;

    switch (sd->state) {
    case sd_identification_state:
    case sd_standby_state:
        sd->state = sd_standby_state;
        qemu_guest_getrandom_nofail(&random_rca, sizeof(random_rca));
        sd_set_rca(sd, random_rca);
        return sd_r6;

    default:
        return sd_invalid_state_for_cmd(sd, req);
    }
}

static sd_rsp_type_t emmc_cmd_SET_RELATIVE_ADDR(SDState *sd, SDRequest req)
{
    switch (sd->state) {
    case sd_identification_state:
    case sd_standby_state:
        sd->state = sd_standby_state;
        sd_set_rca(sd, req.arg >> 16);
        return sd_r1;

    default:
        return sd_invalid_state_for_cmd(sd, req);
    }
}

/* CMD5 */
static sd_rsp_type_t emmc_cmd_sleep_awake(SDState *sd, SDRequest req)
{
    bool do_sleep = extract32(req.arg, 15, 1);

    switch (sd->state) {
    case sd_sleep_state:
        if (!do_sleep) {
            /* Awake */
            sd->state = sd_standby_state;
        }
        return sd_r1b;

    case sd_standby_state:
        if (do_sleep) {
            sd->state = sd_sleep_state;
        }
        return sd_r1b;

    default:
        return sd_invalid_state_for_cmd(sd, req);
    }
}

/* CMD6 */
static sd_rsp_type_t sd_cmd_SWITCH_FUNCTION(SDState *sd, SDRequest req)
{
    if (sd->mode != sd_data_transfer_mode) {
        return sd_invalid_mode_for_cmd(sd, req);
    }
    if (sd->state != sd_transfer_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }

    sd_function_switch(sd, req.arg);
    return sd_cmd_to_sendingdata(sd, req, 0, NULL, 64);
}

static sd_rsp_type_t emmc_cmd_SWITCH(SDState *sd, SDRequest req)
{
    switch (sd->state) {
    case sd_transfer_state:
        sd->state = sd_programming_state;
        emmc_function_switch(sd, req.arg);
        sd->state = sd_transfer_state;
        return sd_r1b;
    default:
        return sd_invalid_state_for_cmd(sd, req);
    }
}

/* CMD7 */
static sd_rsp_type_t sd_cmd_DE_SELECT_CARD(SDState *sd, SDRequest req)
{
    bool same_rca = sd_req_rca_same(sd, req);

    switch (sd->state) {
    case sd_standby_state:
        if (!same_rca) {
            return sd_r0;
        }
        sd->state = sd_transfer_state;
        return sd_r1b;

    case sd_transfer_state:
    case sd_sendingdata_state:
        if (same_rca) {
            break;
        }
        sd->state = sd_standby_state;
        return sd_r1b;

    case sd_disconnect_state:
        if (!same_rca) {
            return sd_r0;
        }
        sd->state = sd_programming_state;
        return sd_r1b;

    case sd_programming_state:
        if (same_rca) {
            break;
        }
        sd->state = sd_disconnect_state;
        return sd_r1b;

    default:
        break;
    }
    return sd_invalid_state_for_cmd(sd, req);
}

/* CMD8 */
static sd_rsp_type_t sd_cmd_SEND_IF_COND(SDState *sd, SDRequest req)
{
    if (sd->spec_version < SD_PHY_SPECv2_00_VERS) {
        return sd_cmd_illegal(sd, req);
    }
    if (sd->state != sd_idle_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }
    sd->vhs = 0;

    /* No response if not exactly one VHS bit is set.  */
    if (!(req.arg >> 8) || (req.arg >> (ctz32(req.arg & ~0xff) + 1))) {
        return sd_is_spi(sd) ? sd_r7 : sd_r0;
    }

    /* Accept.  */
    sd->vhs = req.arg;
    return sd_r7;
}

/* CMD8 */
static sd_rsp_type_t emmc_cmd_SEND_EXT_CSD(SDState *sd, SDRequest req)
{
    if (sd->state != sd_transfer_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }

    return sd_cmd_to_sendingdata(sd, req, sd_req_get_address(sd, req),
                                 sd->ext_csd, sizeof(sd->ext_csd));
}

/* CMD9 */
static sd_rsp_type_t spi_cmd_SEND_CSD(SDState *sd, SDRequest req)
{
    if (sd->state != sd_standby_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }
    return sd_cmd_to_sendingdata(sd, req, sd_req_get_address(sd, req),
                                 sd->csd, 16);
}

static sd_rsp_type_t sd_cmd_SEND_CSD(SDState *sd, SDRequest req)
{
    if (sd->state != sd_standby_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }

    return sd_req_rca_same(sd, req) ? sd_r2_s : sd_r0;
}

/* CMD10 */
static sd_rsp_type_t spi_cmd_SEND_CID(SDState *sd, SDRequest req)
{
    if (sd->state != sd_standby_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }
    return sd_cmd_to_sendingdata(sd, req, sd_req_get_address(sd, req),
                                 sd->cid, 16);
}

static sd_rsp_type_t sd_cmd_SEND_CID(SDState *sd, SDRequest req)
{
    if (sd->state != sd_standby_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }

    return sd_req_rca_same(sd, req) ? sd_r2_i : sd_r0;
}

/* CMD12 */
static sd_rsp_type_t sd_cmd_STOP_TRANSMISSION(SDState *sd, SDRequest req)
{
    switch (sd->state) {
    case sd_sendingdata_state:
        sd->state = sd_transfer_state;
        return sd_r1b;
    case sd_receivingdata_state:
        sd->state = sd_programming_state;
        /* Bzzzzzzztt .... Operation complete.  */
        sd->state = sd_transfer_state;
        return sd_r1;
    default:
        return sd_invalid_state_for_cmd(sd, req);
    }
}

/* CMD13 */
static sd_rsp_type_t sd_cmd_SEND_STATUS(SDState *sd, SDRequest req)
{
    if (sd->mode != sd_data_transfer_mode) {
        return sd_invalid_mode_for_cmd(sd, req);
    }

    switch (sd->state) {
    case sd_standby_state:
    case sd_transfer_state:
    case sd_sendingdata_state:
    case sd_receivingdata_state:
    case sd_programming_state:
    case sd_disconnect_state:
        break;
    default:
        return sd_invalid_state_for_cmd(sd, req);
    }

    if (sd_is_spi(sd)) {
        return sd_r2_s;
    }

    return sd_req_rca_same(sd, req) ? sd_r1 : sd_r0;
}

/* CMD15 */
static sd_rsp_type_t sd_cmd_GO_INACTIVE_STATE(SDState *sd, SDRequest req)
{
    if (sd->mode != sd_data_transfer_mode) {
        return sd_invalid_mode_for_cmd(sd, req);
    }
    switch (sd->state) {
    case sd_standby_state:
    case sd_transfer_state:
    case sd_sendingdata_state:
    case sd_receivingdata_state:
    case sd_programming_state:
    case sd_disconnect_state:
        break;
    default:
        return sd_invalid_state_for_cmd(sd, req);
    }
    if (sd_req_rca_same(sd, req)) {
        sd->state = sd_inactive_state;
    }

    return sd_r0;
}

/* CMD16 */
static sd_rsp_type_t sd_cmd_SET_BLOCKLEN(SDState *sd, SDRequest req)
{
    if (sd->state != sd_transfer_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }
    if (req.arg > (1 << HWBLOCK_SHIFT)) {
        sd->card_status |= BLOCK_LEN_ERROR;
    } else {
        trace_sdcard_set_blocklen(req.arg);
        sd->blk_len = req.arg;
    }

    return sd_r1;
}

/* CMD17 */
static sd_rsp_type_t sd_cmd_READ_SINGLE_BLOCK(SDState *sd, SDRequest req)
{
    uint64_t addr;

    if (sd->state != sd_transfer_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }

    addr = sd_req_get_address(sd, req);
    if (!address_in_range(sd, "READ_SINGLE_BLOCK", addr, sd->blk_len)) {
        return sd_r1;
    }

    sd_blk_read(sd, addr, sd->blk_len);
    return sd_cmd_to_sendingdata(sd, req, addr, NULL, sd->blk_len);
}

/* CMD19 */
static sd_rsp_type_t sd_cmd_SEND_TUNING_BLOCK(SDState *sd, SDRequest req)
{
    if (sd->spec_version < SD_PHY_SPECv3_01_VERS) {
        return sd_cmd_illegal(sd, req);
    }

    return sd_cmd_to_sendingdata(sd, req, 0,
                                 sd_tuning_block_pattern4,
                                 sizeof(sd_tuning_block_pattern4));
}

/* CMD23 */
static sd_rsp_type_t sd_cmd_SET_BLOCK_COUNT(SDState *sd, SDRequest req)
{
    if (sd->spec_version < SD_PHY_SPECv3_01_VERS) {
        return sd_cmd_illegal(sd, req);
    }

    if (sd->state != sd_transfer_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }

    sd->multi_blk_cnt = req.arg;
    if (sd_is_emmc(sd)) {
        sd->multi_blk_cnt &= 0xffff;
    }
    trace_sdcard_set_block_count(sd->multi_blk_cnt);

    return sd_r1;
}

/* CMD24 */
static sd_rsp_type_t sd_cmd_WRITE_SINGLE_BLOCK(SDState *sd, SDRequest req)
{
    uint64_t addr;

    if (sd->state != sd_transfer_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }

    addr = sd_req_get_address(sd, req);
    if (!address_in_range(sd, "WRITE_SINGLE_BLOCK", addr, sd->blk_len)) {
        return sd_r1;
    }

    if (sd->size <= SDSC_MAX_CAPACITY) {
        if (sd_wp_addr(sd, addr)) {
            sd->card_status |= WP_VIOLATION;
        }
    }
    if (sd->csd[14] & 0x30) {
        sd->card_status |= WP_VIOLATION;
    }

    sd->blk_written = 0;
    return sd_cmd_to_receivingdata(sd, req, addr, sd->blk_len);
}

/* CMD26 */
static sd_rsp_type_t emmc_cmd_PROGRAM_CID(SDState *sd, SDRequest req)
{
    return sd_cmd_to_receivingdata(sd, req, 0, sizeof(sd->cid));
}

/* CMD27 */
static sd_rsp_type_t sd_cmd_PROGRAM_CSD(SDState *sd, SDRequest req)
{
    return sd_cmd_to_receivingdata(sd, req, 0, sizeof(sd->csd));
}

static sd_rsp_type_t sd_cmd_SET_CLR_WRITE_PROT(SDState *sd, SDRequest req,
                                               bool is_write)
{
    uint64_t addr;

    if (sd->size > SDSC_MAX_CAPACITY) {
        return sd_illegal;
    }

    if (sd->state != sd_transfer_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }

    addr = sd_req_get_address(sd, req);
    if (!address_in_range(sd, is_write ? "SET_WRITE_PROT" : "CLR_WRITE_PROT",
                          addr, 1)) {
        return sd_r1b;
    }

    sd->state = sd_programming_state;
    if (is_write) {
        set_bit(sd_addr_to_wpnum(addr), sd->wp_group_bmap);
    } else {
        clear_bit(sd_addr_to_wpnum(addr), sd->wp_group_bmap);
    }
    /* Bzzzzzzztt .... Operation complete.  */
    sd->state = sd_transfer_state;
    return sd_r1;
}

/* CMD28 */
static sd_rsp_type_t sd_cmd_SET_WRITE_PROT(SDState *sd, SDRequest req)
{
    return sd_cmd_SET_CLR_WRITE_PROT(sd, req, true);
}

/* CMD29 */
static sd_rsp_type_t sd_cmd_CLR_WRITE_PROT(SDState *sd, SDRequest req)
{
    return sd_cmd_SET_CLR_WRITE_PROT(sd, req, false);
}

/* CMD30 */
static sd_rsp_type_t sd_cmd_SEND_WRITE_PROT(SDState *sd, SDRequest req)
{
    uint64_t addr;
    uint32_t data;

    if (sd->size > SDSC_MAX_CAPACITY) {
        return sd_illegal;
    }

    if (sd->state != sd_transfer_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }

    addr = sd_req_get_address(sd, req);
    if (!address_in_range(sd, "SEND_WRITE_PROT", addr, sd->blk_len)) {
        return sd_r1;
    }

    data = sd_wpbits(sd, req.arg);
    return sd_cmd_to_sendingdata(sd, req, addr, &data, sizeof(data));
}

/* CMD32 */
static sd_rsp_type_t sd_cmd_ERASE_WR_BLK_START(SDState *sd, SDRequest req)
{
    if (sd->state != sd_transfer_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }
    sd->erase_start = req.arg;
    return sd_r1;
}

/* CMD33 */
static sd_rsp_type_t sd_cmd_ERASE_WR_BLK_END(SDState *sd, SDRequest req)
{
    if (sd->state != sd_transfer_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }
    sd->erase_end = req.arg;
    return sd_r1;
}

/* CMD38 */
static sd_rsp_type_t sd_cmd_ERASE(SDState *sd, SDRequest req)
{
    if (sd->state != sd_transfer_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }
    if (sd->csd[14] & 0x30) {
        sd->card_status |= WP_VIOLATION;
        return sd_r1b;
    }

    sd->state = sd_programming_state;
    sd_erase(sd);
    /* Bzzzzzzztt .... Operation complete.  */
    sd->state = sd_transfer_state;
    return sd_r1b;
}

/* CMD42 */
static sd_rsp_type_t sd_cmd_LOCK_UNLOCK(SDState *sd, SDRequest req)
{
    return sd_cmd_to_receivingdata(sd, req, 0, 0);
}

/* CMD55 */
static sd_rsp_type_t sd_cmd_APP_CMD(SDState *sd, SDRequest req)
{
    switch (sd->state) {
    case sd_ready_state:
    case sd_identification_state:
    case sd_inactive_state:
    case sd_sleep_state:
        return sd_invalid_state_for_cmd(sd, req);
    case sd_idle_state:
        if (!sd_is_spi(sd) && sd_req_get_rca(sd, req) != 0x0000) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "SD: illegal RCA 0x%04x for APP_CMD\n", req.cmd);
        }
        /* fall-through */
    default:
        break;
    }
    if (!sd_is_spi(sd) && !sd_req_rca_same(sd, req)) {
        return sd_r0;
    }
    sd->expecting_acmd = true;
    sd->card_status |= APP_CMD;

    return sd_r1;
}

/* CMD56 */
static sd_rsp_type_t sd_cmd_GEN_CMD(SDState *sd, SDRequest req)
{
    if (sd->state != sd_transfer_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }

    /* Vendor specific command: our model is RAZ/WI */
    if (req.arg & 1) {
        memset(sd->data, 0, sizeof(sd->data));
        return sd_cmd_to_sendingdata(sd, req, 0, NULL, 0);
    } else {
        return sd_cmd_to_receivingdata(sd, req, 0, 0);
    }
}

/* CMD58 */
static sd_rsp_type_t spi_cmd_READ_OCR(SDState *sd, SDRequest req)
{
    return sd_r3;
}

/* CMD59 */
static sd_rsp_type_t spi_cmd_CRC_ON_OFF(SDState *sd, SDRequest req)
{
    return sd_r1;
}

/* ACMD6 */
static sd_rsp_type_t sd_acmd_SET_BUS_WIDTH(SDState *sd, SDRequest req)
{
    if (sd->state != sd_transfer_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }

    sd->sd_status[0] &= 0x3f;
    sd->sd_status[0] |= (req.arg & 0x03) << 6;
    return sd_r1;
}

/* ACMD13 */
static sd_rsp_type_t sd_acmd_SD_STATUS(SDState *sd, SDRequest req)
{
    return sd_cmd_to_sendingdata(sd, req, 0,
                                 sd->sd_status, sizeof(sd->sd_status));
}

/* ACMD22 */
static sd_rsp_type_t sd_acmd_SEND_NUM_WR_BLOCKS(SDState *sd, SDRequest req)
{
    return sd_cmd_to_sendingdata(sd, req, 0,
                                 &sd->blk_written, sizeof(sd->blk_written));
}

/* ACMD23 */
static sd_rsp_type_t sd_acmd_SET_WR_BLK_ERASE_COUNT(SDState *sd, SDRequest req)
{
    if (sd->state != sd_transfer_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }
    return sd_r1;
}

/* ACMD41 */
static sd_rsp_type_t sd_cmd_SEND_OP_COND(SDState *sd, SDRequest req)
{
    if (sd->state != sd_idle_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }

    /*
     * If it's the first ACMD41 since reset, we need to decide
     * whether to power up. If this is not an enquiry ACMD41,
     * we immediately report power on and proceed below to the
     * ready state, but if it is, we set a timer to model a
     * delay for power up. This works around a bug in EDK2
     * UEFI, which sends an initial enquiry ACMD41, but
     * assumes that the card is in ready state as soon as it
     * sees the power up bit set.
     */
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
        /*
         * We accept any voltage.  10000 V is nothing.
         *
         * Once we're powered up, we advance straight to ready state
         * unless it's an enquiry ACMD41 (bits 23:0 == 0).
         */
        sd->state = sd_ready_state;
    }

    return sd_r3;
}

/* ACMD42 */
static sd_rsp_type_t sd_acmd_SET_CLR_CARD_DETECT(SDState *sd, SDRequest req)
{
    if (sd->state != sd_transfer_state) {
        return sd_invalid_state_for_cmd(sd, req);
    }

    /* Bringing in the 50KOhm pull-up resistor... Done.  */
    return sd_r1;
}

/* ACMD51 */
static sd_rsp_type_t sd_acmd_SEND_SCR(SDState *sd, SDRequest req)
{
    return sd_cmd_to_sendingdata(sd, req, 0, sd->scr, sizeof(sd->scr));
}

static sd_rsp_type_t sd_normal_command(SDState *sd, SDRequest req)
{
    uint64_t addr;

    sd->last_cmd_name = sd_cmd_name(sd, req.cmd);
    /* CMD55 precedes an ACMD, so we are not interested in tracing it.
     * However there is no ACMD55, so we want to trace this particular case.
     */
    if (req.cmd != 55 || sd->expecting_acmd) {
        trace_sdcard_normal_command(sd->proto->name,
                                    sd->last_cmd_name, req.cmd,
                                    req.arg, sd_state_name(sd->state));
    }

    /* Not interpreting this as an app command */
    sd->card_status &= ~APP_CMD;

    /* CMD23 (set block count) must be immediately followed by CMD18 or CMD25
     * if not, its effects are cancelled */
    if (sd->multi_blk_cnt != 0 && !(req.cmd == 18 || req.cmd == 25)) {
        sd->multi_blk_cnt = 0;
    }

    if (sd->proto->cmd[req.cmd].class == 6 && FIELD_EX32(sd->ocr, OCR,
                                                         CARD_CAPACITY)) {
        /* Only Standard Capacity cards support class 6 commands */
        return sd_illegal;
    }

    if (sd->proto->cmd[req.cmd].handler) {
        return sd->proto->cmd[req.cmd].handler(sd, req);
    }

    switch (req.cmd) {
    /* Block read commands (Class 2) */
    case 18:  /* CMD18:  READ_MULTIPLE_BLOCK */
        addr = sd_req_get_address(sd, req);
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
    case 25:  /* CMD25:  WRITE_MULTIPLE_BLOCK */
        addr = sd_req_get_address(sd, req);
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

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "SD: Unknown CMD%i\n", req.cmd);
        return sd_illegal;
    }

    return sd_invalid_state_for_cmd(sd, req);
}

static sd_rsp_type_t sd_app_command(SDState *sd,
                                    SDRequest req)
{
    sd->last_cmd_name = sd_acmd_name(sd, req.cmd);
    trace_sdcard_app_command(sd->proto->name, sd->last_cmd_name,
                             req.cmd, req.arg, sd_state_name(sd->state));
    sd->card_status |= APP_CMD;

    if (sd->proto->acmd[req.cmd].handler) {
        return sd->proto->acmd[req.cmd].handler(sd, req);
    }

    switch (req.cmd) {
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

static bool cmd_valid_while_locked(SDState *sd, unsigned cmd)
{
    unsigned cmd_class;

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
        return true;
    }
    if (!sd->proto->cmd[cmd].handler) {
        return false;
    }
    cmd_class = sd->proto->cmd[cmd].class;

    return cmd_class == 0 || cmd_class == 7;
}

static int sd_do_command(SDState *sd, SDRequest *req,
                         uint8_t *response) {
    int last_state;
    sd_rsp_type_t rtype;
    int rsplen;

    if (!sd->blk || !blk_is_inserted(sd->blk)) {
        return 0;
    }

    if (sd->state == sd_inactive_state) {
        rtype = sd_illegal;
        goto send_response;
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

    if (sd->state == sd_sleep_state && req->cmd) {
        qemu_log_mask(LOG_GUEST_ERROR, "SD: Card is sleeping\n");
        rtype = sd_r0;
        goto send_response;
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
        sd->card_status = FIELD_DP32(sd->card_status, CSR,
                                     CURRENT_STATE, last_state);
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
        /*
         * Invalid state transition, reset implementation
         * fields to avoid OOB abuse.
         */
        sd->data_start = 0;
        sd->data_offset = 0;
        /* fall-through */
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

    sd->current_cmd = rtype == sd_illegal ? 0 : req->cmd;

    return rsplen;
}

/* Return true if buffer is consumed. Configured by sd_cmd_to_receivingdata() */
static bool sd_generic_write_byte(SDState *sd, uint8_t value)
{
    sd->data[sd->data_offset] = value;

    if (++sd->data_offset >= sd->data_size) {
        sd->state = sd_transfer_state;
        return true;
    }
    return false;
}

/* Return true when buffer is consumed. Configured by sd_cmd_to_sendingdata() */
static bool sd_generic_read_byte(SDState *sd, uint8_t *value)
{
    *value = sd->data[sd->data_offset];

    if (++sd->data_offset >= sd->data_size) {
        sd->state = sd_transfer_state;
        return true;
    }

    return false;
}

static void sd_write_byte(SDState *sd, uint8_t value)
{
    int i;

    if (!sd->blk || !blk_is_inserted(sd->blk)) {
        return;
    }

    if (sd->state != sd_receivingdata_state) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: not in Receiving-Data state\n", __func__);
        return;
    }

    if (sd->card_status & (ADDRESS_ERROR | WP_VIOLATION))
        return;

    trace_sdcard_write_data(sd->proto->name,
                            sd->last_cmd_name,
                            sd->current_cmd, sd->data_offset, value);
    switch (sd->current_cmd) {
    case 24:  /* CMD24:  WRITE_SINGLE_BLOCK */
        if (sd_generic_write_byte(sd, value)) {
            /* TODO: Check CRC before committing */
            sd->state = sd_programming_state;
            sd_blk_write(sd, sd->data_start, sd->data_offset);
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
            sd_blk_write(sd, sd->data_start, sd->data_offset);
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
        if (sd_generic_write_byte(sd, value)) {
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
        if (sd_generic_write_byte(sd, value)) {
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
        if (sd_generic_write_byte(sd, value)) {
            /* TODO: Check CRC before committing */
            sd->state = sd_programming_state;
            sd_lock_command(sd);
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
        }
        break;

    case 56:  /* CMD56:  GEN_CMD */
        sd_generic_write_byte(sd, value);
        break;

    default:
        g_assert_not_reached();
    }
}

static uint8_t sd_read_byte(SDState *sd)
{
    /* TODO: Append CRCs */
    const uint8_t dummy_byte = 0x00;
    uint8_t ret;
    uint32_t io_len;

    if (!sd->blk || !blk_is_inserted(sd->blk)) {
        return dummy_byte;
    }

    if (sd->state != sd_sendingdata_state) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: not in Sending-Data state\n", __func__);
        return dummy_byte;
    }

    if (sd->card_status & (ADDRESS_ERROR | WP_VIOLATION)) {
        return dummy_byte;
    }

    io_len = sd_blk_len(sd);

    trace_sdcard_read_data(sd->proto->name,
                           sd->last_cmd_name, sd->current_cmd,
                           sd->data_offset, sd->data_size, io_len);
    switch (sd->current_cmd) {
    case 6:  /* CMD6:   SWITCH_FUNCTION */
    case 8:  /* CMD8:   SEND_EXT_CSD */
    case 9:  /* CMD9:   SEND_CSD */
    case 10: /* CMD10:  SEND_CID */
    case 13: /* ACMD13: SD_STATUS */
    case 17: /* CMD17:  READ_SINGLE_BLOCK */
    case 19: /* CMD19:  SEND_TUNING_BLOCK (SD) */
    case 22: /* ACMD22: SEND_NUM_WR_BLOCKS */
    case 30: /* CMD30:  SEND_WRITE_PROT */
    case 51: /* ACMD51: SEND_SCR */
    case 56: /* CMD56:  GEN_CMD */
        sd_generic_read_byte(sd, &ret);
        break;

    case 18:  /* CMD18:  READ_MULTIPLE_BLOCK */
        if (sd->data_offset == 0) {
            if (!address_in_range(sd, "READ_MULTIPLE_BLOCK",
                                  sd->data_start, io_len)) {
                return dummy_byte;
            }
            sd_blk_read(sd, sd->data_start, io_len);
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

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: DAT read illegal for command %s\n",
                                       __func__, sd->last_cmd_name);
        return dummy_byte;
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

static const SDProto sd_proto_spi = {
    .name = "SPI",
    .cmd = {
        [0]  = {0,  sd_spi, "GO_IDLE_STATE", sd_cmd_GO_IDLE_STATE},
        [1]  = {0,  sd_spi, "SEND_OP_COND", spi_cmd_SEND_OP_COND},
        [5]  = {9,  sd_spi, "IO_SEND_OP_COND", sd_cmd_optional},
        [6]  = {10, sd_spi, "SWITCH_FUNCTION", sd_cmd_SWITCH_FUNCTION},
        [8]  = {0,  sd_spi, "SEND_IF_COND", sd_cmd_SEND_IF_COND},
        [9]  = {0,  sd_spi, "SEND_CSD", spi_cmd_SEND_CSD},
        [10] = {0,  sd_spi, "SEND_CID", spi_cmd_SEND_CID},
        [12] = {0,  sd_spi, "STOP_TRANSMISSION", sd_cmd_STOP_TRANSMISSION},
        [13] = {0,  sd_spi, "SEND_STATUS", sd_cmd_SEND_STATUS},
        [16] = {2,  sd_spi, "SET_BLOCKLEN", sd_cmd_SET_BLOCKLEN},
        [17] = {2,  sd_spi, "READ_SINGLE_BLOCK", sd_cmd_READ_SINGLE_BLOCK},
        [24] = {4,  sd_spi, "WRITE_SINGLE_BLOCK", sd_cmd_WRITE_SINGLE_BLOCK},
        [27] = {4,  sd_spi, "PROGRAM_CSD", sd_cmd_PROGRAM_CSD},
        [28] = {6,  sd_spi, "SET_WRITE_PROT", sd_cmd_SET_WRITE_PROT},
        [29] = {6,  sd_spi, "CLR_WRITE_PROT", sd_cmd_CLR_WRITE_PROT},
        [30] = {6,  sd_spi, "SEND_WRITE_PROT", sd_cmd_SEND_WRITE_PROT},
        [32] = {5,  sd_spi, "ERASE_WR_BLK_START", sd_cmd_ERASE_WR_BLK_START},
        [33] = {5,  sd_spi, "ERASE_WR_BLK_END", sd_cmd_ERASE_WR_BLK_END},
        [34] = {10, sd_spi, "READ_SEC_CMD", sd_cmd_optional},
        [35] = {10, sd_spi, "WRITE_SEC_CMD", sd_cmd_optional},
        [36] = {10, sd_spi, "SEND_PSI", sd_cmd_optional},
        [37] = {10, sd_spi, "CONTROL_ASSD_SYSTEM", sd_cmd_optional},
        [38] = {5,  sd_spi, "ERASE", sd_cmd_ERASE},
        [42] = {7,  sd_spi, "LOCK_UNLOCK", sd_cmd_LOCK_UNLOCK},
        [50] = {10, sd_spi, "DIRECT_SECURE_READ", sd_cmd_optional},
        [52] = {9,  sd_spi, "IO_RW_DIRECT", sd_cmd_optional},
        [53] = {9,  sd_spi, "IO_RW_EXTENDED", sd_cmd_optional},
        [55] = {8,  sd_spi, "APP_CMD", sd_cmd_APP_CMD},
        [56] = {8,  sd_spi, "GEN_CMD", sd_cmd_GEN_CMD},
        [57] = {10, sd_spi, "DIRECT_SECURE_WRITE", sd_cmd_optional},
        [58] = {0,  sd_spi, "READ_OCR", spi_cmd_READ_OCR},
        [59] = {0,  sd_spi, "CRC_ON_OFF", spi_cmd_CRC_ON_OFF},
    },
    .acmd = {
        [13] = {8,  sd_spi, "SD_STATUS", sd_acmd_SD_STATUS},
        [22] = {8,  sd_spi, "SEND_NUM_WR_BLOCKS", sd_acmd_SEND_NUM_WR_BLOCKS},
        [23] = {8,  sd_spi, "SET_WR_BLK_ERASE_COUNT", sd_acmd_SET_WR_BLK_ERASE_COUNT},
        [41] = {8,  sd_spi, "SEND_OP_COND", spi_cmd_SEND_OP_COND},
        [42] = {8,  sd_spi, "SET_CLR_CARD_DETECT", sd_acmd_SET_CLR_CARD_DETECT},
        [51] = {8,  sd_spi, "SEND_SCR", sd_acmd_SEND_SCR},
    },
};

static const SDProto sd_proto_sd = {
    .name = "SD",
    .cmd = {
        [0]  = {0,  sd_bc,   "GO_IDLE_STATE", sd_cmd_GO_IDLE_STATE},
        [2]  = {0,  sd_bcr,  "ALL_SEND_CID", sd_cmd_ALL_SEND_CID},
        [3]  = {0,  sd_bcr,  "SEND_RELATIVE_ADDR", sd_cmd_SEND_RELATIVE_ADDR},
        [4]  = {0,  sd_bc,   "SEND_DSR", sd_cmd_unimplemented},
        [5]  = {9,  sd_bc,   "IO_SEND_OP_COND", sd_cmd_optional},
        [6]  = {10, sd_adtc, "SWITCH_FUNCTION", sd_cmd_SWITCH_FUNCTION},
        [7]  = {0,  sd_ac,   "(DE)SELECT_CARD", sd_cmd_DE_SELECT_CARD},
        [8]  = {0,  sd_bcr,  "SEND_IF_COND", sd_cmd_SEND_IF_COND},
        [9]  = {0,  sd_ac,   "SEND_CSD", sd_cmd_SEND_CSD},
        [10] = {0,  sd_ac,   "SEND_CID", sd_cmd_SEND_CID},
        [11] = {0,  sd_ac,   "VOLTAGE_SWITCH", sd_cmd_optional},
        [12] = {0,  sd_ac,   "STOP_TRANSMISSION", sd_cmd_STOP_TRANSMISSION},
        [13] = {0,  sd_ac,   "SEND_STATUS", sd_cmd_SEND_STATUS},
        [15] = {0,  sd_ac,   "GO_INACTIVE_STATE", sd_cmd_GO_INACTIVE_STATE},
        [16] = {2,  sd_ac,   "SET_BLOCKLEN", sd_cmd_SET_BLOCKLEN},
        [17] = {2,  sd_adtc, "READ_SINGLE_BLOCK", sd_cmd_READ_SINGLE_BLOCK},
        [19] = {2,  sd_adtc, "SEND_TUNING_BLOCK", sd_cmd_SEND_TUNING_BLOCK},
        [20] = {2,  sd_ac,   "SPEED_CLASS_CONTROL", sd_cmd_optional},
        [23] = {2,  sd_ac,   "SET_BLOCK_COUNT", sd_cmd_SET_BLOCK_COUNT},
        [24] = {4,  sd_adtc, "WRITE_SINGLE_BLOCK", sd_cmd_WRITE_SINGLE_BLOCK},
        [27] = {4,  sd_adtc, "PROGRAM_CSD", sd_cmd_PROGRAM_CSD},
        [28] = {6,  sd_ac,   "SET_WRITE_PROT", sd_cmd_SET_WRITE_PROT},
        [29] = {6,  sd_ac,   "CLR_WRITE_PROT", sd_cmd_CLR_WRITE_PROT},
        [30] = {6,  sd_adtc, "SEND_WRITE_PROT", sd_cmd_SEND_WRITE_PROT},
        [32] = {5,  sd_ac,   "ERASE_WR_BLK_START", sd_cmd_ERASE_WR_BLK_START},
        [33] = {5,  sd_ac,   "ERASE_WR_BLK_END", sd_cmd_ERASE_WR_BLK_END},
        [34] = {10, sd_adtc, "READ_SEC_CMD", sd_cmd_optional},
        [35] = {10, sd_adtc, "WRITE_SEC_CMD", sd_cmd_optional},
        [36] = {10, sd_adtc, "SEND_PSI", sd_cmd_optional},
        [37] = {10, sd_ac,   "CONTROL_ASSD_SYSTEM", sd_cmd_optional},
        [38] = {5,  sd_ac,   "ERASE", sd_cmd_ERASE},
        [42] = {7,  sd_adtc, "LOCK_UNLOCK", sd_cmd_LOCK_UNLOCK},
        [43] = {1,  sd_ac,   "Q_MANAGEMENT", sd_cmd_optional},
        [44] = {1,  sd_ac,   "Q_TASK_INFO_A", sd_cmd_optional},
        [45] = {1,  sd_ac,   "Q_TASK_INFO_B", sd_cmd_optional},
        [46] = {1,  sd_adtc, "Q_RD_TASK", sd_cmd_optional},
        [47] = {1,  sd_adtc, "Q_WR_TASK", sd_cmd_optional},
        [48] = {1,  sd_adtc, "READ_EXTR_SINGLE", sd_cmd_optional},
        [49] = {1,  sd_adtc, "WRITE_EXTR_SINGLE", sd_cmd_optional},
        [50] = {10, sd_adtc, "DIRECT_SECURE_READ", sd_cmd_optional},
        [52] = {9,  sd_bc,   "IO_RW_DIRECT", sd_cmd_optional},
        [53] = {9,  sd_bc,   "IO_RW_EXTENDED", sd_cmd_optional},
        [55] = {8,  sd_ac,   "APP_CMD", sd_cmd_APP_CMD},
        [56] = {8,  sd_adtc, "GEN_CMD", sd_cmd_GEN_CMD},
        [57] = {10, sd_adtc, "DIRECT_SECURE_WRITE", sd_cmd_optional},
        [58] = {11, sd_adtc, "READ_EXTR_MULTI", sd_cmd_optional},
        [59] = {11, sd_adtc, "WRITE_EXTR_MULTI", sd_cmd_optional},
    },
    .acmd = {
        [6]  = {8,  sd_ac,   "SET_BUS_WIDTH", sd_acmd_SET_BUS_WIDTH},
        [13] = {8,  sd_adtc, "SD_STATUS", sd_acmd_SD_STATUS},
        [22] = {8,  sd_adtc, "SEND_NUM_WR_BLOCKS", sd_acmd_SEND_NUM_WR_BLOCKS},
        [23] = {8,  sd_ac,   "SET_WR_BLK_ERASE_COUNT", sd_acmd_SET_WR_BLK_ERASE_COUNT},
        [41] = {8,  sd_bcr,  "SEND_OP_COND", sd_cmd_SEND_OP_COND},
        [42] = {8,  sd_ac,   "SET_CLR_CARD_DETECT", sd_acmd_SET_CLR_CARD_DETECT},
        [51] = {8,  sd_adtc, "SEND_SCR", sd_acmd_SEND_SCR},
    },
};

static const SDProto sd_proto_emmc = {
    /* Only v4.3 is supported */
    .name = "eMMC",
    .cmd = {
        [0]  = {0,  sd_bc,   "GO_IDLE_STATE", sd_cmd_GO_IDLE_STATE},
        [1]  = {0,  sd_bcr,  "SEND_OP_COND", sd_cmd_SEND_OP_COND},
        [2]  = {0,  sd_bcr,  "ALL_SEND_CID", sd_cmd_ALL_SEND_CID},
        [3]  = {0,  sd_ac,   "SET_RELATIVE_ADDR", emmc_cmd_SET_RELATIVE_ADDR},
        [4]  = {0,  sd_bc,   "SEND_DSR", sd_cmd_unimplemented},
        [5]  = {0,  sd_ac,   "SLEEP/AWAKE", emmc_cmd_sleep_awake},
        [6]  = {10, sd_adtc, "SWITCH", emmc_cmd_SWITCH},
        [7]  = {0,  sd_ac,   "(DE)SELECT_CARD", sd_cmd_DE_SELECT_CARD},
        [8]  = {0,  sd_adtc, "SEND_EXT_CSD", emmc_cmd_SEND_EXT_CSD},
        [9]  = {0,  sd_ac,   "SEND_CSD", sd_cmd_SEND_CSD},
        [10] = {0,  sd_ac,   "SEND_CID", sd_cmd_SEND_CID},
        [11] = {1,  sd_adtc, "READ_DAT_UNTIL_STOP", sd_cmd_unimplemented},
        [12] = {0,  sd_ac,   "STOP_TRANSMISSION", sd_cmd_STOP_TRANSMISSION},
        [13] = {0,  sd_ac,   "SEND_STATUS", sd_cmd_SEND_STATUS},
        [14] = {0,  sd_adtc, "BUSTEST_R", sd_cmd_unimplemented},
        [15] = {0,  sd_ac,   "GO_INACTIVE_STATE", sd_cmd_GO_INACTIVE_STATE},
        [16] = {2,  sd_ac,   "SET_BLOCKLEN", sd_cmd_SET_BLOCKLEN},
        [17] = {2,  sd_adtc, "READ_SINGLE_BLOCK", sd_cmd_READ_SINGLE_BLOCK},
        [19] = {0,  sd_adtc, "BUSTEST_W", sd_cmd_unimplemented},
        [20] = {3,  sd_adtc, "WRITE_DAT_UNTIL_STOP", sd_cmd_unimplemented},
        [23] = {2,  sd_ac,   "SET_BLOCK_COUNT", sd_cmd_SET_BLOCK_COUNT},
        [24] = {4,  sd_adtc, "WRITE_SINGLE_BLOCK", sd_cmd_WRITE_SINGLE_BLOCK},
        [26] = {4,  sd_adtc, "PROGRAM_CID", emmc_cmd_PROGRAM_CID},
        [27] = {4,  sd_adtc, "PROGRAM_CSD", sd_cmd_PROGRAM_CSD},
        [28] = {6,  sd_ac,   "SET_WRITE_PROT", sd_cmd_SET_WRITE_PROT},
        [29] = {6,  sd_ac,   "CLR_WRITE_PROT", sd_cmd_CLR_WRITE_PROT},
        [30] = {6,  sd_adtc, "SEND_WRITE_PROT", sd_cmd_SEND_WRITE_PROT},
        [31] = {6,  sd_adtc, "SEND_WRITE_PROT_TYPE", sd_cmd_unimplemented},
        [35] = {5,  sd_ac,   "ERASE_WR_BLK_START", sd_cmd_ERASE_WR_BLK_START},
        [36] = {5,  sd_ac,   "ERASE_WR_BLK_END", sd_cmd_ERASE_WR_BLK_END},
        [38] = {5,  sd_ac,   "ERASE", sd_cmd_ERASE},
        [39] = {9,  sd_ac,   "FAST_IO", sd_cmd_unimplemented},
        [40] = {9,  sd_bcr,  "GO_IRQ_STATE", sd_cmd_unimplemented},
        [42] = {7,  sd_adtc, "LOCK_UNLOCK", sd_cmd_LOCK_UNLOCK},
        [49] = {0,  sd_adtc, "SET_TIME", sd_cmd_unimplemented},
        [55] = {8,  sd_ac,   "APP_CMD", sd_cmd_APP_CMD},
        [56] = {8,  sd_adtc, "GEN_CMD", sd_cmd_GEN_CMD},
    },
};

static void sd_instance_init(Object *obj)
{
    SDState *sd = SDMMC_COMMON(obj);
    SDCardClass *sc = SDMMC_COMMON_GET_CLASS(sd);

    sd->proto = sc->proto;
    sd->last_cmd_name = "UNSET";
    sd->ocr_power_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sd_ocr_powerup, sd);
}

static void sd_instance_finalize(Object *obj)
{
    SDState *sd = SDMMC_COMMON(obj);

    timer_free(sd->ocr_power_timer);
}

static void sd_realize(DeviceState *dev, Error **errp)
{
    SDState *sd = SDMMC_COMMON(dev);
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

static void emmc_realize(DeviceState *dev, Error **errp)
{
    SDState *sd = SDMMC_COMMON(dev);

    sd->spec_version = SD_PHY_SPECv3_01_VERS; /* Actually v4.3 */

    sd_realize(dev, errp);
}

static const Property sdmmc_common_properties[] = {
    DEFINE_PROP_DRIVE("drive", SDState, blk),
};

static const Property sd_properties[] = {
    DEFINE_PROP_UINT8("spec_version", SDState,
                      spec_version, SD_PHY_SPECv3_01_VERS),
};

static const Property emmc_properties[] = {
    DEFINE_PROP_UINT64("boot-partition-size", SDState, boot_part_size, 0),
    DEFINE_PROP_UINT8("boot-config", SDState, boot_config, 0x0),
};

static void sdmmc_common_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SDCardClass *sc = SDMMC_COMMON_CLASS(klass);

    device_class_set_props(dc, sdmmc_common_properties);
    dc->vmsd = &sd_vmstate;
    device_class_set_legacy_reset(dc, sd_reset);
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
    sc->get_inserted = sd_get_inserted;
    sc->get_readonly = sd_get_readonly;
}

static void sd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SDCardClass *sc = SDMMC_COMMON_CLASS(klass);

    dc->realize = sd_realize;
    device_class_set_props(dc, sd_properties);

    sc->set_cid = sd_set_cid;
    sc->set_csd = sd_set_csd;
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
    SDCardClass *sc = SDMMC_COMMON_CLASS(klass);

    dc->desc = "SD SPI";
    sc->proto = &sd_proto_spi;
}

static void emmc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SDCardClass *sc = SDMMC_COMMON_CLASS(klass);

    dc->desc = "eMMC";
    dc->realize = emmc_realize;
    device_class_set_props(dc, emmc_properties);
    /* Reason: Soldered on board */
    dc->user_creatable = false;

    sc->proto = &sd_proto_emmc;

    sc->set_cid = emmc_set_cid;
    sc->set_csd = emmc_set_csd;
}

static const TypeInfo sd_types[] = {
    {
        .name           = TYPE_SDMMC_COMMON,
        .parent         = TYPE_DEVICE,
        .abstract       = true,
        .instance_size  = sizeof(SDState),
        .class_size     = sizeof(SDCardClass),
        .class_init     = sdmmc_common_class_init,
        .instance_init  = sd_instance_init,
        .instance_finalize = sd_instance_finalize,
    },
    {
        .name           = TYPE_SD_CARD,
        .parent         = TYPE_SDMMC_COMMON,
        .class_init     = sd_class_init,
    },
    {
        .name           = TYPE_SD_CARD_SPI,
        .parent         = TYPE_SD_CARD,
        .class_init     = sd_spi_class_init,
    },
    {
        .name           = TYPE_EMMC,
        .parent         = TYPE_SDMMC_COMMON,
        .class_init     = emmc_class_init,
    },
};

DEFINE_TYPES(sd_types)
