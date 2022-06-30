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
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "trace.h"

#define I2C_BROADCAST 0x00

static Property i2c_props[] = {
    DEFINE_PROP_UINT8("address", struct I2CSlave, address, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static const TypeInfo i2c_bus_info = {
    .name = TYPE_I2C_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(I2CBus),
};

static int i2c_bus_pre_save(void *opaque)
{
    I2CBus *bus = opaque;

    bus->saved_address = -1;
    if (!QLIST_EMPTY(&bus->current_devs)) {
        if (!bus->broadcast) {
            bus->saved_address = QLIST_FIRST(&bus->current_devs)->elt->address;
        } else {
            bus->saved_address = I2C_BROADCAST;
        }
    }

    return 0;
}

static const VMStateDescription vmstate_i2c_bus = {
    .name = "i2c_bus",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = i2c_bus_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(saved_address, I2CBus),
        VMSTATE_END_OF_LIST()
    }
};

/* Create a new I2C bus.  */
I2CBus *i2c_init_bus(DeviceState *parent, const char *name)
{
    I2CBus *bus;

    bus = I2C_BUS(qbus_new(TYPE_I2C_BUS, parent, name));
    QLIST_INIT(&bus->current_devs);
    QSIMPLEQ_INIT(&bus->pending_masters);
    vmstate_register(NULL, VMSTATE_INSTANCE_ID_ANY, &vmstate_i2c_bus, bus);
    return bus;
}

void i2c_slave_set_address(I2CSlave *dev, uint8_t address)
{
    dev->address = address;
}

/* Return nonzero if bus is busy.  */
int i2c_bus_busy(I2CBus *bus)
{
    return !QLIST_EMPTY(&bus->current_devs) || bus->bh;
}

bool i2c_scan_bus(I2CBus *bus, uint8_t address, bool broadcast,
                  I2CNodeList *current_devs)
{
    BusChild *kid;

    QTAILQ_FOREACH(kid, &bus->qbus.children, sibling) {
        DeviceState *qdev = kid->child;
        I2CSlave *candidate = I2C_SLAVE(qdev);
        I2CSlaveClass *sc = I2C_SLAVE_GET_CLASS(candidate);

        if (sc->match_and_add(candidate, address, broadcast, current_devs)) {
            if (!broadcast) {
                return true;
            }
        }
    }

    /*
     * If broadcast was true, and the list was full or empty, return true. If
     * broadcast was false, return false.
     */
    return broadcast;
}

/* TODO: Make this handle multiple masters.  */
/*
 * Start or continue an i2c transaction.  When this is called for the
 * first time or after an i2c_end_transfer(), if it returns an error
 * the bus transaction is terminated (or really never started).  If
 * this is called after another i2c_start_transfer() without an
 * intervening i2c_end_transfer(), and it returns an error, the
 * transaction will not be terminated.  The caller must do it.
 *
 * This corresponds with the way real hardware works.  The SMBus
 * protocol uses a start transfer to switch from write to read mode
 * without releasing the bus.  If that fails, the bus is still
 * in a transaction.
 *
 * @event must be I2C_START_RECV or I2C_START_SEND.
 */
static int i2c_do_start_transfer(I2CBus *bus, uint8_t address,
                                 enum i2c_event event)
{
    I2CSlaveClass *sc;
    I2CNode *node;
    bool bus_scanned = false;

    if (address == I2C_BROADCAST) {
        /*
         * This is a broadcast, the current_devs will be all the devices of the
         * bus.
         */
        bus->broadcast = true;
    }

    /*
     * If there are already devices in the list, that means we are in
     * the middle of a transaction and we shouldn't rescan the bus.
     *
     * This happens with any SMBus transaction, even on a pure I2C
     * device.  The interface does a transaction start without
     * terminating the previous transaction.
     */
    if (QLIST_EMPTY(&bus->current_devs)) {
        /* Disregard whether devices were found. */
        (void)i2c_scan_bus(bus, address, bus->broadcast, &bus->current_devs);
        bus_scanned = true;
    }

    if (QLIST_EMPTY(&bus->current_devs)) {
        return 1;
    }

    QLIST_FOREACH(node, &bus->current_devs, next) {
        I2CSlave *s = node->elt;
        int rv;

        sc = I2C_SLAVE_GET_CLASS(s);
        /* If the bus is already busy, assume this is a repeated
           start condition.  */

        if (sc->event) {
            trace_i2c_event(event == I2C_START_SEND ? "start" : "start_async",
                            s->address);
            rv = sc->event(s, event);
            if (rv && !bus->broadcast) {
                if (bus_scanned) {
                    /* First call, terminate the transfer. */
                    i2c_end_transfer(bus);
                }
                return rv;
            }
        }
    }
    return 0;
}

int i2c_start_transfer(I2CBus *bus, uint8_t address, bool is_recv)
{
    return i2c_do_start_transfer(bus, address, is_recv
                                               ? I2C_START_RECV
                                               : I2C_START_SEND);
}

void i2c_bus_master(I2CBus *bus, QEMUBH *bh)
{
    if (i2c_bus_busy(bus)) {
        I2CPendingMaster *node = g_new(struct I2CPendingMaster, 1);
        node->bh = bh;

        QSIMPLEQ_INSERT_TAIL(&bus->pending_masters, node, entry);

        return;
    }

    bus->bh = bh;
    qemu_bh_schedule(bus->bh);
}

void i2c_bus_release(I2CBus *bus)
{
    bus->bh = NULL;
}

int i2c_start_recv(I2CBus *bus, uint8_t address)
{
    return i2c_do_start_transfer(bus, address, I2C_START_RECV);
}

int i2c_start_send(I2CBus *bus, uint8_t address)
{
    return i2c_do_start_transfer(bus, address, I2C_START_SEND);
}

int i2c_start_send_async(I2CBus *bus, uint8_t address)
{
    return i2c_do_start_transfer(bus, address, I2C_START_SEND_ASYNC);
}

void i2c_end_transfer(I2CBus *bus)
{
    I2CSlaveClass *sc;
    I2CNode *node, *next;

    QLIST_FOREACH_SAFE(node, &bus->current_devs, next, next) {
        I2CSlave *s = node->elt;
        sc = I2C_SLAVE_GET_CLASS(s);
        if (sc->event) {
            trace_i2c_event("finish", s->address);
            sc->event(s, I2C_FINISH);
        }
        QLIST_REMOVE(node, next);
        g_free(node);
    }
    bus->broadcast = false;

    if (!QSIMPLEQ_EMPTY(&bus->pending_masters)) {
        I2CPendingMaster *node = QSIMPLEQ_FIRST(&bus->pending_masters);
        bus->bh = node->bh;

        QSIMPLEQ_REMOVE_HEAD(&bus->pending_masters, entry);
        g_free(node);

        qemu_bh_schedule(bus->bh);
    }
}

int i2c_send(I2CBus *bus, uint8_t data)
{
    I2CSlaveClass *sc;
    I2CSlave *s;
    I2CNode *node;
    int ret = 0;

    QLIST_FOREACH(node, &bus->current_devs, next) {
        s = node->elt;
        sc = I2C_SLAVE_GET_CLASS(s);
        if (sc->send) {
            trace_i2c_send(s->address, data);
            ret = ret || sc->send(s, data);
        } else {
            ret = -1;
        }
    }

    return ret ? -1 : 0;
}

int i2c_send_async(I2CBus *bus, uint8_t data)
{
    I2CNode *node = QLIST_FIRST(&bus->current_devs);
    I2CSlave *slave = node->elt;
    I2CSlaveClass *sc = I2C_SLAVE_GET_CLASS(slave);

    if (!sc->send_async) {
        return -1;
    }

    trace_i2c_send_async(slave->address, data);

    sc->send_async(slave, data);

    return 0;
}

uint8_t i2c_recv(I2CBus *bus)
{
    uint8_t data = 0xff;
    I2CSlaveClass *sc;
    I2CSlave *s;

    if (!QLIST_EMPTY(&bus->current_devs) && !bus->broadcast) {
        sc = I2C_SLAVE_GET_CLASS(QLIST_FIRST(&bus->current_devs)->elt);
        if (sc->recv) {
            s = QLIST_FIRST(&bus->current_devs)->elt;
            data = sc->recv(s);
            trace_i2c_recv(s->address, data);
        }
    }

    return data;
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
            trace_i2c_event("nack", node->elt->address);
            sc->event(node->elt, I2C_NACK);
        }
    }
}

