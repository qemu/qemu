/*
 * QEMU I2C bus interface.
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"

typedef struct I2CNode I2CNode;

struct I2CNode {
    I2CSlave *elt;
    QLIST_ENTRY(I2CNode) next;
};

struct I2CBus
{
    BusState qbus;
    QLIST_HEAD(, I2CNode) current_devs;
    uint8_t saved_address;
    bool broadcast;
};

static Property i2c_props[] = {
    DEFINE_PROP_UINT8("address", struct I2CSlave, address, 0),
    DEFINE_PROP_END_OF_LIST(),
};

#define TYPE_I2C_BUS "i2c-bus"
#define I2C_BUS(obj) OBJECT_CHECK(I2CBus, (obj), TYPE_I2C_BUS)

static const TypeInfo i2c_bus_info = {
    .name = TYPE_I2C_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(I2CBus),
};

static void i2c_bus_pre_save(void *opaque)
{
    I2CBus *bus = opaque;

    bus->saved_address = -1;
    if (!QLIST_EMPTY(&bus->current_devs)) {
        if (!bus->broadcast) {
            bus->saved_address = QLIST_FIRST(&bus->current_devs)->elt->address;
        }
    }
}

static const VMStateDescription vmstate_i2c_bus = {
    .name = "i2c_bus",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = i2c_bus_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(saved_address, I2CBus),
        VMSTATE_BOOL(broadcast, I2CBus),
        VMSTATE_END_OF_LIST()
    }
};

/* Create a new I2C bus.  */
I2CBus *i2c_init_bus(DeviceState *parent, const char *name)
{
    I2CBus *bus;

    bus = I2C_BUS(qbus_create(TYPE_I2C_BUS, parent, name));
    QLIST_INIT(&bus->current_devs);
    vmstate_register(NULL, -1, &vmstate_i2c_bus, bus);
    return bus;
}

void i2c_set_slave_address(I2CSlave *dev, uint8_t address)
{
    dev->address = address;
}

/* Return nonzero if bus is busy.  */
int i2c_bus_busy(I2CBus *bus)
{
    return !QLIST_EMPTY(&bus->current_devs);
}

/* Returns non-zero if the address is not valid.  */
/* TODO: Make this handle multiple masters.  */
int i2c_start_transfer(I2CBus *bus, uint8_t address, int recv)
{
    BusChild *kid;
    I2CSlaveClass *sc;
    I2CNode *node;

    if (address == 0x00) {
        /*
         * This is a broadcast, the current_devs will be all the devices of the
         * bus.
         */
        bus->broadcast = true;
    }

    QTAILQ_FOREACH(kid, &bus->qbus.children, sibling) {
        DeviceState *qdev = kid->child;
        I2CSlave *candidate = I2C_SLAVE(qdev);
        if ((candidate->address == address) || (bus->broadcast)) {
            node = g_malloc(sizeof(struct I2CNode));
            node->elt = candidate;
            QLIST_INSERT_HEAD(&bus->current_devs, node, next);
            if (!bus->broadcast) {
                break;
            }
        }
    }

    if (QLIST_EMPTY(&bus->current_devs)) {
        return 1;
    }

    QLIST_FOREACH(node, &bus->current_devs, next) {
        sc = I2C_SLAVE_GET_CLASS(node->elt);
        /* If the bus is already busy, assume this is a repeated
           start condition.  */
        if (sc->event) {
            sc->event(node->elt, recv ? I2C_START_RECV : I2C_START_SEND);
        }
    }
    return 0;
}

void i2c_end_transfer(I2CBus *bus)
{
    I2CSlaveClass *sc;
    I2CNode *node, *next;

    if (QLIST_EMPTY(&bus->current_devs)) {
        return;
    }

    QLIST_FOREACH_SAFE(node, &bus->current_devs, next, next) {
        sc = I2C_SLAVE_GET_CLASS(node->elt);
        if (sc->event) {
            sc->event(node->elt, I2C_FINISH);
        }
        QLIST_REMOVE(node, next);
        g_free(node);
    }
    bus->broadcast = false;
}

int i2c_send(I2CBus *bus, uint8_t data)
{
    I2CSlaveClass *sc;
    I2CNode *node;
    int ret = 0;

    QLIST_FOREACH(node, &bus->current_devs, next) {
        sc = I2C_SLAVE_GET_CLASS(node->elt);
        if (sc->send) {
            ret = ret || sc->send(node->elt, data);
        } else {
            ret = -1;
        }
    }
    return ret ? -1 : 0;
}

int i2c_recv(I2CBus *bus)
{
    I2CSlaveClass *sc;

    if ((QLIST_EMPTY(&bus->current_devs)) || (bus->broadcast)) {
        return -1;
    }

    sc = I2C_SLAVE_GET_CLASS(QLIST_FIRST(&bus->current_devs)->elt);
    if (sc->recv) {
        return sc->recv(QLIST_FIRST(&bus->current_devs)->elt);
    }
    return -1;
}

void i2c_nack(I2CBus *bus)
{
    I2CSlaveClass *sc;
    I2CNode *node;

    if (QLIST_EMPTY(&bus->current_devs)) {
        return;
    }

    QLIST_FOREACH(node, &bus->current_devs, next) {
        sc = I2C_SLAVE_GET_CLASS(node->elt);
        if (sc->event) {
            sc->event(node->elt, I2C_NACK);
        }
    }
}

static int i2c_slave_post_load(void *opaque, int version_id)
{
    I2CSlave *dev = opaque;
    I2CBus *bus;
    I2CNode *node;

    bus = I2C_BUS(qdev_get_parent_bus(DEVICE(dev)));
    if ((bus->saved_address == dev->address) || (bus->broadcast)) {
        node = g_malloc(sizeof(struct I2CNode));
        node->elt = dev;
        QLIST_INSERT_HEAD(&bus->current_devs, node, next);
    }
    return 0;
}

const VMStateDescription vmstate_i2c_slave = {
    .name = "I2CSlave",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = i2c_slave_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(address, I2CSlave),
        VMSTATE_END_OF_LIST()
    }
};

static int i2c_slave_qdev_init(DeviceState *dev)
{
    I2CSlave *s = I2C_SLAVE(dev);
    I2CSlaveClass *sc = I2C_SLAVE_GET_CLASS(s);

    return sc->init(s);
}

DeviceState *i2c_create_slave(I2CBus *bus, const char *name, uint8_t addr)
{
    DeviceState *dev;

    dev = qdev_create(&bus->qbus, name);
    qdev_prop_set_uint8(dev, "address", addr);
    qdev_init_nofail(dev);
    return dev;
}

static void i2c_slave_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->init = i2c_slave_qdev_init;
    set_bit(DEVICE_CATEGORY_MISC, k->categories);
    k->bus_type = TYPE_I2C_BUS;
    k->props = i2c_props;
}

static const TypeInfo i2c_slave_type_info = {
    .name = TYPE_I2C_SLAVE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(I2CSlave),
    .abstract = true,
    .class_size = sizeof(I2CSlaveClass),
    .class_init = i2c_slave_class_init,
};

static void i2c_slave_register_types(void)
{
    type_register_static(&i2c_bus_info);
    type_register_static(&i2c_slave_type_info);
}

type_init(i2c_slave_register_types)
