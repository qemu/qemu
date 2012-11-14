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
#include "qdict.h"

void hmp_info_name(Monitor *mon);
void hmp_info_version(Monitor *mon);
void hmp_info_kvm(Monitor *mon);
void hmp_info_status(Monitor *mon);
void hmp_info_uuid(Monitor *mon);
void hmp_info_chardev(Monitor *mon);
void hmp_info_mice(Monitor *mon);
void hmp_info_migrate(Monitor *mon);
void hmp_info_migrate_capabilities(Monitor *mon);
void hmp_info_migrate_cache_size(Monitor *mon);
void hmp_info_cpus(Monitor *mon);
void hmp_info_block(Monitor *mon);
void hmp_info_blockstats(Monitor *mon);
void hmp_info_vnc(Monitor *mon);
void hmp_info_spice(Monitor *mon);
void hmp_info_balloon(Monitor *mon);
void hmp_info_pci(Monitor *mon);
void hmp_info_block_jobs(Monitor *mon);
void hmp_quit(Monitor *mon, const QDict *qdict);
void hmp_stop(Monitor *mon, const QDict *qdict);
void hmp_system_reset(Monitor *mon, const QDict *qdict);
void hmp_system_powerdown(Monitor *mon, const QDict *qdict);
void hmp_cpu(Monitor *mon, const QDict *qdict);
void hmp_memsave(Monitor *mon, const QDict *qdict);
void hmp_pmemsave(Monitor *mon, const QDict *qdict);
void hmp_cont(Monitor *mon, const QDict *qdict);
void hmp_system_wakeup(Monitor *mon, const QDict *qdict);
void hmp_inject_nmi(Monitor *mon, const QDict *qdict);
void hmp_set_link(Monitor *mon, const QDict *qdict);
void hmp_block_passwd(Monitor *mon, const QDict *qdict);
void hmp_balloon(Monitor *mon, const QDict *qdict);
void hmp_block_resize(Monitor *mon, const QDict *qdict);
void hmp_snapshot_blkdev(Monitor *mon, const QDict *qdict);
void hmp_drive_mirror(Monitor *mon, const QDict *qdict);
void hmp_migrate_cancel(Monitor *mon, const QDict *qdict);
void hmp_migrate_set_downtime(Monitor *mon, const QDict *qdict);
void hmp_migrate_set_speed(Monitor *mon, const QDict *qdict);
void hmp_migrate_set_capability(Monitor *mon, const QDict *qdict);
void hmp_migrate_set_cache_size(Monitor *mon, const QDict *qdict);
void hmp_set_password(Monitor *mon, const QDict *qdict);
void hmp_expire_password(Monitor *mon, const QDict *qdict);
void hmp_eject(Monitor *mon, const QDict *qdict);
void hmp_change(Monitor *mon, const QDict *qdict);
void hmp_block_set_io_throttle(Monitor *mon, const QDict *qdict);
void hmp_block_stream(Monitor *mon, const QDict *qdict);
void hmp_block_job_set_speed(Monitor *mon, const QDict *qdict);
void hmp_block_job_cancel(Monitor *mon, const QDict *qdict);
void hmp_block_job_pause(Monitor *mon, const QDict *qdict);
void hmp_block_job_resume(Monitor *mon, const QDict *qdict);
void hmp_block_job_complete(Monitor *mon, const QDict *qdict);
void hmp_migrate(Monitor *mon, const QDict *qdict);
void hmp_device_del(Monitor *mon, const QDict *qdict);
void hmp_dump_guest_memory(Monitor *mon, const QDict *qdict);
void hmp_netdev_add(Monitor *mon, const QDict *qdict);
void hmp_netdev_del(Monitor *mon, const QDict *qdict);
void hmp_getfd(Monitor *mon, const QDict *qdict);
void hmp_closefd(Monitor *mon, const QDict *qdict);
void hmp_send_key(Monitor *mon, const QDict *qdict);
void hmp_screen_dump(Monitor *mon, const QDict *qdict);
void hmp_nbd_server_start(Monitor *mon, const QDict *qdict);
void hmp_nbd_server_add(Monitor *mon, const QDict *qdict);
void hmp_nbd_server_stop(Monitor *mon, const QDict *qdict);

#endif
