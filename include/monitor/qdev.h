#ifndef QEMU_QDEV_MONITOR_H
#define QEMU_QDEV_MONITOR_H

#include "hw/qdev-core.h"
#include "qemu/typedefs.h"

/*** monitor commands ***/

void hmp_info_qtree(Monitor *mon, const QDict *qdict);
void hmp_info_qdm(Monitor *mon, const QDict *qdict);
void hmp_info_qom_tree(Monitor *mon, const QDict *dict);
void qmp_device_add(QDict *qdict, QObject **ret_data, Error **errp);

int qdev_device_help(QemuOpts *opts);
DeviceState *qdev_device_add(QemuOpts *opts, Error **errp);

#endif
