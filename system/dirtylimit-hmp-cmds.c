/*
 * HMP commands related to migration dirty page rate limit
 *
 * Copyright (c) 2022 CHINA TELECOM CO.,LTD.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"
#include "qobject/qdict.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "system/dirtylimit.h"

void hmp_cancel_vcpu_dirty_limit(MonitorHMP *hmp, const QDict *qdict)
{
    Monitor *mon = MONITOR(hmp);
    int64_t cpu_index = qdict_get_try_int(qdict, "cpu_index", -1);
    Error *err = NULL;

    qmp_cancel_vcpu_dirty_limit(!!(cpu_index != -1), cpu_index, &err);
    if (err) {
        hmp_handle_error(mon, err);
        return;
    }

    monitor_printf(mon, "[Please use 'info vcpu_dirty_limit' to query "
                   "dirty limit for virtual CPU]\n");
}

void hmp_set_vcpu_dirty_limit(MonitorHMP *hmp, const QDict *qdict)
{
    Monitor *mon = MONITOR(hmp);
    int64_t dirty_rate = qdict_get_int(qdict, "dirty_rate");
    int64_t cpu_index = qdict_get_try_int(qdict, "cpu_index", -1);
    Error *err = NULL;

    if (dirty_rate < 0) {
        error_setg(&err, "invalid dirty page limit %" PRId64, dirty_rate);
        goto out;
    }

    qmp_set_vcpu_dirty_limit(!!(cpu_index != -1), cpu_index, dirty_rate, &err);

out:
    hmp_handle_error(mon, err);
}

void hmp_info_vcpu_dirty_limit(MonitorHMP *hmp, const QDict *qdict)
{
    Monitor *mon = MONITOR(hmp);
    DirtyLimitInfoList *info;
    g_autoptr(DirtyLimitInfoList) head = NULL;
    Error *err = NULL;

    if (!dirtylimit_in_service()) {
        monitor_printf(mon, "Dirty page limit not enabled!\n");
        return;
    }

    head = qmp_query_vcpu_dirty_limit(&err);
    if (err) {
        hmp_handle_error(mon, err);
        return;
    }

    for (info = head; info != NULL; info = info->next) {
        monitor_printf(mon, "vcpu[%"PRIi64"], limit rate %"PRIi64 " (MB/s),"
                            " current rate %"PRIi64 " (MB/s)\n",
                            info->value->cpu_index,
                            info->value->limit_rate,
                            info->value->current_rate);
    }
}
