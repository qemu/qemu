/*
 * auxbus.c
 *
 *  Copyright 2015 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option)any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 * This is an implementation of the AUX bus for VESA Display Port v1.1a.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/auxbus.h"
#include "hw/i2c/i2c.h"
#include "monitor/monitor.h"
#include "qapi/error.h"

#ifndef DEBUG_AUX
#define DEBUG_AUX 0
#endif

#define DPRINTF(fmt, ...) do {                                                 \
    if (DEBUG_AUX) {                                                           \
        qemu_log("aux: " fmt , ## __VA_ARGS__);                                \
    }                                                                          \
} while (0)


static void aux_slave_dev_print(Monitor *mon, DeviceState *dev, int indent);
static inline I2CBus *aux_bridge_get_i2c_bus(AUXTOI2CState *bridge);

/* aux-bus implementation (internal not public) */
static void aux_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    /* AUXSlave has an MMIO so we need to change the way we print information
     * in monitor.
     */
    k->print_dev = aux_slave_dev_print;
}

AUXBus *aux_bus_init(DeviceState *parent, const char *name)
{
    AUXBus *bus;
    Object *auxtoi2c;

    bus = AUX_BUS(qbus_create(TYPE_AUX_BUS, parent, name));
    auxtoi2c = object_new_with_props(TYPE_AUXTOI2C, OBJECT(bus), "i2c",
                                     &error_abort, NULL);

    bus->bridge = AUXTOI2C(auxtoi2c);

    /* Memory related. */
    bus->aux_io = g_malloc(sizeof(*bus->aux_io));
    memory_region_init(bus->aux_io, OBJECT(bus), "aux-io", 1 * MiB);
    address_space_init(&bus->aux_addr_space, bus->aux_io, "aux-io");
    return bus;
}

void aux_bus_realize(AUXBus *bus)
{
    qdev_realize(DEVICE(bus->bridge), BUS(bus), &error_fatal);
}

void aux_map_slave(AUXSlave *aux_dev, hwaddr addr)
{
    DeviceState *dev = DEVICE(aux_dev);
    AUXBus *bus = AUX_BUS(qdev_get_parent_bus(dev));
    memory_region_add_subregion(bus->aux_io, addr, aux_dev->mmio);
}

static bool aux_bus_is_bridge(AUXBus *bus, DeviceState *dev)
{
    return (dev == DEVICE(bus->bridge));
}

I2CBus *aux_get_i2c_bus(AUXBus *bus)
{
    return aux_bridge_get_i2c_bus(bus->bridge);
}

