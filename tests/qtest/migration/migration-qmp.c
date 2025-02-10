/*
 * QTest QMP helpers for migration
 *
 * Copyright (c) 2016-2018 Red Hat, Inc. and/or its affiliates
 *   based on the vhost-user-test.c that is:
 *      Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "migration-qmp.h"
#include "migration-util.h"
#include "qapi/error.h"
#include "qapi/qapi-types-migration.h"
#include "qapi/qapi-visit-migration.h"
#include "qobject/qdict.h"
#include "qobject/qjson.h"
#include "qobject/qlist.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"

/*
 * Number of seconds we wait when looking for migration
 * status changes, to avoid test suite hanging forever
 * when things go wrong. Needs to be higher enough to
 * avoid false positives on loaded hosts.
 */
#define MIGRATION_STATUS_WAIT_TIMEOUT 120

/*
 * Wait for a "MIGRATION" event.  This is what Libvirt uses to track
 * migration status changes.
 */
void migration_event_wait(QTestState *s, const char *target)
{
    QDict *response, *data;
    const char *status;
    bool found;

    do {
        response = qtest_qmp_eventwait_ref(s, "MIGRATION");
        data = qdict_get_qdict(response, "data");
        g_assert(data);
        status = qdict_get_str(data, "status");
        found = (strcmp(status, target) == 0);
        qobject_unref(response);
    } while (!found);
}

/*
 * Convert a string representing a single channel to an object.
 * @str may be in JSON or dotted keys format.
 */
QObject *migrate_str_to_channel(const char *str)
{
    Visitor *v;
    MigrationChannel *channel;
    QObject *obj;

    /* Create the channel */
    v = qobject_input_visitor_new_str(str, "channel-type", &error_abort);
    visit_type_MigrationChannel(v, NULL, &channel, &error_abort);
    visit_free(v);

    /* Create the object */
    v = qobject_output_visitor_new(&obj);
    visit_type_MigrationChannel(v, NULL, &channel, &error_abort);
    visit_complete(v, &obj);
    visit_free(v);

    qapi_free_MigrationChannel(channel);
    return obj;
}

void migrate_qmp_fail(QTestState *who, const char *uri,
                      QObject *channels, const char *fmt, ...)
{
    va_list ap;
    QDict *args, *err;

    va_start(ap, fmt);
    args = qdict_from_vjsonf_nofail(fmt, ap);
    va_end(ap);

    g_assert(!qdict_haskey(args, "uri"));
    if (uri) {
        qdict_put_str(args, "uri", uri);
    }

    g_assert(!qdict_haskey(args, "channels"));
    if (channels) {
        qdict_put_obj(args, "channels", channels);
    }

    err = qtest_qmp_assert_failure_ref(
        who, "{ 'execute': 'migrate', 'arguments': %p}", args);

    g_assert(qdict_haskey(err, "desc"));

    qobject_unref(err);
}

/*
 * Send QMP command "migrate".
 * Arguments are built from @fmt... (formatted like
 * qobject_from_jsonf_nofail()) with "uri": @uri spliced in.
 */
void migrate_qmp(QTestState *who, QTestState *to, const char *uri,
                 QObject *channels, const char *fmt, ...)
{
    va_list ap;
    QDict *args;
    g_autofree char *connect_uri = NULL;

    va_start(ap, fmt);
    args = qdict_from_vjsonf_nofail(fmt, ap);
    va_end(ap);

    g_assert(!qdict_haskey(args, "uri"));
    if (uri) {
        qdict_put_str(args, "uri", uri);
    } else if (!channels) {
        connect_uri = migrate_get_connect_uri(to);
        qdict_put_str(args, "uri", connect_uri);
    }

    g_assert(!qdict_haskey(args, "channels"));
    if (channels) {
        QList *channel_list = qobject_to(QList, channels);
        migrate_set_ports(to, channel_list);
        qdict_put_obj(args, "channels", channels);
    }

    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate', 'arguments': %p}", args);
}

void migrate_set_capability(QTestState *who, const char *capability,
                            bool value)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate-set-capabilities',"
                             "'arguments': { "
                             "'capabilities': [ { "
                             "'capability': %s, 'state': %i } ] } }",
                             capability, value);
}

void migrate_incoming_qmp(QTestState *to, const char *uri, QObject *channels,
                          const char *fmt, ...)
{
    va_list ap;
    QDict *args, *rsp;

    va_start(ap, fmt);
    args = qdict_from_vjsonf_nofail(fmt, ap);
    va_end(ap);

    g_assert(!qdict_haskey(args, "uri"));
    if (uri) {
        qdict_put_str(args, "uri", uri);
    }

    g_assert(!qdict_haskey(args, "channels"));
    if (channels) {
        qdict_put_obj(args, "channels", channels);
    }

    /* This function relies on the event to work, make sure it's enabled */
    migrate_set_capability(to, "events", true);

    rsp = qtest_qmp(to, "{ 'execute': 'migrate-incoming', 'arguments': %p}",
                    args);

    if (!qdict_haskey(rsp, "return")) {
        g_autoptr(GString) s = qobject_to_json_pretty(QOBJECT(rsp), true);
        g_test_message("%s", s->str);
    }

    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);

    migration_event_wait(to, "setup");
}

