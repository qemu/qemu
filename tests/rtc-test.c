/*
 * QTest testcase for the MC146818 real-time clock
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#include "libqtest.h"
#include "hw/mc146818rtc_regs.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static uint8_t base = 0x70;

static int bcd2dec(int value)
{
    return (((value >> 4) & 0x0F) * 10) + (value & 0x0F);
}

static int dec2bcd(int value)
{
    return ((value / 10) << 4) | (value % 10);
}

static uint8_t cmos_read(uint8_t reg)
{
    outb(base + 0, reg);
    return inb(base + 1);
}

static void cmos_write(uint8_t reg, uint8_t val)
{
    outb(base + 0, reg);
    outb(base + 1, val);
}

static int tm_cmp(struct tm *lhs, struct tm *rhs)
{
    time_t a, b;
    struct tm d1, d2;

    memcpy(&d1, lhs, sizeof(d1));
    memcpy(&d2, rhs, sizeof(d2));

    a = mktime(&d1);
    b = mktime(&d2);

    if (a < b) {
        return -1;
    } else if (a > b) {
        return 1;
    }

    return 0;
}

#if 0
static void print_tm(struct tm *tm)
{
    printf("%04d-%02d-%02d %02d:%02d:%02d\n",
           tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_gmtoff);
}
#endif

static void cmos_get_date_time(struct tm *date)
{
    int base_year = 2000, hour_offset;
    int sec, min, hour, mday, mon, year;
    time_t ts;
    struct tm dummy;

    sec = cmos_read(RTC_SECONDS);
    min = cmos_read(RTC_MINUTES);
    hour = cmos_read(RTC_HOURS);
    mday = cmos_read(RTC_DAY_OF_MONTH);
    mon = cmos_read(RTC_MONTH);
    year = cmos_read(RTC_YEAR);

    if ((cmos_read(RTC_REG_B) & REG_B_DM) == 0) {
        sec = bcd2dec(sec);
        min = bcd2dec(min);
        hour = bcd2dec(hour);
        mday = bcd2dec(mday);
        mon = bcd2dec(mon);
        year = bcd2dec(year);
        hour_offset = 80;
    } else {
        hour_offset = 0x80;
    }

    if ((cmos_read(0x0B) & REG_B_24H) == 0) {
        if (hour >= hour_offset) {
            hour -= hour_offset;
            hour += 12;
        }
    }

    ts = time(NULL);
    localtime_r(&ts, &dummy);

    date->tm_isdst = dummy.tm_isdst;
    date->tm_sec = sec;
    date->tm_min = min;
    date->tm_hour = hour;
    date->tm_mday = mday;
    date->tm_mon = mon - 1;
    date->tm_year = base_year + year - 1900;
#ifndef __sun__
    date->tm_gmtoff = 0;
#endif

    ts = mktime(date);
}

static void check_time(int wiggle)
{
    struct tm start, date[4], end;
    struct tm *datep;
    time_t ts;

    /*
     * This check assumes a few things.  First, we cannot guarantee that we get
     * a consistent reading from the wall clock because we may hit an edge of
     * the clock while reading.  To work around this, we read four clock readings
     * such that at least two of them should match.  We need to assume that one
     * reading is corrupt so we need four readings to ensure that we have at
     * least two consecutive identical readings
     *
     * It's also possible that we'll cross an edge reading the host clock so
     * simply check to make sure that the clock reading is within the period of
     * when we expect it to be.
     */

    ts = time(NULL);
    gmtime_r(&ts, &start);

    cmos_get_date_time(&date[0]);
    cmos_get_date_time(&date[1]);
    cmos_get_date_time(&date[2]);
    cmos_get_date_time(&date[3]);

    ts = time(NULL);
    gmtime_r(&ts, &end);

    if (tm_cmp(&date[0], &date[1]) == 0) {
        datep = &date[0];
    } else if (tm_cmp(&date[1], &date[2]) == 0) {
        datep = &date[1];
    } else if (tm_cmp(&date[2], &date[3]) == 0) {
        datep = &date[2];
    } else {
        g_assert_not_reached();
    }

    if (!(tm_cmp(&start, datep) <= 0 && tm_cmp(datep, &end) <= 0)) {
        long t, s;

        start.tm_isdst = datep->tm_isdst;

        t = (long)mktime(datep);
        s = (long)mktime(&start);
        if (t < s) {
            g_test_message("RTC is %ld second(s) behind wall-clock\n", (s - t));
        } else {
            g_test_message("RTC is %ld second(s) ahead of wall-clock\n", (t - s));
        }

        g_assert_cmpint(ABS(t - s), <=, wiggle);
    }
}

