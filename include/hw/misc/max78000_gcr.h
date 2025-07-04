/*
 * MAX78000 Global Control Register
 *
 * Copyright (c) 2025 Jackson Donaldson <jcksn@duck.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MAX78000_GCR_H
#define HW_MAX78000_GCR_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MAX78000_GCR "max78000-gcr"
OBJECT_DECLARE_SIMPLE_TYPE(Max78000GcrState, MAX78000_GCR)

#define SYSCTRL     0x0
#define RST0        0x4
#define CLKCTRL     0x8
#define PM          0xc
#define PCLKDIV     0x18
#define PCLKDIS0    0x24
#define MEMCTRL     0x28
#define MEMZ        0x2c
#define SYSST       0x40
#define RST1        0x44
#define PCKDIS1     0x48
#define EVENTEN     0x4c
#define REVISION    0x50
#define SYSIE       0x54
#define ECCERR      0x64
#define ECCED       0x68
#define ECCIE       0x6c
#define ECCADDR     0x70

/* RST0 */
#define SYSTEM_RESET (1 << 31)
#define PERIPHERAL_RESET (1 << 30)
#define SOFT_RESET (1 << 29)
#define UART2_RESET (1 << 28)

#define ADC_RESET (1 << 26)
#define CNN_RESET (1 << 25)
#define TRNG_RESET (1 << 24)

#define RTC_RESET (1 << 17)
#define I2C0_RESET (1 << 16)

#define SPI1_RESET (1 << 13)
#define UART1_RESET (1 << 12)
#define UART0_RESET (1 << 11)

#define TMR3_RESET (1 << 8)
#define TMR2_RESET (1 << 7)
#define TMR1_RESET (1 << 6)
#define TMR0_RESET (1 << 5)

#define GPIO1_RESET (1 << 3)
#define GPIO0_RESET (1 << 2)
#define WDT0_RESET (1 << 1)
#define DMA_RESET (1 << 0)

/* CLKCTRL */
#define SYSCLK_RDY (1 << 13)

/* MEMZ */
#define ram0 (1 << 0)
#define ram1 (1 << 1)
#define ram2 (1 << 2)
#define ram3 (1 << 3)

/* RST1 */
#define CPU1_RESET (1 << 31)

#define SIMO_RESET (1 << 25)
#define DVS_RESET (1 << 24)

#define I2C2_RESET (1 << 20)
#define I2S_RESET (1 << 19)

#define SMPHR_RESET (1 << 16)

#define SPI0_RESET (1 << 11)
#define AES_RESET (1 << 10)
#define CRC_RESET (1 << 9)

#define PT_RESET (1 << 1)
#define I2C1_RESET (1 << 0)


#define SYSRAM0_START 0x20000000
#define SYSRAM1_START 0x20008000
#define SYSRAM2_START 0x20010000
#define SYSRAM3_START 0x2001C000

struct Max78000GcrState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t sysctrl;
    uint32_t rst0;
    uint32_t clkctrl;
    uint32_t pm;
    uint32_t pclkdiv;
    uint32_t pclkdis0;
    uint32_t memctrl;
    uint32_t memz;
    uint32_t sysst;
    uint32_t rst1;
    uint32_t pckdis1;
    uint32_t eventen;
    uint32_t revision;
    uint32_t sysie;
    uint32_t eccerr;
    uint32_t ecced;
    uint32_t eccie;
    uint32_t eccaddr;

    MemoryRegion *sram;
    AddressSpace sram_as;

    DeviceState *uart0;
    DeviceState *uart1;
    DeviceState *uart2;
    DeviceState *trng;
    DeviceState *aes;

};

#endif
