/*
 * QEMU I3C bus interface.
 *
 * Copyright 2025 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/i3c/i3c.h"
#include "hw/core/qdev-properties.h"

/*
 * In test mode (enabled by ENTTM CCC) we're supposed to send a random PID
 * during ENTDAA, so we'll just send "QEMU".
 */
#define TEST_MODE_PROVISIONED_ID 0x0000554d4551ULL

static const Property i3c_props[] = {
    DEFINE_PROP_UINT8("static-address", struct I3CTarget, static_address, 0),
    DEFINE_PROP_UINT8("dcr", struct I3CTarget, dcr, 0),
    DEFINE_PROP_UINT8("bcr", struct I3CTarget, bcr, 0),
    DEFINE_PROP_UINT64("pid", struct I3CTarget, pid, 0),
};

I3CBus *i3c_init_bus(DeviceState *parent, const char *name)
{
    return i3c_init_bus_type(TYPE_I3C_BUS, parent, name);
}

I3CBus *i3c_init_bus_type(const char *type, DeviceState *parent,
                          const char *name)
{
    I3CBus *bus;

    bus = I3C_BUS(qbus_new(type, parent, name));
    QLIST_INIT(&bus->current_devs);
    bus->broadcast = false;
    bus->in_entdaa = false;
    bus->in_ccc = false;

    /* I2C init. */
    g_autofree gchar *i2c_bus_name = g_strdup_printf("%s-legacy-i2c", name);
    bus->i2c_bus = i2c_init_bus(parent, i2c_bus_name);

    return bus;
}

bool i3c_bus_busy(I3CBus *bus)
{
    return !QLIST_EMPTY(&bus->current_devs);
}

static bool i3c_target_match(I3CTarget *candidate, uint8_t address,
                             bool is_recv, bool broadcast, bool in_entdaa)
{
    /* Once a target has a dynamic address, it only responds to that. */
    uint8_t targ_addr = candidate->address ? candidate->address :
                                             candidate->static_address;

    if (in_entdaa) {
        if (address != I3C_BROADCAST) {
            g_autofree char *path =
                object_get_canonical_path(OBJECT(candidate));
            qemu_log_mask(LOG_GUEST_ERROR, "%s: I3C Address 0x%.2x sent during "
                          "ENTDAA instead of a broadcast address\n",
                          path, address);
            return false;
        }

        /*
         * Targets should only ACK ENTDAA broadcasts if they have no dynamic
         * address.
         */
        return candidate->address == 0;
    }

    /* Return if our addresses match, or if it's a broadcast. */
    return targ_addr == address || broadcast;
}

bool i3c_target_match_and_add(I3CBus *bus, I3CTarget *target, uint8_t address,
                              enum I3CEvent event)
{
    I3CTargetClass *tc = I3C_TARGET_GET_CLASS(target);
    bool matched = tc->target_match(target, address, event == I3C_START_RECV,
                                    bus->broadcast, bus->in_entdaa);

    if (matched) {
        I3CNode *node = g_new(struct I3CNode, 1);
        node->target = target;
        QLIST_INSERT_HEAD(&bus->current_devs, node, next);
    }
    return matched;
}

bool i3c_scan_bus(I3CBus *bus, uint8_t address, enum I3CEvent event)
{
    BusChild *child;
    I3CNode *node, *next;

    /* Clear out any devices from a previous (re-)START. */
    QLIST_FOREACH_SAFE(node, &bus->current_devs, next, next) {
        QLIST_REMOVE(node, next);
        g_free(node);
    }

    QTAILQ_FOREACH(child, &bus->parent_obj.children, sibling) {
        DeviceState *qdev = child->child;
        I3CTarget *target = I3C_TARGET(qdev);

        if (i3c_target_match_and_add(bus, target, address, event)) {
            return true;
        }
    }

    /* No one on the bus could respond. */
    return false;
}

/* Class-level event handling, since we do some CCCs at the class level. */
static int i3c_target_event(I3CTarget *t, enum I3CEvent event)
{
    I3CTargetClass *tc = I3C_TARGET_GET_CLASS(t);
    trace_i3c_target_event(t->address, event);

    if (event == I3C_STOP) {
        t->curr_ccc = 0;
        t->ccc_byte_offset = 0;
        t->in_ccc = false;
    }
    return tc->event(t, event);
}

