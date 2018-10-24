/*
 * SSI to SD card adapter.
 *
 * Copyright (c) 2007-2009 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "sysemu/blockdev.h"
#include "hw/ssi/ssi.h"
#include "hw/sd/sd.h"
#include "qapi/error.h"

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
    SSI_SD_RESPONSE,
    SSI_SD_DATA_START,
    SSI_SD_DATA_READ,
} ssi_sd_mode;

typedef struct {
    SSISlave ssidev;
    uint32_t mode;
    int cmd;
    uint8_t cmdarg[4];
    uint8_t response[5];
    int32_t arglen;
    int32_t response_pos;
    int32_t stopping;
    SDBus sdbus;
} ssi_sd_state;

#define TYPE_SSI_SD "ssi-sd"
#define SSI_SD(obj) OBJECT_CHECK(ssi_sd_state, (obj), TYPE_SSI_SD)

/* State word bits.  */
#define SSI_SDR_LOCKED          0x0001
#define SSI_SDR_WP_ERASE        0x0002
#define SSI_SDR_ERROR           0x0004
#define SSI_SDR_CC_ERROR        0x0008
#define SSI_SDR_ECC_FAILED      0x0010
#define SSI_SDR_WP_VIOLATION    0x0020
#define SSI_SDR_ERASE_PARAM     0x0040
#define SSI_SDR_OUT_OF_RANGE    0x0080
#define SSI_SDR_IDLE            0x0100
#define SSI_SDR_ERASE_RESET     0x0200
#define SSI_SDR_ILLEGAL_COMMAND 0x0400
#define SSI_SDR_COM_CRC_ERROR   0x0800
#define SSI_SDR_ERASE_SEQ_ERROR 0x1000
#define SSI_SDR_ADDRESS_ERROR   0x2000
#define SSI_SDR_PARAMETER_ERROR 0x4000

static uint32_t ssi_sd_transfer(SSISlave *dev, uint32_t val)
{
    ssi_sd_state *s = FROM_SSI_SLAVE(ssi_sd_state, dev);

    /* Special case: allow CMD12 (STOP TRANSMISSION) while reading data.  */
    if (s->mode == SSI_SD_DATA_READ && val == 0x4d) {
        s->mode = SSI_SD_CMD;
        /* There must be at least one byte delay before the card responds.  */
        s->stopping = 1;
    }

    switch (s->mode) {
    case SSI_SD_CMD:
        if (val == 0xff) {
            DPRINTF("NULL command\n");
            return 0xff;
        }
        s->cmd = val & 0x3f;
        s->mode = SSI_SD_CMDARG;
        s->arglen = 0;
        return 0xff;
    case SSI_SD_CMDARG:
        if (s->arglen == 4) {
            SDRequest request;
            uint8_t longresp[16];
            /* FIXME: Check CRC.  */
            request.cmd = s->cmd;
            request.arg = ldl_be_p(s->cmdarg);
            DPRINTF("CMD%d arg 0x%08x\n", s->cmd, request.arg);
            s->arglen = sdbus_do_command(&s->sdbus, &request, longresp);
            if (s->arglen <= 0) {
                s->arglen = 1;
                s->response[0] = 4;
                DPRINTF("SD command failed\n");
            } else if (s->cmd == 58) {
                /* CMD58 returns R3 response (OCR)  */
                DPRINTF("Returned OCR\n");
                s->arglen = 5;
                s->response[0] = 1;
                memcpy(&s->response[1], longresp, 4);
            } else if (s->arglen != 4) {
                BADF("Unexpected response to cmd %d\n", s->cmd);
                /* Illegal command is about as near as we can get.  */
                s->arglen = 1;
                s->response[0] = 4;
            } else {
                /* All other commands return status.  */
                uint32_t cardstatus;
                uint16_t status;
                /* CMD13 returns a 2-byte statuse work. Other commands
                   only return the first byte.  */
                s->arglen = (s->cmd == 13) ? 2 : 1;
                cardstatus = ldl_be_p(longresp);
                status = 0;
                if (((cardstatus >> 9) & 0xf) < 4)
                    status |= SSI_SDR_IDLE;
                if (cardstatus & ERASE_RESET)
                    status |= SSI_SDR_ERASE_RESET;
                if (cardstatus & ILLEGAL_COMMAND)
                    status |= SSI_SDR_ILLEGAL_COMMAND;
                if (cardstatus & COM_CRC_ERROR)
                    status |= SSI_SDR_COM_CRC_ERROR;
                if (cardstatus & ERASE_SEQ_ERROR)
                    status |= SSI_SDR_ERASE_SEQ_ERROR;
                if (cardstatus & ADDRESS_ERROR)
                    status |= SSI_SDR_ADDRESS_ERROR;
                if (cardstatus & CARD_IS_LOCKED)
                    status |= SSI_SDR_LOCKED;
                if (cardstatus & (LOCK_UNLOCK_FAILED | WP_ERASE_SKIP))
                    status |= SSI_SDR_WP_ERASE;
                if (cardstatus & SD_ERROR)
                    status |= SSI_SDR_ERROR;
                if (cardstatus & CC_ERROR)
                    status |= SSI_SDR_CC_ERROR;
                if (cardstatus & CARD_ECC_FAILED)
                    status |= SSI_SDR_ECC_FAILED;
                if (cardstatus & WP_VIOLATION)
                    status |= SSI_SDR_WP_VIOLATION;
                if (cardstatus & ERASE_PARAM)
                    status |= SSI_SDR_ERASE_PARAM;
                if (cardstatus & (OUT_OF_RANGE | CID_CSD_OVERWRITE))
                    status |= SSI_SDR_OUT_OF_RANGE;
                /* ??? Don't know what Parameter Error really means, so
                   assume it's set if the second byte is nonzero.  */
                if (status & 0xff)
                    status |= SSI_SDR_PARAMETER_ERROR;
                s->response[0] = status >> 8;
                s->response[1] = status;
                DPRINTF("Card status 0x%02x\n", status);
            }
            s->mode = SSI_SD_RESPONSE;
            s->response_pos = 0;
        } else {
            s->cmdarg[s->arglen++] = val;
        }
        return 0xff;
    case SSI_SD_RESPONSE:
        if (s->stopping) {
            s->stopping = 0;
            return 0xff;
        }
        if (s->response_pos < s->arglen) {
            DPRINTF("Response 0x%02x\n", s->response[s->response_pos]);
            return s->response[s->response_pos++];
        }
        if (sdbus_data_ready(&s->sdbus)) {
            DPRINTF("Data read\n");
            s->mode = SSI_SD_DATA_START;
        } else {
            DPRINTF("End of command\n");
            s->mode = SSI_SD_CMD;
        }
        return 0xff;
    case SSI_SD_DATA_START:
        DPRINTF("Start read block\n");
        s->mode = SSI_SD_DATA_READ;
        return 0xfe;
    case SSI_SD_DATA_READ:
        val = sdbus_read_data(&s->sdbus);
        if (!sdbus_data_ready(&s->sdbus)) {
            DPRINTF("Data read end\n");
            s->mode = SSI_SD_CMD;
        }
        return val;
    }
    /* Should never happen.  */
    return 0xff;
}

