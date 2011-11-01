/*
 * Human Monitor Interface
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef HMP_H
#define HMP_H

#include "qemu-common.h"
#include "qapi-types.h"

void hmp_info_name(Monitor *mon);
void hmp_info_version(Monitor *mon);
void hmp_info_kvm(Monitor *mon);
void hmp_info_status(Monitor *mon);
void hmp_info_uuid(Monitor *mon);
void hmp_info_chardev(Monitor *mon);
void hmp_info_mice(Monitor *mon);
void hmp_info_migrate(Monitor *mon);
void hmp_info_cpus(Monitor *mon);
void hmp_info_block(Monitor *mon);
void hmp_info_blockstats(Monitor *mon);
void hmp_info_vnc(Monitor *mon);
void hmp_info_spice(Monitor *mon);
void hmp_info_balloon(Monitor *mon);
void hmp_info_pci(Monitor *mon);
void hmp_quit(Monitor *mon, const QDict *qdict);
void hmp_stop(Monitor *mon, const QDict *qdict);
void hmp_system_reset(Monitor *mon, const QDict *qdict);
void hmp_system_powerdown(Monitor *mon, const QDict *qdict);
void hmp_cpu(Monitor *mon, const QDict *qdict);

#endif
