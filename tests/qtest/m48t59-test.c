/*
 * QTest testcase for the M48T59 and M48T08 real-time clocks
 *
 * Based on MC146818 RTC test:
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "libqtest.h"

#define RTC_SECONDS             0x9
#define RTC_MINUTES             0xa
#define RTC_HOURS               0xb

#define RTC_DAY_OF_WEEK         0xc
#define RTC_DAY_OF_MONTH        0xd
#define RTC_MONTH               0xe
#define RTC_YEAR                0xf

static uint32_t base;
static uint16_t reg_base = 0x1ff0; /* 0x7f0 for m48t02 */
static int base_year;
static const char *base_machine;
static bool use_mmio;

static uint8_t cmos_read_mmio(QTestState *s, uint8_t reg)
{
    return qtest_readb(s, base + (uint32_t)reg_base + (uint32_t)reg);
}

static void cmos_write_mmio(QTestState *s, uint8_t reg, uint8_t val)
{
    uint8_t data = val;

    qtest_writeb(s, base + (uint32_t)reg_base + (uint32_t)reg, data);
}

static uint8_t cmos_read_ioio(QTestState *s, uint8_t reg)
{
    qtest_outw(s, base + 0, reg_base + (uint16_t)reg);
    return qtest_inb(s, base + 3);
}

static void cmos_write_ioio(QTestState *s, uint8_t reg, uint8_t val)
{
    qtest_outw(s, base + 0, reg_base + (uint16_t)reg);
    qtest_outb(s, base + 3, val);
}

static uint8_t cmos_read(QTestState *s, uint8_t reg)
{
    if (use_mmio) {
        return cmos_read_mmio(s, reg);
    } else {
        return cmos_read_ioio(s, reg);
    }
}

static void cmos_write(QTestState *s, uint8_t reg, uint8_t val)
{
    if (use_mmio) {
        cmos_write_mmio(s, reg, val);
    } else {
        cmos_write_ioio(s, reg, val);
    }
}

static int bcd2dec(int value)
{
    return (((value >> 4) & 0x0F) * 10) + (value & 0x0F);
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
    printf("%04d-%02d-%02d %02d:%02d:%02d %+02ld\n",
           tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_gmtoff);
}
#endif

static void cmos_get_date_time(QTestState *s, struct tm *date)
{
    int sec, min, hour, mday, mon, year;
    time_t ts;
    struct tm dummy;

    sec = cmos_read(s, RTC_SECONDS);
    min = cmos_read(s, RTC_MINUTES);
    hour = cmos_read(s, RTC_HOURS);
    mday = cmos_read(s, RTC_DAY_OF_MONTH);
    mon = cmos_read(s, RTC_MONTH);
    year = cmos_read(s, RTC_YEAR);

    sec = bcd2dec(sec);
    min = bcd2dec(min);
    hour = bcd2dec(hour);
    mday = bcd2dec(mday);
    mon = bcd2dec(mon);
    year = bcd2dec(year);

    ts = time(NULL);
    localtime_r(&ts, &dummy);

    date->tm_isdst = dummy.tm_isdst;
    date->tm_sec = sec;
    date->tm_min = min;
    date->tm_hour = hour;
    date->tm_mday = mday;
    date->tm_mon = mon - 1;
    date->tm_year = base_year + year - 1900;
#if !defined(__sun__) && !defined(_WIN32)
    date->tm_gmtoff = 0;
#endif

    ts = mktime(date);
}

static QTestState *m48t59_qtest_start(void)
{
    return qtest_initf("-M %s -rtc clock=vm", base_machine);
}

static void bcd_check_time(void)
{
    struct tm start, date[4], end;
    struct tm *datep;
    time_t ts;
    const int wiggle = 2;
    QTestState *qts = m48t59_qtest_start();

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

    cmos_get_date_time(qts, &date[0]);
    cmos_get_date_time(qts, &date[1]);
    cmos_get_date_time(qts, &date[2]);
    cmos_get_date_time(qts, &date[3]);

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
        long date_s, start_s;
        unsigned long diff;

        start.tm_isdst = datep->tm_isdst;

        date_s = (long)mktime(datep);
        start_s = (long)mktime(&start);
        if (date_s < start_s) {
            diff = start_s - date_s;
            g_test_message("RTC is %ld second(s) behind wall-clock", diff);
        } else {
            diff = date_s - start_s;
            g_test_message("RTC is %ld second(s) ahead of wall-clock", diff);
        }

        g_assert_cmpint(diff, <=, wiggle);
    }

    qtest_quit(qts);
}

/* success if no crash or abort */
static void fuzz_registers(void)
{
    unsigned int i;
    QTestState *s = m48t59_qtest_start();

    for (i = 0; i < 1000; i++) {
        uint8_t reg, val;

        reg = (uint8_t)g_test_rand_int_range(0, 16);
        val = (uint8_t)g_test_rand_int_range(0, 256);

        if (reg == 7) {
            /* watchdog setup register, may trigger system reset, skip */
            continue;
        }

        cmos_write(s, reg, val);
        cmos_read(s, reg);
    }

    qtest_quit(s);
}

static void base_setup(void)
{
    const char *arch = qtest_get_arch();

    if (g_str_equal(arch, "sparc")) {
        /* Note: For sparc64, we'd need to map-in the PCI bridge memory first */
        base = 0x71200000;
        base_year = 1968;
        base_machine = "SS-5";
        use_mmio = true;
    } else {
        g_assert_not_reached();
    }
}

int main(int argc, char **argv)
{
    base_setup();

    g_test_init(&argc, &argv, NULL);
    if (qtest_has_machine(base_machine)) {
        if (g_test_slow()) {
            /* Do not run this in timing-sensitive environments */
            qtest_add_func("/rtc/bcd-check-time", bcd_check_time);
        }
        qtest_add_func("/rtc/fuzz-registers", fuzz_registers);
    }
    return g_test_run();
}
