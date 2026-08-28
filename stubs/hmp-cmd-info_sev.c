/*
 * Human Monitor 'info sev' stub (CONFIG_SEV)
 *
 * Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"

void hmp_info_sev(MonitorHMP *hmp, const QDict *qdict)
{
    Monitor *mon = MONITOR(hmp);
    monitor_printf(mon, "SEV is not available in this QEMU\n");
}
