/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MIGRATION_QMP_H
#define MIGRATION_QMP_H

#include "migration-util.h"

QObject *migrate_str_to_channel(const char *str);

G_GNUC_PRINTF(4, 5)
void migrate_qmp_fail(QTestState *who, const char *uri,
                      QObject *channels, const char *fmt, ...);

G_GNUC_PRINTF(5, 6)
void migrate_qmp(QTestState *who, QTestState *to, const char *uri,
                 QObject *channels, const char *fmt, ...);

G_GNUC_PRINTF(4, 5)
void migrate_incoming_qmp(QTestState *who, const char *uri,
                          QObject *channels, const char *fmt, ...);

void migration_event_wait(QTestState *s, const char *target);
void migrate_set_capability(QTestState *who, const char *capability,
                            bool value);
int64_t read_ram_property_int(QTestState *who, const char *property);
void migrate_set_parameter_int(QTestState *who, const char *parameter,
                               long long value);
void wait_for_stop(QTestState *who, QTestMigrationState *state);
void wait_for_resume(QTestState *who, QTestMigrationState *state);
void wait_for_suspend(QTestState *who, QTestMigrationState *state);
gchar *migrate_query_status(QTestState *who);
int64_t read_migrate_property_int(QTestState *who, const char *property);
uint64_t get_migration_pass(QTestState *who);
void read_blocktime(QTestState *who);
void wait_for_migration_pass(QTestState *who, QTestMigrationState *src_state);
void migrate_set_parameter_str(QTestState *who, const char *parameter,
                               const char *value);
void migrate_set_parameter_bool(QTestState *who, const char *parameter,
                                int value);
void migrate_ensure_non_converge(QTestState *who);
void migrate_ensure_converge(QTestState *who);
void migrate_pause(QTestState *who);
void migrate_continue(QTestState *who, const char *state);
void migrate_recover(QTestState *who, const char *uri);
void migrate_cancel(QTestState *who);
void migrate_postcopy_start(QTestState *from, QTestState *to,
                            QTestMigrationState *src_state);

#endif /* MIGRATION_QMP_H */
