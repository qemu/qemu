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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <malloc.h>
#include <termios.h>
#include <sys/poll.h>
#include <errno.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include "cpu.h"
#include "vl.h"

//#define DEBUG_CMOS

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
#define REG_IBM_CENTURY_BYTE        0x32
#define REG_IBM_PS2_CENTURY_BYTE    0x37

RTCState rtc_state;

static void cmos_ioport_write(void *opaque, uint32_t addr, uint32_t data)
{
    RTCState *s = opaque;

    if ((addr & 1) == 0) {
        s->cmos_index = data & 0x7f;
    } else {
#ifdef DEBUG_CMOS
        printf("cmos: write index=0x%02x val=0x%02x\n",
               s->cmos_index, data);
#endif        
        switch(addr) {
        case RTC_SECONDS_ALARM:
        case RTC_MINUTES_ALARM:
        case RTC_HOURS_ALARM:
            /* XXX: not supported */
            s->cmos_data[s->cmos_index] = data;
            break;
        case RTC_SECONDS:
        case RTC_MINUTES:
        case RTC_HOURS:
        case RTC_DAY_OF_WEEK:
        case RTC_DAY_OF_MONTH:
        case RTC_MONTH:
        case RTC_YEAR:
            s->cmos_data[s->cmos_index] = data;
            break;
        case RTC_REG_A:
        case RTC_REG_B:
            s->cmos_data[s->cmos_index] = data;
            break;
        case RTC_REG_C:
        case RTC_REG_D:
            /* cannot write to them */
            break;
        default:
            s->cmos_data[s->cmos_index] = data;
            break;
        }
    }
}

static inline int to_bcd(int a)
{
    return ((a / 10) << 4) | (a % 10);
}

static void cmos_update_time(RTCState *s)
{
    struct tm *tm;
    time_t ti;

    ti = time(NULL);
    tm = gmtime(&ti);
    s->cmos_data[RTC_SECONDS] = to_bcd(tm->tm_sec);
    s->cmos_data[RTC_MINUTES] = to_bcd(tm->tm_min);
    s->cmos_data[RTC_HOURS] = to_bcd(tm->tm_hour);
    s->cmos_data[RTC_DAY_OF_WEEK] = to_bcd(tm->tm_wday);
    s->cmos_data[RTC_DAY_OF_MONTH] = to_bcd(tm->tm_mday);
    s->cmos_data[RTC_MONTH] = to_bcd(tm->tm_mon + 1);
    s->cmos_data[RTC_YEAR] = to_bcd(tm->tm_year % 100);
    s->cmos_data[REG_IBM_CENTURY_BYTE] = to_bcd((tm->tm_year / 100) + 19);
    s->cmos_data[REG_IBM_PS2_CENTURY_BYTE] = s->cmos_data[REG_IBM_CENTURY_BYTE];
}

static uint32_t cmos_ioport_read(void *opaque, uint32_t addr)
{
    RTCState *s = opaque;
    int ret;
    if ((addr & 1) == 0) {
        return 0xff;
    } else {
        switch(s->cmos_index) {
        case RTC_SECONDS:
        case RTC_MINUTES:
        case RTC_HOURS:
        case RTC_DAY_OF_WEEK:
        case RTC_DAY_OF_MONTH:
        case RTC_MONTH:
        case RTC_YEAR:
        case REG_IBM_CENTURY_BYTE:
        case REG_IBM_PS2_CENTURY_BYTE:
            cmos_update_time(s);
            ret = s->cmos_data[s->cmos_index];
            break;
        case RTC_REG_A:
            ret = s->cmos_data[s->cmos_index];
            /* toggle update-in-progress bit for Linux (same hack as
               plex86) */
            s->cmos_data[RTC_REG_A] ^= 0x80; 
            break;
        case RTC_REG_C:
            ret = s->cmos_data[s->cmos_index];
            pic_set_irq(s->irq, 0);
            s->cmos_data[RTC_REG_C] = 0x00; 
            break;
        default:
            ret = s->cmos_data[s->cmos_index];
            break;
        }
#ifdef DEBUG_CMOS
        printf("cmos: read index=0x%02x val=0x%02x\n",
               s->cmos_index, ret);
#endif
        return ret;
    }
}

void rtc_timer(void)
{
    RTCState *s = &rtc_state;
    if (s->cmos_data[RTC_REG_B] & 0x50) {
        pic_set_irq(s->irq, 1);
    }
}

void rtc_init(int base, int irq)
{
    RTCState *s = &rtc_state;

    cmos_update_time(s);

    s->irq = irq;
    s->cmos_data[RTC_REG_A] = 0x26;
    s->cmos_data[RTC_REG_B] = 0x02;
    s->cmos_data[RTC_REG_C] = 0x00;
    s->cmos_data[RTC_REG_D] = 0x80;

    register_ioport_write(base, 2, 1, cmos_ioport_write, s);
    register_ioport_read(base, 2, 1, cmos_ioport_read, s);
}

