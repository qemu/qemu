/*
 * Copyright (c) 2010-2011 IBM
 *
 * Authors:
 *         Chunqiang Tang <ctang@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

/*=============================================================================
 *  A short description: this is the header of the simulated block device
 *  driver "sim".
 *============================================================================*/

#ifndef __block_sim_h__
#define __block_sim_h__

void enable_block_sim (int print, int64_t _rand_time);
void sim_list_tasks (void);
int sim_task_by_uuid (int64_t uuid);
int sim_all_tasks (void);
int64_t sim_get_time (void);
void *sim_new_timer (void *cb, void *opaque);
void sim_mod_timer (void *ts, int64_t expire_time);
void sim_free_timer (void *ts);
void sim_del_timer (void *ts);
void sim_set_disk_io_return_code (int ret);

#endif
