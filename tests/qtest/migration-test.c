/*
 * QTest testcase for migration
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
#include "qapi/qmp/qdict.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/range.h"
#include "qemu/sockets.h"
#include "chardev/char.h"
#include "qapi/qapi-visit-sockets.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"

#include "migration-helpers.h"
#include "migration/migration-test.h"

/* TODO actually test the results and get rid of this */
#define qtest_qmp_discard_response(...) qobject_unref(qtest_qmp(__VA_ARGS__))

unsigned start_address;
unsigned end_address;
static bool uffd_feature_thread_id;

#if defined(__linux__)
#include <sys/syscall.h>
#include <sys/vfs.h>
#endif

#if defined(__linux__) && defined(__NR_userfaultfd) && defined(CONFIG_EVENTFD)
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <linux/userfaultfd.h>

static bool ufd_version_check(void)
{
    struct uffdio_api api_struct;
    uint64_t ioctl_mask;

    int ufd = syscall(__NR_userfaultfd, O_CLOEXEC);

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
    uffd_feature_thread_id = api_struct.features & UFFD_FEATURE_THREAD_ID;

    ioctl_mask = (__u64)1 << _UFFDIO_REGISTER |
                 (__u64)1 << _UFFDIO_UNREGISTER;
    if ((api_struct.ioctls & ioctl_mask) != ioctl_mask) {
        g_test_message("Skipping test: Missing userfault feature");
        return false;
    }

    return true;
}

#else
static bool ufd_version_check(void)
{
    g_test_message("Skipping test: Userfault not available (builtdtime)");
    return false;
}

#endif

static const char *tmpfs;

/* The boot file modifies memory area in [start_address, end_address)
 * repeatedly. It outputs a 'B' at a fixed rate while it's still running.
 */
#include "tests/migration/i386/a-b-bootblock.h"
#include "tests/migration/aarch64/a-b-kernel.h"
#include "tests/migration/s390x/a-b-bios.h"

static void init_bootfile(const char *bootpath, void *content, size_t len)
{
    FILE *bootfile = fopen(bootpath, "wb");

    g_assert_cmpint(fwrite(content, len, 1, bootfile), ==, 1);
    fclose(bootfile);
}

/*
 * Wait for some output in the serial output file,
 * we get an 'A' followed by an endless string of 'B's
 * but on the destination we won't have the A.
 */
static void wait_for_serial(const char *side)
{
    char *serialpath = g_strdup_printf("%s/%s", tmpfs, side);
    FILE *serialfile = fopen(serialpath, "r");
    const char *arch = qtest_get_arch();
    int started = (strcmp(side, "src_serial") == 0 &&
                   strcmp(arch, "ppc64") == 0) ? 0 : 1;

    g_free(serialpath);
    do {
        int readvalue = fgetc(serialfile);

        if (!started) {
            /* SLOF prints its banner before starting test,
             * to ignore it, mark the start of the test with '_',
             * ignore all characters until this marker
             */
            switch (readvalue) {
            case '_':
                started = 1;
                break;
            case EOF:
                fseek(serialfile, 0, SEEK_SET);
                usleep(1000);
                break;
            }
            continue;
        }
        switch (readvalue) {
        case 'A':
            /* Fine */
            break;

        case 'B':
            /* It's alive! */
            fclose(serialfile);
            return;

        case EOF:
            started = (strcmp(side, "src_serial") == 0 &&
                       strcmp(arch, "ppc64") == 0) ? 0 : 1;
            fseek(serialfile, 0, SEEK_SET);
            usleep(1000);
            break;

        default:
            fprintf(stderr, "Unexpected %d on %s serial\n", readvalue, side);
            g_assert_not_reached();
        }
    } while (true);
}

/*
 * It's tricky to use qemu's migration event capability with qtest,
 * events suddenly appearing confuse the qmp()/hmp() responses.
 */

