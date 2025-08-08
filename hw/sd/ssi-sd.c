/*
 * SSI to SD card adapter.
 *
 * Copyright (c) 2007-2009 CodeSourcery.
 * Written by Paul Brook
 *
 * Copyright (c) 2021 Wind River Systems, Inc.
 * Improved by Bin Meng <bin.meng@windriver.com>
 *
 * Validated with U-Boot v2021.01 and Linux v5.10 mmc_spi driver
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "system/blockdev.h"
#include "hw/ssi/ssi.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/sd/sd.h"
#include "qapi/error.h"
#include "qemu/crc-ccitt.h"
#include "qemu/module.h"
#include "qom/object.h"

//#define DEBUG_SSI_SD 1

#ifdef DEBUG_SSI_SD
#define DPRINTF(fmt, ...) \
do { printf("ssi_sd: " fmt , ## __VA_ARGS__); } while (0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "ssi_sd: error: " fmt , ## __VA_ARGS__); exit(1);} while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "ssi_sd: error: " fmt , ## __VA_ARGS__);} while (0)
#endif

typedef enum {
    SSI_SD_CMD = 0,
    SSI_SD_CMDARG,
    SSI_SD_PREP_RESP,
    SSI_SD_RESPONSE,
    SSI_SD_PREP_DATA,
    SSI_SD_DATA_START,
    SSI_SD_DATA_READ,
    SSI_SD_DATA_CRC16,
    SSI_SD_DATA_WRITE,
    SSI_SD_SKIP_CRC16,
} ssi_sd_mode;

struct ssi_sd_state {
    SSIPeripheral ssidev;
    uint32_t mode;
    int cmd;
    uint8_t cmdarg[4];
    uint8_t response[5];
    uint16_t crc16;
    int32_t read_bytes;
    int32_t write_bytes;
    int32_t arglen;
    int32_t response_pos;
    int32_t stopping;
    SDBus sdbus;
};

#define TYPE_SSI_SD "ssi-sd"
OBJECT_DECLARE_SIMPLE_TYPE(ssi_sd_state, SSI_SD)

/* multiple block write */
#define SSI_TOKEN_MULTI_WRITE   0xfc
/* terminate multiple block write */
#define SSI_TOKEN_STOP_TRAN     0xfd
/* single block read/write, multiple block read */
#define SSI_TOKEN_SINGLE        0xfe

/* dummy value - don't care */
#define SSI_DUMMY               0xff

/* data accepted */
#define DATA_RESPONSE_ACCEPTED  0x05

