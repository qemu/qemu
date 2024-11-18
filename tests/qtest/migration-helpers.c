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
#include "qobject/qjson.h"
#include "qapi/qapi-visit-sockets.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/error.h"
#include "qobject/qlist.h"
#include "qemu/cutils.h"
#include "qemu/memalign.h"

#include "migration-helpers.h"

/*
 * Number of seconds we wait when looking for migration
 * status changes, to avoid test suite hanging forever
 * when things go wrong. Needs to be higher enough to
 * avoid false positives on loaded hosts.
 */
#define MIGRATION_STATUS_WAIT_TIMEOUT 120

static char *SocketAddress_to_str(SocketAddress *addr)
{
    switch (addr->type) {
    case SOCKET_ADDRESS_TYPE_INET:
        return g_strdup_printf("tcp:%s:%s",
                               addr->u.inet.host,
                               addr->u.inet.port);
    case SOCKET_ADDRESS_TYPE_UNIX:
        return g_strdup_printf("unix:%s",
                               addr->u.q_unix.path);
    case SOCKET_ADDRESS_TYPE_FD:
        return g_strdup_printf("fd:%s", addr->u.fd.str);
    case SOCKET_ADDRESS_TYPE_VSOCK:
        return g_strdup_printf("vsock:%s:%s",
                               addr->u.vsock.cid,
                               addr->u.vsock.port);
    default:
        return g_strdup("unknown address type");
    }
}

static QDict *SocketAddress_to_qdict(SocketAddress *addr)
{
    QDict *dict = qdict_new();

    switch (addr->type) {
    case SOCKET_ADDRESS_TYPE_INET:
        qdict_put_str(dict, "type", "inet");
        qdict_put_str(dict, "host", addr->u.inet.host);
        qdict_put_str(dict, "port", addr->u.inet.port);
        break;
    case SOCKET_ADDRESS_TYPE_UNIX:
        qdict_put_str(dict, "type", "unix");
        qdict_put_str(dict, "path", addr->u.q_unix.path);
        break;
    case SOCKET_ADDRESS_TYPE_FD:
        qdict_put_str(dict, "type", "fd");
        qdict_put_str(dict, "str", addr->u.fd.str);
        break;
    case SOCKET_ADDRESS_TYPE_VSOCK:
        qdict_put_str(dict, "type", "vsock");
        qdict_put_str(dict, "cid", addr->u.vsock.cid);
        qdict_put_str(dict, "port", addr->u.vsock.port);
        break;
    default:
        g_assert_not_reached();
    }

    return dict;
}

static SocketAddressList *migrate_get_socket_address(QTestState *who)
{
    QDict *rsp;
    SocketAddressList *addrs;
    Visitor *iv = NULL;
    QObject *object;

    rsp = migrate_query(who);
    object = qdict_get(rsp, "socket-address");

    iv = qobject_input_visitor_new(object);
    visit_type_SocketAddressList(iv, NULL, &addrs, &error_abort);
    visit_free(iv);

    qobject_unref(rsp);
    return addrs;
}

static char *
migrate_get_connect_uri(QTestState *who)
{
    SocketAddressList *addrs;
    char *connect_uri;

    addrs = migrate_get_socket_address(who);
    connect_uri = SocketAddress_to_str(addrs->value);

    qapi_free_SocketAddressList(addrs);
    return connect_uri;
}

static QDict *
migrate_get_connect_qdict(QTestState *who)
{
    SocketAddressList *addrs;
    QDict *connect_qdict;

    addrs = migrate_get_socket_address(who);
    connect_qdict = SocketAddress_to_qdict(addrs->value);

    qapi_free_SocketAddressList(addrs);
    return connect_qdict;
}

static void migrate_set_ports(QTestState *to, QList *channel_list)
{
    QDict *addr;
    QListEntry *entry;
    const char *addr_port = NULL;

    addr = migrate_get_connect_qdict(to);

    QLIST_FOREACH_ENTRY(channel_list, entry) {
        QDict *channel = qobject_to(QDict, qlist_entry_obj(entry));
        QDict *addrdict = qdict_get_qdict(channel, "addr");

        if (qdict_haskey(addrdict, "port") &&
            qdict_haskey(addr, "port") &&
            (strcmp(qdict_get_str(addrdict, "port"), "0") == 0)) {
                addr_port = qdict_get_str(addr, "port");
                qdict_put_str(addrdict, "port", addr_port);
        }
    }

    qobject_unref(addr);
}

bool migrate_watch_for_events(QTestState *who, const char *name,
                              QDict *event, void *opaque)
{
    QTestMigrationState *state = opaque;

    if (g_str_equal(name, "STOP")) {
        state->stop_seen = true;
        return true;
    } else if (g_str_equal(name, "SUSPEND")) {
        state->suspend_seen = true;
        return true;
    } else if (g_str_equal(name, "RESUME")) {
        state->resume_seen = true;
        return true;
    }

    return false;
}

void migrate_qmp_fail(QTestState *who, const char *uri,
                      const char *channels, const char *fmt, ...)
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
        QObject *channels_obj = qobject_from_json(channels, &error_abort);
        qdict_put_obj(args, "channels", channels_obj);
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
                 const char *channels, const char *fmt, ...)
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
        QObject *channels_obj = qobject_from_json(channels, &error_abort);
        QList *channel_list = qobject_to(QList, channels_obj);
        migrate_set_ports(to, channel_list);
        qdict_put_obj(args, "channels", channels_obj);
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

void migrate_incoming_qmp(QTestState *to, const char *uri, const char *fmt, ...)
{
    va_list ap;
    QDict *args, *rsp;

    va_start(ap, fmt);
    args = qdict_from_vjsonf_nofail(fmt, ap);
    va_end(ap);

    g_assert(!qdict_haskey(args, "uri"));
    qdict_put_str(args, "uri", uri);

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

typedef struct {
    char *name;
    void (*func)(void);
} MigrationTest;

static void migration_test_destroy(gpointer data)
{
    MigrationTest *test = (MigrationTest *)data;

    g_free(test->name);
    g_free(test);
}

static void migration_test_wrapper(const void *data)
{
    MigrationTest *test = (MigrationTest *)data;

    g_test_message("Running /%s%s", qtest_get_arch(), test->name);
    test->func();
}

void migration_test_add(const char *path, void (*fn)(void))
{
    MigrationTest *test = g_new0(MigrationTest, 1);

    test->func = fn;
    test->name = g_strdup(path);

    qtest_add_data_func_full(path, test, migration_test_wrapper,
                             migration_test_destroy);
}

#ifdef O_DIRECT
/*
 * Probe for O_DIRECT support on the filesystem. Since this is used
 * for tests, be conservative, if anything fails, assume it's
 * unsupported.
 */
bool probe_o_direct_support(const char *tmpfs)
{
    g_autofree char *filename = g_strdup_printf("%s/probe-o-direct", tmpfs);
    int fd, flags = O_CREAT | O_RDWR | O_TRUNC | O_DIRECT;
    void *buf;
    ssize_t ret, len;
    uint64_t offset;

    fd = open(filename, flags, 0660);
    if (fd < 0) {
        unlink(filename);
        return false;
    }

    /*
     * Using 1MB alignment as conservative choice to satisfy any
     * plausible architecture default page size, and/or filesystem
     * alignment restrictions.
     */
    len = 0x100000;
    offset = 0x100000;

    buf = qemu_try_memalign(len, len);
    g_assert(buf);

    ret = pwrite(fd, buf, len, offset);
    unlink(filename);
    g_free(buf);

    if (ret < 0) {
        return false;
    }

    return true;
}
#endif

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
