/*
 * QEMU MOS6522 VIA stubs
 *
 * Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"

#ifdef CONFIG_HMP
void hmp_info_via(MonitorHMP *hmp, const QDict *qdict)
{
    monitor_hmp_printf(hmp, "MOS6522 VIA is not available in this QEMU\n");
}
#endif