static uint32_t ssi_sd_transfer(SSIPeripheral *dev, uint32_t val)
{
    ssi_sd_state *s = SSI_SD(dev);
    SDRequest request;
    uint8_t longresp[5];

    if (!sdbus_get_inserted(&s->sdbus)) {
        return SSI_DUMMY;
    }

    /*
     * Special case: allow CMD12 (STOP TRANSMISSION) while reading data.
     *
     * See "Physical Layer Specification Version 8.00" chapter 7.5.2.2,
     * to avoid conflict between CMD12 response and next data block,
     * timing of CMD12 should be controlled as follows:
     *
     * - CMD12 issued at the timing that end bit of CMD12 and end bit of
     *   data block is overlapped
     * - CMD12 issued after one clock cycle after host receives a token
     *   (either Start Block token or Data Error token)
     *
     * We need to catch CMD12 in all of the data read states.
     */
    if (s->mode >= SSI_SD_PREP_DATA && s->mode <= SSI_SD_DATA_CRC16) {
        if (val == 0x4c) {
            s->mode = SSI_SD_CMD;
            /* There must be at least one byte delay before the card responds */
            s->stopping = 1;
        }
    }

    switch (s->mode) {
    case SSI_SD_CMD:
        switch (val) {
        case SSI_DUMMY:
            DPRINTF("NULL command\n");
            return SSI_DUMMY;
            break;
        case SSI_TOKEN_SINGLE:
        case SSI_TOKEN_MULTI_WRITE:
            DPRINTF("Start write block\n");
            s->mode = SSI_SD_DATA_WRITE;
            return SSI_DUMMY;
        case SSI_TOKEN_STOP_TRAN:
            DPRINTF("Stop multiple write\n");

            /* manually issue cmd12 to stop the transfer */
            request.cmd = 12;
            request.arg = 0;
            s->arglen = sdbus_do_command(&s->sdbus, &request,
                                         longresp, sizeof(longresp));
            if (s->arglen == 0) {
                s->arglen = 1;
                /* a zero value indicates the card is busy */
                s->response[0] = 0;
                DPRINTF("SD card busy\n");
            } else {
                s->arglen = 1;
                /* a non-zero value indicates the card is ready */
                s->response[0] = SSI_DUMMY;
            }

            return SSI_DUMMY;
        }

        s->cmd = val & 0x3f;
        s->mode = SSI_SD_CMDARG;
        s->arglen = 0;
        return SSI_DUMMY;
    case SSI_SD_CMDARG:
        if (s->arglen == 4) {
            /* FIXME: Check CRC.  */
            request.cmd = s->cmd;
            request.arg = ldl_be_p(s->cmdarg);
            s->arglen = sdbus_do_command(&s->sdbus, &request,
                                         longresp, sizeof(longresp));
            DPRINTF("CMD%d arg 0x%08x = %d\n", s->cmd, request.arg, s->arglen);
            assert(s->arglen > 0);
            memcpy(s->response, longresp, s->arglen);

            /* handle R1b (busy signal) */
            if (s->cmd == 28 || s->cmd == 29 || s->cmd == 38) {
                s->stopping = 1;
            }
            s->mode = SSI_SD_PREP_RESP;
            s->response_pos = 0;
        } else {
            s->cmdarg[s->arglen++] = val;
        }
        return SSI_DUMMY;
    case SSI_SD_PREP_RESP:
        DPRINTF("Prepare card response (Ncr)\n");
        s->mode = SSI_SD_RESPONSE;
        return SSI_DUMMY;
    case SSI_SD_RESPONSE:
        if (s->response_pos < s->arglen) {
            DPRINTF("Response 0x%02x\n", s->response[s->response_pos]);
            return s->response[s->response_pos++];
        }
        if (s->stopping) {
            s->stopping = 0;
            s->mode = SSI_SD_CMD;
            return SSI_DUMMY;
        }
        if (sdbus_data_ready(&s->sdbus)) {
            DPRINTF("Data read\n");
            s->mode = SSI_SD_DATA_START;
        } else {
            DPRINTF("End of command\n");
            s->mode = SSI_SD_CMD;
        }
        return SSI_DUMMY;
    case SSI_SD_PREP_DATA:
        DPRINTF("Prepare data block (Nac)\n");
        s->mode = SSI_SD_DATA_START;
        return SSI_DUMMY;
    case SSI_SD_DATA_START:
        DPRINTF("Start read block\n");
        s->mode = SSI_SD_DATA_READ;
        s->response_pos = 0;
        return SSI_TOKEN_SINGLE;
    case SSI_SD_DATA_READ:
        val = sdbus_read_byte(&s->sdbus);
        s->read_bytes++;
        s->crc16 = crc_ccitt_false(s->crc16, (uint8_t *)&val, 1);
        if (!sdbus_data_ready(&s->sdbus) || s->read_bytes == 512) {
            DPRINTF("Data read end\n");
            s->mode = SSI_SD_DATA_CRC16;
        }
        return val;
    case SSI_SD_DATA_CRC16:
        val = (s->crc16 & 0xff00) >> 8;
        s->crc16 <<= 8;
        s->response_pos++;
        if (s->response_pos == 2) {
            DPRINTF("CRC16 read end\n");
            if (s->read_bytes == 512 && s->cmd != 17) {
                s->mode = SSI_SD_PREP_DATA;
            } else {
                s->mode = SSI_SD_CMD;
            }
            s->read_bytes = 0;
            s->response_pos = 0;
        }
        return val;
    case SSI_SD_DATA_WRITE:
        sdbus_write_byte(&s->sdbus, val);
        s->write_bytes++;
        if (!sdbus_receive_ready(&s->sdbus) || s->write_bytes == 512) {
            DPRINTF("Data write end\n");
            s->mode = SSI_SD_SKIP_CRC16;
            s->response_pos = 0;
        }
        return val;
    case SSI_SD_SKIP_CRC16:
        /* we don't verify the crc16 */
        s->response_pos++;
        if (s->response_pos == 2) {
            DPRINTF("CRC16 receive end\n");
            s->mode = SSI_SD_RESPONSE;
            s->write_bytes = 0;
            s->arglen = 1;
            s->response[0] = DATA_RESPONSE_ACCEPTED;
            s->response_pos = 0;
        }
        return SSI_DUMMY;
    }
    /* Should never happen.  */
    return SSI_DUMMY;
}

