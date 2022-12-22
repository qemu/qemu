/*
 * Header file for the Xilinx Versal's PMC IOU SLCR
 *
 * Copyright (C) 2021 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
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

/*
 * This is a model of Xilinx Versal's PMC I/O Peripheral Control and Status
 * module documented in Versal's Technical Reference manual [1] and the Versal
 * ACAP Register reference [2].
 *
 * References:
 *
 * [1] Versal ACAP Technical Reference Manual,
 *     https://www.xilinx.com/support/documentation/architecture-manuals/am011-versal-acap-trm.pdf
 *
 * [2] Versal ACAP Register Reference,
 *     https://www.xilinx.com/html_docs/registers/am012/am012-versal-register-reference.html#mod___pmc_iop_slcr.html
 *
 * QEMU interface:
 * + sysbus MMIO region 0: MemoryRegion for the device's registers
 * + sysbus IRQ 0: PMC (AXI and APB) parity error interrupt detected by the PMC
 *   I/O peripherals.
 * + sysbus IRQ 1: Device interrupt.
 * + Named GPIO output "sd-emmc-sel[0]": Enables 0: SD mode or 1: eMMC mode on
 *   SD/eMMC controller 0.
 * + Named GPIO output "sd-emmc-sel[1]": Enables 0: SD mode or 1: eMMC mode on
 *   SD/eMMC controller 1.
 * + Named GPIO output "qspi-ospi-mux-sel": Selects 0: QSPI linear region or 1:
 *   OSPI linear region.
 * + Named GPIO output "ospi-mux-sel": Selects 0: OSPI Indirect access mode or
 *   1: OSPI direct access mode.
 */

#ifndef XLNX_VERSAL_PMC_IOU_SLCR_H
#define XLNX_VERSAL_PMC_IOU_SLCR_H

#include "hw/sysbus.h"
#include "hw/register.h"

#define TYPE_XILINX_VERSAL_PMC_IOU_SLCR "xlnx.versal-pmc-iou-slcr"

OBJECT_DECLARE_SIMPLE_TYPE(XlnxVersalPmcIouSlcr, XILINX_VERSAL_PMC_IOU_SLCR)

#define XILINX_VERSAL_PMC_IOU_SLCR_R_MAX (0x828 / 4 + 1)

struct XlnxVersalPmcIouSlcr {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq_parity_imr;
    qemu_irq irq_imr;
    qemu_irq sd_emmc_sel[2];
    qemu_irq qspi_ospi_mux_sel;
    qemu_irq ospi_mux_sel;

    uint32_t regs[XILINX_VERSAL_PMC_IOU_SLCR_R_MAX];
    RegisterInfo regs_info[XILINX_VERSAL_PMC_IOU_SLCR_R_MAX];
};

#endif /* XLNX_VERSAL_PMC_IOU_SLCR_H */