static int wiggle = 2;

static void set_year_20xx(void)
{
    /* Set BCD mode */
    cmos_write(RTC_REG_B, cmos_read(RTC_REG_B) & ~REG_B_DM);
    cmos_write(RTC_REG_A, 0x76);
    cmos_write(RTC_YEAR, 0x11);
    cmos_write(RTC_CENTURY, 0x20);
    cmos_write(RTC_MONTH, 0x02);
    cmos_write(RTC_DAY_OF_MONTH, 0x02);
    cmos_write(RTC_HOURS, 0x02);
    cmos_write(RTC_MINUTES, 0x04);
    cmos_write(RTC_SECONDS, 0x58);
    cmos_write(RTC_REG_A, 0x26);

    g_assert_cmpint(cmos_read(RTC_HOURS), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_MINUTES), ==, 0x04);
    g_assert_cmpint(cmos_read(RTC_SECONDS), >=, 0x58);
    g_assert_cmpint(cmos_read(RTC_DAY_OF_MONTH), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_MONTH), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_YEAR), ==, 0x11);
    g_assert_cmpint(cmos_read(RTC_CENTURY), ==, 0x20);

    if (sizeof(time_t) == 4) {
        return;
    }

    /* Set a date in 2080 to ensure there is no year-2038 overflow.  */
    cmos_write(RTC_REG_A, 0x76);
    cmos_write(RTC_YEAR, 0x80);
    cmos_write(RTC_REG_A, 0x26);

    g_assert_cmpint(cmos_read(RTC_HOURS), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_MINUTES), ==, 0x04);
    g_assert_cmpint(cmos_read(RTC_SECONDS), >=, 0x58);
    g_assert_cmpint(cmos_read(RTC_DAY_OF_MONTH), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_MONTH), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_YEAR), ==, 0x80);
    g_assert_cmpint(cmos_read(RTC_CENTURY), ==, 0x20);

    cmos_write(RTC_REG_A, 0x76);
    cmos_write(RTC_YEAR, 0x11);
    cmos_write(RTC_REG_A, 0x26);

    g_assert_cmpint(cmos_read(RTC_HOURS), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_MINUTES), ==, 0x04);
    g_assert_cmpint(cmos_read(RTC_SECONDS), >=, 0x58);
    g_assert_cmpint(cmos_read(RTC_DAY_OF_MONTH), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_MONTH), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_YEAR), ==, 0x11);
    g_assert_cmpint(cmos_read(RTC_CENTURY), ==, 0x20);
}

static void set_year_1980(void)
{
    /* Set BCD mode */
    cmos_write(RTC_REG_B, cmos_read(RTC_REG_B) & ~REG_B_DM);
    cmos_write(RTC_REG_A, 0x76);
    cmos_write(RTC_YEAR, 0x80);
    cmos_write(RTC_CENTURY, 0x19);
    cmos_write(RTC_MONTH, 0x02);
    cmos_write(RTC_DAY_OF_MONTH, 0x02);
    cmos_write(RTC_HOURS, 0x02);
    cmos_write(RTC_MINUTES, 0x04);
    cmos_write(RTC_SECONDS, 0x58);
    cmos_write(RTC_REG_A, 0x26);

    g_assert_cmpint(cmos_read(RTC_HOURS), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_MINUTES), ==, 0x04);
    g_assert_cmpint(cmos_read(RTC_SECONDS), >=, 0x58);
    g_assert_cmpint(cmos_read(RTC_DAY_OF_MONTH), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_MONTH), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_YEAR), ==, 0x80);
    g_assert_cmpint(cmos_read(RTC_CENTURY), ==, 0x19);
}

static void bcd_check_time(void)
{
    /* Set BCD mode */
    cmos_write(RTC_REG_B, cmos_read(RTC_REG_B) & ~REG_B_DM);
    check_time(wiggle);
}

static void dec_check_time(void)
{
    /* Set DEC mode */
    cmos_write(RTC_REG_B, cmos_read(RTC_REG_B) | REG_B_DM);
    check_time(wiggle);
}

static void set_alarm_time(struct tm *tm)
{
    int sec;

    sec = tm->tm_sec;

    if ((cmos_read(RTC_REG_B) & REG_B_DM) == 0) {
        sec = dec2bcd(sec);
    }

    cmos_write(RTC_SECONDS_ALARM, sec);
    cmos_write(RTC_MINUTES_ALARM, RTC_ALARM_DONT_CARE);
    cmos_write(RTC_HOURS_ALARM, RTC_ALARM_DONT_CARE);
}

