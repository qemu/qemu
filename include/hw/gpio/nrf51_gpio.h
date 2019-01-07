/*
 * nRF51 System-on-Chip general purpose input/output register definition
 *
 * QEMU interface:
 * + sysbus MMIO regions 0: GPIO registers
 * + Unnamed GPIO inputs 0-31: Set tri-state input level for GPIO pin.
 *   Level -1: Externally Disconnected/Floating; Pull-up/down will be regarded
 *   Level 0: Input externally driven LOW
 *   Level 1: Input externally driven HIGH
 * + Unnamed GPIO outputs 0-31:
 *   Level -1: Disconnected/Floating
 *   Level 0: Driven LOW
 *   Level 1: Driven HIGH
 *
 * Accuracy of the peripheral model:
 * + The nRF51 GPIO output driver supports two modes, standard and high-current
 *   mode. These different drive modes are not modeled and handled the same.
 * + Pin SENSEing is not modeled/implemented.
 *
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 */
#ifndef NRF51_GPIO_H
#define NRF51_GPIO_H

#include "hw/sysbus.h"
#define TYPE_NRF51_GPIO "nrf51_soc.gpio"
#define NRF51_GPIO(obj) OBJECT_CHECK(NRF51GPIOState, (obj), TYPE_NRF51_GPIO)

#define NRF51_GPIO_PINS 32

#define NRF51_GPIO_SIZE 0x1000

#define NRF51_GPIO_REG_OUT          0x504
#define NRF51_GPIO_REG_OUTSET       0x508
#define NRF51_GPIO_REG_OUTCLR       0x50C
#define NRF51_GPIO_REG_IN           0x510
#define NRF51_GPIO_REG_DIR          0x514
#define NRF51_GPIO_REG_DIRSET       0x518
#define NRF51_GPIO_REG_DIRCLR       0x51C
#define NRF51_GPIO_REG_CNF_START    0x700
#define NRF51_GPIO_REG_CNF_END      0x77F

#define NRF51_GPIO_PULLDOWN 1
#define NRF51_GPIO_PULLUP 3

typedef struct NRF51GPIOState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t out;
    uint32_t in;
    uint32_t in_mask;
    uint32_t dir;
    uint32_t cnf[NRF51_GPIO_PINS];

    uint32_t old_out;
    uint32_t old_out_connected;

    qemu_irq output[NRF51_GPIO_PINS];
} NRF51GPIOState;


#endif
