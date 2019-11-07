/*
 * QEMU model of the Xilinx ZynqMP Real Time Clock (RTC).
 *
 * Copyright (c) 2017 Xilinx Inc.
 *
 * Written-by: Alistair Francis
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

#ifndef HW_RTC_XLNX_ZYNQMP_H
#define HW_RTC_XLNX_ZYNQMP_H

#include "hw/register.h"
#include "hw/sysbus.h"

#define TYPE_XLNX_ZYNQMP_RTC "xlnx-zynmp.rtc"

#define XLNX_ZYNQMP_RTC(obj) \
     OBJECT_CHECK(XlnxZynqMPRTC, (obj), TYPE_XLNX_ZYNQMP_RTC)

REG32(SET_TIME_WRITE, 0x0)
REG32(SET_TIME_READ, 0x4)
REG32(CALIB_WRITE, 0x8)
    FIELD(CALIB_WRITE, FRACTION_EN, 20, 1)
    FIELD(CALIB_WRITE, FRACTION_DATA, 16, 4)
    FIELD(CALIB_WRITE, MAX_TICK, 0, 16)
REG32(CALIB_READ, 0xc)
    FIELD(CALIB_READ, FRACTION_EN, 20, 1)
    FIELD(CALIB_READ, FRACTION_DATA, 16, 4)
    FIELD(CALIB_READ, MAX_TICK, 0, 16)
REG32(CURRENT_TIME, 0x10)
REG32(CURRENT_TICK, 0x14)
    FIELD(CURRENT_TICK, VALUE, 0, 16)
REG32(ALARM, 0x18)
REG32(RTC_INT_STATUS, 0x20)
    FIELD(RTC_INT_STATUS, ALARM, 1, 1)
    FIELD(RTC_INT_STATUS, SECONDS, 0, 1)
REG32(RTC_INT_MASK, 0x24)
    FIELD(RTC_INT_MASK, ALARM, 1, 1)
    FIELD(RTC_INT_MASK, SECONDS, 0, 1)
REG32(RTC_INT_EN, 0x28)
    FIELD(RTC_INT_EN, ALARM, 1, 1)
    FIELD(RTC_INT_EN, SECONDS, 0, 1)
REG32(RTC_INT_DIS, 0x2c)
    FIELD(RTC_INT_DIS, ALARM, 1, 1)
    FIELD(RTC_INT_DIS, SECONDS, 0, 1)
REG32(ADDR_ERROR, 0x30)
    FIELD(ADDR_ERROR, STATUS, 0, 1)
REG32(ADDR_ERROR_INT_MASK, 0x34)
    FIELD(ADDR_ERROR_INT_MASK, MASK, 0, 1)
REG32(ADDR_ERROR_INT_EN, 0x38)
    FIELD(ADDR_ERROR_INT_EN, MASK, 0, 1)
REG32(ADDR_ERROR_INT_DIS, 0x3c)
    FIELD(ADDR_ERROR_INT_DIS, MASK, 0, 1)
REG32(CONTROL, 0x40)
    FIELD(CONTROL, BATTERY_DISABLE, 31, 1)
    FIELD(CONTROL, OSC_CNTRL, 24, 4)
    FIELD(CONTROL, SLVERR_ENABLE, 0, 1)
REG32(SAFETY_CHK, 0x50)

#define XLNX_ZYNQMP_RTC_R_MAX (R_SAFETY_CHK + 1)

typedef struct XlnxZynqMPRTC {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq_rtc_int;
    qemu_irq irq_addr_error_int;

    uint32_t tick_offset;

    uint32_t regs[XLNX_ZYNQMP_RTC_R_MAX];
    RegisterInfo regs_info[XLNX_ZYNQMP_RTC_R_MAX];
} XlnxZynqMPRTC;

#endif