static void alarm_time(void)
{
    struct tm now;
    time_t ts;
    int i;

    ts = time(NULL);
    gmtime_r(&ts, &now);

    /* set DEC mode */
    cmos_write(RTC_REG_B, cmos_read(RTC_REG_B) | REG_B_DM);

    g_assert(!get_irq(RTC_ISA_IRQ));
    cmos_read(RTC_REG_C);

    now.tm_sec = (now.tm_sec + 2) % 60;
    set_alarm_time(&now);
    cmos_write(RTC_REG_B, cmos_read(RTC_REG_B) | REG_B_AIE);

    for (i = 0; i < 2 + wiggle; i++) {
        if (get_irq(RTC_ISA_IRQ)) {
            break;
        }

        clock_step(1000000000);
    }

    g_assert(get_irq(RTC_ISA_IRQ));
    g_assert((cmos_read(RTC_REG_C) & REG_C_AF) != 0);
    g_assert(cmos_read(RTC_REG_C) == 0);
}

/* success if no crash or abort */
static void fuzz_registers(void)
{
    unsigned int i;

    for (i = 0; i < 1000; i++) {
        uint8_t reg, val;

        reg = (uint8_t)g_test_rand_int_range(0, 16);
        val = (uint8_t)g_test_rand_int_range(0, 256);

        cmos_write(reg, val);
        cmos_read(reg);
    }
}

static void register_b_set_flag(void)
{
    /* Enable binary-coded decimal (BCD) mode and SET flag in Register B*/
    cmos_write(RTC_REG_B, (cmos_read(RTC_REG_B) & ~REG_B_DM) | REG_B_SET);

    cmos_write(RTC_REG_A, 0x76);
    cmos_write(RTC_YEAR, 0x11);
    cmos_write(RTC_CENTURY, 0x20);
    cmos_write(RTC_MONTH, 0x02);
    cmos_write(RTC_DAY_OF_MONTH, 0x02);
    cmos_write(RTC_HOURS, 0x02);
    cmos_write(RTC_MINUTES, 0x04);
    cmos_write(RTC_SECONDS, 0x58);
    cmos_write(RTC_REG_A, 0x26);

    /* Since SET flag is still enabled, these are equality checks. */
    g_assert_cmpint(cmos_read(RTC_HOURS), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_MINUTES), ==, 0x04);
    g_assert_cmpint(cmos_read(RTC_SECONDS), ==, 0x58);
    g_assert_cmpint(cmos_read(RTC_DAY_OF_MONTH), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_MONTH), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_YEAR), ==, 0x11);
    g_assert_cmpint(cmos_read(RTC_CENTURY), ==, 0x20);

    /* Disable SET flag in Register B */
    cmos_write(RTC_REG_B, cmos_read(RTC_REG_B) & ~REG_B_SET);

    g_assert_cmpint(cmos_read(RTC_HOURS), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_MINUTES), ==, 0x04);

    /* Since SET flag is disabled, this is an inequality check.
     * We (reasonably) assume that no (sexagesimal) overflow occurs. */
    g_assert_cmpint(cmos_read(RTC_SECONDS), >=, 0x58);
    g_assert_cmpint(cmos_read(RTC_DAY_OF_MONTH), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_MONTH), ==, 0x02);
    g_assert_cmpint(cmos_read(RTC_YEAR), ==, 0x11);
    g_assert_cmpint(cmos_read(RTC_CENTURY), ==, 0x20);
}

int main(int argc, char **argv)
{
    QTestState *s = NULL;
    int ret;

    g_test_init(&argc, &argv, NULL);

    s = qtest_start("-display none -rtc clock=vm");
    qtest_irq_intercept_in(s, "ioapic");

    qtest_add_func("/rtc/bcd/check-time", bcd_check_time);
    qtest_add_func("/rtc/dec/check-time", dec_check_time);
    qtest_add_func("/rtc/alarm-time", alarm_time);
    qtest_add_func("/rtc/set-year/20xx", set_year_20xx);
    qtest_add_func("/rtc/set-year/1980", set_year_1980);
    qtest_add_func("/rtc/misc/register_b_set_flag", register_b_set_flag);
    qtest_add_func("/rtc/misc/fuzz-registers", fuzz_registers);
    ret = g_test_run();

    if (s) {
        qtest_quit(s);
    }

    return ret;
}
