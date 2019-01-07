/*
 * Nordic Semiconductor nRF51  SoC
 *
 * Copyright 2018 Joel Stanley <joel@jms.id.au>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef NRF51_SOC_H
#define NRF51_SOC_H

#include "hw/sysbus.h"
#include "hw/arm/armv7m.h"
#include "hw/char/nrf51_uart.h"
#include "hw/misc/nrf51_rng.h"

#define TYPE_NRF51_SOC "nrf51-soc"
#define NRF51_SOC(obj) \
    OBJECT_CHECK(NRF51State, (obj), TYPE_NRF51_SOC)

typedef struct NRF51State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    ARMv7MState cpu;

    NRF51UARTState uart;
    NRF51RNGState rng;

    MemoryRegion iomem;
    MemoryRegion sram;
    MemoryRegion flash;

    uint32_t sram_size;
    uint32_t flash_size;

    MemoryRegion *board_memory;

    MemoryRegion container;

} NRF51State;

#endif

