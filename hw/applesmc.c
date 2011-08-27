/*
 *  Apple SMC controller
 *
 *  Copyright (c) 2007 Alexander Graf
 *
 *  Authors: Alexander Graf <agraf@suse.de>
 *           Susanne Graf <suse@csgraf.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************
 *
 * In all Intel-based Apple hardware there is an SMC chip to control the
 * backlight, fans and several other generic device parameters. It also
 * contains the magic keys used to dongle Mac OS X to the device.
 *
 * This driver was mostly created by looking at the Linux AppleSMC driver
 * implementation and does not support IRQ.
 *
 */

#include "hw.h"
#include "isa.h"
#include "console.h"
#include "qemu-timer.h"

/* #define DEBUG_SMC */

#define APPLESMC_DEFAULT_IOBASE        0x300
/* data port used by Apple SMC */
#define APPLESMC_DATA_PORT             0x0
/* command/status port used by Apple SMC */
#define APPLESMC_CMD_PORT              0x4
#define APPLESMC_NR_PORTS              32
#define APPLESMC_MAX_DATA_LENGTH       32

#define APPLESMC_READ_CMD              0x10
#define APPLESMC_WRITE_CMD             0x11
#define APPLESMC_GET_KEY_BY_INDEX_CMD  0x12
#define APPLESMC_GET_KEY_TYPE_CMD      0x13

#ifdef DEBUG_SMC
#define smc_debug(...) fprintf(stderr, "AppleSMC: " __VA_ARGS__)
#else
#define smc_debug(...) do { } while(0)
#endif

static char default_osk[64] = "This is a dummy key. Enter the real key "
                              "using the -osk parameter";

struct AppleSMCData {
    uint8_t len;
    const char *key;
    const char *data;
    QLIST_ENTRY(AppleSMCData) node;
};

struct AppleSMCStatus {
    ISADevice dev;
    uint32_t iobase;
    uint8_t cmd;
    uint8_t status;
    uint8_t key[4];
    uint8_t read_pos;
    uint8_t data_len;
    uint8_t data_pos;
    uint8_t data[255];
    uint8_t charactic[4];
    char *osk;
    QLIST_HEAD(, AppleSMCData) data_def;
};

static void applesmc_io_cmd_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    struct AppleSMCStatus *s = opaque;

    smc_debug("CMD Write B: %#x = %#x\n", addr, val);
    switch(val) {
        case APPLESMC_READ_CMD:
            s->status = 0x0c;
            break;
    }
    s->cmd = val;
    s->read_pos = 0;
    s->data_pos = 0;
}

static void applesmc_fill_data(struct AppleSMCStatus *s)
{
    struct AppleSMCData *d;

    QLIST_FOREACH(d, &s->data_def, node) {
        if (!memcmp(d->key, s->key, 4)) {
            smc_debug("Key matched (%s Len=%d Data=%s)\n", d->key,
                      d->len, d->data);
            memcpy(s->data, d->data, d->len);
            return;
        }
    }
}

static void applesmc_io_data_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    struct AppleSMCStatus *s = opaque;

    smc_debug("DATA Write B: %#x = %#x\n", addr, val);
    switch(s->cmd) {
        case APPLESMC_READ_CMD:
            if(s->read_pos < 4) {
                s->key[s->read_pos] = val;
                s->status = 0x04;
            } else if(s->read_pos == 4) {
                s->data_len = val;
                s->status = 0x05;
                s->data_pos = 0;
                smc_debug("Key = %c%c%c%c Len = %d\n", s->key[0],
                          s->key[1], s->key[2], s->key[3], val);
                applesmc_fill_data(s);
            }
            s->read_pos++;
            break;
    }
}

static uint32_t applesmc_io_data_readb(void *opaque, uint32_t addr1)
{
    struct AppleSMCStatus *s = opaque;
    uint8_t retval = 0;

    switch(s->cmd) {
        case APPLESMC_READ_CMD:
            if(s->data_pos < s->data_len) {
                retval = s->data[s->data_pos];
                smc_debug("READ_DATA[%d] = %#hhx\n", s->data_pos,
                          retval);
                s->data_pos++;
                if(s->data_pos == s->data_len) {
                    s->status = 0x00;
                    smc_debug("EOF\n");
                } else
                    s->status = 0x05;
            }
    }
    smc_debug("DATA Read b: %#x = %#x\n", addr1, retval);

    return retval;
}

static uint32_t applesmc_io_cmd_readb(void *opaque, uint32_t addr1)
{
    struct AppleSMCStatus *s = opaque;

    smc_debug("CMD Read B: %#x\n", addr1);
    return s->status;
}

static void applesmc_add_key(struct AppleSMCStatus *s, const char *key,
                             int len, const char *data)
{
    struct AppleSMCData *def;

    def = g_malloc0(sizeof(struct AppleSMCData));
    def->key = key;
    def->len = len;
    def->data = data;

    QLIST_INSERT_HEAD(&s->data_def, def, node);
}

static void qdev_applesmc_isa_reset(DeviceState *dev)
{
    struct AppleSMCStatus *s = DO_UPCAST(struct AppleSMCStatus, dev.qdev, dev);
    struct AppleSMCData *d, *next;

    /* Remove existing entries */
    QLIST_FOREACH_SAFE(d, &s->data_def, node, next) {
        QLIST_REMOVE(d, node);
    }

    applesmc_add_key(s, "REV ", 6, "\x01\x13\x0f\x00\x00\x03");
    applesmc_add_key(s, "OSK0", 32, s->osk);
    applesmc_add_key(s, "OSK1", 32, s->osk + 32);
    applesmc_add_key(s, "NATJ", 1, "\0");
    applesmc_add_key(s, "MSSP", 1, "\0");
    applesmc_add_key(s, "MSSD", 1, "\0x3");
}

static int applesmc_isa_init(ISADevice *dev)
{
    struct AppleSMCStatus *s = DO_UPCAST(struct AppleSMCStatus, dev, dev);

    register_ioport_read(s->iobase + APPLESMC_DATA_PORT, 4, 1,
                         applesmc_io_data_readb, s);
    register_ioport_read(s->iobase + APPLESMC_CMD_PORT, 4, 1,
                         applesmc_io_cmd_readb, s);
    register_ioport_write(s->iobase + APPLESMC_DATA_PORT, 4, 1,
                          applesmc_io_data_writeb, s);
    register_ioport_write(s->iobase + APPLESMC_CMD_PORT, 4, 1,
                          applesmc_io_cmd_writeb, s);

    if (!s->osk || (strlen(s->osk) != 64)) {
        fprintf(stderr, "WARNING: Using AppleSMC with invalid key\n");
        s->osk = default_osk;
    }

    QLIST_INIT(&s->data_def);
    qdev_applesmc_isa_reset(&dev->qdev);

    return 0;
}

static ISADeviceInfo applesmc_isa_info = {
    .qdev.name  = "isa-applesmc",
    .qdev.size  = sizeof(struct AppleSMCStatus),
    .qdev.reset = qdev_applesmc_isa_reset,
    .init       = applesmc_isa_init,
    .qdev.props = (Property[]) {
        DEFINE_PROP_HEX32("iobase", struct AppleSMCStatus, iobase,
                          APPLESMC_DEFAULT_IOBASE),
        DEFINE_PROP_STRING("osk", struct AppleSMCStatus, osk),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void applesmc_register_devices(void)
{
    isa_qdev_register(&applesmc_isa_info);
}

device_init(applesmc_register_devices)