/*
 * Sends a START or repeated START and the address for an I3C transaction.
 *
 * This function returns 0 if a device on the bus was able to respond to the
 * address, and non-zero otherwise.
 * A non-zero return represents a NACK.
 */
static int i3c_do_start_transfer(I3CBus *bus, uint8_t address,
                                 enum I3CEvent event)
{
    I3CTargetClass *tc;
    I3CNode *node;

    if (address == I3C_BROADCAST) {
        bus->broadcast = true;
        /* If we're not in ENTDAA, a broadcast is the start of a new CCC. */
        if (!bus->in_entdaa) {
            bus->in_ccc = false;
        }
    } else {
        bus->broadcast = false;
    }

    /* No one responded to the address, NACK it. */
    if (!i3c_scan_bus(bus, address, event)) {
        return -1;
    }

    QLIST_FOREACH(node, &bus->current_devs, next) {
        I3CTarget *t = node->target;

        tc = I3C_TARGET_GET_CLASS(t);
        if (tc->event) {
            int rv = i3c_target_event(t, event);
            if (rv && !bus->broadcast) {
                return rv;
            }
        }
    }

    return 0;
}

int i3c_start_transfer(I3CBus *bus, uint8_t address, bool is_recv)
{
    trace_i3c_start_transfer(address, is_recv);
    return i3c_do_start_transfer(bus, address, is_recv
                                               ? I3C_START_RECV
                                               : I3C_START_SEND);
}

int i3c_start_recv(I3CBus *bus, uint8_t address)
{
    trace_i3c_start_transfer(address, true);
    return i3c_do_start_transfer(bus, address, I3C_START_RECV);
}

int i3c_start_send(I3CBus *bus, uint8_t address)
{
    trace_i3c_start_transfer(address, false);
    return i3c_do_start_transfer(bus, address, I3C_START_SEND);
}

void i3c_end_transfer(I3CBus *bus)
{
    I3CTargetClass *tc;
    I3CNode *node, *next;

    trace_i3c_end_transfer();

    /*
     * If we're in ENTDAA, we need to notify all devices when ENTDAA is done.
     * This is because everyone initially participates due to the broadcast,
     * but gradually drops out as they get assigned addresses.
     * Since the current_devs list only stores who's currently participating,
     * and not everyone who previously participated, we send the STOP to all
     * children.
     */
    if (bus->in_entdaa) {
        BusChild *child;

        QTAILQ_FOREACH(child, &bus->parent_obj.children, sibling) {
            DeviceState *qdev = child->child;
            I3CTarget *t = I3C_TARGET(qdev);
            tc = I3C_TARGET_GET_CLASS(t);
            if (tc->event) {
                i3c_target_event(t, I3C_STOP);
            }
        }
    } else {
        QLIST_FOREACH_SAFE(node, &bus->current_devs, next, next) {
            I3CTarget *t = node->target;
            tc = I3C_TARGET_GET_CLASS(t);
            if (tc->event) {
                i3c_target_event(t, I3C_STOP);
            }
            QLIST_REMOVE(node, next);
            g_free(node);
        }
    }
    bus->broadcast = false;
    bus->in_entdaa = false;
    bus->in_ccc = false;
}

/*
 * Any CCCs that are universal across all I3C devices should be handled here.
 * Once they're handled, we pass the CCC up to the I3C target to do anything
 * else it may want with the bytes.
 */
