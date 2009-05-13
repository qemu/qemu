/*
 * QEMU SMBus device emulation.
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the LGPL.
 */

/* TODO: Implement PEC.  */

#include "hw.h"
#include "i2c.h"
#include "smbus.h"

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
    DPRINTF("Quick Command %d\n", recv);
    if (dev->quick_cmd)
        dev->quick_cmd(dev, recv);
}

static void smbus_do_write(SMBusDevice *dev)
{
    if (dev->data_len == 0) {
        smbus_do_quick_cmd(dev, 0);
    } else if (dev->data_len == 1) {
        DPRINTF("Send Byte\n");
        if (dev->send_byte) {
            dev->send_byte(dev, dev->data_buf[0]);
        }
    } else {
        dev->command = dev->data_buf[0];
        DPRINTF("Command %d len %d\n", dev->command, dev->data_len - 1);
        if (dev->write_data) {
            dev->write_data(dev, dev->command, dev->data_buf + 1,
                            dev->data_len - 1);
        }
    }
}

static void smbus_i2c_event(i2c_slave *s, enum i2c_event event)
{
    SMBusDevice *dev = (SMBusDevice *)s;
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
        switch (dev->mode) {
        case SMBUS_WRITE_DATA:
            smbus_do_write(dev);
            break;
        case SMBUS_RECV_BYTE:
            smbus_do_quick_cmd(dev, 1);
            break;
        case SMBUS_READ_DATA:
            BADF("Unexpected stop during receive\n");
            break;
        default:
            /* Nothing to do.  */
            break;
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
}

static int smbus_i2c_recv(i2c_slave *s)
{
    SMBusDevice *dev = (SMBusDevice *)s;
    int ret;

    switch (dev->mode) {
    case SMBUS_RECV_BYTE:
        if (dev->receive_byte) {
            ret = dev->receive_byte(dev);
        } else {
            ret = 0;
        }
        DPRINTF("Receive Byte %02x\n", ret);
        dev->mode = SMBUS_DONE;
        break;
    case SMBUS_READ_DATA:
        if (dev->read_data) {
            ret = dev->read_data(dev, dev->command, dev->data_len);
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

static int smbus_i2c_send(i2c_slave *s, uint8_t data)
{
    SMBusDevice *dev = (SMBusDevice *)s;
    switch (dev->mode) {
    case SMBUS_WRITE_DATA:
        DPRINTF("Write data %02x\n", data);
        dev->data_buf[dev->data_len++] = data;
        break;
    default:
        BADF("Unexpected write in state %d\n", dev->mode);
        break;
    }
    return 0;
}

SMBusDevice *smbus_device_init(i2c_bus *bus, int address, int size)
{
    SMBusDevice *dev;

    if (size < sizeof(SMBusDevice))
        hw_error("SMBus struct too small");

    dev = (SMBusDevice *)i2c_slave_init(bus, address, size);
    dev->i2c.event = smbus_i2c_event;
    dev->i2c.recv = smbus_i2c_recv;
    dev->i2c.send = smbus_i2c_send;

    return dev;
}

/* Master device commands.  */
void smbus_quick_command(i2c_bus *bus, int addr, int read)
{
    i2c_start_transfer(bus, addr, read);
    i2c_end_transfer(bus);
}

uint8_t smbus_receive_byte(i2c_bus *bus, int addr)
{
    uint8_t data;

    i2c_start_transfer(bus, addr, 1);
    data = i2c_recv(bus);
    i2c_nack(bus);
    i2c_end_transfer(bus);
    return data;
}

void smbus_send_byte(i2c_bus *bus, int addr, uint8_t data)
{
    i2c_start_transfer(bus, addr, 0);
    i2c_send(bus, data);
    i2c_end_transfer(bus);
}

uint8_t smbus_read_byte(i2c_bus *bus, int addr, uint8_t command)
{
    uint8_t data;
    i2c_start_transfer(bus, addr, 0);
    i2c_send(bus, command);
    i2c_start_transfer(bus, addr, 1);
    data = i2c_recv(bus);
    i2c_nack(bus);
    i2c_end_transfer(bus);
    return data;
}

void smbus_write_byte(i2c_bus *bus, int addr, uint8_t command, uint8_t data)
{
    i2c_start_transfer(bus, addr, 0);
    i2c_send(bus, command);
    i2c_send(bus, data);
    i2c_end_transfer(bus);
}

uint16_t smbus_read_word(i2c_bus *bus, int addr, uint8_t command)
{
    uint16_t data;
    i2c_start_transfer(bus, addr, 0);
    i2c_send(bus, command);
    i2c_start_transfer(bus, addr, 1);
    data = i2c_recv(bus);
    data |= i2c_recv(bus) << 8;
    i2c_nack(bus);
    i2c_end_transfer(bus);
    return data;
}

void smbus_write_word(i2c_bus *bus, int addr, uint8_t command, uint16_t data)
{
    i2c_start_transfer(bus, addr, 0);
    i2c_send(bus, command);
    i2c_send(bus, data & 0xff);
    i2c_send(bus, data >> 8);
    i2c_end_transfer(bus);
}

int smbus_read_block(i2c_bus *bus, int addr, uint8_t command, uint8_t *data)
{
    int len;
    int i;

    i2c_start_transfer(bus, addr, 0);
    i2c_send(bus, command);
    i2c_start_transfer(bus, addr, 1);
    len = i2c_recv(bus);
    if (len > 32)
        len = 0;
    for (i = 0; i < len; i++)
        data[i] = i2c_recv(bus);
    i2c_nack(bus);
    i2c_end_transfer(bus);
    return len;
}

void smbus_write_block(i2c_bus *bus, int addr, uint8_t command, uint8_t *data,
                       int len)
{
    int i;

    if (len > 32)
        len = 32;

    i2c_start_transfer(bus, addr, 0);
    i2c_send(bus, command);
    i2c_send(bus, len);
    for (i = 0; i < len; i++)
        i2c_send(bus, data[i]);
    i2c_end_transfer(bus);
}