static int ssi_sd_post_load(void *opaque, int version_id)
{
    ssi_sd_state *s = (ssi_sd_state *)opaque;

    if (s->mode > SSI_SD_SKIP_CRC16) {
        return -EINVAL;
    }
    if (s->mode == SSI_SD_CMDARG &&
        (s->arglen >= ARRAY_SIZE(s->cmdarg))) {
        return -EINVAL;
    }
    if (s->mode == SSI_SD_RESPONSE &&
        (s->response_pos < 0 || s->response_pos >= ARRAY_SIZE(s->response) ||
        (!s->stopping && s->arglen > ARRAY_SIZE(s->response)))) {
        return -EINVAL;
    }

    return 0;
}

static const VMStateDescription vmstate_ssi_sd = {
    .name = "ssi_sd",
    .version_id = 7,
    .minimum_version_id = 7,
    .post_load = ssi_sd_post_load,
    .fields = (const VMStateField []) {
        VMSTATE_UINT32(mode, ssi_sd_state),
        VMSTATE_INT32(cmd, ssi_sd_state),
        VMSTATE_UINT8_ARRAY(cmdarg, ssi_sd_state, 4),
        VMSTATE_UINT8_ARRAY(response, ssi_sd_state, 5),
        VMSTATE_UINT16(crc16, ssi_sd_state),
        VMSTATE_INT32(read_bytes, ssi_sd_state),
        VMSTATE_INT32(write_bytes, ssi_sd_state),
        VMSTATE_INT32(arglen, ssi_sd_state),
        VMSTATE_INT32(response_pos, ssi_sd_state),
        VMSTATE_INT32(stopping, ssi_sd_state),
        VMSTATE_SSI_PERIPHERAL(ssidev, ssi_sd_state),
        VMSTATE_END_OF_LIST()
    }
};

static void ssi_sd_realize(SSIPeripheral *d, Error **errp)
{
    ssi_sd_state *s = SSI_SD(d);

    qbus_init(&s->sdbus, sizeof(s->sdbus), TYPE_SD_BUS, DEVICE(d), "sd-bus");
}

static void ssi_sd_reset(DeviceState *dev)
{
    ssi_sd_state *s = SSI_SD(dev);

    s->mode = SSI_SD_CMD;
    s->cmd = 0;
    memset(s->cmdarg, 0, sizeof(s->cmdarg));
    memset(s->response, 0, sizeof(s->response));
    s->crc16 = 0;
    s->read_bytes = 0;
    s->write_bytes = 0;
    s->arglen = 0;
    s->response_pos = 0;
    s->stopping = 0;
}

static void ssi_sd_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);

    k->realize = ssi_sd_realize;
    k->transfer = ssi_sd_transfer;
    k->cs_polarity = SSI_CS_LOW;
    dc->vmsd = &vmstate_ssi_sd;
    device_class_set_legacy_reset(dc, ssi_sd_reset);
    /* Reason: GPIO chip-select line should be wired up */
    dc->user_creatable = false;
}

static const TypeInfo ssi_sd_types[] = {
    {
        .name           = TYPE_SSI_SD,
        .parent         = TYPE_SSI_PERIPHERAL,
        .instance_size  = sizeof(ssi_sd_state),
        .class_init     = ssi_sd_class_init,
    },
};

DEFINE_TYPES(ssi_sd_types)
