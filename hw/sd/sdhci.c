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

#include "hw/hw.h"
#include "sysemu/blockdev.h"
#include "sysemu/dma.h"
#include "qemu/timer.h"
#include "qemu/bitops.h"

#include "sdhci.h"

/* host controller debug messages */
#ifndef SDHC_DEBUG
#define SDHC_DEBUG                        0
#endif

#if SDHC_DEBUG == 0
    #define DPRINT_L1(fmt, args...)       do { } while (0)
    #define DPRINT_L2(fmt, args...)       do { } while (0)
    #define ERRPRINT(fmt, args...)        do { } while (0)
#elif SDHC_DEBUG == 1
    #define DPRINT_L1(fmt, args...)       \
        do {fprintf(stderr, "QEMU SDHC: "fmt, ## args); } while (0)
    #define DPRINT_L2(fmt, args...)       do { } while (0)
    #define ERRPRINT(fmt, args...)        \
        do {fprintf(stderr, "QEMU SDHC ERROR: "fmt, ## args); } while (0)
#else
    #define DPRINT_L1(fmt, args...)       \
        do {fprintf(stderr, "QEMU SDHC: "fmt, ## args); } while (0)
    #define DPRINT_L2(fmt, args...)       \
        do {fprintf(stderr, "QEMU SDHC: "fmt, ## args); } while (0)
    #define ERRPRINT(fmt, args...)        \
        do {fprintf(stderr, "QEMU SDHC ERROR: "fmt, ## args); } while (0)
#endif

/* Default SD/MMC host controller features information, which will be
 * presented in CAPABILITIES register of generic SD host controller at reset.
 * If not stated otherwise:
 * 0 - not supported, 1 - supported, other - prohibited.
 */
#define SDHC_CAPAB_64BITBUS       0ul        /* 64-bit System Bus Support */
#define SDHC_CAPAB_18V            1ul        /* Voltage support 1.8v */
#define SDHC_CAPAB_30V            0ul        /* Voltage support 3.0v */
#define SDHC_CAPAB_33V            1ul        /* Voltage support 3.3v */
#define SDHC_CAPAB_SUSPRESUME     0ul        /* Suspend/resume support */
#define SDHC_CAPAB_SDMA           1ul        /* SDMA support */
#define SDHC_CAPAB_HIGHSPEED      1ul        /* High speed support */
#define SDHC_CAPAB_ADMA1          1ul        /* ADMA1 support */
#define SDHC_CAPAB_ADMA2          1ul        /* ADMA2 support */
/* Maximum host controller R/W buffers size
 * Possible values: 512, 1024, 2048 bytes */
#define SDHC_CAPAB_MAXBLOCKLENGTH 512ul
/* Maximum clock frequency for SDclock in MHz
 * value in range 10-63 MHz, 0 - not defined */
#define SDHC_CAPAB_BASECLKFREQ    0ul
#define SDHC_CAPAB_TOUNIT         1ul  /* Timeout clock unit 0 - kHz, 1 - MHz */
/* Timeout clock frequency 1-63, 0 - not defined */
#define SDHC_CAPAB_TOCLKFREQ      0ul

/* Now check all parameters and calculate CAPABILITIES REGISTER value */
#if SDHC_CAPAB_64BITBUS > 1 || SDHC_CAPAB_18V > 1 || SDHC_CAPAB_30V > 1 ||     \
    SDHC_CAPAB_33V > 1 || SDHC_CAPAB_SUSPRESUME > 1 || SDHC_CAPAB_SDMA > 1 ||  \
    SDHC_CAPAB_HIGHSPEED > 1 || SDHC_CAPAB_ADMA2 > 1 || SDHC_CAPAB_ADMA1 > 1 ||\
    SDHC_CAPAB_TOUNIT > 1
#error Capabilities features can have value 0 or 1 only!
#endif

#if SDHC_CAPAB_MAXBLOCKLENGTH == 512
#define MAX_BLOCK_LENGTH 0ul
#elif SDHC_CAPAB_MAXBLOCKLENGTH == 1024
#define MAX_BLOCK_LENGTH 1ul
#elif SDHC_CAPAB_MAXBLOCKLENGTH == 2048
#define MAX_BLOCK_LENGTH 2ul
#else
#error Max host controller block size can have value 512, 1024 or 2048 only!
#endif

#if (SDHC_CAPAB_BASECLKFREQ > 0 && SDHC_CAPAB_BASECLKFREQ < 10) || \
    SDHC_CAPAB_BASECLKFREQ > 63
#error SDclock frequency can have value in range 0, 10-63 only!
#endif

#if SDHC_CAPAB_TOCLKFREQ > 63
#error Timeout clock frequency can have value in range 0-63 only!
#endif

#define SDHC_CAPAB_REG_DEFAULT                                 \
   ((SDHC_CAPAB_64BITBUS << 28) | (SDHC_CAPAB_18V << 26) |     \
    (SDHC_CAPAB_30V << 25) | (SDHC_CAPAB_33V << 24) |          \
    (SDHC_CAPAB_SUSPRESUME << 23) | (SDHC_CAPAB_SDMA << 22) |  \
    (SDHC_CAPAB_HIGHSPEED << 21) | (SDHC_CAPAB_ADMA1 << 20) |  \
    (SDHC_CAPAB_ADMA2 << 19) | (MAX_BLOCK_LENGTH << 16) |      \
    (SDHC_CAPAB_BASECLKFREQ << 8) | (SDHC_CAPAB_TOUNIT << 7) | \
    (SDHC_CAPAB_TOCLKFREQ))

#define MASKED_WRITE(reg, mask, val)  (reg = (reg & (mask)) | (val))

static uint8_t sdhci_slotint(SDHCIState *s)
{
    return (s->norintsts & s->norintsigen) || (s->errintsts & s->errintsigen) ||
         ((s->norintsts & SDHC_NIS_INSERT) && (s->wakcon & SDHC_WKUP_ON_INS)) ||
         ((s->norintsts & SDHC_NIS_REMOVE) && (s->wakcon & SDHC_WKUP_ON_RMV));
}

static inline void sdhci_update_irq(SDHCIState *s)
{
    qemu_set_irq(s->irq, sdhci_slotint(s));
}

static void sdhci_raise_insertion_irq(void *opaque)
{
    SDHCIState *s = (SDHCIState *)opaque;

    if (s->norintsts & SDHC_NIS_REMOVE) {
        timer_mod(s->insert_timer,
                       qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + SDHC_INSERTION_DELAY);
    } else {
        s->prnsts = 0x1ff0000;
        if (s->norintstsen & SDHC_NISEN_INSERT) {
            s->norintsts |= SDHC_NIS_INSERT;
        }
        sdhci_update_irq(s);
    }
}

static void sdhci_insert_eject_cb(void *opaque, int irq, int level)
{
    SDHCIState *s = (SDHCIState *)opaque;
    DPRINT_L1("Card state changed: %s!\n", level ? "insert" : "eject");

    if ((s->norintsts & SDHC_NIS_REMOVE) && level) {
        /* Give target some time to notice card ejection */
        timer_mod(s->insert_timer,
                       qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + SDHC_INSERTION_DELAY);
    } else {
        if (level) {
            s->prnsts = 0x1ff0000;
            if (s->norintstsen & SDHC_NISEN_INSERT) {
                s->norintsts |= SDHC_NIS_INSERT;
            }
        } else {
            s->prnsts = 0x1fa0000;
            s->pwrcon &= ~SDHC_POWER_ON;
            s->clkcon &= ~SDHC_CLOCK_SDCLK_EN;
            if (s->norintstsen & SDHC_NISEN_REMOVE) {
                s->norintsts |= SDHC_NIS_REMOVE;
            }
        }
        sdhci_update_irq(s);
    }
}

static void sdhci_card_readonly_cb(void *opaque, int irq, int level)
{
    SDHCIState *s = (SDHCIState *)opaque;

    if (level) {
        s->prnsts &= ~SDHC_WRITE_PROTECT;
    } else {
        /* Write enabled */
        s->prnsts |= SDHC_WRITE_PROTECT;
    }
}

static void sdhci_reset(SDHCIState *s)
{
    timer_del(s->insert_timer);
    timer_del(s->transfer_timer);
    /* Set all registers to 0. Capabilities registers are not cleared
     * and assumed to always preserve their value, given to them during
     * initialization */
    memset(&s->sdmasysad, 0, (uintptr_t)&s->capareg - (uintptr_t)&s->sdmasysad);

    sd_set_cb(s->card, s->ro_cb, s->eject_cb);
    s->data_count = 0;
    s->stopped_state = sdhc_not_stopped;
}

static void sdhci_do_data_transfer(void *opaque)
{
    SDHCIState *s = (SDHCIState *)opaque;

    SDHCI_GET_CLASS(s)->data_transfer(s);
}

static void sdhci_send_command(SDHCIState *s)
{
    SDRequest request;
    uint8_t response[16];
    int rlen;

    s->errintsts = 0;
    s->acmd12errsts = 0;
    request.cmd = s->cmdreg >> 8;
    request.arg = s->argument;
    DPRINT_L1("sending CMD%u ARG[0x%08x]\n", request.cmd, request.arg);
    rlen = sd_do_command(s->card, &request, response);

    if (s->cmdreg & SDHC_CMD_RESPONSE) {
        if (rlen == 4) {
            s->rspreg[0] = (response[0] << 24) | (response[1] << 16) |
                           (response[2] << 8)  |  response[3];
            s->rspreg[1] = s->rspreg[2] = s->rspreg[3] = 0;
            DPRINT_L1("Response: RSPREG[31..0]=0x%08x\n", s->rspreg[0]);
        } else if (rlen == 16) {
            s->rspreg[0] = (response[11] << 24) | (response[12] << 16) |
                           (response[13] << 8) |  response[14];
            s->rspreg[1] = (response[7] << 24) | (response[8] << 16) |
                           (response[9] << 8)  |  response[10];
            s->rspreg[2] = (response[3] << 24) | (response[4] << 16) |
                           (response[5] << 8)  |  response[6];
            s->rspreg[3] = (response[0] << 16) | (response[1] << 8) |
                            response[2];
            DPRINT_L1("Response received:\n RSPREG[127..96]=0x%08x, RSPREG[95.."
                  "64]=0x%08x,\n RSPREG[63..32]=0x%08x, RSPREG[31..0]=0x%08x\n",
                  s->rspreg[3], s->rspreg[2], s->rspreg[1], s->rspreg[0]);
        } else {
            ERRPRINT("Timeout waiting for command response\n");
            if (s->errintstsen & SDHC_EISEN_CMDTIMEOUT) {
                s->errintsts |= SDHC_EIS_CMDTIMEOUT;
                s->norintsts |= SDHC_NIS_ERR;
            }
        }

        if ((s->norintstsen & SDHC_NISEN_TRSCMP) &&
            (s->cmdreg & SDHC_CMD_RESPONSE) == SDHC_CMD_RSP_WITH_BUSY) {
            s->norintsts |= SDHC_NIS_TRSCMP;
        }
    } else if (rlen != 0 && (s->errintstsen & SDHC_EISEN_CMDIDX)) {
        s->errintsts |= SDHC_EIS_CMDIDX;
        s->norintsts |= SDHC_NIS_ERR;
    }

    if (s->norintstsen & SDHC_NISEN_CMDCMP) {
        s->norintsts |= SDHC_NIS_CMDCMP;
    }

    sdhci_update_irq(s);

    if (s->blksize && (s->cmdreg & SDHC_CMD_DATA_PRESENT)) {
        s->data_count = 0;
        sdhci_do_data_transfer(s);
    }
}

static void sdhci_end_transfer(SDHCIState *s)
{
    /* Automatically send CMD12 to stop transfer if AutoCMD12 enabled */
    if ((s->trnmod & SDHC_TRNS_ACMD12) != 0) {
        SDRequest request;
        uint8_t response[16];

        request.cmd = 0x0C;
        request.arg = 0;
        DPRINT_L1("Automatically issue CMD%d %08x\n", request.cmd, request.arg);
        sd_do_command(s->card, &request, response);
        /* Auto CMD12 response goes to the upper Response register */
        s->rspreg[3] = (response[0] << 24) | (response[1] << 16) |
                (response[2] << 8) | response[3];
    }

    s->prnsts &= ~(SDHC_DOING_READ | SDHC_DOING_WRITE |
            SDHC_DAT_LINE_ACTIVE | SDHC_DATA_INHIBIT |
            SDHC_SPACE_AVAILABLE | SDHC_DATA_AVAILABLE);

    if (s->norintstsen & SDHC_NISEN_TRSCMP) {
        s->norintsts |= SDHC_NIS_TRSCMP;
    }

    sdhci_update_irq(s);
}

/*
 * Programmed i/o data transfer
 */

/* Fill host controller's read buffer with BLKSIZE bytes of data from card */
static void sdhci_read_block_from_card(SDHCIState *s)
{
    int index = 0;

    if ((s->trnmod & SDHC_TRNS_MULTI) &&
            (s->trnmod & SDHC_TRNS_BLK_CNT_EN) && (s->blkcnt == 0)) {
        return;
    }

    for (index = 0; index < (s->blksize & 0x0fff); index++) {
        s->fifo_buffer[index] = sd_read_data(s->card);
    }

    /* New data now available for READ through Buffer Port Register */
    s->prnsts |= SDHC_DATA_AVAILABLE;
    if (s->norintstsen & SDHC_NISEN_RBUFRDY) {
        s->norintsts |= SDHC_NIS_RBUFRDY;
    }

    /* Clear DAT line active status if that was the last block */
    if ((s->trnmod & SDHC_TRNS_MULTI) == 0 ||
            ((s->trnmod & SDHC_TRNS_MULTI) && s->blkcnt == 1)) {
        s->prnsts &= ~SDHC_DAT_LINE_ACTIVE;
    }

    /* If stop at block gap request was set and it's not the last block of
     * data - generate Block Event interrupt */
    if (s->stopped_state == sdhc_gap_read && (s->trnmod & SDHC_TRNS_MULTI) &&
            s->blkcnt != 1)    {
        s->prnsts &= ~SDHC_DAT_LINE_ACTIVE;
        if (s->norintstsen & SDHC_EISEN_BLKGAP) {
            s->norintsts |= SDHC_EIS_BLKGAP;
        }
    }

    sdhci_update_irq(s);
}

/* Read @size byte of data from host controller @s BUFFER DATA PORT register */
static uint32_t sdhci_read_dataport(SDHCIState *s, unsigned size)
{
    uint32_t value = 0;
    int i;

    /* first check that a valid data exists in host controller input buffer */
    if ((s->prnsts & SDHC_DATA_AVAILABLE) == 0) {
        ERRPRINT("Trying to read from empty buffer\n");
        return 0;
    }

    for (i = 0; i < size; i++) {
        value |= s->fifo_buffer[s->data_count] << i * 8;
        s->data_count++;
        /* check if we've read all valid data (blksize bytes) from buffer */
        if ((s->data_count) >= (s->blksize & 0x0fff)) {
            DPRINT_L2("All %u bytes of data have been read from input buffer\n",
                    s->data_count);
            s->prnsts &= ~SDHC_DATA_AVAILABLE; /* no more data in a buffer */
            s->data_count = 0;  /* next buff read must start at position [0] */

            if (s->trnmod & SDHC_TRNS_BLK_CNT_EN) {
                s->blkcnt--;
            }

            /* if that was the last block of data */
            if ((s->trnmod & SDHC_TRNS_MULTI) == 0 ||
                ((s->trnmod & SDHC_TRNS_BLK_CNT_EN) && (s->blkcnt == 0)) ||
                 /* stop at gap request */
                (s->stopped_state == sdhc_gap_read &&
                 !(s->prnsts & SDHC_DAT_LINE_ACTIVE))) {
                SDHCI_GET_CLASS(s)->end_data_transfer(s);
            } else { /* if there are more data, read next block from card */
                SDHCI_GET_CLASS(s)->read_block_from_card(s);
            }
            break;
        }
    }

    return value;
}

/* Write data from host controller FIFO to card */
static void sdhci_write_block_to_card(SDHCIState *s)
{
    int index = 0;

    if (s->prnsts & SDHC_SPACE_AVAILABLE) {
        if (s->norintstsen & SDHC_NISEN_WBUFRDY) {
            s->norintsts |= SDHC_NIS_WBUFRDY;
        }
        sdhci_update_irq(s);
        return;
    }

    if (s->trnmod & SDHC_TRNS_BLK_CNT_EN) {
        if (s->blkcnt == 0) {
            return;
        } else {
            s->blkcnt--;
        }
    }

    for (index = 0; index < (s->blksize & 0x0fff); index++) {
        sd_write_data(s->card, s->fifo_buffer[index]);
    }

    /* Next data can be written through BUFFER DATORT register */
    s->prnsts |= SDHC_SPACE_AVAILABLE;

    /* Finish transfer if that was the last block of data */
    if ((s->trnmod & SDHC_TRNS_MULTI) == 0 ||
            ((s->trnmod & SDHC_TRNS_MULTI) &&
            (s->trnmod & SDHC_TRNS_BLK_CNT_EN) && (s->blkcnt == 0))) {
        SDHCI_GET_CLASS(s)->end_data_transfer(s);
    } else if (s->norintstsen & SDHC_NISEN_WBUFRDY) {
        s->norintsts |= SDHC_NIS_WBUFRDY;
    }

    /* Generate Block Gap Event if requested and if not the last block */
    if (s->stopped_state == sdhc_gap_write && (s->trnmod & SDHC_TRNS_MULTI) &&
            s->blkcnt > 0) {
        s->prnsts &= ~SDHC_DOING_WRITE;
        if (s->norintstsen & SDHC_EISEN_BLKGAP) {
            s->norintsts |= SDHC_EIS_BLKGAP;
        }
        SDHCI_GET_CLASS(s)->end_data_transfer(s);
    }

    sdhci_update_irq(s);
}

/* Write @size bytes of @value data to host controller @s Buffer Data Port
 * register */
static void sdhci_write_dataport(SDHCIState *s, uint32_t value, unsigned size)
{
    unsigned i;

    /* Check that there is free space left in a buffer */
    if (!(s->prnsts & SDHC_SPACE_AVAILABLE)) {
        ERRPRINT("Can't write to data buffer: buffer full\n");
        return;
    }

    for (i = 0; i < size; i++) {
        s->fifo_buffer[s->data_count] = value & 0xFF;
        s->data_count++;
        value >>= 8;
        if (s->data_count >= (s->blksize & 0x0fff)) {
            DPRINT_L2("write buffer filled with %u bytes of data\n",
                    s->data_count);
            s->data_count = 0;
            s->prnsts &= ~SDHC_SPACE_AVAILABLE;
            if (s->prnsts & SDHC_DOING_WRITE) {
                SDHCI_GET_CLASS(s)->write_block_to_card(s);
            }
        }
    }
}

/*
 * Single DMA data transfer
 */

/* Multi block SDMA transfer */
static void sdhci_sdma_transfer_multi_blocks(SDHCIState *s)
{
    bool page_aligned = false;
    unsigned int n, begin;
    const uint16_t block_size = s->blksize & 0x0fff;
    uint32_t boundary_chk = 1 << (((s->blksize & 0xf000) >> 12) + 12);
    uint32_t boundary_count = boundary_chk - (s->sdmasysad % boundary_chk);

    /* XXX: Some sd/mmc drivers (for example, u-boot-slp) do not account for
     * possible stop at page boundary if initial address is not page aligned,
     * allow them to work properly */
    if ((s->sdmasysad % boundary_chk) == 0) {
        page_aligned = true;
    }

    if (s->trnmod & SDHC_TRNS_READ) {
        s->prnsts |= SDHC_DOING_READ | SDHC_DATA_INHIBIT |
                SDHC_DAT_LINE_ACTIVE;
        while (s->blkcnt) {
            if (s->data_count == 0) {
                for (n = 0; n < block_size; n++) {
                    s->fifo_buffer[n] = sd_read_data(s->card);
                }
            }
            begin = s->data_count;
            if (((boundary_count + begin) < block_size) && page_aligned) {
                s->data_count = boundary_count + begin;
                boundary_count = 0;
             } else {
                s->data_count = block_size;
                boundary_count -= block_size - begin;
                if (s->trnmod & SDHC_TRNS_BLK_CNT_EN) {
                    s->blkcnt--;
                }
            }
            dma_memory_write(&address_space_memory, s->sdmasysad,
                             &s->fifo_buffer[begin], s->data_count - begin);
            s->sdmasysad += s->data_count - begin;
            if (s->data_count == block_size) {
                s->data_count = 0;
            }
            if (page_aligned && boundary_count == 0) {
                break;
            }
        }
    } else {
        s->prnsts |= SDHC_DOING_WRITE | SDHC_DATA_INHIBIT |
                SDHC_DAT_LINE_ACTIVE;
        while (s->blkcnt) {
            begin = s->data_count;
            if (((boundary_count + begin) < block_size) && page_aligned) {
                s->data_count = boundary_count + begin;
                boundary_count = 0;
             } else {
                s->data_count = block_size;
                boundary_count -= block_size - begin;
            }
            dma_memory_read(&address_space_memory, s->sdmasysad,
                            &s->fifo_buffer[begin], s->data_count);
            s->sdmasysad += s->data_count - begin;
            if (s->data_count == block_size) {
                for (n = 0; n < block_size; n++) {
                    sd_write_data(s->card, s->fifo_buffer[n]);
                }
                s->data_count = 0;
                if (s->trnmod & SDHC_TRNS_BLK_CNT_EN) {
                    s->blkcnt--;
                }
            }
            if (page_aligned && boundary_count == 0) {
                break;
            }
        }
    }

    if (s->blkcnt == 0) {
        SDHCI_GET_CLASS(s)->end_data_transfer(s);
    } else {
        if (s->norintstsen & SDHC_NISEN_DMA) {
            s->norintsts |= SDHC_NIS_DMA;
        }
        sdhci_update_irq(s);
    }
}

/* single block SDMA transfer */

static void sdhci_sdma_transfer_single_block(SDHCIState *s)
{
    int n;
    uint32_t datacnt = s->blksize & 0x0fff;

    if (s->trnmod & SDHC_TRNS_READ) {
        for (n = 0; n < datacnt; n++) {
            s->fifo_buffer[n] = sd_read_data(s->card);
        }
        dma_memory_write(&address_space_memory, s->sdmasysad, s->fifo_buffer,
                         datacnt);
    } else {
        dma_memory_read(&address_space_memory, s->sdmasysad, s->fifo_buffer,
                        datacnt);
        for (n = 0; n < datacnt; n++) {
            sd_write_data(s->card, s->fifo_buffer[n]);
        }
    }

    if (s->trnmod & SDHC_TRNS_BLK_CNT_EN) {
        s->blkcnt--;
    }

    SDHCI_GET_CLASS(s)->end_data_transfer(s);
}

typedef struct ADMADescr {
    hwaddr addr;
    uint16_t length;
    uint8_t attr;
    uint8_t incr;
} ADMADescr;

static void get_adma_description(SDHCIState *s, ADMADescr *dscr)
{
    uint32_t adma1 = 0;
    uint64_t adma2 = 0;
    hwaddr entry_addr = (hwaddr)s->admasysaddr;
    switch (SDHC_DMA_TYPE(s->hostctl)) {
    case SDHC_CTRL_ADMA2_32:
        dma_memory_read(&address_space_memory, entry_addr, (uint8_t *)&adma2,
                        sizeof(adma2));
        adma2 = le64_to_cpu(adma2);
        /* The spec does not specify endianness of descriptor table.
         * We currently assume that it is LE.
         */
        dscr->addr = (hwaddr)extract64(adma2, 32, 32) & ~0x3ull;
        dscr->length = (uint16_t)extract64(adma2, 16, 16);
        dscr->attr = (uint8_t)extract64(adma2, 0, 7);
        dscr->incr = 8;
        break;
    case SDHC_CTRL_ADMA1_32:
        dma_memory_read(&address_space_memory, entry_addr, (uint8_t *)&adma1,
                        sizeof(adma1));
        adma1 = le32_to_cpu(adma1);
        dscr->addr = (hwaddr)(adma1 & 0xFFFFF000);
        dscr->attr = (uint8_t)extract32(adma1, 0, 7);
        dscr->incr = 4;
        if ((dscr->attr & SDHC_ADMA_ATTR_ACT_MASK) == SDHC_ADMA_ATTR_SET_LEN) {
            dscr->length = (uint16_t)extract32(adma1, 12, 16);
        } else {
            dscr->length = 4096;
        }
        break;
    case SDHC_CTRL_ADMA2_64:
        dma_memory_read(&address_space_memory, entry_addr,
                        (uint8_t *)(&dscr->attr), 1);
        dma_memory_read(&address_space_memory, entry_addr + 2,
                        (uint8_t *)(&dscr->length), 2);
        dscr->length = le16_to_cpu(dscr->length);
        dma_memory_read(&address_space_memory, entry_addr + 4,
                        (uint8_t *)(&dscr->addr), 8);
        dscr->attr = le64_to_cpu(dscr->attr);
        dscr->attr &= 0xfffffff8;
        dscr->incr = 12;
        break;
    }
}

/* Advanced DMA data transfer */

static void sdhci_do_adma(SDHCIState *s)
{
    unsigned int n, begin, length;
    const uint16_t block_size = s->blksize & 0x0fff;
    ADMADescr dscr;
    int i;

    for (i = 0; i < SDHC_ADMA_DESCS_PER_DELAY; ++i) {
        s->admaerr &= ~SDHC_ADMAERR_LENGTH_MISMATCH;

        get_adma_description(s, &dscr);
        DPRINT_L2("ADMA loop: addr=" TARGET_FMT_plx ", len=%d, attr=%x\n",
                dscr.addr, dscr.length, dscr.attr);

        if ((dscr.attr & SDHC_ADMA_ATTR_VALID) == 0) {
            /* Indicate that error occurred in ST_FDS state */
            s->admaerr &= ~SDHC_ADMAERR_STATE_MASK;
            s->admaerr |= SDHC_ADMAERR_STATE_ST_FDS;

            /* Generate ADMA error interrupt */
            if (s->errintstsen & SDHC_EISEN_ADMAERR) {
                s->errintsts |= SDHC_EIS_ADMAERR;
                s->norintsts |= SDHC_NIS_ERR;
            }

            sdhci_update_irq(s);
            return;
        }

        length = dscr.length ? dscr.length : 65536;

        switch (dscr.attr & SDHC_ADMA_ATTR_ACT_MASK) {
        case SDHC_ADMA_ATTR_ACT_TRAN:  /* data transfer */

            if (s->trnmod & SDHC_TRNS_READ) {
                while (length) {
                    if (s->data_count == 0) {
                        for (n = 0; n < block_size; n++) {
                            s->fifo_buffer[n] = sd_read_data(s->card);
                        }
                    }
                    begin = s->data_count;
                    if ((length + begin) < block_size) {
                        s->data_count = length + begin;
                        length = 0;
                     } else {
                        s->data_count = block_size;
                        length -= block_size - begin;
                    }
                    dma_memory_write(&address_space_memory, dscr.addr,
                                     &s->fifo_buffer[begin],
                                     s->data_count - begin);
                    dscr.addr += s->data_count - begin;
                    if (s->data_count == block_size) {
                        s->data_count = 0;
                        if (s->trnmod & SDHC_TRNS_BLK_CNT_EN) {
                            s->blkcnt--;
                            if (s->blkcnt == 0) {
                                break;
                            }
                        }
                    }
                }
            } else {
                while (length) {
                    begin = s->data_count;
                    if ((length + begin) < block_size) {
                        s->data_count = length + begin;
                        length = 0;
                     } else {
                        s->data_count = block_size;
                        length -= block_size - begin;
                    }
                    dma_memory_read(&address_space_memory, dscr.addr,
                                    &s->fifo_buffer[begin], s->data_count);
                    dscr.addr += s->data_count - begin;
                    if (s->data_count == block_size) {
                        for (n = 0; n < block_size; n++) {
                            sd_write_data(s->card, s->fifo_buffer[n]);
                        }
                        s->data_count = 0;
                        if (s->trnmod & SDHC_TRNS_BLK_CNT_EN) {
                            s->blkcnt--;
                            if (s->blkcnt == 0) {
                                break;
                            }
                        }
                    }
                }
            }
            s->admasysaddr += dscr.incr;
            break;
        case SDHC_ADMA_ATTR_ACT_LINK:   /* link to next descriptor table */
            s->admasysaddr = dscr.addr;
            DPRINT_L1("ADMA link: admasysaddr=0x%lx\n", s->admasysaddr);
            break;
        default:
            s->admasysaddr += dscr.incr;
            break;
        }

        if (dscr.attr & SDHC_ADMA_ATTR_INT) {
            DPRINT_L1("ADMA interrupt: admasysaddr=0x%lx\n", s->admasysaddr);
            if (s->norintstsen & SDHC_NISEN_DMA) {
                s->norintsts |= SDHC_NIS_DMA;
            }

            sdhci_update_irq(s);
        }

        /* ADMA transfer terminates if blkcnt == 0 or by END attribute */
        if (((s->trnmod & SDHC_TRNS_BLK_CNT_EN) &&
                    (s->blkcnt == 0)) || (dscr.attr & SDHC_ADMA_ATTR_END)) {
            DPRINT_L2("ADMA transfer completed\n");
            if (length || ((dscr.attr & SDHC_ADMA_ATTR_END) &&
                (s->trnmod & SDHC_TRNS_BLK_CNT_EN) &&
                s->blkcnt != 0)) {
                ERRPRINT("SD/MMC host ADMA length mismatch\n");
                s->admaerr |= SDHC_ADMAERR_LENGTH_MISMATCH |
                        SDHC_ADMAERR_STATE_ST_TFR;
                if (s->errintstsen & SDHC_EISEN_ADMAERR) {
                    ERRPRINT("Set ADMA error flag\n");
                    s->errintsts |= SDHC_EIS_ADMAERR;
                    s->norintsts |= SDHC_NIS_ERR;
                }

                sdhci_update_irq(s);
            }
            SDHCI_GET_CLASS(s)->end_data_transfer(s);
            return;
        }

    }

    /* we have unfinished business - reschedule to continue ADMA */
    timer_mod(s->transfer_timer,
                   qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + SDHC_TRANSFER_DELAY);
}

/* Perform data transfer according to controller configuration */

static void sdhci_data_transfer(SDHCIState *s)
{
    SDHCIClass *k = SDHCI_GET_CLASS(s);

    if (s->trnmod & SDHC_TRNS_DMA) {
        switch (SDHC_DMA_TYPE(s->hostctl)) {
        case SDHC_CTRL_SDMA:
            if ((s->trnmod & SDHC_TRNS_MULTI) &&
                    (!(s->trnmod & SDHC_TRNS_BLK_CNT_EN) || s->blkcnt == 0)) {
                break;
            }

            if ((s->blkcnt == 1) || !(s->trnmod & SDHC_TRNS_MULTI)) {
                k->do_sdma_single(s);
            } else {
                k->do_sdma_multi(s);
            }

            break;
        case SDHC_CTRL_ADMA1_32:
            if (!(s->capareg & SDHC_CAN_DO_ADMA1)) {
                ERRPRINT("ADMA1 not supported\n");
                break;
            }

            k->do_adma(s);
            break;
        case SDHC_CTRL_ADMA2_32:
            if (!(s->capareg & SDHC_CAN_DO_ADMA2)) {
                ERRPRINT("ADMA2 not supported\n");
                break;
            }

            k->do_adma(s);
            break;
        case SDHC_CTRL_ADMA2_64:
            if (!(s->capareg & SDHC_CAN_DO_ADMA2) ||
                    !(s->capareg & SDHC_64_BIT_BUS_SUPPORT)) {
                ERRPRINT("64 bit ADMA not supported\n");
                break;
            }

            k->do_adma(s);
            break;
        default:
            ERRPRINT("Unsupported DMA type\n");
            break;
        }
    } else {
        if ((s->trnmod & SDHC_TRNS_READ) && sd_data_ready(s->card)) {
            s->prnsts |= SDHC_DOING_READ | SDHC_DATA_INHIBIT |
                    SDHC_DAT_LINE_ACTIVE;
            SDHCI_GET_CLASS(s)->read_block_from_card(s);
        } else {
            s->prnsts |= SDHC_DOING_WRITE | SDHC_DAT_LINE_ACTIVE |
                    SDHC_SPACE_AVAILABLE | SDHC_DATA_INHIBIT;
            SDHCI_GET_CLASS(s)->write_block_to_card(s);
        }
    }
}

static bool sdhci_can_issue_command(SDHCIState *s)
{
    if (!SDHC_CLOCK_IS_ON(s->clkcon) || !(s->pwrcon & SDHC_POWER_ON) ||
        (((s->prnsts & SDHC_DATA_INHIBIT) || s->stopped_state) &&
        ((s->cmdreg & SDHC_CMD_DATA_PRESENT) ||
        ((s->cmdreg & SDHC_CMD_RESPONSE) == SDHC_CMD_RSP_WITH_BUSY &&
        !(SDHC_COMMAND_TYPE(s->cmdreg) == SDHC_CMD_ABORT))))) {
        return false;
    }

    return true;
}

/* The Buffer Data Port register must be accessed in sequential and
 * continuous manner */
static inline bool
sdhci_buff_access_is_sequential(SDHCIState *s, unsigned byte_num)
{
    if ((s->data_count & 0x3) != byte_num) {
        ERRPRINT("Non-sequential access to Buffer Data Port register"
                "is prohibited\n");
        return false;
    }
    return true;
}

static uint32_t sdhci_read(SDHCIState *s, unsigned int offset, unsigned size)
{
    uint32_t ret = 0;

    switch (offset & ~0x3) {
    case SDHC_SYSAD:
        ret = s->sdmasysad;
        break;
    case SDHC_BLKSIZE:
        ret = s->blksize | (s->blkcnt << 16);
        break;
    case SDHC_ARGUMENT:
        ret = s->argument;
        break;
    case SDHC_TRNMOD:
        ret = s->trnmod | (s->cmdreg << 16);
        break;
    case SDHC_RSPREG0 ... SDHC_RSPREG3:
        ret = s->rspreg[((offset & ~0x3) - SDHC_RSPREG0) >> 2];
        break;
    case  SDHC_BDATA:
        if (sdhci_buff_access_is_sequential(s, offset - SDHC_BDATA)) {
            ret = SDHCI_GET_CLASS(s)->bdata_read(s, size);
            DPRINT_L2("read %ub: addr[0x%04x] -> %u(0x%x)\n", size, offset,
                      ret, ret);
            return ret;
        }
        break;
    case SDHC_PRNSTS:
        ret = s->prnsts;
        break;
    case SDHC_HOSTCTL:
        ret = s->hostctl | (s->pwrcon << 8) | (s->blkgap << 16) |
              (s->wakcon << 24);
        break;
    case SDHC_CLKCON:
        ret = s->clkcon | (s->timeoutcon << 16);
        break;
    case SDHC_NORINTSTS:
        ret = s->norintsts | (s->errintsts << 16);
        break;
    case SDHC_NORINTSTSEN:
        ret = s->norintstsen | (s->errintstsen << 16);
        break;
    case SDHC_NORINTSIGEN:
        ret = s->norintsigen | (s->errintsigen << 16);
        break;
    case SDHC_ACMD12ERRSTS:
        ret = s->acmd12errsts;
        break;
    case SDHC_CAPAREG:
        ret = s->capareg;
        break;
    case SDHC_MAXCURR:
        ret = s->maxcurr;
        break;
    case SDHC_ADMAERR:
        ret =  s->admaerr;
        break;
    case SDHC_ADMASYSADDR:
        ret = (uint32_t)s->admasysaddr;
        break;
    case SDHC_ADMASYSADDR + 4:
        ret = (uint32_t)(s->admasysaddr >> 32);
        break;
    case SDHC_SLOT_INT_STATUS:
        ret = (SD_HOST_SPECv2_VERS << 16) | sdhci_slotint(s);
        break;
    default:
        ERRPRINT("bad %ub read: addr[0x%04x]\n", size, offset);
        break;
    }

    ret >>= (offset & 0x3) * 8;
    ret &= (1ULL << (size * 8)) - 1;
    DPRINT_L2("read %ub: addr[0x%04x] -> %u(0x%x)\n", size, offset, ret, ret);
    return ret;
}

static inline void sdhci_blkgap_write(SDHCIState *s, uint8_t value)
{
    if ((value & SDHC_STOP_AT_GAP_REQ) && (s->blkgap & SDHC_STOP_AT_GAP_REQ)) {
        return;
    }
    s->blkgap = value & SDHC_STOP_AT_GAP_REQ;

    if ((value & SDHC_CONTINUE_REQ) && s->stopped_state &&
            (s->blkgap & SDHC_STOP_AT_GAP_REQ) == 0) {
        if (s->stopped_state == sdhc_gap_read) {
            s->prnsts |= SDHC_DAT_LINE_ACTIVE | SDHC_DOING_READ;
            SDHCI_GET_CLASS(s)->read_block_from_card(s);
        } else {
            s->prnsts |= SDHC_DAT_LINE_ACTIVE | SDHC_DOING_WRITE;
            SDHCI_GET_CLASS(s)->write_block_to_card(s);
        }
        s->stopped_state = sdhc_not_stopped;
    } else if (!s->stopped_state && (value & SDHC_STOP_AT_GAP_REQ)) {
        if (s->prnsts & SDHC_DOING_READ) {
            s->stopped_state = sdhc_gap_read;
        } else if (s->prnsts & SDHC_DOING_WRITE) {
            s->stopped_state = sdhc_gap_write;
        }
    }
}

static inline void sdhci_reset_write(SDHCIState *s, uint8_t value)
{
    switch (value) {
    case SDHC_RESET_ALL:
        DEVICE_GET_CLASS(s)->reset(DEVICE(s));
        break;
    case SDHC_RESET_CMD:
        s->prnsts &= ~SDHC_CMD_INHIBIT;
        s->norintsts &= ~SDHC_NIS_CMDCMP;
        break;
    case SDHC_RESET_DATA:
        s->data_count = 0;
        s->prnsts &= ~(SDHC_SPACE_AVAILABLE | SDHC_DATA_AVAILABLE |
                SDHC_DOING_READ | SDHC_DOING_WRITE |
                SDHC_DATA_INHIBIT | SDHC_DAT_LINE_ACTIVE);
        s->blkgap &= ~(SDHC_STOP_AT_GAP_REQ | SDHC_CONTINUE_REQ);
        s->stopped_state = sdhc_not_stopped;
        s->norintsts &= ~(SDHC_NIS_WBUFRDY | SDHC_NIS_RBUFRDY |
                SDHC_NIS_DMA | SDHC_NIS_TRSCMP | SDHC_NIS_BLKGAP);
        break;
    }
}

static void
sdhci_write(SDHCIState *s, unsigned int offset, uint32_t value, unsigned size)
{
    unsigned shift =  8 * (offset & 0x3);
    uint32_t mask = ~(((1ULL << (size * 8)) - 1) << shift);
    value <<= shift;

    switch (offset & ~0x3) {
    case SDHC_SYSAD:
        s->sdmasysad = (s->sdmasysad & mask) | value;
        MASKED_WRITE(s->sdmasysad, mask, value);
        /* Writing to last byte of sdmasysad might trigger transfer */
        if (!(mask & 0xFF000000) && TRANSFERRING_DATA(s->prnsts) && s->blkcnt &&
                s->blksize && SDHC_DMA_TYPE(s->hostctl) == SDHC_CTRL_SDMA) {
            SDHCI_GET_CLASS(s)->do_sdma_multi(s);
        }
        break;
    case SDHC_BLKSIZE:
        if (!TRANSFERRING_DATA(s->prnsts)) {
            MASKED_WRITE(s->blksize, mask, value);
            MASKED_WRITE(s->blkcnt, mask >> 16, value >> 16);
        }
        break;
    case SDHC_ARGUMENT:
        MASKED_WRITE(s->argument, mask, value);
        break;
    case SDHC_TRNMOD:
        /* DMA can be enabled only if it is supported as indicated by
         * capabilities register */
        if (!(s->capareg & SDHC_CAN_DO_DMA)) {
            value &= ~SDHC_TRNS_DMA;
        }
        MASKED_WRITE(s->trnmod, mask, value);
        MASKED_WRITE(s->cmdreg, mask >> 16, value >> 16);

        /* Writing to the upper byte of CMDREG triggers SD command generation */
        if ((mask & 0xFF000000) || !SDHCI_GET_CLASS(s)->can_issue_command(s)) {
            break;
        }

        SDHCI_GET_CLASS(s)->send_command(s);
        break;
    case  SDHC_BDATA:
        if (sdhci_buff_access_is_sequential(s, offset - SDHC_BDATA)) {
            SDHCI_GET_CLASS(s)->bdata_write(s, value >> shift, size);
        }
        break;
    case SDHC_HOSTCTL:
        if (!(mask & 0xFF0000)) {
            sdhci_blkgap_write(s, value >> 16);
        }
        MASKED_WRITE(s->hostctl, mask, value);
        MASKED_WRITE(s->pwrcon, mask >> 8, value >> 8);
        MASKED_WRITE(s->wakcon, mask >> 24, value >> 24);
        if (!(s->prnsts & SDHC_CARD_PRESENT) || ((s->pwrcon >> 1) & 0x7) < 5 ||
                !(s->capareg & (1 << (31 - ((s->pwrcon >> 1) & 0x7))))) {
            s->pwrcon &= ~SDHC_POWER_ON;
        }
        break;
    case SDHC_CLKCON:
        if (!(mask & 0xFF000000)) {
            sdhci_reset_write(s, value >> 24);
        }
        MASKED_WRITE(s->clkcon, mask, value);
        MASKED_WRITE(s->timeoutcon, mask >> 16, value >> 16);
        if (s->clkcon & SDHC_CLOCK_INT_EN) {
            s->clkcon |= SDHC_CLOCK_INT_STABLE;
        } else {
            s->clkcon &= ~SDHC_CLOCK_INT_STABLE;
        }
        break;
    case SDHC_NORINTSTS:
        if (s->norintstsen & SDHC_NISEN_CARDINT) {
            value &= ~SDHC_NIS_CARDINT;
        }
        s->norintsts &= mask | ~value;
        s->errintsts &= (mask >> 16) | ~(value >> 16);
        if (s->errintsts) {
            s->norintsts |= SDHC_NIS_ERR;
        } else {
            s->norintsts &= ~SDHC_NIS_ERR;
        }
        sdhci_update_irq(s);
        break;
    case SDHC_NORINTSTSEN:
        MASKED_WRITE(s->norintstsen, mask, value);
        MASKED_WRITE(s->errintstsen, mask >> 16, value >> 16);
        s->norintsts &= s->norintstsen;
        s->errintsts &= s->errintstsen;
        if (s->errintsts) {
            s->norintsts |= SDHC_NIS_ERR;
        } else {
            s->norintsts &= ~SDHC_NIS_ERR;
        }
        sdhci_update_irq(s);
        break;
    case SDHC_NORINTSIGEN:
        MASKED_WRITE(s->norintsigen, mask, value);
        MASKED_WRITE(s->errintsigen, mask >> 16, value >> 16);
        sdhci_update_irq(s);
        break;
    case SDHC_ADMAERR:
        MASKED_WRITE(s->admaerr, mask, value);
        break;
    case SDHC_ADMASYSADDR:
        s->admasysaddr = (s->admasysaddr & (0xFFFFFFFF00000000ULL |
                (uint64_t)mask)) | (uint64_t)value;
        break;
    case SDHC_ADMASYSADDR + 4:
        s->admasysaddr = (s->admasysaddr & (0x00000000FFFFFFFFULL |
                ((uint64_t)mask << 32))) | ((uint64_t)value << 32);
        break;
    case SDHC_FEAER:
        s->acmd12errsts |= value;
        s->errintsts |= (value >> 16) & s->errintstsen;
        if (s->acmd12errsts) {
            s->errintsts |= SDHC_EIS_CMD12ERR;
        }
        if (s->errintsts) {
            s->norintsts |= SDHC_NIS_ERR;
        }
        sdhci_update_irq(s);
        break;
    default:
        ERRPRINT("bad %ub write offset: addr[0x%04x] <- %u(0x%x)\n",
                size, offset, value >> shift, value >> shift);
        break;
    }
    DPRINT_L2("write %ub: addr[0x%04x] <- %u(0x%x)\n",
            size, offset, value >> shift, value >> shift);
}

static uint64_t
sdhci_readfn(void *opaque, hwaddr offset, unsigned size)
{
    SDHCIState *s = (SDHCIState *)opaque;

    return SDHCI_GET_CLASS(s)->mem_read(s, offset, size);
}

static void
sdhci_writefn(void *opaque, hwaddr off, uint64_t val, unsigned sz)
{
    SDHCIState *s = (SDHCIState *)opaque;

    SDHCI_GET_CLASS(s)->mem_write(s, off, val, sz);
}

static const MemoryRegionOps sdhci_mmio_ops = {
    .read = sdhci_readfn,
    .write = sdhci_writefn,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static inline unsigned int sdhci_get_fifolen(SDHCIState *s)
{
    switch (SDHC_CAPAB_BLOCKSIZE(s->capareg)) {
    case 0:
        return 512;
    case 1:
        return 1024;
    case 2:
        return 2048;
    default:
        hw_error("SDHC: unsupported value for maximum block size\n");
        return 0;
    }
}

static void sdhci_initfn(Object *obj)
{
    SDHCIState *s = SDHCI(obj);
    DriveInfo *di;

    di = drive_get_next(IF_SD);
    s->card = sd_init(di ? di->bdrv : NULL, false);
    if (s->card == NULL) {
        exit(1);
    }
    s->eject_cb = qemu_allocate_irq(sdhci_insert_eject_cb, s, 0);
    s->ro_cb = qemu_allocate_irq(sdhci_card_readonly_cb, s, 0);
    sd_set_cb(s->card, s->ro_cb, s->eject_cb);

    s->insert_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sdhci_raise_insertion_irq, s);
    s->transfer_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sdhci_do_data_transfer, s);
}

static void sdhci_uninitfn(Object *obj)
{
    SDHCIState *s = SDHCI(obj);

    timer_del(s->insert_timer);
    timer_free(s->insert_timer);
    timer_del(s->transfer_timer);
    timer_free(s->transfer_timer);
    qemu_free_irq(s->eject_cb);
    qemu_free_irq(s->ro_cb);

    if (s->fifo_buffer) {
        g_free(s->fifo_buffer);
        s->fifo_buffer = NULL;
    }
}

const VMStateDescription sdhci_vmstate = {
    .name = "sdhci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(sdmasysad, SDHCIState),
        VMSTATE_UINT16(blksize, SDHCIState),
        VMSTATE_UINT16(blkcnt, SDHCIState),
        VMSTATE_UINT32(argument, SDHCIState),
        VMSTATE_UINT16(trnmod, SDHCIState),
        VMSTATE_UINT16(cmdreg, SDHCIState),
        VMSTATE_UINT32_ARRAY(rspreg, SDHCIState, 4),
        VMSTATE_UINT32(prnsts, SDHCIState),
        VMSTATE_UINT8(hostctl, SDHCIState),
        VMSTATE_UINT8(pwrcon, SDHCIState),
        VMSTATE_UINT8(blkgap, SDHCIState),
        VMSTATE_UINT8(wakcon, SDHCIState),
        VMSTATE_UINT16(clkcon, SDHCIState),
        VMSTATE_UINT8(timeoutcon, SDHCIState),
        VMSTATE_UINT8(admaerr, SDHCIState),
        VMSTATE_UINT16(norintsts, SDHCIState),
        VMSTATE_UINT16(errintsts, SDHCIState),
        VMSTATE_UINT16(norintstsen, SDHCIState),
        VMSTATE_UINT16(errintstsen, SDHCIState),
        VMSTATE_UINT16(norintsigen, SDHCIState),
        VMSTATE_UINT16(errintsigen, SDHCIState),
        VMSTATE_UINT16(acmd12errsts, SDHCIState),
        VMSTATE_UINT16(data_count, SDHCIState),
        VMSTATE_UINT64(admasysaddr, SDHCIState),
        VMSTATE_UINT8(stopped_state, SDHCIState),
        VMSTATE_VBUFFER_UINT32(fifo_buffer, SDHCIState, 1, NULL, 0, buf_maxsz),
        VMSTATE_TIMER(insert_timer, SDHCIState),
        VMSTATE_TIMER(transfer_timer, SDHCIState),
        VMSTATE_END_OF_LIST()
    }
};

/* Capabilities registers provide information on supported features of this
 * specific host controller implementation */
static Property sdhci_properties[] = {
    DEFINE_PROP_UINT32("capareg", SDHCIState, capareg,
            SDHC_CAPAB_REG_DEFAULT),
    DEFINE_PROP_UINT32("maxcurr", SDHCIState, maxcurr, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void sdhci_realize(DeviceState *dev, Error ** errp)
{
    SDHCIState *s = SDHCI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    s->buf_maxsz = sdhci_get_fifolen(s);
    s->fifo_buffer = g_malloc0(s->buf_maxsz);
    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, OBJECT(s), &sdhci_mmio_ops, s, "sdhci",
            SDHC_REGISTERS_MAP_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void sdhci_generic_reset(DeviceState *ds)
{
    SDHCIState *s = SDHCI(ds);
    SDHCI_GET_CLASS(s)->reset(s);
}

static void sdhci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SDHCIClass *k = SDHCI_CLASS(klass);

    dc->vmsd = &sdhci_vmstate;
    dc->props = sdhci_properties;
    dc->reset = sdhci_generic_reset;
    dc->realize = sdhci_realize;

    k->reset = sdhci_reset;
    k->mem_read = sdhci_read;
    k->mem_write = sdhci_write;
    k->send_command = sdhci_send_command;
    k->can_issue_command = sdhci_can_issue_command;
    k->data_transfer = sdhci_data_transfer;
    k->end_data_transfer = sdhci_end_transfer;
    k->do_sdma_single = sdhci_sdma_transfer_single_block;
    k->do_sdma_multi = sdhci_sdma_transfer_multi_blocks;
    k->do_adma = sdhci_do_adma;
    k->read_block_from_card = sdhci_read_block_from_card;
    k->write_block_to_card = sdhci_write_block_to_card;
    k->bdata_read = sdhci_read_dataport;
    k->bdata_write = sdhci_write_dataport;
}

static const TypeInfo sdhci_type_info = {
    .name = TYPE_SDHCI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SDHCIState),
    .instance_init = sdhci_initfn,
    .instance_finalize = sdhci_uninitfn,
    .class_init = sdhci_class_init,
    .class_size = sizeof(SDHCIClass)
};

static void sdhci_register_types(void)
{
    type_register_static(&sdhci_type_info);
}

type_init(sdhci_register_types)
