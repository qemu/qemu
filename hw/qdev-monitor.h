#ifndef QEMU_QDEV_MONITOR_H
#define QEMU_QDEV_MONITOR_H

#include "qdev-core.h"
#include "monitor.h"

/*** monitor commands ***/

void do_info_qtree(Monitor *mon);
void do_info_qdm(Monitor *mon);
int do_device_add(Monitor *mon, const QDict *qdict, QObject **ret_data);
int do_device_del(Monitor *mon, const QDict *qdict, QObject **ret_data);
int qdev_device_help(QemuOpts *opts);
DeviceState *qdev_device_add(QemuOpts *opts);

#endif