static int64_t read_ram_property_int(QTestState *who, const char *property)
{
    QDict *rsp_return, *rsp_ram;
    int64_t result;

    rsp_return = migrate_query(who);
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

static int64_t read_migrate_property_int(QTestState *who, const char *property)
{
    QDict *rsp_return;
    int64_t result;

    rsp_return = migrate_query(who);
    result = qdict_get_try_int(rsp_return, property, 0);
    qobject_unref(rsp_return);
    return result;
}

static uint64_t get_migration_pass(QTestState *who)
{
    return read_ram_property_int(who, "dirty-sync-count");
}

static void read_blocktime(QTestState *who)
{
    QDict *rsp_return;

    rsp_return = migrate_query(who);
    g_assert(qdict_haskey(rsp_return, "postcopy-blocktime"));
    qobject_unref(rsp_return);
}

static void wait_for_migration_pass(QTestState *who)
{
    uint64_t initial_pass = get_migration_pass(who);
    uint64_t pass;

    /* Wait for the 1st sync */
    while (!got_stop && !initial_pass) {
        usleep(1000);
        initial_pass = get_migration_pass(who);
    }

    do {
        usleep(1000);
        pass = get_migration_pass(who);
    } while (pass == initial_pass && !got_stop);
}

static void check_guests_ram(QTestState *who)
{
    /* Our ASM test will have been incrementing one byte from each page from
     * start_address to < end_address in order. This gives us a constraint
     * that any page's byte should be equal or less than the previous pages
     * byte (mod 256); and they should all be equal except for one transition
     * at the point where we meet the incrementer. (We're running this with
     * the guest stopped).
     */
    unsigned address;
    uint8_t first_byte;
    uint8_t last_byte;
    bool hit_edge = false;
    int bad = 0;

    qtest_memread(who, start_address, &first_byte, 1);
    last_byte = first_byte;

    for (address = start_address + TEST_MEM_PAGE_SIZE; address < end_address;
         address += TEST_MEM_PAGE_SIZE)
    {
        uint8_t b;
        qtest_memread(who, address, &b, 1);
        if (b != last_byte) {
            if (((b + 1) % 256) == last_byte && !hit_edge) {
                /* This is OK, the guest stopped at the point of
                 * incrementing the previous page but didn't get
                 * to us yet.
                 */
                hit_edge = true;
                last_byte = b;
            } else {
                bad++;
                if (bad <= 10) {
                    fprintf(stderr, "Memory content inconsistency at %x"
                            " first_byte = %x last_byte = %x current = %x"
                            " hit_edge = %x\n",
                            address, first_byte, last_byte, b, hit_edge);
                }
            }
        }
    }
    if (bad >= 10) {
        fprintf(stderr, "and in another %d pages", bad - 10);
    }
    g_assert(bad == 0);
}

static void cleanup(const char *filename)
{
    char *path = g_strdup_printf("%s/%s", tmpfs, filename);

    unlink(path);
    g_free(path);
}

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
        return g_strdup_printf("tcp:%s:%s",
                               addr->u.vsock.cid,
                               addr->u.vsock.port);
    default:
        return g_strdup("unknown address type");
    }
}

static char *migrate_get_socket_address(QTestState *who, const char *parameter)
{
    QDict *rsp;
    char *result;
    Error *local_err = NULL;
    SocketAddressList *addrs;
    Visitor *iv = NULL;
    QObject *object;

    rsp = migrate_query(who);
    object = qdict_get(rsp, parameter);

    iv = qobject_input_visitor_new(object);
    visit_type_SocketAddressList(iv, NULL, &addrs, &local_err);
    visit_free(iv);

    /* we are only using a single address */
    result = SocketAddress_to_str(addrs->value);

    qapi_free_SocketAddressList(addrs);
    qobject_unref(rsp);
    return result;
}

static long long migrate_get_parameter_int(QTestState *who,
                                           const char *parameter)
{
    QDict *rsp;
    long long result;

    rsp = wait_command(who, "{ 'execute': 'query-migrate-parameters' }");
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

static void migrate_set_parameter_int(QTestState *who, const char *parameter,
                                      long long value)
{
    QDict *rsp;

    rsp = qtest_qmp(who,
                    "{ 'execute': 'migrate-set-parameters',"
                    "'arguments': { %s: %lld } }",
                    parameter, value);
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);
    migrate_check_parameter_int(who, parameter, value);
}

static char *migrate_get_parameter_str(QTestState *who,
                                       const char *parameter)
{
    QDict *rsp;
    char *result;

    rsp = wait_command(who, "{ 'execute': 'query-migrate-parameters' }");
    result = g_strdup(qdict_get_str(rsp, parameter));
    qobject_unref(rsp);
    return result;
}

static void migrate_check_parameter_str(QTestState *who, const char *parameter,
                                        const char *value)
{
    char *result;

    result = migrate_get_parameter_str(who, parameter);
    g_assert_cmpstr(result, ==, value);
    g_free(result);
}

static void migrate_set_parameter_str(QTestState *who, const char *parameter,
                                      const char *value)
{
    QDict *rsp;

    rsp = qtest_qmp(who,
                    "{ 'execute': 'migrate-set-parameters',"
                    "'arguments': { %s: %s } }",
                    parameter, value);
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);
    migrate_check_parameter_str(who, parameter, value);
}

static void migrate_pause(QTestState *who)
{
    QDict *rsp;

    rsp = wait_command(who, "{ 'execute': 'migrate-pause' }");
    qobject_unref(rsp);
}

static void migrate_continue(QTestState *who, const char *state)
{
    QDict *rsp;

    rsp = wait_command(who,
                       "{ 'execute': 'migrate-continue',"
                       "  'arguments': { 'state': %s } }",
                       state);
    qobject_unref(rsp);
}

static void migrate_recover(QTestState *who, const char *uri)
{
    QDict *rsp;

    rsp = wait_command(who,
                       "{ 'execute': 'migrate-recover', "
                       "  'id': 'recover-cmd', "
                       "  'arguments': { 'uri': %s } }",
                       uri);
    qobject_unref(rsp);
}

static void migrate_cancel(QTestState *who)
{
    QDict *rsp;

    rsp = wait_command(who, "{ 'execute': 'migrate_cancel' }");
    qobject_unref(rsp);
}

static void migrate_set_capability(QTestState *who, const char *capability,
                                   bool value)
{
    QDict *rsp;

    rsp = qtest_qmp(who,
                    "{ 'execute': 'migrate-set-capabilities',"
                    "'arguments': { "
                    "'capabilities': [ { "
                    "'capability': %s, 'state': %i } ] } }",
                    capability, value);
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);
}

static void migrate_postcopy_start(QTestState *from, QTestState *to)
{
    QDict *rsp;

    rsp = wait_command(from, "{ 'execute': 'migrate-start-postcopy' }");
    qobject_unref(rsp);

    if (!got_stop) {
        qtest_qmp_eventwait(from, "STOP");
    }

    qtest_qmp_eventwait(to, "RESUME");
}

