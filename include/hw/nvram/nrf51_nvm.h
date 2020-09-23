/*
 * Nordic Semiconductor nRF51 non-volatile memory
 *
 * It provides an interface to erase regions in flash memory.
 * Furthermore it provides the user and factory information registers.
 *
 * QEMU interface:
 * + sysbus MMIO regions 0: NVMC peripheral registers
 * + sysbus MMIO regions 1: FICR peripheral registers
 * + sysbus MMIO regions 2: UICR peripheral registers
 * + flash-size property: flash size in bytes.
 *
 * Accuracy of the peripheral model:
 * + Code regions (MPU configuration) are disregarded.
 *
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 */
#ifndef NRF51_NVM_H
#define NRF51_NVM_H

#include "hw/sysbus.h"
#include "qom/object.h"
#define TYPE_NRF51_NVM "nrf51_soc.nvm"
OBJECT_DECLARE_SIMPLE_TYPE(NRF51NVMState, NRF51_NVM)

#define NRF51_UICR_FIXTURE_SIZE 64

#define NRF51_NVMC_SIZE         0x1000

#define NRF51_NVMC_READY        0x400
#define NRF51_NVMC_READY_READY  0x01
#define NRF51_NVMC_CONFIG       0x504
#define NRF51_NVMC_CONFIG_MASK  0x03
#define NRF51_NVMC_CONFIG_WEN   0x01
#define NRF51_NVMC_CONFIG_EEN   0x02
#define NRF51_NVMC_ERASEPCR1    0x508
#define NRF51_NVMC_ERASEPCR0    0x510
#define NRF51_NVMC_ERASEALL     0x50C
#define NRF51_NVMC_ERASEUICR    0x514
#define NRF51_NVMC_ERASE        0x01

#define NRF51_UICR_SIZE         0x100

struct NRF51NVMState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    MemoryRegion ficr;
    MemoryRegion uicr;
    MemoryRegion flash;

    uint32_t uicr_content[NRF51_UICR_FIXTURE_SIZE];
    uint32_t flash_size;
    uint8_t *storage;

    uint32_t config;

};


#endif
