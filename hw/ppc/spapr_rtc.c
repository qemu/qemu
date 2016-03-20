/*
 * QEMU PowerPC pSeries Logical Partition (aka sPAPR) hardware System Emulator
 *
 * RTAS Real Time Clock
 *
 * Copyright (c) 2010-2011 David Gibson, IBM Corporation.
 * Copyright 2014 David Gibson, Red Hat.
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
 *
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "hw/ppc/spapr.h"
#include "qapi-event.h"
#include "qemu/cutils.h"

#define SPAPR_RTC(obj) \
    OBJECT_CHECK(sPAPRRTCState, (obj), TYPE_SPAPR_RTC)

typedef struct sPAPRRTCState sPAPRRTCState;
struct sPAPRRTCState {
    /*< private >*/
    SysBusDevice parent_obj;
    int64_t ns_offset;
};

void spapr_rtc_read(DeviceState *dev, struct tm *tm, uint32_t *ns)
{
    sPAPRRTCState *rtc = SPAPR_RTC(dev);
    int64_t host_ns = qemu_clock_get_ns(rtc_clock);
    int64_t guest_ns;
    time_t guest_s;

    assert(rtc);

    guest_ns = host_ns + rtc->ns_offset;
    guest_s = guest_ns / NANOSECONDS_PER_SECOND;

    if (tm) {
        gmtime_r(&guest_s, tm);
    }
    if (ns) {
        *ns = guest_ns;
    }
}

int spapr_rtc_import_offset(DeviceState *dev, int64_t legacy_offset)
{
    sPAPRRTCState *rtc;

    if (!dev) {
        return -ENODEV;
    }

    rtc = SPAPR_RTC(dev);

    rtc->ns_offset = legacy_offset * NANOSECONDS_PER_SECOND;

    return 0;
}

static void rtas_get_time_of_day(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                 uint32_t token, uint32_t nargs,
                                 target_ulong args,
                                 uint32_t nret, target_ulong rets)
{
    struct tm tm;
    uint32_t ns;

    if ((nargs != 0) || (nret != 8)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    if (!spapr->rtc) {
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }

    spapr_rtc_read(spapr->rtc, &tm, &ns);

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
    rtas_st(rets, 1, tm.tm_year + 1900);
    rtas_st(rets, 2, tm.tm_mon + 1);
    rtas_st(rets, 3, tm.tm_mday);
    rtas_st(rets, 4, tm.tm_hour);
    rtas_st(rets, 5, tm.tm_min);
    rtas_st(rets, 6, tm.tm_sec);
    rtas_st(rets, 7, ns);
}

static void rtas_set_time_of_day(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                 uint32_t token, uint32_t nargs,
                                 target_ulong args,
                                 uint32_t nret, target_ulong rets)
{
    sPAPRRTCState *rtc;
    struct tm tm;
    time_t new_s;
    int64_t host_ns;

    if ((nargs != 7) || (nret != 1)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    if (!spapr->rtc) {
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }

    tm.tm_year = rtas_ld(args, 0) - 1900;
    tm.tm_mon = rtas_ld(args, 1) - 1;
    tm.tm_mday = rtas_ld(args, 2);
    tm.tm_hour = rtas_ld(args, 3);
    tm.tm_min = rtas_ld(args, 4);
    tm.tm_sec = rtas_ld(args, 5);

    new_s = mktimegm(&tm);
    if (new_s == -1) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    /* Generate a monitor event for the change */
    qapi_event_send_rtc_change(qemu_timedate_diff(&tm), &error_abort);

    rtc = SPAPR_RTC(spapr->rtc);

    host_ns = qemu_clock_get_ns(rtc_clock);

    rtc->ns_offset = (new_s * NANOSECONDS_PER_SECOND) - host_ns;

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void spapr_rtc_qom_date(Object *obj, struct tm *current_tm, Error **errp)
{
    spapr_rtc_read(DEVICE(obj), current_tm, NULL);
}

static void spapr_rtc_realize(DeviceState *dev, Error **errp)
{
    sPAPRRTCState *rtc = SPAPR_RTC(dev);
    struct tm tm;
    time_t host_s;
    int64_t rtc_ns;

    /* Initialize the RTAS RTC from host time */

    qemu_get_timedate(&tm, 0);
    host_s = mktimegm(&tm);
    rtc_ns = qemu_clock_get_ns(rtc_clock);
    rtc->ns_offset = host_s * NANOSECONDS_PER_SECOND - rtc_ns;

    object_property_add_tm(OBJECT(rtc), "date", spapr_rtc_qom_date, NULL);
}

static const VMStateDescription vmstate_spapr_rtc = {
    .name = "spapr/rtc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT64(ns_offset, sPAPRRTCState),
        VMSTATE_END_OF_LIST()
    },
};

static void spapr_rtc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = spapr_rtc_realize;
    dc->vmsd = &vmstate_spapr_rtc;

    spapr_rtas_register(RTAS_GET_TIME_OF_DAY, "get-time-of-day",
                        rtas_get_time_of_day);
    spapr_rtas_register(RTAS_SET_TIME_OF_DAY, "set-time-of-day",
                        rtas_set_time_of_day);
}

static const TypeInfo spapr_rtc_info = {
    .name          = TYPE_SPAPR_RTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(sPAPRRTCState),
    .class_init    = spapr_rtc_class_init,
};

static void spapr_rtc_register_types(void)
{
    type_register_static(&spapr_rtc_info);
}
type_init(spapr_rtc_register_types)