typedef struct {
    bool hide_stderr;
    bool use_shmem;
    /* only launch the target process */
    bool only_target;
    char *opts_source;
    char *opts_target;
} MigrateStart;

static MigrateStart *migrate_start_new(void)
{
    MigrateStart *args = g_new0(MigrateStart, 1);

    args->opts_source = g_strdup("");
    args->opts_target = g_strdup("");
    return args;
}

static void migrate_start_destroy(MigrateStart *args)
{
    g_free(args->opts_source);
    g_free(args->opts_target);
    g_free(args);
}

static int test_migrate_start(QTestState **from, QTestState **to,
                              const char *uri, MigrateStart *args)
{
    gchar *arch_source, *arch_target;
    gchar *cmd_source, *cmd_target;
    const gchar *ignore_stderr;
    char *bootpath = NULL;
    char *shmem_opts;
    char *shmem_path;
    const char *arch = qtest_get_arch();
    const char *machine_opts = NULL;
    const char *memory_size;
    int ret = 0;

    if (args->use_shmem) {
        if (!g_file_test("/dev/shm", G_FILE_TEST_IS_DIR)) {
            g_test_skip("/dev/shm is not supported");
            ret = -1;
            goto out;
        }
    }

    got_stop = false;
    bootpath = g_strdup_printf("%s/bootsect", tmpfs);
    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        /* the assembled x86 boot sector should be exactly one sector large */
        assert(sizeof(x86_bootsect) == 512);
        init_bootfile(bootpath, x86_bootsect, sizeof(x86_bootsect));
        memory_size = "150M";
        arch_source = g_strdup_printf("-drive file=%s,format=raw", bootpath);
        arch_target = g_strdup(arch_source);
        start_address = X86_TEST_MEM_START;
        end_address = X86_TEST_MEM_END;
    } else if (g_str_equal(arch, "s390x")) {
        init_bootfile(bootpath, s390x_elf, sizeof(s390x_elf));
        memory_size = "128M";
        arch_source = g_strdup_printf("-bios %s", bootpath);
        arch_target = g_strdup(arch_source);
        start_address = S390_TEST_MEM_START;
        end_address = S390_TEST_MEM_END;
    } else if (strcmp(arch, "ppc64") == 0) {
        machine_opts = "vsmt=8";
        memory_size = "256M";
        start_address = PPC_TEST_MEM_START;
        end_address = PPC_TEST_MEM_END;
        arch_source = g_strdup_printf("-nodefaults "
                                      "-prom-env 'use-nvramrc?=true' -prom-env "
                                      "'nvramrc=hex .\" _\" begin %x %x "
                                      "do i c@ 1 + i c! 1000 +loop .\" B\" 0 "
                                      "until'", end_address, start_address);
        arch_target = g_strdup("");
    } else if (strcmp(arch, "aarch64") == 0) {
        init_bootfile(bootpath, aarch64_kernel, sizeof(aarch64_kernel));
        machine_opts = "virt,gic-version=max";
        memory_size = "150M";
        arch_source = g_strdup_printf("-cpu max "
                                      "-kernel %s",
                                      bootpath);
        arch_target = g_strdup(arch_source);
        start_address = ARM_TEST_MEM_START;
        end_address = ARM_TEST_MEM_END;

        g_assert(sizeof(aarch64_kernel) <= ARM_TEST_MAX_KERNEL_SIZE);
    } else {
        g_assert_not_reached();
    }

    g_free(bootpath);

    if (args->hide_stderr) {
        ignore_stderr = "2>/dev/null";
    } else {
        ignore_stderr = "";
    }

    if (args->use_shmem) {
        shmem_path = g_strdup_printf("/dev/shm/qemu-%d", getpid());
        shmem_opts = g_strdup_printf(
            "-object memory-backend-file,id=mem0,size=%s"
            ",mem-path=%s,share=on -numa node,memdev=mem0",
            memory_size, shmem_path);
    } else {
        shmem_path = NULL;
        shmem_opts = g_strdup("");
    }

    cmd_source = g_strdup_printf("-accel kvm -accel tcg%s%s "
                                 "-name source,debug-threads=on "
                                 "-m %s "
                                 "-serial file:%s/src_serial "
                                 "%s %s %s %s",
                                 machine_opts ? " -machine " : "",
                                 machine_opts ? machine_opts : "",
                                 memory_size, tmpfs,
                                 arch_source, shmem_opts, args->opts_source,
                                 ignore_stderr);
    g_free(arch_source);
    if (!args->only_target) {
        *from = qtest_init(cmd_source);
    }
    g_free(cmd_source);

    cmd_target = g_strdup_printf("-accel kvm -accel tcg%s%s "
                                 "-name target,debug-threads=on "
                                 "-m %s "
                                 "-serial file:%s/dest_serial "
                                 "-incoming %s "
                                 "%s %s %s %s",
                                 machine_opts ? " -machine " : "",
                                 machine_opts ? machine_opts : "",
                                 memory_size, tmpfs, uri,
                                 arch_target, shmem_opts,
                                 args->opts_target, ignore_stderr);
    g_free(arch_target);
    *to = qtest_init(cmd_target);
    g_free(cmd_target);

    g_free(shmem_opts);
    /*
     * Remove shmem file immediately to avoid memory leak in test failed case.
     * It's valid becase QEMU has already opened this file
     */
    if (args->use_shmem) {
        unlink(shmem_path);
        g_free(shmem_path);
    }

