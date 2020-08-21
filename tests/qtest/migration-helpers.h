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
#ifndef MIGRATION_HELPERS_H_
#define MIGRATION_HELPERS_H_

#include "libqos/libqtest.h"

extern bool got_stop;

GCC_FMT_ATTR(3, 4)
QDict *wait_command_fd(QTestState *who, int fd, const char *command, ...);

GCC_FMT_ATTR(2, 3)
QDict *wait_command(QTestState *who, const char *command, ...);

GCC_FMT_ATTR(3, 4)
void migrate_qmp(QTestState *who, const char *uri, const char *fmt, ...);

QDict *migrate_query(QTestState *who);

void wait_for_migration_status(QTestState *who,
                               const char *goal, const char **ungoals);

void wait_for_migration_complete(QTestState *who);

void wait_for_migration_fail(QTestState *from, bool allow_active);

#endif /* MIGRATION_HELPERS_H_ */
