/*
 * auxbus.h
 *
 *  Copyright (C)2014 : GreenSocs Ltd
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

#ifndef HW_MISC_AUXBUS_H
#define HW_MISC_AUXBUS_H

#include "exec/memory.h"
#include "hw/qdev-core.h"
#include "qom/object.h"

typedef struct AUXSlave AUXSlave;
typedef enum AUXCommand AUXCommand;
typedef enum AUXReply AUXReply;

#define TYPE_AUXTOI2C "aux-to-i2c-bridge"
OBJECT_DECLARE_SIMPLE_TYPE(AUXTOI2CState, AUXTOI2C)

enum AUXCommand {
    WRITE_I2C = 0,
    READ_I2C = 1,
    WRITE_I2C_STATUS = 2,
    WRITE_I2C_MOT = 4,
    READ_I2C_MOT = 5,
    WRITE_AUX = 8,
    READ_AUX = 9
};

enum AUXReply {
    AUX_I2C_ACK = 0,
    AUX_NACK = 1,
    AUX_DEFER = 2,
    AUX_I2C_NACK = 4,
    AUX_I2C_DEFER = 8
};

#define TYPE_AUX_BUS "aux-bus"
OBJECT_DECLARE_SIMPLE_TYPE(AUXBus, AUX_BUS)

struct AUXBus {
    /* < private > */
    BusState qbus;

    /* < public > */
    AUXSlave *current_dev;
    AUXSlave *dev;
    uint32_t last_i2c_address;
    AUXCommand last_transaction;

    AUXTOI2CState *bridge;

    MemoryRegion *aux_io;
    AddressSpace aux_addr_space;
};

#define TYPE_AUX_SLAVE "aux-slave"
OBJECT_DECLARE_SIMPLE_TYPE(AUXSlave, AUX_SLAVE)

struct AUXSlave {
    /* < private > */
    DeviceState parent_obj;

    /* < public > */
    MemoryRegion *mmio;
};

/**
 * aux_bus_init: Initialize an AUX bus.
 *
 * Returns the new AUX bus created.
 *
 * @parent The device where this bus is located.
 * @name The name of the bus.
 */
AUXBus *aux_bus_init(DeviceState *parent, const char *name);

/**
 * aux_bus_realize: Realize an AUX bus.
 *
 * @bus: The AUX bus.
 */
void aux_bus_realize(AUXBus *bus);

/*
 * aux_request: Make a request on the bus.
 *
 * Returns the reply of the request.
 *
 * @bus The bus where the request happen.
 * @cmd The command requested.
 * @address The 20bits address of the slave.
 * @len The length of the read or write.
 * @data The data array which will be filled or read during transfer.
 */
AUXReply aux_request(AUXBus *bus, AUXCommand cmd, uint32_t address,
                              uint8_t len, uint8_t *data);

/*
 * aux_get_i2c_bus: Get the i2c bus for I2C over AUX command.
 *
 * Returns the i2c bus associated to this AUX bus.
 *
 * @bus The AUX bus.
 */
I2CBus *aux_get_i2c_bus(AUXBus *bus);

/*
 * aux_init_mmio: Init an mmio for an AUX slave.
 *
 * @aux_slave The AUX slave.
 * @mmio The mmio to be registered.
 */
void aux_init_mmio(AUXSlave *aux_slave, MemoryRegion *mmio);

/* aux_map_slave: Map the mmio for an AUX slave on the bus.
 *
 * @dev The AUX slave.
 * @addr The address for the slave's mmio.
 */
void aux_map_slave(AUXSlave *dev, hwaddr addr);

#endif /* HW_MISC_AUXBUS_H */
