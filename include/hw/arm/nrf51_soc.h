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
#include "hw/gpio/nrf51_gpio.h"
#include "hw/nvram/nrf51_nvm.h"
#include "hw/timer/nrf51_timer.h"
#include "hw/clock.h"
#include "qom/object.h"

#define TYPE_NRF51_SOC "nrf51-soc"
OBJECT_DECLARE_SIMPLE_TYPE(NRF51State, NRF51_SOC)

#define NRF51_NUM_TIMERS 3

struct NRF51State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    ARMv7MState armv7m;

    NRF51UARTState uart;
    NRF51RNGState rng;
    NRF51NVMState nvm;
    NRF51GPIOState gpio;
    NRF51TimerState timer[NRF51_NUM_TIMERS];

    MemoryRegion iomem;
    MemoryRegion sram;
    MemoryRegion flash;
    MemoryRegion clock;
    MemoryRegion twi;

    uint32_t sram_size;
    uint32_t flash_size;

    MemoryRegion *board_memory;

    MemoryRegion container;

    Clock *sysclk;
};

#endif