AUXReply aux_request(AUXBus *bus, AUXCommand cmd, uint32_t address,
                      uint8_t len, uint8_t *data)
{
    AUXReply ret = AUX_NACK;
    I2CBus *i2c_bus = aux_get_i2c_bus(bus);
    size_t i;
    bool is_write = false;

    DPRINTF("request at address 0x%" PRIX32 ", command %u, len %u\n", address,
            cmd, len);

    switch (cmd) {
    /*
     * Forward the request on the AUX bus..
     */
    case WRITE_AUX:
    case READ_AUX:
        is_write = cmd == READ_AUX ? false : true;
        for (i = 0; i < len; i++) {
            if (!address_space_rw(&bus->aux_addr_space, address++,
                                  MEMTXATTRS_UNSPECIFIED, data++, 1,
                                  is_write)) {
                ret = AUX_I2C_ACK;
            } else {
                ret = AUX_NACK;
                break;
            }
        }
        break;
    /*
     * Classic I2C transactions..
     */
    case READ_I2C:
    case WRITE_I2C:
        is_write = cmd == READ_I2C ? false : true;
        if (i2c_bus_busy(i2c_bus)) {
            i2c_end_transfer(i2c_bus);
        }

        if (i2c_start_transfer(i2c_bus, address, is_write)) {
            ret = AUX_I2C_NACK;
            break;
        }

        ret = AUX_I2C_ACK;
        while (len > 0) {
            if (i2c_send_recv(i2c_bus, data++, is_write) < 0) {
                ret = AUX_I2C_NACK;
                break;
            }
            len--;
        }
        i2c_end_transfer(i2c_bus);
        break;
    /*
     * I2C MOT transactions.
     *
     * Here we send a start when:
     *  - We didn't start transaction yet.
     *  - We had a READ and we do a WRITE.
     *  - We changed the address.
     */
    case WRITE_I2C_MOT:
    case READ_I2C_MOT:
        is_write = cmd == READ_I2C_MOT ? false : true;
        ret = AUX_I2C_NACK;
        if (!i2c_bus_busy(i2c_bus)) {
            /*
             * No transactions started..
             */
            if (i2c_start_transfer(i2c_bus, address, is_write)) {
                break;
            }
        } else if ((address != bus->last_i2c_address) ||
                   (bus->last_transaction != cmd)) {
            /*
             * Transaction started but we need to restart..
             */
            i2c_end_transfer(i2c_bus);
            if (i2c_start_transfer(i2c_bus, address, is_write)) {
                break;
            }
        }

        bus->last_transaction = cmd;
        bus->last_i2c_address = address;
        while (len > 0) {
            if (i2c_send_recv(i2c_bus, data++, is_write) < 0) {
                i2c_end_transfer(i2c_bus);
                break;
            }
            len--;
        }
        if (len == 0) {
            ret = AUX_I2C_ACK;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "AUX cmd=%u not implemented\n", cmd);
        return AUX_NACK;
    }

    DPRINTF("reply: %u\n", ret);
    return ret;
}

static const TypeInfo aux_bus_info = {
    .name = TYPE_AUX_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(AUXBus),
    .class_init = aux_bus_class_init
};

/* aux-i2c implementation (internal not public) */
struct AUXTOI2CState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    I2CBus *i2c_bus;
};

static void aux_bridge_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    /* This device is private and is created only once for each
     * aux-bus in aux_bus_init(..). So don't allow the user to add one.
     */
    dc->user_creatable = false;
}

static void aux_bridge_init(Object *obj)
{
    AUXTOI2CState *s = AUXTOI2C(obj);

    s->i2c_bus = i2c_init_bus(DEVICE(obj), "aux-i2c");
}

static inline I2CBus *aux_bridge_get_i2c_bus(AUXTOI2CState *bridge)
{
    return bridge->i2c_bus;
}

static const TypeInfo aux_to_i2c_type_info = {
    .name = TYPE_AUXTOI2C,
    .parent = TYPE_AUX_SLAVE,
    .class_init = aux_bridge_class_init,
    .instance_size = sizeof(AUXTOI2CState),
    .instance_init = aux_bridge_init
};

/* aux-slave implementation */
static void aux_slave_dev_print(Monitor *mon, DeviceState *dev, int indent)
{
    AUXBus *bus = AUX_BUS(qdev_get_parent_bus(dev));
    AUXSlave *s;

    /* Don't print anything if the device is I2C "bridge". */
    if (aux_bus_is_bridge(bus, dev)) {
        return;
    }

    s = AUX_SLAVE(dev);

    monitor_printf(mon, "%*smemory " TARGET_FMT_plx "/" TARGET_FMT_plx "\n",
                   indent, "",
                   object_property_get_uint(OBJECT(s->mmio), "addr", NULL),
                   memory_region_size(s->mmio));
}

void aux_init_mmio(AUXSlave *aux_slave, MemoryRegion *mmio)
{
    assert(!aux_slave->mmio);
    aux_slave->mmio = mmio;
}

static void aux_slave_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, k->categories);
    k->bus_type = TYPE_AUX_BUS;
}

static const TypeInfo aux_slave_type_info = {
    .name = TYPE_AUX_SLAVE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(AUXSlave),
    .abstract = true,
    .class_init = aux_slave_class_init,
};

static void aux_register_types(void)
{
    type_register_static(&aux_bus_info);
    type_register_static(&aux_slave_type_info);
    type_register_static(&aux_to_i2c_type_info);
}

type_init(aux_register_types)
