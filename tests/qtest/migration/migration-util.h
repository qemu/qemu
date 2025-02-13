/*
 * QTest migration helpers
 *
 * Copyright (c) 2016-2018 Red Hat, Inc. and/or its affiliates
 *   based on the vhost-user-test.c that is:
 *      Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef MIGRATION_UTIL_H
#define MIGRATION_UTIL_H

#include "libqtest.h"

typedef struct QTestMigrationState {
    bool stop_seen;
    bool resume_seen;
    bool suspend_seen;
    bool suspend_me;
} QTestMigrationState;

bool migrate_watch_for_events(QTestState *who, const char *name,
                              QDict *event, void *opaque);

QDict *migrate_query(QTestState *who);
QDict *migrate_query_not_failed(QTestState *who);

void wait_for_migration_status(QTestState *who,
                               const char *goal, const char **ungoals);

void wait_for_migration_complete(QTestState *who);

void wait_for_migration_fail(QTestState *from, bool allow_active);

char *find_common_machine_version(const char *mtype, const char *var1,
                                  const char *var2);
char *resolve_machine_version(const char *alias, const char *var1,
                              const char *var2);
#ifdef O_DIRECT
bool probe_o_direct_support(const char *tmpfs);
#else
static inline bool probe_o_direct_support(const char *tmpfs)
{
    return false;
}
#endif

bool ufd_version_check(bool *uffd_feature_thread_id);
bool kvm_dirty_ring_supported(void);
void migration_test_add(const char *path, void (*fn)(void));
void migration_test_add_suffix(const char *path, const char *suffix,
                               void (*fn)(void *));
char *migrate_get_connect_uri(QTestState *who);
void migrate_set_ports(QTestState *to, QList *channel_list);

#endif /* MIGRATION_UTIL_H */
