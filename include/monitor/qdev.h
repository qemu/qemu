#ifndef QEMU_QDEV_MONITOR_H
#define QEMU_QDEV_MONITOR_H

#include "hw/qdev-core.h"
#include "monitor/monitor.h"

/*** monitor commands ***/

void do_info_qtree(Monitor *mon, const QDict *qdict);
void do_info_qdm(Monitor *mon, const QDict *qdict);
int do_device_add(Monitor *mon, const QDict *qdict, QObject **ret_data);
int qdev_device_help(QemuOpts *opts);
DeviceState *qdev_device_add(QemuOpts *opts);

#endif
