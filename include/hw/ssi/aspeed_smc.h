/*
 * ASPEED AST2400 SMC Controller (SPI Flash Only)
 *
 * Copyright (C) 2016 IBM Corp.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef ASPEED_SMC_H
#define ASPEED_SMC_H

#include "hw/ssi/ssi.h"
#include "hw/sysbus.h"
#include "qom/object.h"

struct AspeedSMCState;

#define TYPE_ASPEED_SMC_FLASH "aspeed.smc.flash"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedSMCFlash, ASPEED_SMC_FLASH)
struct AspeedSMCFlash {
    SysBusDevice parent_obj;

    struct AspeedSMCState *controller;
    uint8_t cs;

    MemoryRegion mmio;
};

#define TYPE_ASPEED_SMC "aspeed.smc"
OBJECT_DECLARE_TYPE(AspeedSMCState, AspeedSMCClass, ASPEED_SMC)

#define ASPEED_SMC_R_MAX        (0x100 / 4)
#define ASPEED_SMC_CS_MAX       5

struct AspeedSMCState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    MemoryRegion mmio_flash_container;
    MemoryRegion mmio_flash;

    qemu_irq irq;

    qemu_irq *cs_lines;
    bool inject_failure;

    SSIBus *spi;

    uint32_t regs[ASPEED_SMC_R_MAX];

    /* depends on the controller type */
    uint8_t r_conf;
    uint8_t r_ce_ctrl;
    uint8_t r_ctrl0;
    uint8_t r_timings;
    uint8_t conf_enable_w0;

    AddressSpace flash_as;
    MemoryRegion *dram_mr;
    AddressSpace dram_as;

    AspeedSMCFlash flashes[ASPEED_SMC_CS_MAX];

    uint8_t snoop_index;
    uint8_t snoop_dummies;
};

typedef struct AspeedSegments {
    hwaddr addr;
    uint32_t size;
} AspeedSegments;

struct AspeedSMCClass {
    SysBusDeviceClass parent_obj;

    uint8_t r_conf;
    uint8_t r_ce_ctrl;
    uint8_t r_ctrl0;
    uint8_t r_timings;
    uint8_t nregs_timings;
    uint8_t conf_enable_w0;
    uint8_t cs_num_max;
    const uint32_t *resets;
    const AspeedSegments *segments;
    uint32_t segment_addr_mask;
    hwaddr flash_window_base;
    uint32_t flash_window_size;
    uint32_t features;
    hwaddr dma_flash_mask;
    hwaddr dma_dram_mask;
    uint32_t nregs;
    uint32_t (*segment_to_reg)(const AspeedSMCState *s,
                               const AspeedSegments *seg);
    void (*reg_to_segment)(const AspeedSMCState *s, uint32_t reg,
                           AspeedSegments *seg);
    void (*dma_ctrl)(AspeedSMCState *s, uint32_t value);
    int (*addr_width)(const AspeedSMCState *s);
};

#endif /* ASPEED_SMC_H */