out:
    migrate_start_destroy(args);
    return ret;
}

static void test_migrate_end(QTestState *from, QTestState *to, bool test_dest)
{
    unsigned char dest_byte_a, dest_byte_b, dest_byte_c, dest_byte_d;

    qtest_quit(from);

    if (test_dest) {
        qtest_memread(to, start_address, &dest_byte_a, 1);

        /* Destination still running, wait for a byte to change */
        do {
            qtest_memread(to, start_address, &dest_byte_b, 1);
            usleep(1000 * 10);
        } while (dest_byte_a == dest_byte_b);

        qtest_qmp_discard_response(to, "{ 'execute' : 'stop'}");

        /* With it stopped, check nothing changes */
        qtest_memread(to, start_address, &dest_byte_c, 1);
        usleep(1000 * 200);
        qtest_memread(to, start_address, &dest_byte_d, 1);
        g_assert_cmpint(dest_byte_c, ==, dest_byte_d);

        check_guests_ram(to);
    }

    qtest_quit(to);

    cleanup("bootsect");
    cleanup("migsocket");
    cleanup("src_serial");
    cleanup("dest_serial");
}

static void deprecated_set_downtime(QTestState *who, const double value)
{
    QDict *rsp;

    rsp = qtest_qmp(who,
                    "{ 'execute': 'migrate_set_downtime',"
                    " 'arguments': { 'value': %f } }", value);
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);
    migrate_check_parameter_int(who, "downtime-limit", value * 1000);
}

static void deprecated_set_speed(QTestState *who, long long value)
{
    QDict *rsp;

    rsp = qtest_qmp(who, "{ 'execute': 'migrate_set_speed',"
                          "'arguments': { 'value': %lld } }", value);
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);
    migrate_check_parameter_int(who, "max-bandwidth", value);
}

static void deprecated_set_cache_size(QTestState *who, long long value)
{
    QDict *rsp;

    rsp = qtest_qmp(who, "{ 'execute': 'migrate-set-cache-size',"
                         "'arguments': { 'value': %lld } }", value);
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);
    migrate_check_parameter_int(who, "xbzrle-cache-size", value);
}

static void test_deprecated(void)
{
    QTestState *from;

    from = qtest_init("-machine none");

    deprecated_set_downtime(from, 0.12345);
    deprecated_set_speed(from, 12345);
    deprecated_set_cache_size(from, 4096);

    qtest_quit(from);
}

static int migrate_postcopy_prepare(QTestState **from_ptr,
                                    QTestState **to_ptr,
                                    MigrateStart *args)
{
    char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    QTestState *from, *to;

    if (test_migrate_start(&from, &to, uri, args)) {
        return -1;
    }

    migrate_set_capability(from, "postcopy-ram", true);
    migrate_set_capability(to, "postcopy-ram", true);
    migrate_set_capability(to, "postcopy-blocktime", true);

    /* We want to pick a speed slow enough that the test completes
     * quickly, but that it doesn't complete precopy even on a slow
     * machine, so also set the downtime.
     */
    migrate_set_parameter_int(from, "max-bandwidth", 30000000);
    migrate_set_parameter_int(from, "downtime-limit", 1);

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    migrate_qmp(from, uri, "{}");
    g_free(uri);

    wait_for_migration_pass(from);

    *from_ptr = from;
    *to_ptr = to;

    return 0;
}

static void migrate_postcopy_complete(QTestState *from, QTestState *to)
{
    wait_for_migration_complete(from);

    /* Make sure we get at least one "B" on destination */
    wait_for_serial("dest_serial");

    if (uffd_feature_thread_id) {
        read_blocktime(to);
    }

    test_migrate_end(from, to, true);
}

static void test_postcopy(void)
{
    MigrateStart *args = migrate_start_new();
    QTestState *from, *to;

    if (migrate_postcopy_prepare(&from, &to, args)) {
        return;
    }
    migrate_postcopy_start(from, to);
    migrate_postcopy_complete(from, to);
}

static void test_postcopy_recovery(void)
{
    MigrateStart *args = migrate_start_new();
    QTestState *from, *to;
    char *uri;

    args->hide_stderr = true;

    if (migrate_postcopy_prepare(&from, &to, args)) {
        return;
    }

    /* Turn postcopy speed down, 4K/s is slow enough on any machines */
    migrate_set_parameter_int(from, "max-postcopy-bandwidth", 4096);

    /* Now we start the postcopy */
    migrate_postcopy_start(from, to);

    /*
     * Wait until postcopy is really started; we can only run the
     * migrate-pause command during a postcopy
     */
    wait_for_migration_status(from, "postcopy-active", NULL);

    /*
     * Manually stop the postcopy migration. This emulates a network
     * failure with the migration socket
     */
    migrate_pause(from);

    /*
     * Wait for destination side to reach postcopy-paused state.  The
     * migrate-recover command can only succeed if destination machine
     * is in the paused state
     */
    wait_for_migration_status(to, "postcopy-paused",
                              (const char * []) { "failed", "active",
                                                  "completed", NULL });

    /*
     * Create a new socket to emulate a new channel that is different
     * from the broken migration channel; tell the destination to
     * listen to the new port
     */
    uri = g_strdup_printf("unix:%s/migsocket-recover", tmpfs);
    migrate_recover(to, uri);

    /*
     * Try to rebuild the migration channel using the resume flag and
     * the newly created channel
     */
    wait_for_migration_status(from, "postcopy-paused",
                              (const char * []) { "failed", "active",
                                                  "completed", NULL });
    migrate_qmp(from, uri, "{'resume': true}");
    g_free(uri);

    /* Restore the postcopy bandwidth to unlimited */
    migrate_set_parameter_int(from, "max-postcopy-bandwidth", 0);

    migrate_postcopy_complete(from, to);
}