void i2c_ack(I2CBus *bus)
{
    if (!bus->bh) {
        return;
    }

    trace_i2c_ack();

    qemu_bh_schedule(bus->bh);
}

static int i2c_slave_post_load(void *opaque, int version_id)
{
    I2CSlave *dev = opaque;
    I2CBus *bus;
    I2CNode *node;

    bus = I2C_BUS(qdev_get_parent_bus(DEVICE(dev)));
    if ((bus->saved_address == dev->address) ||
        (bus->saved_address == I2C_BROADCAST)) {
        node = g_new(struct I2CNode, 1);
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

I2CSlave *i2c_slave_new(const char *name, uint8_t addr)
{
    DeviceState *dev;

    dev = qdev_new(name);
    qdev_prop_set_uint8(dev, "address", addr);
    return I2C_SLAVE(dev);
}

bool i2c_slave_realize_and_unref(I2CSlave *dev, I2CBus *bus, Error **errp)
{
    return qdev_realize_and_unref(&dev->qdev, &bus->qbus, errp);
}

I2CSlave *i2c_slave_create_simple(I2CBus *bus, const char *name, uint8_t addr)
{
    I2CSlave *dev = i2c_slave_new(name, addr);

    i2c_slave_realize_and_unref(dev, bus, &error_abort);

    return dev;
}

static bool i2c_slave_match(I2CSlave *candidate, uint8_t address,
                            bool broadcast, I2CNodeList *current_devs)
{
    if ((candidate->address == address) || (broadcast)) {
        I2CNode *node = g_new(struct I2CNode, 1);
        node->elt = candidate;
        QLIST_INSERT_HEAD(current_devs, node, next);
        return true;
    }

    /* Not found and not broadcast. */
    return false;
}

static void i2c_slave_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(klass);
    set_bit(DEVICE_CATEGORY_MISC, k->categories);
    k->bus_type = TYPE_I2C_BUS;
    device_class_set_props(k, i2c_props);
    sc->match_and_add = i2c_slave_match;
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
