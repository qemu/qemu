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

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "libqtest.h"
#include "hw/timer/mc146818rtc_regs.h"

static uint8_t base = 0x70;

static int bcd2dec(int value)
{
    return (((value >> 4) & 0x0F) * 10) + (value & 0x0F);
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
    cmos_write(RTC_REG_B, REG_B_24H);
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
    cmos_write(RTC_REG_B, REG_B_24H);
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
    cmos_write(RTC_REG_B, REG_B_24H);
    check_time(wiggle);
}

static void dec_check_time(void)
{
    /* Set DEC mode */
    cmos_write(RTC_REG_B, REG_B_24H | REG_B_DM);
    check_time(wiggle);
}

static void alarm_time(void)
{
    struct tm now;
    time_t ts;
    int i;

    ts = time(NULL);
    gmtime_r(&ts, &now);

    /* set DEC mode */
    cmos_write(RTC_REG_B, REG_B_24H | REG_B_DM);

    g_assert(!get_irq(RTC_ISA_IRQ));
    cmos_read(RTC_REG_C);

    now.tm_sec = (now.tm_sec + 2) % 60;
    cmos_write(RTC_SECONDS_ALARM, now.tm_sec);
    cmos_write(RTC_MINUTES_ALARM, RTC_ALARM_DONT_CARE);
    cmos_write(RTC_HOURS_ALARM, RTC_ALARM_DONT_CARE);
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

static void set_time(int mode, int h, int m, int s)
{
    /* set BCD 12 hour mode */
    cmos_write(RTC_REG_B, mode);

    cmos_write(RTC_REG_A, 0x76);
    cmos_write(RTC_HOURS, h);
    cmos_write(RTC_MINUTES, m);
    cmos_write(RTC_SECONDS, s);
    cmos_write(RTC_REG_A, 0x26);
}

#define assert_time(h, m, s) \
    do { \
        g_assert_cmpint(cmos_read(RTC_HOURS), ==, h); \
        g_assert_cmpint(cmos_read(RTC_MINUTES), ==, m); \
        g_assert_cmpint(cmos_read(RTC_SECONDS), ==, s); \
    } while(0)

static void basic_12h_bcd(void)
{
    /* set BCD 12 hour mode */
    set_time(0, 0x81, 0x59, 0x00);
    clock_step(1000000000LL);
    assert_time(0x81, 0x59, 0x01);
    clock_step(59000000000LL);
    assert_time(0x82, 0x00, 0x00);

    /* test BCD wraparound */
    set_time(0, 0x09, 0x59, 0x59);
    clock_step(60000000000LL);
    assert_time(0x10, 0x00, 0x59);

    /* 12 AM -> 1 AM */
    set_time(0, 0x12, 0x59, 0x59);
    clock_step(1000000000LL);
    assert_time(0x01, 0x00, 0x00);

    /* 12 PM -> 1 PM */
    set_time(0, 0x92, 0x59, 0x59);
    clock_step(1000000000LL);
    assert_time(0x81, 0x00, 0x00);

    /* 11 AM -> 12 PM */
    set_time(0, 0x11, 0x59, 0x59);
    clock_step(1000000000LL);
    assert_time(0x92, 0x00, 0x00);
    /* TODO: test day wraparound */

    /* 11 PM -> 12 AM */
    set_time(0, 0x91, 0x59, 0x59);
    clock_step(1000000000LL);
    assert_time(0x12, 0x00, 0x00);
    /* TODO: test day wraparound */
}

static void basic_12h_dec(void)
{
    /* set decimal 12 hour mode */
    set_time(REG_B_DM, 0x81, 59, 0);
    clock_step(1000000000LL);
    assert_time(0x81, 59, 1);
    clock_step(59000000000LL);
    assert_time(0x82, 0, 0);

    /* 12 PM -> 1 PM */
    set_time(REG_B_DM, 0x8c, 59, 59);
    clock_step(1000000000LL);
    assert_time(0x81, 0, 0);

    /* 12 AM -> 1 AM */
    set_time(REG_B_DM, 0x0c, 59, 59);
    clock_step(1000000000LL);
    assert_time(0x01, 0, 0);

    /* 11 AM -> 12 PM */
    set_time(REG_B_DM, 0x0b, 59, 59);
    clock_step(1000000000LL);
    assert_time(0x8c, 0, 0);

    /* 11 PM -> 12 AM */
    set_time(REG_B_DM, 0x8b, 59, 59);
    clock_step(1000000000LL);
    assert_time(0x0c, 0, 0);
    /* TODO: test day wraparound */
}

static void basic_24h_bcd(void)
{
    /* set BCD 24 hour mode */
    set_time(REG_B_24H, 0x09, 0x59, 0x00);
    clock_step(1000000000LL);
    assert_time(0x09, 0x59, 0x01);
    clock_step(59000000000LL);
    assert_time(0x10, 0x00, 0x00);

    /* test BCD wraparound */
    set_time(REG_B_24H, 0x09, 0x59, 0x00);
    clock_step(60000000000LL);
    assert_time(0x10, 0x00, 0x00);

    /* TODO: test day wraparound */
    set_time(REG_B_24H, 0x23, 0x59, 0x00);
    clock_step(60000000000LL);
    assert_time(0x00, 0x00, 0x00);
}

static void basic_24h_dec(void)
{
    /* set decimal 24 hour mode */
    set_time(REG_B_24H | REG_B_DM, 9, 59, 0);
    clock_step(1000000000LL);
    assert_time(9, 59, 1);
    clock_step(59000000000LL);
    assert_time(10, 0, 0);

    /* test BCD wraparound */
    set_time(REG_B_24H | REG_B_DM, 9, 59, 0);
    clock_step(60000000000LL);
    assert_time(10, 0, 0);

    /* TODO: test day wraparound */
    set_time(REG_B_24H | REG_B_DM, 23, 59, 0);
    clock_step(60000000000LL);
    assert_time(0, 0, 0);
}

static void am_pm_alarm(void)
{
    cmos_write(RTC_MINUTES_ALARM, 0xC0);
    cmos_write(RTC_SECONDS_ALARM, 0xC0);

    /* set BCD 12 hour mode */
    cmos_write(RTC_REG_B, 0);

    /* Set time and alarm hour.  */
    cmos_write(RTC_REG_A, 0x76);
    cmos_write(RTC_HOURS_ALARM, 0x82);
    cmos_write(RTC_HOURS, 0x81);
    cmos_write(RTC_MINUTES, 0x59);
    cmos_write(RTC_SECONDS, 0x00);
    cmos_read(RTC_REG_C);
    cmos_write(RTC_REG_A, 0x26);

    /* Check that alarm triggers when AM/PM is set.  */
    clock_step(60000000000LL);
    g_assert(cmos_read(RTC_HOURS) == 0x82);
    g_assert((cmos_read(RTC_REG_C) & REG_C_AF) != 0);

    /*
     * Each of the following two tests takes over 60 seconds due to the time
     * needed to report the PIT interrupts.  Unfortunately, our PIT device
     * model keeps counting even when GATE=0, so we cannot simply disable
     * it in main().
     */
    if (g_test_quick()) {
        return;
    }

    /* set DEC 12 hour mode */
    cmos_write(RTC_REG_B, REG_B_DM);

    /* Set time and alarm hour.  */
    cmos_write(RTC_REG_A, 0x76);
    cmos_write(RTC_HOURS_ALARM, 0x82);
    cmos_write(RTC_HOURS, 3);
    cmos_write(RTC_MINUTES, 0);
    cmos_write(RTC_SECONDS, 0);
    cmos_read(RTC_REG_C);
    cmos_write(RTC_REG_A, 0x26);

    /* Check that alarm triggers.  */
    clock_step(3600 * 11 * 1000000000LL);
    g_assert(cmos_read(RTC_HOURS) == 0x82);
    g_assert((cmos_read(RTC_REG_C) & REG_C_AF) != 0);

    /* Same as above, with inverted HOURS and HOURS_ALARM.  */
    cmos_write(RTC_REG_A, 0x76);
    cmos_write(RTC_HOURS_ALARM, 2);
    cmos_write(RTC_HOURS, 3);
    cmos_write(RTC_MINUTES, 0);
    cmos_write(RTC_SECONDS, 0);
    cmos_read(RTC_REG_C);
    cmos_write(RTC_REG_A, 0x26);

    /* Check that alarm does not trigger if hours differ only by AM/PM.  */
    clock_step(3600 * 11 * 1000000000LL);
    g_assert(cmos_read(RTC_HOURS) == 0x82);
    g_assert((cmos_read(RTC_REG_C) & REG_C_AF) == 0);
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
    cmos_write(RTC_REG_B, REG_B_24H | REG_B_SET);

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

    s = qtest_start("-rtc clock=vm");
    qtest_irq_intercept_in(s, "ioapic");

    qtest_add_func("/rtc/check-time/bcd", bcd_check_time);
    qtest_add_func("/rtc/check-time/dec", dec_check_time);
    qtest_add_func("/rtc/alarm/interrupt", alarm_time);
    qtest_add_func("/rtc/alarm/am-pm", am_pm_alarm);
    qtest_add_func("/rtc/basic/dec-24h", basic_24h_dec);
    qtest_add_func("/rtc/basic/bcd-24h", basic_24h_bcd);
    qtest_add_func("/rtc/basic/dec-12h", basic_12h_dec);
    qtest_add_func("/rtc/basic/bcd-12h", basic_12h_bcd);
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
