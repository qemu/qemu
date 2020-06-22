/*
 * SiFive System-on-Chip general purpose input/output register definition
 *
 * Copyright 2019 AdaCore
 *
 * Base on nrf51_gpio.c:
 *
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef SIFIVE_GPIO_H
#define SIFIVE_GPIO_H

#include "hw/sysbus.h"

#define TYPE_SIFIVE_GPIO "sifive_soc.gpio"
#define SIFIVE_GPIO(obj) OBJECT_CHECK(SIFIVEGPIOState, (obj), TYPE_SIFIVE_GPIO)

#define SIFIVE_GPIO_PINS 32

#define SIFIVE_GPIO_SIZE 0x100

#define SIFIVE_GPIO_REG_VALUE      0x000
#define SIFIVE_GPIO_REG_INPUT_EN   0x004
#define SIFIVE_GPIO_REG_OUTPUT_EN  0x008
#define SIFIVE_GPIO_REG_PORT       0x00C
#define SIFIVE_GPIO_REG_PUE        0x010
#define SIFIVE_GPIO_REG_DS         0x014
#define SIFIVE_GPIO_REG_RISE_IE    0x018
#define SIFIVE_GPIO_REG_RISE_IP    0x01C
#define SIFIVE_GPIO_REG_FALL_IE    0x020
#define SIFIVE_GPIO_REG_FALL_IP    0x024
#define SIFIVE_GPIO_REG_HIGH_IE    0x028
#define SIFIVE_GPIO_REG_HIGH_IP    0x02C
#define SIFIVE_GPIO_REG_LOW_IE     0x030
#define SIFIVE_GPIO_REG_LOW_IP     0x034
#define SIFIVE_GPIO_REG_IOF_EN     0x038
#define SIFIVE_GPIO_REG_IOF_SEL    0x03C
#define SIFIVE_GPIO_REG_OUT_XOR    0x040

typedef struct SIFIVEGPIOState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    qemu_irq irq[SIFIVE_GPIO_PINS];
    qemu_irq output[SIFIVE_GPIO_PINS];

    uint32_t value;             /* Actual value of the pin */
    uint32_t input_en;
    uint32_t output_en;
    uint32_t port;              /* Pin value requested by the user */
    uint32_t pue;
    uint32_t ds;
    uint32_t rise_ie;
    uint32_t rise_ip;
    uint32_t fall_ie;
    uint32_t fall_ip;
    uint32_t high_ie;
    uint32_t high_ip;
    uint32_t low_ie;
    uint32_t low_ip;
    uint32_t iof_en;
    uint32_t iof_sel;
    uint32_t out_xor;
    uint32_t in;
    uint32_t in_mask;

    /* config */
    uint32_t ngpio;
} SIFIVEGPIOState;

#endif /* SIFIVE_GPIO_H */