static void test_baddest(void)
{
    MigrateStart *args = migrate_start_new();
    QTestState *from, *to;

    args->hide_stderr = true;

    if (test_migrate_start(&from, &to, "tcp:0:0", args)) {
        return;
    }
    migrate_qmp(from, "tcp:0:0", "{}");
    wait_for_migration_fail(from, false);
    test_migrate_end(from, to, false);
}

static void test_precopy_unix(void)
{
    char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateStart *args = migrate_start_new();
    QTestState *from, *to;

    if (test_migrate_start(&from, &to, uri, args)) {
        return;
    }

    /* We want to pick a speed slow enough that the test completes
     * quickly, but that it doesn't complete precopy even on a slow
     * machine, so also set the downtime.
     */
    /* 1 ms should make it not converge*/
    migrate_set_parameter_int(from, "downtime-limit", 1);
    /* 1GB/s */
    migrate_set_parameter_int(from, "max-bandwidth", 1000000000);

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    migrate_qmp(from, uri, "{}");

    wait_for_migration_pass(from);

    /* 300 ms should converge */
    migrate_set_parameter_int(from, "downtime-limit", 300);

    if (!got_stop) {
        qtest_qmp_eventwait(from, "STOP");
    }

    qtest_qmp_eventwait(to, "RESUME");

    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);

    test_migrate_end(from, to, true);
    g_free(uri);
}

#if 0
/* Currently upset on aarch64 TCG */
static void test_ignore_shared(void)
{
    char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    QTestState *from, *to;

    if (test_migrate_start(&from, &to, uri, false, true, NULL, NULL)) {
        return;
    }

    migrate_set_capability(from, "x-ignore-shared", true);
    migrate_set_capability(to, "x-ignore-shared", true);

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    migrate_qmp(from, uri, "{}");

    wait_for_migration_pass(from);

    if (!got_stop) {
        qtest_qmp_eventwait(from, "STOP");
    }

    qtest_qmp_eventwait(to, "RESUME");

    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);

    /* Check whether shared RAM has been really skipped */
    g_assert_cmpint(read_ram_property_int(from, "transferred"), <, 1024 * 1024);

    test_migrate_end(from, to, true);
    g_free(uri);
}
#endif

static void test_xbzrle(const char *uri)
{
    MigrateStart *args = migrate_start_new();
    QTestState *from, *to;

    if (test_migrate_start(&from, &to, uri, args)) {
        return;
    }

    /*
     * We want to pick a speed slow enough that the test completes
     * quickly, but that it doesn't complete precopy even on a slow
     * machine, so also set the downtime.
     */
    /* 1 ms should make it not converge*/
    migrate_set_parameter_int(from, "downtime-limit", 1);
    /* 1GB/s */
    migrate_set_parameter_int(from, "max-bandwidth", 1000000000);

    migrate_set_parameter_int(from, "xbzrle-cache-size", 33554432);

    migrate_set_capability(from, "xbzrle", "true");
    migrate_set_capability(to, "xbzrle", "true");
    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    migrate_qmp(from, uri, "{}");

    wait_for_migration_pass(from);

    /* 300ms should converge */
    migrate_set_parameter_int(from, "downtime-limit", 300);

    if (!got_stop) {
        qtest_qmp_eventwait(from, "STOP");
    }
    qtest_qmp_eventwait(to, "RESUME");

    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);

    test_migrate_end(from, to, true);
}

static void test_xbzrle_unix(void)
{
    char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);

    test_xbzrle(uri);
    g_free(uri);
}

static void test_precopy_tcp(void)
{
    MigrateStart *args = migrate_start_new();
    char *uri;
    QTestState *from, *to;

    if (test_migrate_start(&from, &to, "tcp:127.0.0.1:0", args)) {
        return;
    }

    /*
     * We want to pick a speed slow enough that the test completes
     * quickly, but that it doesn't complete precopy even on a slow
     * machine, so also set the downtime.
     */
    /* 1 ms should make it not converge*/
    migrate_set_parameter_int(from, "downtime-limit", 1);
    /* 1GB/s */
    migrate_set_parameter_int(from, "max-bandwidth", 1000000000);

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    uri = migrate_get_socket_address(to, "socket-address");

    migrate_qmp(from, uri, "{}");

    wait_for_migration_pass(from);

    /* 300ms should converge */
    migrate_set_parameter_int(from, "downtime-limit", 300);

    if (!got_stop) {
        qtest_qmp_eventwait(from, "STOP");
    }
    qtest_qmp_eventwait(to, "RESUME");

    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);

    test_migrate_end(from, to, true);
    g_free(uri);
}