static int ssi_sd_post_load(void *opaque, int version_id)
{
    ssi_sd_state *s = (ssi_sd_state *)opaque;

    if (s->mode > SSI_SD_DATA_READ) {
        return -EINVAL;
    }
    if (s->mode == SSI_SD_CMDARG &&
        (s->arglen < 0 || s->arglen >= ARRAY_SIZE(s->cmdarg))) {
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
    .version_id = 2,
    .minimum_version_id = 2,
    .post_load = ssi_sd_post_load,
    .fields = (VMStateField []) {
        VMSTATE_UINT32(mode, ssi_sd_state),
        VMSTATE_INT32(cmd, ssi_sd_state),
        VMSTATE_UINT8_ARRAY(cmdarg, ssi_sd_state, 4),
        VMSTATE_UINT8_ARRAY(response, ssi_sd_state, 5),
        VMSTATE_INT32(arglen, ssi_sd_state),
        VMSTATE_INT32(response_pos, ssi_sd_state),
        VMSTATE_INT32(stopping, ssi_sd_state),
        VMSTATE_SSI_SLAVE(ssidev, ssi_sd_state),
        VMSTATE_END_OF_LIST()
    }
};

static void ssi_sd_realize(SSISlave *d, Error **errp)
{
    ssi_sd_state *s = FROM_SSI_SLAVE(ssi_sd_state, d);
    DeviceState *carddev;
    DriveInfo *dinfo;
    Error *err = NULL;

    qbus_create_inplace(&s->sdbus, sizeof(s->sdbus), TYPE_SD_BUS,
                        DEVICE(d), "sd-bus");

    /* Create and plug in the sd card */
    /* FIXME use a qdev drive property instead of drive_get_next() */
    dinfo = drive_get_next(IF_SD);
    carddev = qdev_create(&s->sdbus.qbus, TYPE_SD_CARD);
    if (dinfo) {
        qdev_prop_set_drive(carddev, "drive", blk_by_legacy_dinfo(dinfo), &err);
    }
    object_property_set_bool(OBJECT(carddev), true, "spi", &err);
    object_property_set_bool(OBJECT(carddev), true, "realized", &err);
    if (err) {
        error_setg(errp, "failed to init SD card: %s", error_get_pretty(err));
        return;
    }
}

static void ssi_sd_reset(DeviceState *dev)
{
    ssi_sd_state *s = SSI_SD(dev);

    s->mode = SSI_SD_CMD;
    s->cmd = 0;
    memset(s->cmdarg, 0, sizeof(s->cmdarg));
    memset(s->response, 0, sizeof(s->response));
    s->arglen = 0;
    s->response_pos = 0;
    s->stopping = 0;
}

static void ssi_sd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SSISlaveClass *k = SSI_SLAVE_CLASS(klass);

    k->realize = ssi_sd_realize;
    k->transfer = ssi_sd_transfer;
    k->cs_polarity = SSI_CS_LOW;
    dc->vmsd = &vmstate_ssi_sd;
    dc->reset = ssi_sd_reset;
    /* Reason: init() method uses drive_get_next() */
    dc->user_creatable = false;
}

static const TypeInfo ssi_sd_info = {
    .name          = TYPE_SSI_SD,
    .parent        = TYPE_SSI_SLAVE,
    .instance_size = sizeof(ssi_sd_state),
    .class_init    = ssi_sd_class_init,
};

static void ssi_sd_register_types(void)
{
    type_register_static(&ssi_sd_info);
}

type_init(ssi_sd_register_types)
