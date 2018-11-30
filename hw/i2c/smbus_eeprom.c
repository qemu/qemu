/*
 * QEMU SMBus EEPROM device
 *
 * Copyright (c) 2007 Arastra, Inc.
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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/smbus_slave.h"
#include "hw/i2c/smbus_eeprom.h"

//#define DEBUG

typedef struct SMBusEEPROMDevice {
    SMBusDevice smbusdev;
    void *data;
    uint8_t offset;
} SMBusEEPROMDevice;

static void eeprom_quick_cmd(SMBusDevice *dev, uint8_t read)
{
#ifdef DEBUG
    printf("eeprom_quick_cmd: addr=0x%02x read=%d\n", dev->i2c.address, read);
#endif
}

static uint8_t eeprom_receive_byte(SMBusDevice *dev)
{
    SMBusEEPROMDevice *eeprom = (SMBusEEPROMDevice *) dev;
    uint8_t *data = eeprom->data;
    uint8_t val = data[eeprom->offset++];
#ifdef DEBUG
    printf("eeprom_receive_byte: addr=0x%02x val=0x%02x\n",
           dev->i2c.address, val);
#endif
    return val;
}

static int eeprom_write_data(SMBusDevice *dev, uint8_t *buf, uint8_t len)
{
    SMBusEEPROMDevice *eeprom = (SMBusEEPROMDevice *) dev;
    uint8_t *data = eeprom->data;

#ifdef DEBUG
    printf("eeprom_write_byte: addr=0x%02x cmd=0x%02x val=0x%02x\n",
           dev->i2c.address, buf[0], buf[1]);
#endif
    /* len is guaranteed to be > 0 */
    eeprom->offset = buf[0];
    buf++;
    len--;

    for (; len > 0; len--) {
        data[eeprom->offset] = *buf++;
        eeprom->offset = (eeprom->offset + 1) % 256;
    }

    return 0;
}

static void smbus_eeprom_realize(DeviceState *dev, Error **errp)
{
    SMBusEEPROMDevice *eeprom = (SMBusEEPROMDevice *)dev;

    eeprom->offset = 0;
}

static Property smbus_eeprom_properties[] = {
    DEFINE_PROP_PTR("data", SMBusEEPROMDevice, data),
    DEFINE_PROP_END_OF_LIST(),
};

static void smbus_eeprom_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SMBusDeviceClass *sc = SMBUS_DEVICE_CLASS(klass);

    dc->realize = smbus_eeprom_realize;
    sc->quick_cmd = eeprom_quick_cmd;
    sc->receive_byte = eeprom_receive_byte;
    sc->write_data = eeprom_write_data;
    dc->props = smbus_eeprom_properties;
    /* Reason: pointer property "data" */
    dc->user_creatable = false;
}

static const TypeInfo smbus_eeprom_info = {
    .name          = "smbus-eeprom",
    .parent        = TYPE_SMBUS_DEVICE,
    .instance_size = sizeof(SMBusEEPROMDevice),
    .class_init    = smbus_eeprom_class_initfn,
};

static void smbus_eeprom_register_types(void)
{
    type_register_static(&smbus_eeprom_info);
}

type_init(smbus_eeprom_register_types)

void smbus_eeprom_init_one(I2CBus *smbus, uint8_t address, uint8_t *eeprom_buf)
{
    DeviceState *dev;

    dev = qdev_create((BusState *) smbus, "smbus-eeprom");
    qdev_prop_set_uint8(dev, "address", address);
    qdev_prop_set_ptr(dev, "data", eeprom_buf);
    qdev_init_nofail(dev);
}

void smbus_eeprom_init(I2CBus *smbus, int nb_eeprom,
                       const uint8_t *eeprom_spd, int eeprom_spd_size)
{
    int i;
    uint8_t *eeprom_buf = g_malloc0(8 * 256); /* XXX: make this persistent */
    if (eeprom_spd_size > 0) {
        memcpy(eeprom_buf, eeprom_spd, eeprom_spd_size);
    }

    for (i = 0; i < nb_eeprom; i++) {
        smbus_eeprom_init_one(smbus, 0x50 + i, eeprom_buf + (i * 256));
    }
}