static int i3c_target_handle_ccc_write(I3CTarget *t, const uint8_t *data,
                                       uint32_t num_to_send, uint32_t *num_sent)
{
    I3CTargetClass *tc = I3C_TARGET_GET_CLASS(t);
    *num_sent = 0;

    /* Is this the start of a new CCC? */
    if (!t->in_ccc) {
        t->curr_ccc = *data;
        t->in_ccc = true;
        *num_sent = 1;
        trace_i3c_target_handle_ccc(t->address, t->curr_ccc);
    }

    switch (t->curr_ccc) {
    case I3C_CCC_ENTDAA:
        /*
         * This is the last byte of ENTDAA, the controller is assigning us an
         * address.
         */
        if (t->ccc_byte_offset == 8) {
            t->address = *data;
            t->in_ccc = false;
            t->curr_ccc = 0;
            t->ccc_byte_offset = 0;
            *num_sent = 1;
        }
        break;
    case I3C_CCCD_SETDASA:
        t->address = t->static_address;
        break;
    case I3C_CCC_SETAASA:
        t->address = t->static_address;
        break;
    case I3C_CCC_RSTDAA:
        t->address = 0;
        break;
    case I3C_CCCD_SETNEWDA:
        /* If this isn't the CCC byte, it's our new address. */
        if (*num_sent == 0) {
            t->address = *data;
            *num_sent = 1;
        }
        break;
    case I3C_CCC_ENTTM:
        /*
         * If there are still more to look at, the next byte is the test mode
         * byte.
         */
        if (*num_sent != num_to_send) {
            /* Enter test mode if the byte is non-zero. Otherwise exit. */
            t->in_test_mode = !!data[*num_sent];
            ++*num_sent;
        }
        break;
    /* Ignore other CCCs it's better to handle on a device-by-device basis. */
    default:
        break;
    }
    return tc->handle_ccc_write(t, data, num_to_send, num_sent);
}

int i3c_send_byte(I3CBus *bus, uint8_t data)
{
    /*
     * Ignored, the caller can determine how many were sent based on if this was
     * ACKed/NACKed.
     */
    uint32_t num_sent;
    return i3c_send(bus, &data, 1, &num_sent);
}

int i3c_send(I3CBus *bus, const uint8_t *data, uint32_t num_to_send,
             uint32_t *num_sent)
{
    I3CTargetClass *tc;
    I3CTarget *t;
    I3CNode *node;
    int ret = 0;

    /* If this message is a broadcast and no CCC has been found, grab it. */
    if (bus->broadcast && !bus->in_ccc) {
        bus->ccc = *data;
        bus->in_ccc = true;
        /*
         * We need to keep track if we're currently in ENTDAA.
         * On any other CCC, the CCC is over on a RESTART or STOP, but ENTDAA
         * is only over on a STOP.
         */
        if (bus->ccc == I3C_CCC_ENTDAA) {
            bus->in_entdaa = true;
        }
    }

    QLIST_FOREACH(node, &bus->current_devs, next) {
        t = node->target;
        tc = I3C_TARGET_GET_CLASS(t);
        if (bus->in_ccc) {
            if (!tc->handle_ccc_write) {
                ret = -1;
                continue;
            }
            ret = i3c_target_handle_ccc_write(t, data, num_to_send, num_sent);
            /* Targets should only NACK on a direct CCC. */
            if (ret && !CCC_IS_DIRECT(bus->ccc)) {
                ret = 0;
            }
        } else {
            if (tc->send) {
                ret = ret || tc->send(t, data, num_to_send, num_sent);
            } else {
                ret = -1;
            }
        }
    }

    trace_i3c_send(*num_sent, num_to_send, ret == 0);

    return ret ? -1 : 0;
}