static void test_migrate_fd_proto(void)
{
    MigrateStart *args = migrate_start_new();
    QTestState *from, *to;
    int ret;
    int pair[2];
    QDict *rsp;
    const char *error_desc;

    if (test_migrate_start(&from, &to, "defer", args)) {
        return;
    }

    /*
     * We want to pick a speed slow enough that the test completes
     * quickly, but that it doesn't complete precopy even on a slow
     * machine, so also set the downtime.
     */
    /* 1 ms should make it not converge */
    migrate_set_parameter_int(from, "downtime-limit", 1);
    /* 1GB/s */
    migrate_set_parameter_int(from, "max-bandwidth", 1000000000);

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    /* Create two connected sockets for migration */
    ret = socketpair(PF_LOCAL, SOCK_STREAM, 0, pair);
    g_assert_cmpint(ret, ==, 0);

    /* Send the 1st socket to the target */
    rsp = wait_command_fd(to, pair[0],
                          "{ 'execute': 'getfd',"
                          "  'arguments': { 'fdname': 'fd-mig' }}");
    qobject_unref(rsp);
    close(pair[0]);

    /* Start incoming migration from the 1st socket */
    rsp = wait_command(to, "{ 'execute': 'migrate-incoming',"
                           "  'arguments': { 'uri': 'fd:fd-mig' }}");
    qobject_unref(rsp);

    /* Send the 2nd socket to the target */
    rsp = wait_command_fd(from, pair[1],
                          "{ 'execute': 'getfd',"
                          "  'arguments': { 'fdname': 'fd-mig' }}");
    qobject_unref(rsp);
    close(pair[1]);

    /* Start migration to the 2nd socket*/
    migrate_qmp(from, "fd:fd-mig", "{}");

    wait_for_migration_pass(from);

    /* 300ms should converge */
    migrate_set_parameter_int(from, "downtime-limit", 300);

    if (!got_stop) {
        qtest_qmp_eventwait(from, "STOP");
    }
    qtest_qmp_eventwait(to, "RESUME");

    /* Test closing fds */
    /* We assume, that QEMU removes named fd from its list,
     * so this should fail */
    rsp = qtest_qmp(from, "{ 'execute': 'closefd',"
                          "  'arguments': { 'fdname': 'fd-mig' }}");
    g_assert_true(qdict_haskey(rsp, "error"));
    error_desc = qdict_get_str(qdict_get_qdict(rsp, "error"), "desc");
    g_assert_cmpstr(error_desc, ==, "File descriptor named 'fd-mig' not found");
    qobject_unref(rsp);

    rsp = qtest_qmp(to, "{ 'execute': 'closefd',"
                        "  'arguments': { 'fdname': 'fd-mig' }}");
    g_assert_true(qdict_haskey(rsp, "error"));
    error_desc = qdict_get_str(qdict_get_qdict(rsp, "error"), "desc");
    g_assert_cmpstr(error_desc, ==, "File descriptor named 'fd-mig' not found");
    qobject_unref(rsp);

    /* Complete migration */
    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);
    test_migrate_end(from, to, true);
}

static void do_test_validate_uuid(MigrateStart *args, bool should_fail)
{
    char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    QTestState *from, *to;

    if (test_migrate_start(&from, &to, uri, args)) {
        return;
    }

    /*
     * UUID validation is at the begin of migration. So, the main process of
     * migration is not interesting for us here. Thus, set huge downtime for
     * very fast migration.
     */
    migrate_set_parameter_int(from, "downtime-limit", 1000000);
    migrate_set_capability(from, "validate-uuid", true);

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    migrate_qmp(from, uri, "{}");

    if (should_fail) {
        qtest_set_expected_status(to, 1);
        wait_for_migration_fail(from, true);
    } else {
        wait_for_migration_complete(from);
    }

    test_migrate_end(from, to, false);
    g_free(uri);
}

static void test_validate_uuid(void)
{
    MigrateStart *args = migrate_start_new();

    g_free(args->opts_source);
    g_free(args->opts_target);
    args->opts_source = g_strdup("-uuid 11111111-1111-1111-1111-111111111111");
    args->opts_target = g_strdup("-uuid 11111111-1111-1111-1111-111111111111");
    do_test_validate_uuid(args, false);
}

static void test_validate_uuid_error(void)
{
    MigrateStart *args = migrate_start_new();

    g_free(args->opts_source);
    g_free(args->opts_target);
    args->opts_source = g_strdup("-uuid 11111111-1111-1111-1111-111111111111");
    args->opts_target = g_strdup("-uuid 22222222-2222-2222-2222-222222222222");
    args->hide_stderr = true;
    do_test_validate_uuid(args, true);
}

static void test_validate_uuid_src_not_set(void)
{
    MigrateStart *args = migrate_start_new();

    g_free(args->opts_target);
    args->opts_target = g_strdup("-uuid 22222222-2222-2222-2222-222222222222");
    args->hide_stderr = true;
    do_test_validate_uuid(args, false);
}

static void test_validate_uuid_dst_not_set(void)
{
    MigrateStart *args = migrate_start_new();

    g_free(args->opts_source);
    args->opts_source = g_strdup("-uuid 11111111-1111-1111-1111-111111111111");
    args->hide_stderr = true;
    do_test_validate_uuid(args, false);
}

