/*
 * Microbit stub for Nordic Semiconductor nRF51 SoC Two-Wire Interface
 * http://infocenter.nordicsemi.com/pdf/nRF51_RM_v3.0.1.pdf
 *
 * Copyright 2019 Red Hat, Inc.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef MICROBIT_I2C_H
#define MICROBIT_I2C_H

#include "hw/sysbus.h"
#include "hw/arm/nrf51.h"
#include "qom/object.h"

#define NRF51_TWI_TASK_STARTRX 0x000
#define NRF51_TWI_TASK_STARTTX 0x008
#define NRF51_TWI_TASK_STOP 0x014
#define NRF51_TWI_EVENT_STOPPED 0x104
#define NRF51_TWI_EVENT_RXDREADY 0x108
#define NRF51_TWI_EVENT_TXDSENT 0x11c
#define NRF51_TWI_REG_ENABLE 0x500
#define NRF51_TWI_REG_RXD 0x518
#define NRF51_TWI_REG_TXD 0x51c
#define NRF51_TWI_REG_ADDRESS 0x588

#define TYPE_MICROBIT_I2C "microbit.i2c"
OBJECT_DECLARE_SIMPLE_TYPE(MicrobitI2CState, MICROBIT_I2C)

#define MICROBIT_I2C_NREGS (NRF51_PERIPHERAL_SIZE / sizeof(uint32_t))

struct MicrobitI2CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[MICROBIT_I2C_NREGS];
    uint32_t read_idx;
};

#endif /* MICROBIT_I2C_H */
