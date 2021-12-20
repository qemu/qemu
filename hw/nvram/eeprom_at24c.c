/*
 * *AT24C* series I2C EEPROM
 *
 * Copyright (c) 2015 Michael Davidsaver
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the LICENSE file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/i2c/i2c.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "sysemu/block-backend.h"
#include "qom/object.h"

/* #define DEBUG_AT24C */

#ifdef DEBUG_AT24C
#define DPRINTK(FMT, ...) printf(TYPE_AT24C_EE " : " FMT, ## __VA_ARGS__)
#else
#define DPRINTK(FMT, ...) do {} while (0)
#endif

#define ERR(FMT, ...) fprintf(stderr, TYPE_AT24C_EE " : " FMT, \
                            ## __VA_ARGS__)

#define TYPE_AT24C_EE "at24c-eeprom"
typedef struct EEPROMState EEPROMState;
DECLARE_INSTANCE_CHECKER(EEPROMState, AT24C_EE,
                         TYPE_AT24C_EE)

struct EEPROMState {
    I2CSlave parent_obj;

    /* address counter */
    uint16_t cur;
    /* total size in bytes */
    uint32_t rsize;
    bool writable;
    /* cells changed since last START? */
    bool changed;
    /* during WRITE, # of address bytes transfered */
    uint8_t haveaddr;

    uint8_t *mem;

    BlockBackend *blk;
};

static
int at24c_eeprom_event(I2CSlave *s, enum i2c_event event)
{
    EEPROMState *ee = AT24C_EE(s);

    switch (event) {
    case I2C_START_SEND:
    case I2C_FINISH:
        ee->haveaddr = 0;
        /* fallthrough */
    case I2C_START_RECV:
        DPRINTK("clear\n");
        if (ee->blk && ee->changed) {
            int len = blk_pwrite(ee->blk, 0, ee->mem, ee->rsize, 0);
            if (len != ee->rsize) {
                ERR(TYPE_AT24C_EE
                        " : failed to write backing file\n");
            }
            DPRINTK("Wrote to backing file\n");
        }
        ee->changed = false;
        break;
    case I2C_NACK:
        break;
    }
    return 0;
}

static
uint8_t at24c_eeprom_recv(I2CSlave *s)
{
    EEPROMState *ee = AT24C_EE(s);
    uint8_t ret;

    if (ee->haveaddr == 1) {
        return 0xff;
    }

    ret = ee->mem[ee->cur];

    ee->cur = (ee->cur + 1u) % ee->rsize;
    DPRINTK("Recv %02x %c\n", ret, ret);

    return ret;
}

static
int at24c_eeprom_send(I2CSlave *s, uint8_t data)
{
    EEPROMState *ee = AT24C_EE(s);

    if (ee->haveaddr < 2) {
        ee->cur <<= 8;
        ee->cur |= data;
        ee->haveaddr++;
        if (ee->haveaddr == 2) {
            ee->cur %= ee->rsize;
            DPRINTK("Set pointer %04x\n", ee->cur);
        }

    } else {
        if (ee->writable) {
            DPRINTK("Send %02x\n", data);
            ee->mem[ee->cur] = data;
            ee->changed = true;
        } else {
            DPRINTK("Send error %02x read-only\n", data);
        }
        ee->cur = (ee->cur + 1u) % ee->rsize;

    }

    return 0;
}

static void at24c_eeprom_realize(DeviceState *dev, Error **errp)
{
    EEPROMState *ee = AT24C_EE(dev);

    if (ee->blk) {
        int64_t len = blk_getlength(ee->blk);

        if (len != ee->rsize) {
            error_setg(errp, "%s: Backing file size %" PRId64 " != %u",
                       TYPE_AT24C_EE, len, ee->rsize);
            return;
        }

        if (blk_set_perm(ee->blk, BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                         BLK_PERM_ALL, &error_fatal) < 0)
        {
            error_setg(errp, "%s: Backing file incorrect permission",
                       TYPE_AT24C_EE);
            return;
        }
    }

    ee->mem = g_malloc0(ee->rsize);
}

static
void at24c_eeprom_reset(DeviceState *state)
{
    EEPROMState *ee = AT24C_EE(state);

    ee->changed = false;
    ee->cur = 0;
    ee->haveaddr = 0;

    memset(ee->mem, 0, ee->rsize);

    if (ee->blk) {
        int len = blk_pread(ee->blk, 0, ee->mem, ee->rsize);

        if (len != ee->rsize) {
            ERR(TYPE_AT24C_EE
                    " : Failed initial sync with backing file\n");
        }
        DPRINTK("Reset read backing file\n");
    }
}

static Property at24c_eeprom_props[] = {
    DEFINE_PROP_UINT32("rom-size", EEPROMState, rsize, 0),
    DEFINE_PROP_BOOL("writable", EEPROMState, writable, true),
    DEFINE_PROP_DRIVE("drive", EEPROMState, blk),
    DEFINE_PROP_END_OF_LIST()
};

static
void at24c_eeprom_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->realize = &at24c_eeprom_realize;
    k->event = &at24c_eeprom_event;
    k->recv = &at24c_eeprom_recv;
    k->send = &at24c_eeprom_send;

    device_class_set_props(dc, at24c_eeprom_props);
    dc->reset = at24c_eeprom_reset;
}

static
const TypeInfo at24c_eeprom_type = {
    .name = TYPE_AT24C_EE,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(EEPROMState),
    .class_size = sizeof(I2CSlaveClass),
    .class_init = at24c_eeprom_class_init,
};

static void at24c_eeprom_register(void)
{
    type_register_static(&at24c_eeprom_type);
}

type_init(at24c_eeprom_register)