static bool check_migration_status(QTestState *who, const char *goal,
                                   const char **ungoals)
{
    bool ready;
    char *current_status;
    const char **ungoal;

    current_status = migrate_query_status(who);
    ready = strcmp(current_status, goal) == 0;
    if (!ungoals) {
        g_assert_cmpstr(current_status, !=, "failed");
        /*
         * If looking for a state other than completed,
         * completion of migration would cause the test to
         * hang.
         */
        if (strcmp(goal, "completed") != 0) {
            g_assert_cmpstr(current_status, !=, "completed");
        }
    } else {
        for (ungoal = ungoals; *ungoal; ungoal++) {
            g_assert_cmpstr(current_status, !=,  *ungoal);
        }
    }
    g_free(current_status);
    return ready;
}

void wait_for_migration_status(QTestState *who,
                               const char *goal, const char **ungoals)
{
    g_test_timer_start();
    while (!check_migration_status(who, goal, ungoals)) {
        usleep(1000);

        g_assert(g_test_timer_elapsed() < MIGRATION_STATUS_WAIT_TIMEOUT);
    }
}

void wait_for_migration_complete(QTestState *who)
{
    wait_for_migration_status(who, "completed", NULL);
}

void wait_for_migration_fail(QTestState *from, bool allow_active)
{
    g_test_timer_start();
    QDict *rsp_return;
    char *status;
    bool failed;

    do {
        status = migrate_query_status(from);
        bool result = !strcmp(status, "setup") || !strcmp(status, "failed") ||
            (allow_active && !strcmp(status, "active"));
        if (!result) {
            fprintf(stderr, "%s: unexpected status status=%s allow_active=%d\n",
                    __func__, status, allow_active);
        }
        g_assert(result);
        failed = !strcmp(status, "failed");
        g_free(status);

        g_assert(g_test_timer_elapsed() < MIGRATION_STATUS_WAIT_TIMEOUT);
    } while (!failed);

    /* Is the machine currently running? */
    rsp_return = qtest_qmp_assert_success_ref(from,
                                              "{ 'execute': 'query-status' }");
    g_assert(qdict_haskey(rsp_return, "running"));
    g_assert(qdict_get_bool(rsp_return, "running"));
    qobject_unref(rsp_return);
}

void wait_for_stop(QTestState *who, QTestMigrationState *state)
{
    if (!state->stop_seen) {
        qtest_qmp_eventwait(who, "STOP");
    }
}

void wait_for_resume(QTestState *who, QTestMigrationState *state)
{
    if (!state->resume_seen) {
        qtest_qmp_eventwait(who, "RESUME");
    }
}

void wait_for_suspend(QTestState *who, QTestMigrationState *state)
{
    if (state->suspend_me && !state->suspend_seen) {
        qtest_qmp_eventwait(who, "SUSPEND");
    }
}

/*
 * Note: caller is responsible to free the returned object via
 * qobject_unref() after use
 */
QDict *migrate_query(QTestState *who)
{
    return qtest_qmp_assert_success_ref(who, "{ 'execute': 'query-migrate' }");
}

QDict *migrate_query_not_failed(QTestState *who)
{
    const char *status;
    QDict *rsp = migrate_query(who);
    status = qdict_get_str(rsp, "status");
    if (g_str_equal(status, "failed")) {
        g_printerr("query-migrate shows failed migration: %s\n",
                   qdict_get_str(rsp, "error-desc"));
    }
    g_assert(!g_str_equal(status, "failed"));
    return rsp;
}

/*
 * Note: caller is responsible to free the returned object via
 * g_free() after use
 */
gchar *migrate_query_status(QTestState *who)
{
    QDict *rsp_return = migrate_query(who);
    gchar *status = g_strdup(qdict_get_str(rsp_return, "status"));

    g_assert(status);
    qobject_unref(rsp_return);

    return status;
}

int64_t read_ram_property_int(QTestState *who, const char *property)
{
    QDict *rsp_return, *rsp_ram;
    int64_t result;

    rsp_return = migrate_query_not_failed(who);
    if (!qdict_haskey(rsp_return, "ram")) {
        /* Still in setup */
        result = 0;
    } else {
        rsp_ram = qdict_get_qdict(rsp_return, "ram");
        result = qdict_get_try_int(rsp_ram, property, 0);
    }
    qobject_unref(rsp_return);
    return result;
}

int64_t read_migrate_property_int(QTestState *who, const char *property)
{
    QDict *rsp_return;
    int64_t result;

    rsp_return = migrate_query_not_failed(who);
    result = qdict_get_try_int(rsp_return, property, 0);
    qobject_unref(rsp_return);
    return result;
}

