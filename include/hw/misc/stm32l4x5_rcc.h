/*
 * STM32L4X5 RCC (Reset and clock control)
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * The reference used is the STMicroElectronics RM0351 Reference manual
 * for STM32L4x5 and STM32L4x6 advanced Arm ® -based 32-bit MCUs.
 *
 * Inspired by the BCM2835 CPRMAN clock manager by Luc Michel.
 */

#ifndef HW_STM32L4X5_RCC_H
#define HW_STM32L4X5_RCC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_STM32L4X5_RCC "stm32l4x5-rcc"
OBJECT_DECLARE_SIMPLE_TYPE(Stm32l4x5RccState, STM32L4X5_RCC)

/* In the Stm32l4x5 clock tree, mux have at most 7 sources */
#define RCC_NUM_CLOCK_MUX_SRC 7
struct Stm32l4x5RccState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t cr;
    uint32_t icscr;
    uint32_t cfgr;
    uint32_t pllcfgr;
    uint32_t pllsai1cfgr;
    uint32_t pllsai2cfgr;
    uint32_t cier;
    uint32_t cifr;
    uint32_t ahb1rstr;
    uint32_t ahb2rstr;
    uint32_t ahb3rstr;
    uint32_t apb1rstr1;
    uint32_t apb1rstr2;
    uint32_t apb2rstr;
    uint32_t ahb1enr;
    uint32_t ahb2enr;
    uint32_t ahb3enr;
    uint32_t apb1enr1;
    uint32_t apb1enr2;
    uint32_t apb2enr;
    uint32_t ahb1smenr;
    uint32_t ahb2smenr;
    uint32_t ahb3smenr;
    uint32_t apb1smenr1;
    uint32_t apb1smenr2;
    uint32_t apb2smenr;
    uint32_t ccipr;
    uint32_t bdcr;
    uint32_t csr;

    /* Clock sources */
    Clock *gnd;
    Clock *hsi16_rc;
    Clock *msi_rc;
    Clock *hse;
    Clock *lsi_rc;
    Clock *lse_crystal;
    Clock *sai1_extclk;
    Clock *sai2_extclk;

    qemu_irq irq;
    uint64_t hse_frequency;
    uint64_t sai1_extclk_frequency;
    uint64_t sai2_extclk_frequency;
};

#endif /* HW_STM32L4X5_RCC_H */