static int i3c_target_handle_ccc_read(I3CTarget *t, uint8_t *data,
                                      uint32_t num_to_read, uint32_t *num_read)
{
    I3CTargetClass *tc = I3C_TARGET_GET_CLASS(t);
    uint8_t read_count = 0;
    uint64_t pid;

    switch (t->curr_ccc) {
    case I3C_CCC_ENTDAA:
        if (t->in_test_mode) {
            pid = TEST_MODE_PROVISIONED_ID;
        } else {
            pid = t->pid;
        }
        /* Return the 6-byte PID, followed by BCR then DCR. */
        while (t->ccc_byte_offset < 6) {
            if (read_count >= num_to_read) {
                break;
            }
            data[read_count] = (pid >> (t->ccc_byte_offset * 8)) & 0xff;
            t->ccc_byte_offset++;
            read_count++;
        }
        if (read_count < num_to_read) {
            data[read_count] = t->bcr;
            t->ccc_byte_offset++;
            read_count++;
        }
        if (read_count < num_to_read) {
            data[read_count] = t->dcr;
            t->ccc_byte_offset++;
            read_count++;
        }
        *num_read = read_count;
        break;
    case I3C_CCCD_GETPID:
        while (t->ccc_byte_offset < 6) {
            if (read_count >= num_to_read) {
                break;
            }
            data[read_count] = (t->pid >> (t->ccc_byte_offset * 8)) & 0xff;
            t->ccc_byte_offset++;
            read_count++;
        }
        *num_read = read_count;
        break;
    case I3C_CCCD_GETBCR:
        *data = t->bcr;
        *num_read = 1;
        break;
    case I3C_CCCD_GETDCR:
        *data = t->dcr;
        *num_read = 1;
        break;
    default:
        /* Unhandled on the I3CTarget class level. */
        break;
    }

    return tc->handle_ccc_read(t, data, num_to_read, num_read);
}

int i3c_recv_byte(I3CBus *bus, uint8_t *data)
{
     /*
      * Ignored, the caller can determine how many bytes were read based on if
      * this is ACKed/NACKed.
      */
    uint32_t num_read;
    return i3c_recv(bus, data, 1, &num_read);
}

int i3c_recv(I3CBus *bus, uint8_t *data, uint32_t num_to_read,
             uint32_t *num_read)
{
    int ret = 0;
    I3CTargetClass *tc;
    I3CTarget *t;

    *data = 0xff;
    if (!QLIST_EMPTY(&bus->current_devs)) {
        tc = I3C_TARGET_GET_CLASS(QLIST_FIRST(&bus->current_devs)->target);
        t = QLIST_FIRST(&bus->current_devs)->target;
        if (bus->in_ccc) {
            if (!tc->handle_ccc_read) {
                return -1;
            }
            ret = i3c_target_handle_ccc_read(t, data, num_to_read, num_read);
        } else {
            if (tc->recv) {
                /*
                 * Targets cannot NACK on a direct transfer, so the data
                 * is returned directly.
                 */
                *num_read = tc->recv(t, data, num_to_read);
            }
        }
    }

    trace_i3c_recv(*num_read, num_to_read, ret == 0);

    return ret;
}

void i3c_nack(I3CBus *bus)
{
    I3CTargetClass *tc;
    I3CNode *node;

    if (QLIST_EMPTY(&bus->current_devs)) {
        return;
    }

    QLIST_FOREACH(node, &bus->current_devs, next) {
        tc = I3C_TARGET_GET_CLASS(node->target);
        if (tc->event) {
            i3c_target_event(node->target, I3C_NACK);
        }
    }
}

int i3c_target_send_ibi(I3CTarget *t, uint8_t addr, bool is_recv)
{
    I3CBus *bus = I3C_BUS(t->parent_obj.parent_bus);
    I3CBusClass *bc = I3C_BUS_GET_CLASS(bus);
    trace_i3c_target_send_ibi(addr, is_recv);
    return bc->ibi_handle(bus, addr, is_recv);
}

int i3c_target_send_ibi_bytes(I3CTarget *t, uint8_t data)
{
    I3CBus *bus = I3C_BUS(t->parent_obj.parent_bus);
    I3CBusClass *bc = I3C_BUS_GET_CLASS(bus);
    trace_i3c_target_send_ibi_bytes(data);
    return bc->ibi_recv(bus, data);
}

int i3c_target_ibi_finish(I3CTarget *t, uint8_t data)
{
    I3CBus *bus = I3C_BUS(t->parent_obj.parent_bus);
    I3CBusClass *bc = I3C_BUS_GET_CLASS(bus);
    trace_i3c_target_ibi_finish();
    return bc->ibi_finish(bus);
}

static bool i3c_addr_is_rsvd(uint8_t addr)
{
    const bool is_rsvd[255] = {
        [0x00] = true,
        [0x01] = true,
        [0x02] = true,
        [0x3e] = true,
        [0x5e] = true,
        [0x6e] = true,
        [0x76] = true,
        [0x7a] = true,
        [0x7c] = true,
        [0x7e] = true,
        [0x7f] = true,
    };

    return is_rsvd[addr];
}

