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
#include "qapi/qmp/qjson.h"

#include "migration-helpers.h"

/*
 * Number of seconds we wait when looking for migration
 * status changes, to avoid test suite hanging forever
 * when things go wrong. Needs to be higher enough to
 * avoid false positives on loaded hosts.
 */
#define MIGRATION_STATUS_WAIT_TIMEOUT 120

bool got_stop;

static void check_stop_event(QTestState *who)
{
    QDict *event = qtest_qmp_event_ref(who, "STOP");
    if (event) {
        got_stop = true;
        qobject_unref(event);
    }
}

#ifndef _WIN32
/*
 * Events can get in the way of responses we are actually waiting for.
 */
QDict *wait_command_fd(QTestState *who, int fd, const char *command, ...)
{
    va_list ap;
    QDict *resp, *ret;

    va_start(ap, command);
    qtest_qmp_vsend_fds(who, &fd, 1, command, ap);
    va_end(ap);

    resp = qtest_qmp_receive(who);
    check_stop_event(who);

    g_assert(!qdict_haskey(resp, "error"));
    g_assert(qdict_haskey(resp, "return"));

    ret = qdict_get_qdict(resp, "return");
    qobject_ref(ret);
    qobject_unref(resp);

    return ret;
}
#endif

/*
 * Events can get in the way of responses we are actually waiting for.
 */
QDict *wait_command(QTestState *who, const char *command, ...)
{
    va_list ap;
    QDict *resp, *ret;

    va_start(ap, command);
    resp = qtest_vqmp(who, command, ap);
    va_end(ap);

    check_stop_event(who);

    g_assert(!qdict_haskey(resp, "error"));
    g_assert(qdict_haskey(resp, "return"));

    ret = qdict_get_qdict(resp, "return");
    qobject_ref(ret);
    qobject_unref(resp);

    return ret;
}

/*
 * Execute the qmp command only
 */
QDict *qmp_command(QTestState *who, const char *command, ...)
{
    va_list ap;
    QDict *resp, *ret;

    va_start(ap, command);
    resp = qtest_vqmp(who, command, ap);
    va_end(ap);

    g_assert(!qdict_haskey(resp, "error"));
    g_assert(qdict_haskey(resp, "return"));

    ret = qdict_get_qdict(resp, "return");
    qobject_ref(ret);
    qobject_unref(resp);

    return ret;
}

/*
 * Send QMP command "migrate".
 * Arguments are built from @fmt... (formatted like
 * qobject_from_jsonf_nofail()) with "uri": @uri spliced in.
 */
void migrate_qmp(QTestState *who, const char *uri, const char *fmt, ...)
{
    va_list ap;
    QDict *args, *rsp;

    va_start(ap, fmt);
    args = qdict_from_vjsonf_nofail(fmt, ap);
    va_end(ap);

    g_assert(!qdict_haskey(args, "uri"));
    qdict_put_str(args, "uri", uri);

    rsp = qtest_qmp(who, "{ 'execute': 'migrate', 'arguments': %p}", args);

    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);
}

/*
 * Note: caller is responsible to free the returned object via
 * qobject_unref() after use
 */
QDict *migrate_query(QTestState *who)
{
    return wait_command(who, "{ 'execute': 'query-migrate' }");
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
    rsp_return = wait_command(from, "{ 'execute': 'query-status' }");
    g_assert(qdict_haskey(rsp_return, "running"));
    g_assert(qdict_get_bool(rsp_return, "running"));
    qobject_unref(rsp_return);
}
