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

typedef struct AspeedSegments {
    hwaddr addr;
    uint32_t size;
} AspeedSegments;

struct AspeedSMCState;
typedef struct AspeedSMCController {
    const char *name;
    uint8_t r_conf;
    uint8_t r_ce_ctrl;
    uint8_t r_ctrl0;
    uint8_t r_timings;
    uint8_t conf_enable_w0;
    uint8_t max_slaves;
    const AspeedSegments *segments;
    hwaddr flash_window_base;
    uint32_t flash_window_size;
    bool has_dma;
    uint32_t nregs;
} AspeedSMCController;

typedef struct AspeedSMCFlash {
    struct AspeedSMCState *controller;

    uint8_t id;
    uint32_t size;

    MemoryRegion mmio;
    DeviceState *flash;
} AspeedSMCFlash;

#define TYPE_ASPEED_SMC "aspeed.smc"
#define ASPEED_SMC(obj) OBJECT_CHECK(AspeedSMCState, (obj), TYPE_ASPEED_SMC)
#define ASPEED_SMC_CLASS(klass) \
     OBJECT_CLASS_CHECK(AspeedSMCClass, (klass), TYPE_ASPEED_SMC)
#define ASPEED_SMC_GET_CLASS(obj) \
     OBJECT_GET_CLASS(AspeedSMCClass, (obj), TYPE_ASPEED_SMC)

typedef struct  AspeedSMCClass {
    SysBusDevice parent_obj;
    const AspeedSMCController *ctrl;
}  AspeedSMCClass;

#define ASPEED_SMC_R_MAX        (0x100 / 4)

typedef struct AspeedSMCState {
    SysBusDevice parent_obj;

    const AspeedSMCController *ctrl;

    MemoryRegion mmio;
    MemoryRegion mmio_flash;

    qemu_irq irq;
    int irqline;

    uint32_t num_cs;
    qemu_irq *cs_lines;

    SSIBus *spi;

    uint32_t regs[ASPEED_SMC_R_MAX];

    /* depends on the controller type */
    uint8_t r_conf;
    uint8_t r_ce_ctrl;
    uint8_t r_ctrl0;
    uint8_t r_timings;
    uint8_t conf_enable_w0;

    AspeedSMCFlash *flashes;
} AspeedSMCState;

#endif /* ASPEED_SMC_H */
