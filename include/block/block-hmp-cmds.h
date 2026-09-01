/*
 * HMP commands related to the block layer
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2020 Red Hat, Inc.
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef BLOCK_BLOCK_HMP_CMDS_H
#define BLOCK_BLOCK_HMP_CMDS_H

#include "qemu/coroutine.h"
#include "monitor/hmp.h"

void hmp_drive_add(MonitorHMP *hmp, const QDict *qdict);

void hmp_commit(MonitorHMP *hmp, const QDict *qdict);
void hmp_drive_del(MonitorHMP *hmp, const QDict *qdict);

void hmp_drive_mirror(MonitorHMP *hmp, const QDict *qdict);
void hmp_drive_backup(MonitorHMP *hmp, const QDict *qdict);

void hmp_block_job_set_speed(MonitorHMP *hmp, const QDict *qdict);
void hmp_block_job_cancel(MonitorHMP *hmp, const QDict *qdict);
void hmp_block_job_pause(MonitorHMP *hmp, const QDict *qdict);
void hmp_block_job_resume(MonitorHMP *hmp, const QDict *qdict);
void hmp_block_job_complete(MonitorHMP *hmp, const QDict *qdict);

void hmp_snapshot_blkdev(MonitorHMP *hmp, const QDict *qdict);
void hmp_snapshot_blkdev_internal(MonitorHMP *hmp, const QDict *qdict);
void hmp_snapshot_delete_blkdev_internal(MonitorHMP *hmp, const QDict *qdict);

void hmp_nbd_server_start(MonitorHMP *hmp, const QDict *qdict);
void hmp_nbd_server_add(MonitorHMP *hmp, const QDict *qdict);
void hmp_nbd_server_remove(MonitorHMP *hmp, const QDict *qdict);
void hmp_nbd_server_stop(MonitorHMP *hmp, const QDict *qdict);

void coroutine_fn hmp_block_resize(MonitorHMP *hmp, const QDict *qdict);
void hmp_block_stream(MonitorHMP *hmp, const QDict *qdict);
void hmp_block_passwd(MonitorHMP *hmp, const QDict *qdict);
void hmp_block_set_io_throttle(MonitorHMP *hmp, const QDict *qdict);
void hmp_eject(MonitorHMP *hmp, const QDict *qdict);

void hmp_qemu_io(MonitorHMP *hmp, const QDict *qdict);

void hmp_info_block(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_blockstats(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_block_jobs(MonitorHMP *hmp, const QDict *qdict);
void hmp_info_snapshots(MonitorHMP *hmp, const QDict *qdict);

#endif
