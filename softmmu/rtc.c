/*
 * RTC configuration and clock read
 *
 * Copyright (c) 2003-2020 QEMU contributors
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

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "sysemu/replay.h"
#include "sysemu/sysemu.h"
#include "sysemu/rtc.h"
#include "hw/rtc/mc146818rtc.h"

static enum {
    RTC_BASE_UTC,
    RTC_BASE_LOCALTIME,
    RTC_BASE_DATETIME,
} rtc_base_type = RTC_BASE_UTC;
static time_t rtc_ref_start_datetime;
static int rtc_realtime_clock_offset; /* used only with QEMU_CLOCK_REALTIME */
static int rtc_host_datetime_offset = -1; /* valid & used only with
                                             RTC_BASE_DATETIME */
QEMUClockType rtc_clock;
/***********************************************************/
/* RTC reference time/date access */
static time_t qemu_ref_timedate(QEMUClockType clock)
{
    time_t value = qemu_clock_get_ms(clock) / 1000;
    switch (clock) {
    case QEMU_CLOCK_REALTIME:
        value -= rtc_realtime_clock_offset;
        /* fall through */
    case QEMU_CLOCK_VIRTUAL:
        value += rtc_ref_start_datetime;
        break;
    case QEMU_CLOCK_HOST:
        if (rtc_base_type == RTC_BASE_DATETIME) {
            value -= rtc_host_datetime_offset;
        }
        break;
    default:
        assert(0);
    }
    return value;
}

void qemu_get_timedate(struct tm *tm, int offset)
{
    time_t ti = qemu_ref_timedate(rtc_clock);

    ti += offset;

    switch (rtc_base_type) {
    case RTC_BASE_DATETIME:
    case RTC_BASE_UTC:
        gmtime_r(&ti, tm);
        break;
    case RTC_BASE_LOCALTIME:
        localtime_r(&ti, tm);
        break;
    }
}

int qemu_timedate_diff(struct tm *tm)
{
    time_t seconds;

    switch (rtc_base_type) {
    case RTC_BASE_DATETIME:
    case RTC_BASE_UTC:
        seconds = mktimegm(tm);
        break;
    case RTC_BASE_LOCALTIME:
    {
        struct tm tmp = *tm;
        tmp.tm_isdst = -1; /* use timezone to figure it out */
        seconds = mktime(&tmp);
        break;
    }
    default:
        abort();
    }

    return seconds - qemu_ref_timedate(QEMU_CLOCK_HOST);
}

static void configure_rtc_base_datetime(const char *startdate)
{
    time_t rtc_start_datetime;
    struct tm tm;

    if (sscanf(startdate, "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon,
               &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
        /* OK */
    } else if (sscanf(startdate, "%d-%d-%d",
                      &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) {
        tm.tm_hour = 0;
        tm.tm_min = 0;
        tm.tm_sec = 0;
    } else {
        goto date_fail;
    }
    tm.tm_year -= 1900;
    tm.tm_mon--;
    rtc_start_datetime = mktimegm(&tm);
    if (rtc_start_datetime == -1) {
    date_fail:
        error_report("invalid datetime format");
        error_printf("valid formats: "
                     "'2006-06-17T16:01:21' or '2006-06-17'\n");
        exit(1);
    }
    rtc_host_datetime_offset = rtc_ref_start_datetime - rtc_start_datetime;
    rtc_ref_start_datetime = rtc_start_datetime;
}

void configure_rtc(QemuOpts *opts)
{
    const char *value;

    /* Set defaults */
    rtc_clock = QEMU_CLOCK_HOST;
    rtc_ref_start_datetime = qemu_clock_get_ms(QEMU_CLOCK_HOST) / 1000;
    rtc_realtime_clock_offset = qemu_clock_get_ms(QEMU_CLOCK_REALTIME) / 1000;

    value = qemu_opt_get(opts, "base");
    if (value) {
        if (!strcmp(value, "utc")) {
            rtc_base_type = RTC_BASE_UTC;
        } else if (!strcmp(value, "localtime")) {
            rtc_base_type = RTC_BASE_LOCALTIME;
            replay_add_blocker("-rtc base=localtime");
        } else {
            rtc_base_type = RTC_BASE_DATETIME;
            configure_rtc_base_datetime(value);
        }
    }
    value = qemu_opt_get(opts, "clock");
    if (value) {
        if (!strcmp(value, "host")) {
            rtc_clock = QEMU_CLOCK_HOST;
        } else if (!strcmp(value, "rt")) {
            rtc_clock = QEMU_CLOCK_REALTIME;
        } else if (!strcmp(value, "vm")) {
            rtc_clock = QEMU_CLOCK_VIRTUAL;
        } else {
            error_report("invalid option value '%s'", value);
            exit(1);
        }
    }
    value = qemu_opt_get(opts, "driftfix");
    if (value) {
        if (!strcmp(value, "slew")) {
            object_register_sugar_prop(TYPE_MC146818_RTC,
                                       "lost_tick_policy",
                                       "slew",
                                       false);
            if (!object_class_by_name(TYPE_MC146818_RTC)) {
                warn_report("driftfix 'slew' is not available with this machine");
            }
        } else if (!strcmp(value, "none")) {
            /* discard is default */
        } else {
            error_report("invalid option value '%s'", value);
            exit(1);
        }
    }
}
