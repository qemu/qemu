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

#include "qemu/osdep.h"
#include "qemu/ctype.h"
#include "qapi/qmp/qjson.h"

#include "migration-helpers.h"

/*
 * Number of seconds we wait when looking for migration
 * status changes, to avoid test suite hanging forever
 * when things go wrong. Needs to be higher enough to
 * avoid false positives on loaded hosts.
 */
#define MIGRATION_STATUS_WAIT_TIMEOUT 120

bool migrate_watch_for_stop(QTestState *who, const char *name,
                            QDict *event, void *opaque)
{
    bool *seen = opaque;

    if (g_str_equal(name, "STOP")) {
        *seen = true;
        return true;
    }

    return false;
}

bool migrate_watch_for_resume(QTestState *who, const char *name,
                              QDict *event, void *opaque)
{
    bool *seen = opaque;

    if (g_str_equal(name, "RESUME")) {
        *seen = true;
        return true;
    }

    return false;
}

void migrate_qmp_fail(QTestState *who, const char *uri, const char *fmt, ...)
{
    va_list ap;
    QDict *args, *err;

    va_start(ap, fmt);
    args = qdict_from_vjsonf_nofail(fmt, ap);
    va_end(ap);

    g_assert(!qdict_haskey(args, "uri"));
    qdict_put_str(args, "uri", uri);

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
void migrate_qmp(QTestState *who, const char *uri, const char *fmt, ...)
{
    va_list ap;
    QDict *args;

    va_start(ap, fmt);
    args = qdict_from_vjsonf_nofail(fmt, ap);
    va_end(ap);

    g_assert(!qdict_haskey(args, "uri"));
    qdict_put_str(args, "uri", uri);

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

void migrate_incoming_qmp(QTestState *to, const char *uri, const char *fmt, ...)
{
    va_list ap;
    QDict *args, *rsp, *data;

    va_start(ap, fmt);
    args = qdict_from_vjsonf_nofail(fmt, ap);
    va_end(ap);

    g_assert(!qdict_haskey(args, "uri"));
    qdict_put_str(args, "uri", uri);

    migrate_set_capability(to, "events", true);

    rsp = qtest_qmp(to, "{ 'execute': 'migrate-incoming', 'arguments': %p}",
                    args);
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);

    rsp = qtest_qmp_eventwait_ref(to, "MIGRATION");
    g_assert(qdict_haskey(rsp, "data"));

    data = qdict_get_qdict(rsp, "data");
    g_assert(qdict_haskey(data, "status"));
    g_assert_cmpstr(qdict_get_str(data, "status"), ==, "setup");

    qobject_unref(rsp);
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
static gchar *migrate_query_status(QTestState *who)
{
    QDict *rsp_return = migrate_query(who);
    gchar *status = g_strdup(qdict_get_str(rsp_return, "status"));

    g_assert(status);
    qobject_unref(rsp_return);

    return status;
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

char *find_common_machine_version(const char *mtype, const char *var1,
                                  const char *var2)
{
    g_autofree char *type1 = qtest_resolve_machine_alias(var1, mtype);
    g_autofree char *type2 = qtest_resolve_machine_alias(var2, mtype);

    g_assert(type1 && type2);

    if (g_str_equal(type1, type2)) {
        /* either can be used */
        return g_strdup(type1);
    }

    if (qtest_has_machine_with_env(var2, type1)) {
        return g_strdup(type1);
    }

    if (qtest_has_machine_with_env(var1, type2)) {
        return g_strdup(type2);
    }

    g_test_message("No common machine version for machine type '%s' between "
                   "binaries %s and %s", mtype, getenv(var1), getenv(var2));
    g_assert_not_reached();
}

char *resolve_machine_version(const char *alias, const char *var1,
                              const char *var2)
{
    const char *mname = g_getenv("QTEST_QEMU_MACHINE_TYPE");
    g_autofree char *machine_name = NULL;

    if (mname) {
        const char *dash = strrchr(mname, '-');
        const char *dot = strrchr(mname, '.');

        machine_name = g_strdup(mname);

        if (dash && dot) {
            assert(qtest_has_machine(machine_name));
            return g_steal_pointer(&machine_name);
        }
        /* else: probably an alias, let it be resolved below */
    } else {
        /* use the hardcoded alias */
        machine_name = g_strdup(alias);
    }

    return find_common_machine_version(machine_name, var1, var2);
}
