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

#ifndef MIGRATION_HELPERS_H
#define MIGRATION_HELPERS_H

#include "libqtest.h"

bool migrate_watch_for_stop(QTestState *who, const char *name,
                            QDict *event, void *opaque);
bool migrate_watch_for_resume(QTestState *who, const char *name,
                              QDict *event, void *opaque);

G_GNUC_PRINTF(3, 4)
void migrate_qmp(QTestState *who, const char *uri, const char *fmt, ...);

G_GNUC_PRINTF(3, 4)
void migrate_incoming_qmp(QTestState *who, const char *uri,
                          const char *fmt, ...);

G_GNUC_PRINTF(3, 4)
void migrate_qmp_fail(QTestState *who, const char *uri, const char *fmt, ...);

void migrate_set_capability(QTestState *who, const char *capability,
                            bool value);

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
#endif /* MIGRATION_HELPERS_H */
