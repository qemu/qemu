/*
 *  Migration Threads info
 *
 *  Copyright (c) 2022 HUAWEI TECHNOLOGIES CO., LTD.
 *
 *  Authors:
 *  Jiang Jiacheng <jiangjiacheng@huawei.com>
 *
 *  This work is licensed under the terms of the GNU GPL, version 2 or later.
 *  See the COPYING file in the top-level directory.
 */

#include "threadinfo.h"

static QLIST_HEAD(, MigrationThread) migration_threads;

MigrationThread *MigrationThreadAdd(const char *name, int thread_id)
{
    MigrationThread *thread =  g_new0(MigrationThread, 1);
    thread->name = name;
    thread->thread_id = thread_id;

    QLIST_INSERT_HEAD(&migration_threads, thread, node);

    return thread;
}

void MigrationThreadDel(MigrationThread *thread)
{
    if (thread) {
        QLIST_REMOVE(thread, node);
        g_free(thread);
    }
}

MigrationThreadInfoList *qmp_query_migrationthreads(Error **errp)
{
    MigrationThreadInfoList *head = NULL;
    MigrationThreadInfoList **tail = &head;
    MigrationThread *thread = NULL;

    QLIST_FOREACH(thread, &migration_threads, node) {
        MigrationThreadInfo *info = g_new0(MigrationThreadInfo, 1);
        info->name = g_strdup(thread->name);
        info->thread_id = thread->thread_id;

        QAPI_LIST_APPEND(tail, info);
    }

    return head;
}
