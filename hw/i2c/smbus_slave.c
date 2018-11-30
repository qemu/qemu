/*
 * QEMU SMBus device emulation.
 *
 * This code is a helper for SMBus device emulation.  It implements an
 * I2C device inteface and runs the SMBus protocol from the device
 * point of view and maps those to simple calls to emulate.
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 */

/* TODO: Implement PEC.  */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/smbus_slave.h"

//#define DEBUG_SMBUS 1

#ifdef DEBUG_SMBUS
#define DPRINTF(fmt, ...) \
do { printf("smbus(%02x): " fmt , dev->i2c.address, ## __VA_ARGS__); } while (0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "smbus: error: " fmt , ## __VA_ARGS__); exit(1);} while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "smbus: error: " fmt , ## __VA_ARGS__);} while (0)
#endif

enum {
    SMBUS_IDLE,
    SMBUS_WRITE_DATA,
    SMBUS_RECV_BYTE,
    SMBUS_READ_DATA,
    SMBUS_DONE,
    SMBUS_CONFUSED = -1
};

static void smbus_do_quick_cmd(SMBusDevice *dev, int recv)
{
    SMBusDeviceClass *sc = SMBUS_DEVICE_GET_CLASS(dev);

    DPRINTF("Quick Command %d\n", recv);
    if (sc->quick_cmd) {
        sc->quick_cmd(dev, recv);
    }
}

static void smbus_do_write(SMBusDevice *dev)
{
    SMBusDeviceClass *sc = SMBUS_DEVICE_GET_CLASS(dev);

    if (dev->data_len == 1) {
        DPRINTF("Send Byte\n");
        if (sc->send_byte) {
            sc->send_byte(dev, dev->data_buf[0]);
        }
    } else {
        dev->command = dev->data_buf[0];
        DPRINTF("Command %d len %d\n", dev->command, dev->data_len - 1);
        if (sc->write_data) {
            sc->write_data(dev, dev->command, dev->data_buf + 1,
                           dev->data_len - 1);
        }
    }
}

static int smbus_i2c_event(I2CSlave *s, enum i2c_event event)
{
    SMBusDevice *dev = SMBUS_DEVICE(s);

    switch (event) {
    case I2C_START_SEND:
        switch (dev->mode) {
        case SMBUS_IDLE:
            DPRINTF("Incoming data\n");
            dev->mode = SMBUS_WRITE_DATA;
            break;
        default:
            BADF("Unexpected send start condition in state %d\n", dev->mode);
            dev->mode = SMBUS_CONFUSED;
            break;
        }
        break;

    case I2C_START_RECV:
        switch (dev->mode) {
        case SMBUS_IDLE:
            DPRINTF("Read mode\n");
            dev->mode = SMBUS_RECV_BYTE;
            break;
        case SMBUS_WRITE_DATA:
            if (dev->data_len == 0) {
                BADF("Read after write with no data\n");
                dev->mode = SMBUS_CONFUSED;
            } else {
                if (dev->data_len > 1) {
                    smbus_do_write(dev);
                } else {
                    dev->command = dev->data_buf[0];
                    DPRINTF("%02x: Command %d\n", dev->i2c.address,
                            dev->command);
                }
                DPRINTF("Read mode\n");
                dev->data_len = 0;
                dev->mode = SMBUS_READ_DATA;
            }
            break;
        default:
            BADF("Unexpected recv start condition in state %d\n", dev->mode);
            dev->mode = SMBUS_CONFUSED;
            break;
        }
        break;

    case I2C_FINISH:
        if (dev->data_len == 0) {
            if (dev->mode == SMBUS_WRITE_DATA || dev->mode == SMBUS_READ_DATA) {
                smbus_do_quick_cmd(dev, dev->mode == SMBUS_READ_DATA);
            }
        } else {
            switch (dev->mode) {
            case SMBUS_WRITE_DATA:
                smbus_do_write(dev);
                break;

            case SMBUS_READ_DATA:
                BADF("Unexpected stop during receive\n");
                break;

            default:
                /* Nothing to do.  */
                break;
            }
        }
        dev->mode = SMBUS_IDLE;
        dev->data_len = 0;
        break;

    case I2C_NACK:
        switch (dev->mode) {
        case SMBUS_DONE:
            /* Nothing to do.  */
            break;
        case SMBUS_READ_DATA:
            dev->mode = SMBUS_DONE;
            break;
        default:
            BADF("Unexpected NACK in state %d\n", dev->mode);
            dev->mode = SMBUS_CONFUSED;
            break;
        }
    }

    return 0;
}

static uint8_t smbus_i2c_recv(I2CSlave *s)
{
    SMBusDevice *dev = SMBUS_DEVICE(s);
    SMBusDeviceClass *sc = SMBUS_DEVICE_GET_CLASS(dev);
    uint8_t ret;

    switch (dev->mode) {
    case SMBUS_RECV_BYTE:
        if (sc->receive_byte) {
            ret = sc->receive_byte(dev);
        } else {
            ret = 0;
        }
        DPRINTF("Receive Byte %02x\n", ret);
        dev->mode = SMBUS_DONE;
        break;
    case SMBUS_READ_DATA:
        if (sc->read_data) {
            ret = sc->read_data(dev, dev->command, dev->data_len);
            dev->data_len++;
        } else {
            ret = 0;
        }
        DPRINTF("Read data %02x\n", ret);
        break;
    default:
        BADF("Unexpected read in state %d\n", dev->mode);
        dev->mode = SMBUS_CONFUSED;
        ret = 0;
        break;
    }
    return ret;
}

static int smbus_i2c_send(I2CSlave *s, uint8_t data)
{
    SMBusDevice *dev = SMBUS_DEVICE(s);

    switch (dev->mode) {
    case SMBUS_WRITE_DATA:
        DPRINTF("Write data %02x\n", data);
        if (dev->data_len >= sizeof(dev->data_buf)) {
            BADF("Too many bytes sent\n");
        } else {
            dev->data_buf[dev->data_len++] = data;
        }
        break;
    default:
        BADF("Unexpected write in state %d\n", dev->mode);
        break;
    }
    return 0;
}

static void smbus_device_class_init(ObjectClass *klass, void *data)
{
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(klass);

    sc->event = smbus_i2c_event;
    sc->recv = smbus_i2c_recv;
    sc->send = smbus_i2c_send;
}

static const TypeInfo smbus_device_type_info = {
    .name = TYPE_SMBUS_DEVICE,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(SMBusDevice),
    .abstract = true,
    .class_size = sizeof(SMBusDeviceClass),
    .class_init = smbus_device_class_init,
};

static void smbus_device_register_types(void)
{
    type_register_static(&smbus_device_type_info);
}

type_init(smbus_device_register_types)
