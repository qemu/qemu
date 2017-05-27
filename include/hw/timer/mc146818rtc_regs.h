/*
 * QEMU MC146818 RTC emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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

#ifndef MC146818RTC_REGS_H
#define MC146818RTC_REGS_H

#define RTC_ISA_IRQ 8

#define RTC_SECONDS             0
#define RTC_SECONDS_ALARM       1
#define RTC_MINUTES             2
#define RTC_MINUTES_ALARM       3
#define RTC_HOURS               4
#define RTC_HOURS_ALARM         5
#define RTC_ALARM_DONT_CARE    0xC0

#define RTC_DAY_OF_WEEK         6
#define RTC_DAY_OF_MONTH        7
#define RTC_MONTH               8
#define RTC_YEAR                9

#define RTC_REG_A               10
#define RTC_REG_B               11
#define RTC_REG_C               12
#define RTC_REG_D               13

/* PC cmos mappings */
#define RTC_CENTURY              0x32
#define RTC_IBM_PS2_CENTURY_BYTE 0x37

#define REG_A_UIP 0x80

#define REG_B_SET  0x80
#define REG_B_PIE  0x40
#define REG_B_AIE  0x20
#define REG_B_UIE  0x10
#define REG_B_SQWE 0x08
#define REG_B_DM   0x04
#define REG_B_24H  0x02

#define REG_C_UF   0x10
#define REG_C_IRQF 0x80
#define REG_C_PF   0x40
#define REG_C_AF   0x20
#define REG_C_MASK 0x70

static inline uint32_t periodic_period_to_clock(int period_code)
{
    if (!period_code) {
        return 0;
   }

    if (period_code <= 2) {
        period_code += 7;
    }
    /* period in 32 Khz cycles */
   return 1 << (period_code - 1);
}

#define RTC_CLOCK_RATE            32768

static inline int64_t periodic_clock_to_ns(int64_t clocks)
{
    return muldiv64(clocks, NANOSECONDS_PER_SECOND, RTC_CLOCK_RATE);
}

#endif
