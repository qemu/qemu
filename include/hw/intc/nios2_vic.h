/*
 * Vectored Interrupt Controller for nios2 processor
 *
 * Copyright (c) 2022 Neuroblade
 *
 * Interface:
 * QOM property "cpu": link to the Nios2 CPU (must be set)
 * Unnamed GPIO inputs 0..NIOS2_VIC_MAX_IRQ-1: input IRQ lines
 * IRQ should be connected to nios2 IRQ0.
 *
 * Reference: "Embedded Peripherals IP User Guide
 *             for Intel® Quartus® Prime Design Suite: 21.4"
 * Chapter 38 "Vectored Interrupt Controller Core"
 * See: https://www.intel.com/content/www/us/en/docs/programmable/683130/21-4/vectored-interrupt-controller-core.html
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

#ifndef HW_INTC_NIOS2_VIC_H
#define HW_INTC_NIOS2_VIC_H

#define TYPE_NIOS2_VIC "nios2-vic"
OBJECT_DECLARE_SIMPLE_TYPE(Nios2VIC, NIOS2_VIC)

#define NIOS2_VIC_MAX_IRQ 32

struct Nios2VIC {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    qemu_irq output_int;

    /* properties */
    CPUState *cpu;
    MemoryRegion csr;

    uint32_t int_config[NIOS2_VIC_MAX_IRQ];
    uint32_t vic_config;
    uint32_t int_raw_status;
    uint32_t int_enable;
    uint32_t sw_int;
    uint32_t vic_status;
    uint32_t vec_tbl_base;
    uint32_t vec_tbl_addr;
};

#endif /* HW_INTC_NIOS2_VIC_H */
