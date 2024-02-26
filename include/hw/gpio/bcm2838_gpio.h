/*
 * Raspberry Pi (BCM2838) GPIO Controller
 * This implementation is based on bcm2835_gpio (hw/gpio/bcm2835_gpio.c)
 *
 * Copyright (c) 2022 Auriga LLC
 *
 * Authors:
 *  Lotosh, Aleksey <aleksey.lotosh@auriga.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2838_GPIO_H
#define BCM2838_GPIO_H

#include "hw/sd/sd.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_BCM2838_GPIO "bcm2838-gpio"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2838GpioState, BCM2838_GPIO)

#define BCM2838_GPIO_REGS_SIZE 0x1000
#define BCM2838_GPIO_NUM       58
#define GPIO_PUP_PDN_CNTRL_NUM 4

struct BCM2838GpioState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    /* SDBus selector */
    SDBus sdbus;
    SDBus *sdbus_sdhci;
    SDBus *sdbus_sdhost;

    uint8_t fsel[BCM2838_GPIO_NUM];
    uint32_t lev0, lev1;
    uint8_t sd_fsel;
    qemu_irq out[BCM2838_GPIO_NUM];
    uint32_t pup_cntrl_reg[GPIO_PUP_PDN_CNTRL_NUM];
};

#endif