static void test_migrate_auto_converge(void)
{
    char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateStart *args = migrate_start_new();
    QTestState *from, *to;
    int64_t remaining, percentage;

    /*
     * We want the test to be stable and as fast as possible.
     * E.g., with 1Gb/s bandwith migration may pass without throttling,
     * so we need to decrease a bandwidth.
     */
    const int64_t init_pct = 5, inc_pct = 50, max_pct = 95;
    const int64_t max_bandwidth = 400000000; /* ~400Mb/s */
    const int64_t downtime_limit = 250; /* 250ms */
    /*
     * We migrate through unix-socket (> 500Mb/s).
     * Thus, expected migration speed ~= bandwidth limit (< 500Mb/s).
     * So, we can predict expected_threshold
     */
    const int64_t expected_threshold = max_bandwidth * downtime_limit / 1000;

    if (test_migrate_start(&from, &to, uri, args)) {
        return;
    }

    migrate_set_capability(from, "auto-converge", true);
    migrate_set_parameter_int(from, "cpu-throttle-initial", init_pct);
    migrate_set_parameter_int(from, "cpu-throttle-increment", inc_pct);
    migrate_set_parameter_int(from, "max-cpu-throttle", max_pct);

    /*
     * Set the initial parameters so that the migration could not converge
     * without throttling.
     */
    migrate_set_parameter_int(from, "downtime-limit", 1);
    migrate_set_parameter_int(from, "max-bandwidth", 1000000); /* ~1Mb/s */

    /* To check remaining size after precopy */
    migrate_set_capability(from, "pause-before-switchover", true);

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    migrate_qmp(from, uri, "{}");

    /* Wait for throttling begins */
    percentage = 0;
    while (percentage == 0) {
        percentage = read_migrate_property_int(from, "cpu-throttle-percentage");
        usleep(100);
        g_assert_false(got_stop);
    }
    /* The first percentage of throttling should be equal to init_pct */
    g_assert_cmpint(percentage, ==, init_pct);
    /* Now, when we tested that throttling works, let it converge */
    migrate_set_parameter_int(from, "downtime-limit", downtime_limit);
    migrate_set_parameter_int(from, "max-bandwidth", max_bandwidth);

    /*
     * Wait for pre-switchover status to check last throttle percentage
     * and remaining. These values will be zeroed later
     */
    wait_for_migration_status(from, "pre-switchover", NULL);

    /* The final percentage of throttling shouldn't be greater than max_pct */
    percentage = read_migrate_property_int(from, "cpu-throttle-percentage");
    g_assert_cmpint(percentage, <=, max_pct);

    remaining = read_ram_property_int(from, "remaining");
    g_assert_cmpint(remaining, <,
                    (expected_threshold + expected_threshold / 100));

    migrate_continue(from, "pre-switchover");

    qtest_qmp_eventwait(to, "RESUME");

    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);

    g_free(uri);

    test_migrate_end(from, to, true);
}

static void test_multifd_tcp(const char *method)
{
    MigrateStart *args = migrate_start_new();
    QTestState *from, *to;
    QDict *rsp;
    char *uri;

    if (test_migrate_start(&from, &to, "defer", args)) {
        return;
    }

    /*
     * We want to pick a speed slow enough that the test completes
     * quickly, but that it doesn't complete precopy even on a slow
     * machine, so also set the downtime.
     */
    /* 1 ms should make it not converge*/
    migrate_set_parameter_int(from, "downtime-limit", 1);
    /* 1GB/s */
    migrate_set_parameter_int(from, "max-bandwidth", 1000000000);

    migrate_set_parameter_int(from, "multifd-channels", 16);
    migrate_set_parameter_int(to, "multifd-channels", 16);

    migrate_set_parameter_str(from, "multifd-compression", method);
    migrate_set_parameter_str(to, "multifd-compression", method);

    migrate_set_capability(from, "multifd", "true");
    migrate_set_capability(to, "multifd", "true");

    /* Start incoming migration from the 1st socket */
    rsp = wait_command(to, "{ 'execute': 'migrate-incoming',"
                           "  'arguments': { 'uri': 'tcp:127.0.0.1:0' }}");
    qobject_unref(rsp);

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    uri = migrate_get_socket_address(to, "socket-address");

    migrate_qmp(from, uri, "{}");

    wait_for_migration_pass(from);

    /* 300ms it should converge */
    migrate_set_parameter_int(from, "downtime-limit", 300);

    if (!got_stop) {
        qtest_qmp_eventwait(from, "STOP");
    }
    qtest_qmp_eventwait(to, "RESUME");

    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);
    test_migrate_end(from, to, true);
    g_free(uri);
}

static void test_multifd_tcp_none(void)
{
    test_multifd_tcp("none");
}

static void test_multifd_tcp_zlib(void)
{
    test_multifd_tcp("zlib");
}

#ifdef CONFIG_ZSTD
static void test_multifd_tcp_zstd(void)
{
    test_multifd_tcp("zstd");
}
#endif

/*
 * This test does:
 *  source               target
 *                       migrate_incoming
 *     migrate
 *     migrate_cancel
 *                       launch another target
 *     migrate
 *
 *  And see that it works
 */