uint64_t get_migration_pass(QTestState *who)
{
    return read_ram_property_int(who, "dirty-sync-count");
}

void read_blocktime(QTestState *who)
{
    QDict *rsp_return;

    rsp_return = migrate_query_not_failed(who);
    g_assert(qdict_haskey(rsp_return, "postcopy-blocktime"));
    qobject_unref(rsp_return);
}

/*
 * Wait for two changes in the migration pass count, but bail if we stop.
 */
void wait_for_migration_pass(QTestState *who, QTestMigrationState *src_state)
{
    uint64_t pass, prev_pass = 0, changes = 0;

    while (changes < 2 && !src_state->stop_seen && !src_state->suspend_seen) {
        usleep(1000);
        pass = get_migration_pass(who);
        changes += (pass != prev_pass);
        prev_pass = pass;
    }
}

static long long migrate_get_parameter_int(QTestState *who,
                                           const char *parameter)
{
    QDict *rsp;
    long long result;

    rsp = qtest_qmp_assert_success_ref(
        who, "{ 'execute': 'query-migrate-parameters' }");
    result = qdict_get_int(rsp, parameter);
    qobject_unref(rsp);
    return result;
}

static void migrate_check_parameter_int(QTestState *who, const char *parameter,
                                        long long value)
{
    long long result;

    result = migrate_get_parameter_int(who, parameter);
    g_assert_cmpint(result, ==, value);
}

void migrate_set_parameter_int(QTestState *who, const char *parameter,
                               long long value)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate-set-parameters',"
                             "'arguments': { %s: %lld } }",
                             parameter, value);
    migrate_check_parameter_int(who, parameter, value);
}

static char *migrate_get_parameter_str(QTestState *who, const char *parameter)
{
    QDict *rsp;
    char *result;

    rsp = qtest_qmp_assert_success_ref(
        who, "{ 'execute': 'query-migrate-parameters' }");
    result = g_strdup(qdict_get_str(rsp, parameter));
    qobject_unref(rsp);
    return result;
}

static void migrate_check_parameter_str(QTestState *who, const char *parameter,
                                        const char *value)
{
    g_autofree char *result = migrate_get_parameter_str(who, parameter);
    g_assert_cmpstr(result, ==, value);
}

void migrate_set_parameter_str(QTestState *who, const char *parameter,
                               const char *value)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate-set-parameters',"
                             "'arguments': { %s: %s } }",
                             parameter, value);
    migrate_check_parameter_str(who, parameter, value);
}

static long long migrate_get_parameter_bool(QTestState *who,
                                            const char *parameter)
{
    QDict *rsp;
    int result;

    rsp = qtest_qmp_assert_success_ref(
        who, "{ 'execute': 'query-migrate-parameters' }");
    result = qdict_get_bool(rsp, parameter);
    qobject_unref(rsp);
    return !!result;
}

static void migrate_check_parameter_bool(QTestState *who, const char *parameter,
                                         int value)
{
    int result;

    result = migrate_get_parameter_bool(who, parameter);
    g_assert_cmpint(result, ==, value);
}

void migrate_set_parameter_bool(QTestState *who, const char *parameter,
                                int value)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate-set-parameters',"
                             "'arguments': { %s: %i } }",
                             parameter, value);
    migrate_check_parameter_bool(who, parameter, value);
}

void migrate_ensure_non_converge(QTestState *who)
{
    /* Can't converge with 1ms downtime + 3 mbs bandwidth limit */
    migrate_set_parameter_int(who, "max-bandwidth", 3 * 1000 * 1000);
    migrate_set_parameter_int(who, "downtime-limit", 1);
}

void migrate_ensure_converge(QTestState *who)
{
    /* Should converge with 30s downtime + 1 gbs bandwidth limit */
    migrate_set_parameter_int(who, "max-bandwidth", 1 * 1000 * 1000 * 1000);
    migrate_set_parameter_int(who, "downtime-limit", 30 * 1000);
}

void migrate_pause(QTestState *who)
{
    qtest_qmp_assert_success(who, "{ 'execute': 'migrate-pause' }");
}

void migrate_continue(QTestState *who, const char *state)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate-continue',"
                             "  'arguments': { 'state': %s } }",
                             state);
}

void migrate_recover(QTestState *who, const char *uri)
{
    qtest_qmp_assert_success(who,
                             "{ 'exec-oob': 'migrate-recover', "
                             "  'id': 'recover-cmd', "
                             "  'arguments': { 'uri': %s } }",
                             uri);
}

void migrate_cancel(QTestState *who)
{
    qtest_qmp_assert_success(who, "{ 'execute': 'migrate_cancel' }");
}

void migrate_postcopy_start(QTestState *from, QTestState *to,
                            QTestMigrationState *src_state)
{
    qtest_qmp_assert_success(from, "{ 'execute': 'migrate-start-postcopy' }");

    wait_for_stop(from, src_state);
    qtest_qmp_eventwait(to, "RESUME");
}
