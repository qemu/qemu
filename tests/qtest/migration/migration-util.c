/*
 * QTest migration utilities
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
#include "qapi/qapi-visit-sockets.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/error.h"
#include "qobject/qlist.h"
#include "qemu/cutils.h"
#include "qemu/memalign.h"

#include "migration/bootfile.h"
#include "migration/migration-util.h"

#if defined(__linux__)
#include <sys/ioctl.h>
#include <sys/syscall.h>
#endif

/* for uffd_version_check() */
#if defined(__linux__) && defined(__NR_userfaultfd) && defined(CONFIG_EVENTFD)
#include <sys/eventfd.h>
#include "qemu/userfaultfd.h"
#endif

/* For dirty ring test; so far only x86_64 is supported */
#if defined(__linux__) && defined(HOST_X86_64)
#include "linux/kvm.h"
#endif


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

char *migrate_get_connect_uri(QTestState *who)
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

void migrate_set_ports(QTestState *to, QList *channel_list)
{
    g_autoptr(QDict) addr = NULL;
    QListEntry *entry;
    const char *addr_port = NULL;

    QLIST_FOREACH_ENTRY(channel_list, entry) {
        QDict *channel = qobject_to(QDict, qlist_entry_obj(entry));
        QDict *addrdict = qdict_get_qdict(channel, "addr");

        if (!qdict_haskey(addrdict, "port") ||
            strcmp(qdict_get_str(addrdict, "port"), "0")) {
            continue;
        }

        /*
         * Fetch addr only if needed, so tests that are not yet connected to
         * the monitor do not query it.  Such tests cannot use port=0.
         */
        if (!addr) {
            addr = migrate_get_connect_qdict(to);
        }

        if (qdict_haskey(addr, "port")) {
            addr_port = qdict_get_str(addr, "port");
            qdict_put_str(addrdict, "port", addr_port);
        }
    }
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
    void (*func_full)(void *);
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

static void migration_test_wrapper_full(const void *data)
{
    MigrationTest *test = (MigrationTest *)data;

    g_test_message("Running /%s%s", qtest_get_arch(), test->name);
    test->func_full(test->name);
}

void migration_test_add_suffix(const char *path, const char *suffix,
                               void (*fn)(void *))
{
    MigrationTest *test = g_new0(MigrationTest, 1);

    g_assert(g_str_has_suffix(path, "/"));
    g_assert(!g_str_has_prefix(suffix, "/"));

    test->func_full = fn;
    test->name = g_strconcat(path, suffix, NULL);

    qtest_add_data_func_full(test->name, test, migration_test_wrapper_full,
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
    memset(buf, 0, len);

    ret = pwrite(fd, buf, len, offset);
    unlink(filename);
    g_free(buf);

    if (ret < 0) {
        return false;
    }

    return true;
}
#endif

#if defined(__linux__) && defined(__NR_userfaultfd) && defined(CONFIG_EVENTFD)
bool ufd_version_check(bool *uffd_feature_thread_id)
{
    struct uffdio_api api_struct;
    uint64_t ioctl_mask;

    int ufd = uffd_open(O_CLOEXEC);

    if (ufd == -1) {
        g_test_message("Skipping test: userfaultfd not available");
        return false;
    }

    api_struct.api = UFFD_API;
    api_struct.features = 0;
    if (ioctl(ufd, UFFDIO_API, &api_struct)) {
        g_test_message("Skipping test: UFFDIO_API failed");
        return false;
    }

    if (uffd_feature_thread_id) {
        *uffd_feature_thread_id = api_struct.features & UFFD_FEATURE_THREAD_ID;
    }

    ioctl_mask = (1ULL << _UFFDIO_REGISTER |
                  1ULL << _UFFDIO_UNREGISTER);
    if ((api_struct.ioctls & ioctl_mask) != ioctl_mask) {
        g_test_message("Skipping test: Missing userfault feature");
        return false;
    }

    return true;
}
#else
bool ufd_version_check(bool *uffd_feature_thread_id)
{
    g_test_message("Skipping test: Userfault not available (builtdtime)");
    return false;
}
#endif

bool kvm_dirty_ring_supported(void)
{
#if defined(__linux__) && defined(HOST_X86_64)
    int ret, kvm_fd = open("/dev/kvm", O_RDONLY);

    if (kvm_fd < 0) {
        return false;
    }

    ret = ioctl(kvm_fd, KVM_CHECK_EXTENSION, KVM_CAP_DIRTY_LOG_RING);
    close(kvm_fd);

    /* We test with 4096 slots */
    if (ret < 4096) {
        return false;
    }

    return true;
#else
    return false;
#endif
}