static void test_multifd_tcp_cancel(void)
{
    MigrateStart *args = migrate_start_new();
    QTestState *from, *to, *to2;
    QDict *rsp;
    char *uri;

    args->hide_stderr = true;

    if (test_migrate_start(&from, &to, "defer", args)) {
        return;
    }

    /*
     * We want to pick a speed slow enough that the test completes
     * quickly, but that it doesn't complete precopy even on a slow
     * machine, so also set the downtime.
     */
    /* 1 ms should make it not converge*/
    migrate_set_parameter_int(from, "downtime-limit", 1);
    /* 300MB/s */
    migrate_set_parameter_int(from, "max-bandwidth", 30000000);

    migrate_set_parameter_int(from, "multifd-channels", 16);
    migrate_set_parameter_int(to, "multifd-channels", 16);

    migrate_set_capability(from, "multifd", "true");
    migrate_set_capability(to, "multifd", "true");

    /* Start incoming migration from the 1st socket */
    rsp = wait_command(to, "{ 'execute': 'migrate-incoming',"
                           "  'arguments': { 'uri': 'tcp:127.0.0.1:0' }}");
    qobject_unref(rsp);

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    uri = migrate_get_socket_address(to, "socket-address");

    migrate_qmp(from, uri, "{}");

    wait_for_migration_pass(from);

    migrate_cancel(from);

    args = migrate_start_new();
    args->only_target = true;

    if (test_migrate_start(&from, &to2, "defer", args)) {
        return;
    }

    migrate_set_parameter_int(to2, "multifd-channels", 16);

    migrate_set_capability(to2, "multifd", "true");

    /* Start incoming migration from the 1st socket */
    rsp = wait_command(to2, "{ 'execute': 'migrate-incoming',"
                            "  'arguments': { 'uri': 'tcp:127.0.0.1:0' }}");
    qobject_unref(rsp);

    g_free(uri);
    uri = migrate_get_socket_address(to2, "socket-address");

    wait_for_migration_status(from, "cancelled", NULL);

    /* 300ms it should converge */
    migrate_set_parameter_int(from, "downtime-limit", 300);
    /* 1GB/s */
    migrate_set_parameter_int(from, "max-bandwidth", 1000000000);

    migrate_qmp(from, uri, "{}");

    wait_for_migration_pass(from);

    if (!got_stop) {
        qtest_qmp_eventwait(from, "STOP");
    }
    qtest_qmp_eventwait(to2, "RESUME");

    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);
    test_migrate_end(from, to2, true);
    g_free(uri);
}

int main(int argc, char **argv)
{
    char template[] = "/tmp/migration-test-XXXXXX";
    int ret;

    g_test_init(&argc, &argv, NULL);

    if (!ufd_version_check()) {
        return g_test_run();
    }

    /*
     * On ppc64, the test only works with kvm-hv, but not with kvm-pr and TCG
     * is touchy due to race conditions on dirty bits (especially on PPC for
     * some reason)
     */
    if (g_str_equal(qtest_get_arch(), "ppc64") &&
        (access("/sys/module/kvm_hv", F_OK) ||
         access("/dev/kvm", R_OK | W_OK))) {
        g_test_message("Skipping test: kvm_hv not available");
        return g_test_run();
    }

    /*
     * Similar to ppc64, s390x seems to be touchy with TCG, so disable it
     * there until the problems are resolved
     */
    if (g_str_equal(qtest_get_arch(), "s390x")) {
#if defined(HOST_S390X)
        if (access("/dev/kvm", R_OK | W_OK)) {
            g_test_message("Skipping test: kvm not available");
            return g_test_run();
        }
#else
        g_test_message("Skipping test: Need s390x host to work properly");
        return g_test_run();
#endif
    }

    tmpfs = mkdtemp(template);
    if (!tmpfs) {
        g_test_message("mkdtemp on path (%s): %s", template, strerror(errno));
    }
    g_assert(tmpfs);

    module_call_init(MODULE_INIT_QOM);

    qtest_add_func("/migration/postcopy/unix", test_postcopy);
    qtest_add_func("/migration/postcopy/recovery", test_postcopy_recovery);
    qtest_add_func("/migration/deprecated", test_deprecated);
    qtest_add_func("/migration/bad_dest", test_baddest);
    qtest_add_func("/migration/precopy/unix", test_precopy_unix);
    qtest_add_func("/migration/precopy/tcp", test_precopy_tcp);
    /* qtest_add_func("/migration/ignore_shared", test_ignore_shared); */
    qtest_add_func("/migration/xbzrle/unix", test_xbzrle_unix);
    qtest_add_func("/migration/fd_proto", test_migrate_fd_proto);
    qtest_add_func("/migration/validate_uuid", test_validate_uuid);
    qtest_add_func("/migration/validate_uuid_error", test_validate_uuid_error);
    qtest_add_func("/migration/validate_uuid_src_not_set",
                   test_validate_uuid_src_not_set);
    qtest_add_func("/migration/validate_uuid_dst_not_set",
                   test_validate_uuid_dst_not_set);

    qtest_add_func("/migration/auto_converge", test_migrate_auto_converge);
    qtest_add_func("/migration/multifd/tcp/none", test_multifd_tcp_none);
    qtest_add_func("/migration/multifd/tcp/cancel", test_multifd_tcp_cancel);
    qtest_add_func("/migration/multifd/tcp/zlib", test_multifd_tcp_zlib);
#ifdef CONFIG_ZSTD
    qtest_add_func("/migration/multifd/tcp/zstd", test_multifd_tcp_zstd);
#endif

    ret = g_test_run();

    g_assert_cmpint(ret, ==, 0);

    ret = rmdir(tmpfs);
    if (ret != 0) {
        g_test_message("unable to rmdir: path (%s): %s",
                       tmpfs, strerror(errno));
    }

    return ret;
}