I3CTarget *i3c_target_new(const char *name, uint8_t addr, uint8_t dcr,
                          uint8_t bcr, uint64_t pid)
{
    DeviceState *dev;

    dev = qdev_new(name);
    qdev_prop_set_uint8(dev, "static-address", addr);
    qdev_prop_set_uint8(dev, "dcr", dcr);
    qdev_prop_set_uint8(dev, "bcr", bcr);
    qdev_prop_set_uint64(dev, "pid", pid);

    if (i3c_addr_is_rsvd(addr)) {
        g_autofree char *path = object_get_canonical_path(OBJECT(dev));
        qemu_log_mask(LOG_GUEST_ERROR, "%s: I3C target created with reserved "
                      "address 0x%.2x\n", path, addr);
    }
    return I3C_TARGET(dev);
}

bool i3c_target_realize_and_unref(I3CTarget *dev, I3CBus *bus, Error **errp)
{
    return qdev_realize_and_unref(&dev->parent_obj, &bus->parent_obj, errp);
}

I3CTarget *i3c_target_create_simple(I3CBus *bus, const char *name, uint8_t addr,
                                    uint8_t dcr, uint8_t bcr, uint64_t pid)
{
    I3CTarget *dev = i3c_target_new(name, addr, dcr, bcr, pid);
    dev->address = 0;
    i3c_target_realize_and_unref(dev, bus, &error_abort);

    return dev;
}

/* Legacy I2C functions. */
void legacy_i2c_nack(I3CBus *bus)
{
    trace_legacy_i2c_nack();
    i2c_nack(bus->i2c_bus);
}

uint8_t legacy_i2c_recv(I3CBus *bus)
{
    uint8_t byte = i2c_recv(bus->i2c_bus);
    trace_legacy_i2c_recv(byte);
    return byte;
}

int legacy_i2c_send(I3CBus *bus, uint8_t data)
{
    trace_legacy_i2c_send(data);
    return i2c_send(bus->i2c_bus, data);
}

int legacy_i2c_start_transfer(I3CBus *bus, uint8_t address, bool is_recv)
{
    trace_legacy_i2c_start_transfer(address, is_recv);
    return i2c_start_transfer(bus->i2c_bus, address, is_recv);
}

int legacy_i2c_start_recv(I3CBus *bus, uint8_t address)
{
    trace_legacy_i2c_start_transfer(address, true);
    return i2c_start_transfer(bus->i2c_bus, address, /*is_recv=*/true);
}

int legacy_i2c_start_send(I3CBus *bus, uint8_t address)
{
    trace_legacy_i2c_start_transfer(address, false);
    return i2c_start_transfer(bus->i2c_bus, address, /*is_recv=*/false);
}

void legacy_i2c_end_transfer(I3CBus *bus)
{
    trace_legacy_i2c_end_transfer();
    i2c_end_transfer(bus->i2c_bus);
}

I2CSlave *legacy_i2c_device_create_simple(I3CBus *bus, const char *name,
                                          uint8_t addr)
{
    I2CSlave *dev = i2c_slave_new(name, addr);

    i2c_slave_realize_and_unref(dev, bus->i2c_bus, &error_abort);
    return dev;
}

static void i3c_target_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    I3CTargetClass *sc = I3C_TARGET_CLASS(klass);
    set_bit(DEVICE_CATEGORY_MISC, k->categories);
    k->bus_type = TYPE_I3C_BUS;
    device_class_set_props(k, i3c_props);
    sc->target_match = i3c_target_match;
}

static const TypeInfo i3c_types[] = {
    {
        .name = TYPE_I3C_BUS,
        .parent = TYPE_BUS,
        .instance_size = sizeof(I3CBus),
        .class_size = sizeof(I3CBusClass),
    },
    {
        .name = TYPE_I3C_TARGET,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(I3CTarget),
        .abstract = true,
        .class_size = sizeof(I3CTargetClass),
        .class_init = i3c_target_class_init,
    },
};

DEFINE_TYPES(i3c_types)