/* Generate SDRAM SPD EEPROM data describing a module of type and size */
uint8_t *spd_data_generate(enum sdram_type type, ram_addr_t ram_size,
                           Error **errp)
{
    uint8_t *spd;
    uint8_t nbanks;
    uint16_t density;
    uint32_t size;
    int min_log2, max_log2, sz_log2;
    int i;

    switch (type) {
    case SDR:
        min_log2 = 2;
        max_log2 = 9;
        break;
    case DDR:
        min_log2 = 5;
        max_log2 = 12;
        break;
    case DDR2:
        min_log2 = 7;
        max_log2 = 14;
        break;
    default:
        g_assert_not_reached();
    }
    size = ram_size >> 20; /* work in terms of megabytes */
    if (size < 4) {
        error_setg(errp, "SDRAM size is too small");
        return NULL;
    }
    sz_log2 = 31 - clz32(size);
    size = 1U << sz_log2;
    if (ram_size > size * MiB) {
        error_setg(errp, "SDRAM size 0x"RAM_ADDR_FMT" is not a power of 2, "
                   "truncating to %u MB", ram_size, size);
    }
    if (sz_log2 < min_log2) {
        error_setg(errp,
                   "Memory size is too small for SDRAM type, adjusting type");
        if (size >= 32) {
            type = DDR;
            min_log2 = 5;
            max_log2 = 12;
        } else {
            type = SDR;
            min_log2 = 2;
            max_log2 = 9;
        }
    }

    nbanks = 1;
    while (sz_log2 > max_log2 && nbanks < 8) {
        sz_log2--;
        nbanks++;
    }

    if (size > (1ULL << sz_log2) * nbanks) {
        error_setg(errp, "Memory size is too big for SDRAM, truncating");
    }

    /* split to 2 banks if possible to avoid a bug in MIPS Malta firmware */
    if (nbanks == 1 && sz_log2 > min_log2) {
        sz_log2--;
        nbanks++;
    }

    density = 1ULL << (sz_log2 - 2);
    switch (type) {
    case DDR2:
        density = (density & 0xe0) | (density >> 8 & 0x1f);
        break;
    case DDR:
        density = (density & 0xf8) | (density >> 8 & 0x07);
        break;
    case SDR:
    default:
        density &= 0xff;
        break;
    }

    spd = g_malloc0(256);
    spd[0] = 128;   /* data bytes in EEPROM */
    spd[1] = 8;     /* log2 size of EEPROM */
    spd[2] = type;
    spd[3] = 13;    /* row address bits */
    spd[4] = 10;    /* column address bits */
    spd[5] = (type == DDR2 ? nbanks - 1 : nbanks);
    spd[6] = 64;    /* module data width */
                    /* reserved / data width high */
    spd[8] = 4;     /* interface voltage level */
    spd[9] = 0x25;  /* highest CAS latency */
    spd[10] = 1;    /* access time */
                    /* DIMM configuration 0 = non-ECC */
    spd[12] = 0x82; /* refresh requirements */
    spd[13] = 8;    /* primary SDRAM width */
                    /* ECC SDRAM width */
    spd[15] = (type == DDR2 ? 0 : 1); /* reserved / delay for random col rd */
    spd[16] = 12;   /* burst lengths supported */
    spd[17] = 4;    /* banks per SDRAM device */
    spd[18] = 12;   /* ~CAS latencies supported */
    spd[19] = (type == DDR2 ? 0 : 1); /* reserved / ~CS latencies supported */
    spd[20] = 2;    /* DIMM type / ~WE latencies */
                    /* module features */
                    /* memory chip features */
    spd[23] = 0x12; /* clock cycle time @ medium CAS latency */
                    /* data access time */
                    /* clock cycle time @ short CAS latency */
                    /* data access time */
    spd[27] = 20;   /* min. row precharge time */
    spd[28] = 15;   /* min. row active row delay */
    spd[29] = 20;   /* min. ~RAS to ~CAS delay */
    spd[30] = 45;   /* min. active to precharge time */
    spd[31] = density;
    spd[32] = 20;   /* addr/cmd setup time */
    spd[33] = 8;    /* addr/cmd hold time */
    spd[34] = 20;   /* data input setup time */
    spd[35] = 8;    /* data input hold time */

    /* checksum */
    for (i = 0; i < 63; i++) {
        spd[63] += spd[i];
    }
    return spd;
}
