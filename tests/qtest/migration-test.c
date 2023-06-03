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
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/range.h"
#include "qemu/sockets.h"
#include "chardev/char.h"
#include "qapi/qapi-visit-sockets.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "crypto/tlscredspsk.h"
#include "qapi/qmp/qlist.h"

#include "migration-helpers.h"
#include "tests/migration/migration-test.h"
#ifdef CONFIG_GNUTLS
# include "tests/unit/crypto-tls-psk-helpers.h"
# ifdef CONFIG_TASN1
#  include "tests/unit/crypto-tls-x509-helpers.h"
# endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

/* For dirty ring test; so far only x86_64 is supported */
#if defined(__linux__) && defined(HOST_X86_64)
#include "linux/kvm.h"
#endif

unsigned start_address;
unsigned end_address;
static bool uffd_feature_thread_id;
static bool got_src_stop;
static bool got_dst_resume;

/*
 * Dirtylimit stop working if dirty page rate error
 * value less than DIRTYLIMIT_TOLERANCE_RANGE
 */
#define DIRTYLIMIT_TOLERANCE_RANGE  25  /* MB/s */

#if defined(__linux__)
#include <sys/syscall.h>
#include <sys/vfs.h>
#endif

#if defined(__linux__) && defined(__NR_userfaultfd) && defined(CONFIG_EVENTFD)
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include "qemu/userfaultfd.h"

static bool ufd_version_check(void)
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

static char *tmpfs;

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
    g_autofree char *serialpath = g_strdup_printf("%s/%s", tmpfs, side);
    FILE *serialfile = fopen(serialpath, "r");
    const char *arch = qtest_get_arch();
    int started = (strcmp(side, "src_serial") == 0 &&
                   strcmp(arch, "ppc64") == 0) ? 0 : 1;

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

static int64_t read_migrate_property_int(QTestState *who, const char *property)
{
    QDict *rsp_return;
    int64_t result;

    rsp_return = migrate_query_not_failed(who);
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

    rsp_return = migrate_query_not_failed(who);
    g_assert(qdict_haskey(rsp_return, "postcopy-blocktime"));
    qobject_unref(rsp_return);
}

static void wait_for_migration_pass(QTestState *who)
{
    uint64_t initial_pass = get_migration_pass(who);
    uint64_t pass;

    /* Wait for the 1st sync */
    while (!got_src_stop && !initial_pass) {
        usleep(1000);
        initial_pass = get_migration_pass(who);
    }

    do {
        usleep(1000);
        pass = get_migration_pass(who);
    } while (pass == initial_pass && !got_src_stop);
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
    g_autofree char *path = g_strdup_printf("%s/%s", tmpfs, filename);

    unlink(path);
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
    SocketAddressList *addrs;
    Visitor *iv = NULL;
    QObject *object;

    rsp = migrate_query(who);
    object = qdict_get(rsp, parameter);

    iv = qobject_input_visitor_new(object);
    visit_type_SocketAddressList(iv, NULL, &addrs, &error_abort);
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

static void migrate_set_parameter_int(QTestState *who, const char *parameter,
                                      long long value)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate-set-parameters',"
                             "'arguments': { %s: %lld } }",
                             parameter, value);
    migrate_check_parameter_int(who, parameter, value);
}

static char *migrate_get_parameter_str(QTestState *who,
                                       const char *parameter)
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

static void migrate_set_parameter_str(QTestState *who, const char *parameter,
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

static void migrate_set_parameter_bool(QTestState *who, const char *parameter,
                                      int value)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate-set-parameters',"
                             "'arguments': { %s: %i } }",
                             parameter, value);
    migrate_check_parameter_bool(who, parameter, value);
}

static void migrate_ensure_non_converge(QTestState *who)
{
    /* Can't converge with 1ms downtime + 3 mbs bandwidth limit */
    migrate_set_parameter_int(who, "max-bandwidth", 3 * 1000 * 1000);
    migrate_set_parameter_int(who, "downtime-limit", 1);
}

static void migrate_ensure_converge(QTestState *who)
{
    /* Should converge with 30s downtime + 1 gbs bandwidth limit */
    migrate_set_parameter_int(who, "max-bandwidth", 1 * 1000 * 1000 * 1000);
    migrate_set_parameter_int(who, "downtime-limit", 30 * 1000);
}

static void migrate_pause(QTestState *who)
{
    qtest_qmp_assert_success(who, "{ 'execute': 'migrate-pause' }");
}

static void migrate_continue(QTestState *who, const char *state)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate-continue',"
                             "  'arguments': { 'state': %s } }",
                             state);
}

static void migrate_recover(QTestState *who, const char *uri)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate-recover', "
                             "  'id': 'recover-cmd', "
                             "  'arguments': { 'uri': %s } }",
                             uri);
}

static void migrate_cancel(QTestState *who)
{
    qtest_qmp_assert_success(who, "{ 'execute': 'migrate_cancel' }");
}

static void migrate_set_capability(QTestState *who, const char *capability,
                                   bool value)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'migrate-set-capabilities',"
                             "'arguments': { "
                             "'capabilities': [ { "
                             "'capability': %s, 'state': %i } ] } }",
                             capability, value);
}

static void migrate_postcopy_start(QTestState *from, QTestState *to)
{
    qtest_qmp_assert_success(from, "{ 'execute': 'migrate-start-postcopy' }");

    if (!got_src_stop) {
        qtest_qmp_eventwait(from, "STOP");
    }

    qtest_qmp_eventwait(to, "RESUME");
}

typedef struct {
    /*
     * QTEST_LOG=1 may override this.  When QTEST_LOG=1, we always dump errors
     * unconditionally, because it means the user would like to be verbose.
     */
    bool hide_stderr;
    bool use_shmem;
    /* only launch the target process */
    bool only_target;
    /* Use dirty ring if true; dirty logging otherwise */
    bool use_dirty_ring;
    const char *opts_source;
    const char *opts_target;
} MigrateStart;

/*
 * A hook that runs after the src and dst QEMUs have been
 * created, but before the migration is started. This can
 * be used to set migration parameters and capabilities.
 *
 * Returns: NULL, or a pointer to opaque state to be
 *          later passed to the TestMigrateFinishHook
 */
typedef void * (*TestMigrateStartHook)(QTestState *from,
                                       QTestState *to);

/*
 * A hook that runs after the migration has finished,
 * regardless of whether it succeeded or failed, but
 * before QEMU has terminated (unless it self-terminated
 * due to migration error)
 *
 * @opaque is a pointer to state previously returned
 * by the TestMigrateStartHook if any, or NULL.
 */
typedef void (*TestMigrateFinishHook)(QTestState *from,
                                      QTestState *to,
                                      void *opaque);

typedef struct {
    /* Optional: fine tune start parameters */
    MigrateStart start;

    /* Required: the URI for the dst QEMU to listen on */
    const char *listen_uri;

    /*
     * Optional: the URI for the src QEMU to connect to
     * If NULL, then it will query the dst QEMU for its actual
     * listening address and use that as the connect address.
     * This allows for dynamically picking a free TCP port.
     */
    const char *connect_uri;

    /* Optional: callback to run at start to set migration parameters */
    TestMigrateStartHook start_hook;
    /* Optional: callback to run at finish to cleanup */
    TestMigrateFinishHook finish_hook;

    /*
     * Optional: normally we expect the migration process to complete.
     *
     * There can be a variety of reasons and stages in which failure
     * can happen during tests.
     *
     * If a failure is expected to happen at time of establishing
     * the connection, then MIG_TEST_FAIL will indicate that the dst
     * QEMU is expected to stay running and accept future migration
     * connections.
     *
     * If a failure is expected to happen while processing the
     * migration stream, then MIG_TEST_FAIL_DEST_QUIT_ERR will indicate
     * that the dst QEMU is expected to quit with non-zero exit status
     */
    enum {
        /* This test should succeed, the default */
        MIG_TEST_SUCCEED = 0,
        /* This test should fail, dest qemu should keep alive */
        MIG_TEST_FAIL,
        /* This test should fail, dest qemu should fail with abnormal status */
        MIG_TEST_FAIL_DEST_QUIT_ERR,
    } result;

    /* Optional: set number of migration passes to wait for, if live==true */
    unsigned int iterations;

    /*
     * Optional: whether the guest CPUs should be running during a precopy
     * migration test.  We used to always run with live but it took much
     * longer so we reduced live tests to only the ones that have solid
     * reason to be tested live-only.  For each of the new test cases for
     * precopy please provide justifications to use live explicitly (please
     * refer to existing ones with live=true), or use live=off by default.
     */
    bool live;

    /* Postcopy specific fields */
    void *postcopy_data;
    bool postcopy_preempt;
} MigrateCommon;

static int test_migrate_start(QTestState **from, QTestState **to,
                              const char *uri, MigrateStart *args)
{
    g_autofree gchar *arch_source = NULL;
    g_autofree gchar *arch_target = NULL;
    g_autofree gchar *cmd_source = NULL;
    g_autofree gchar *cmd_target = NULL;
    const gchar *ignore_stderr;
    g_autofree char *bootpath = NULL;
    g_autofree char *shmem_opts = NULL;
    g_autofree char *shmem_path = NULL;
    const char *arch = qtest_get_arch();
    const char *machine_opts = NULL;
    const char *memory_size;

    if (args->use_shmem) {
        if (!g_file_test("/dev/shm", G_FILE_TEST_IS_DIR)) {
            g_test_skip("/dev/shm is not supported");
            return -1;
        }
    }

    got_src_stop = false;
    got_dst_resume = false;
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

    if (!getenv("QTEST_LOG") && args->hide_stderr) {
#ifndef _WIN32
        ignore_stderr = "2>/dev/null";
#else
        /*
         * On Windows the QEMU executable is created via CreateProcess() and
         * IO redirection does not work, so don't bother adding IO redirection
         * to the command line.
         */
        ignore_stderr = "";
#endif
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

    cmd_source = g_strdup_printf("-accel kvm%s -accel tcg%s%s "
                                 "-name source,debug-threads=on "
                                 "-m %s "
                                 "-serial file:%s/src_serial "
                                 "%s %s %s %s",
                                 args->use_dirty_ring ?
                                 ",dirty-ring-size=4096" : "",
                                 machine_opts ? " -machine " : "",
                                 machine_opts ? machine_opts : "",
                                 memory_size, tmpfs,
                                 arch_source, shmem_opts,
                                 args->opts_source ? args->opts_source : "",
                                 ignore_stderr);
    if (!args->only_target) {
        *from = qtest_init(cmd_source);
        qtest_qmp_set_event_callback(*from,
                                     migrate_watch_for_stop,
                                     &got_src_stop);
    }

    cmd_target = g_strdup_printf("-accel kvm%s -accel tcg%s%s "
                                 "-name target,debug-threads=on "
                                 "-m %s "
                                 "-serial file:%s/dest_serial "
                                 "-incoming %s "
                                 "%s %s %s %s",
                                 args->use_dirty_ring ?
                                 ",dirty-ring-size=4096" : "",
                                 machine_opts ? " -machine " : "",
                                 machine_opts ? machine_opts : "",
                                 memory_size, tmpfs, uri,
                                 arch_target, shmem_opts,
                                 args->opts_target ? args->opts_target : "",
                                 ignore_stderr);
    *to = qtest_init(cmd_target);
    qtest_qmp_set_event_callback(*to,
                                 migrate_watch_for_resume,
                                 &got_dst_resume);

    /*
     * Remove shmem file immediately to avoid memory leak in test failed case.
     * It's valid becase QEMU has already opened this file
     */
    if (args->use_shmem) {
        unlink(shmem_path);
    }

    return 0;
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

        qtest_qmp_assert_success(to, "{ 'execute' : 'stop'}");

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

#ifdef CONFIG_GNUTLS
struct TestMigrateTLSPSKData {
    char *workdir;
    char *workdiralt;
    char *pskfile;
    char *pskfilealt;
};

static void *
test_migrate_tls_psk_start_common(QTestState *from,
                                  QTestState *to,
                                  bool mismatch)
{
    struct TestMigrateTLSPSKData *data =
        g_new0(struct TestMigrateTLSPSKData, 1);

    data->workdir = g_strdup_printf("%s/tlscredspsk0", tmpfs);
    data->pskfile = g_strdup_printf("%s/%s", data->workdir,
                                    QCRYPTO_TLS_CREDS_PSKFILE);
    g_mkdir_with_parents(data->workdir, 0700);
    test_tls_psk_init(data->pskfile);

    if (mismatch) {
        data->workdiralt = g_strdup_printf("%s/tlscredspskalt0", tmpfs);
        data->pskfilealt = g_strdup_printf("%s/%s", data->workdiralt,
                                           QCRYPTO_TLS_CREDS_PSKFILE);
        g_mkdir_with_parents(data->workdiralt, 0700);
        test_tls_psk_init_alt(data->pskfilealt);
    }

    qtest_qmp_assert_success(from,
                             "{ 'execute': 'object-add',"
                             "  'arguments': { 'qom-type': 'tls-creds-psk',"
                             "                 'id': 'tlscredspsk0',"
                             "                 'endpoint': 'client',"
                             "                 'dir': %s,"
                             "                 'username': 'qemu'} }",
                             data->workdir);

    qtest_qmp_assert_success(to,
                             "{ 'execute': 'object-add',"
                             "  'arguments': { 'qom-type': 'tls-creds-psk',"
                             "                 'id': 'tlscredspsk0',"
                             "                 'endpoint': 'server',"
                             "                 'dir': %s } }",
                             mismatch ? data->workdiralt : data->workdir);

    migrate_set_parameter_str(from, "tls-creds", "tlscredspsk0");
    migrate_set_parameter_str(to, "tls-creds", "tlscredspsk0");

    return data;
}

static void *
test_migrate_tls_psk_start_match(QTestState *from,
                                 QTestState *to)
{
    return test_migrate_tls_psk_start_common(from, to, false);
}

static void *
test_migrate_tls_psk_start_mismatch(QTestState *from,
                                    QTestState *to)
{
    return test_migrate_tls_psk_start_common(from, to, true);
}

static void
test_migrate_tls_psk_finish(QTestState *from,
                            QTestState *to,
                            void *opaque)
{
    struct TestMigrateTLSPSKData *data = opaque;

    test_tls_psk_cleanup(data->pskfile);
    if (data->pskfilealt) {
        test_tls_psk_cleanup(data->pskfilealt);
    }
    rmdir(data->workdir);
    if (data->workdiralt) {
        rmdir(data->workdiralt);
    }

    g_free(data->workdiralt);
    g_free(data->pskfilealt);
    g_free(data->workdir);
    g_free(data->pskfile);
    g_free(data);
}

#ifdef CONFIG_TASN1
typedef struct {
    char *workdir;
    char *keyfile;
    char *cacert;
    char *servercert;
    char *serverkey;
    char *clientcert;
    char *clientkey;
} TestMigrateTLSX509Data;

typedef struct {
    bool verifyclient;
    bool clientcert;
    bool hostileclient;
    bool authzclient;
    const char *certhostname;
    const char *certipaddr;
} TestMigrateTLSX509;

static void *
test_migrate_tls_x509_start_common(QTestState *from,
                                   QTestState *to,
                                   TestMigrateTLSX509 *args)
{
    TestMigrateTLSX509Data *data = g_new0(TestMigrateTLSX509Data, 1);

    data->workdir = g_strdup_printf("%s/tlscredsx5090", tmpfs);
    data->keyfile = g_strdup_printf("%s/key.pem", data->workdir);

    data->cacert = g_strdup_printf("%s/ca-cert.pem", data->workdir);
    data->serverkey = g_strdup_printf("%s/server-key.pem", data->workdir);
    data->servercert = g_strdup_printf("%s/server-cert.pem", data->workdir);
    if (args->clientcert) {
        data->clientkey = g_strdup_printf("%s/client-key.pem", data->workdir);
        data->clientcert = g_strdup_printf("%s/client-cert.pem", data->workdir);
    }

    g_mkdir_with_parents(data->workdir, 0700);

    test_tls_init(data->keyfile);
#ifndef _WIN32
    g_assert(link(data->keyfile, data->serverkey) == 0);
#else
    g_assert(CreateHardLink(data->serverkey, data->keyfile, NULL) != 0);
#endif
    if (args->clientcert) {
#ifndef _WIN32
        g_assert(link(data->keyfile, data->clientkey) == 0);
#else
        g_assert(CreateHardLink(data->clientkey, data->keyfile, NULL) != 0);
#endif
    }

    TLS_ROOT_REQ_SIMPLE(cacertreq, data->cacert);
    if (args->clientcert) {
        TLS_CERT_REQ_SIMPLE_CLIENT(servercertreq, cacertreq,
                                   args->hostileclient ?
                                   QCRYPTO_TLS_TEST_CLIENT_HOSTILE_NAME :
                                   QCRYPTO_TLS_TEST_CLIENT_NAME,
                                   data->clientcert);
    }

    TLS_CERT_REQ_SIMPLE_SERVER(clientcertreq, cacertreq,
                               data->servercert,
                               args->certhostname,
                               args->certipaddr);

    qtest_qmp_assert_success(from,
                             "{ 'execute': 'object-add',"
                             "  'arguments': { 'qom-type': 'tls-creds-x509',"
                             "                 'id': 'tlscredsx509client0',"
                             "                 'endpoint': 'client',"
                             "                 'dir': %s,"
                             "                 'sanity-check': true,"
                             "                 'verify-peer': true} }",
                             data->workdir);
    migrate_set_parameter_str(from, "tls-creds", "tlscredsx509client0");
    if (args->certhostname) {
        migrate_set_parameter_str(from, "tls-hostname", args->certhostname);
    }

    qtest_qmp_assert_success(to,
                             "{ 'execute': 'object-add',"
                             "  'arguments': { 'qom-type': 'tls-creds-x509',"
                             "                 'id': 'tlscredsx509server0',"
                             "                 'endpoint': 'server',"
                             "                 'dir': %s,"
                             "                 'sanity-check': true,"
                             "                 'verify-peer': %i} }",
                             data->workdir, args->verifyclient);
    migrate_set_parameter_str(to, "tls-creds", "tlscredsx509server0");

    if (args->authzclient) {
        qtest_qmp_assert_success(to,
                                 "{ 'execute': 'object-add',"
                                 "  'arguments': { 'qom-type': 'authz-simple',"
                                 "                 'id': 'tlsauthz0',"
                                 "                 'identity': %s} }",
                                 "CN=" QCRYPTO_TLS_TEST_CLIENT_NAME);
        migrate_set_parameter_str(to, "tls-authz", "tlsauthz0");
    }

    return data;
}

/*
 * The normal case: match server's cert hostname against
 * whatever host we were telling QEMU to connect to (if any)
 */
static void *
test_migrate_tls_x509_start_default_host(QTestState *from,
                                         QTestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .clientcert = true,
        .certipaddr = "127.0.0.1"
    };
    return test_migrate_tls_x509_start_common(from, to, &args);
}

/*
 * The unusual case: the server's cert is different from
 * the address we're telling QEMU to connect to (if any),
 * so we must give QEMU an explicit hostname to validate
 */
static void *
test_migrate_tls_x509_start_override_host(QTestState *from,
                                          QTestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .clientcert = true,
        .certhostname = "qemu.org",
    };
    return test_migrate_tls_x509_start_common(from, to, &args);
}

/*
 * The unusual case: the server's cert is different from
 * the address we're telling QEMU to connect to, and so we
 * expect the client to reject the server
 */
static void *
test_migrate_tls_x509_start_mismatch_host(QTestState *from,
                                          QTestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .clientcert = true,
        .certipaddr = "10.0.0.1",
    };
    return test_migrate_tls_x509_start_common(from, to, &args);
}

static void *
test_migrate_tls_x509_start_friendly_client(QTestState *from,
                                            QTestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .clientcert = true,
        .authzclient = true,
        .certipaddr = "127.0.0.1",
    };
    return test_migrate_tls_x509_start_common(from, to, &args);
}

static void *
test_migrate_tls_x509_start_hostile_client(QTestState *from,
                                           QTestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .clientcert = true,
        .hostileclient = true,
        .authzclient = true,
        .certipaddr = "127.0.0.1",
    };
    return test_migrate_tls_x509_start_common(from, to, &args);
}

/*
 * The case with no client certificate presented,
 * and no server verification
 */
static void *
test_migrate_tls_x509_start_allow_anon_client(QTestState *from,
                                              QTestState *to)
{
    TestMigrateTLSX509 args = {
        .certipaddr = "127.0.0.1",
    };
    return test_migrate_tls_x509_start_common(from, to, &args);
}

/*
 * The case with no client certificate presented,
 * and server verification rejecting
 */
static void *
test_migrate_tls_x509_start_reject_anon_client(QTestState *from,
                                               QTestState *to)
{
    TestMigrateTLSX509 args = {
        .verifyclient = true,
        .certipaddr = "127.0.0.1",
    };
    return test_migrate_tls_x509_start_common(from, to, &args);
}

static void
test_migrate_tls_x509_finish(QTestState *from,
                             QTestState *to,
                             void *opaque)
{
    TestMigrateTLSX509Data *data = opaque;

    test_tls_cleanup(data->keyfile);
    g_free(data->keyfile);

    unlink(data->cacert);
    g_free(data->cacert);
    unlink(data->servercert);
    g_free(data->servercert);
    unlink(data->serverkey);
    g_free(data->serverkey);

    if (data->clientcert) {
        unlink(data->clientcert);
        g_free(data->clientcert);
    }
    if (data->clientkey) {
        unlink(data->clientkey);
        g_free(data->clientkey);
    }

    rmdir(data->workdir);
    g_free(data->workdir);

    g_free(data);
}
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

static void *
test_migrate_compress_start(QTestState *from,
                            QTestState *to)
{
    migrate_set_parameter_int(from, "compress-level", 1);
    migrate_set_parameter_int(from, "compress-threads", 4);
    migrate_set_parameter_bool(from, "compress-wait-thread", true);
    migrate_set_parameter_int(to, "decompress-threads", 4);

    migrate_set_capability(from, "compress", true);
    migrate_set_capability(to, "compress", true);

    return NULL;
}

static void *
test_migrate_compress_nowait_start(QTestState *from,
                                   QTestState *to)
{
    migrate_set_parameter_int(from, "compress-level", 9);
    migrate_set_parameter_int(from, "compress-threads", 1);
    migrate_set_parameter_bool(from, "compress-wait-thread", false);
    migrate_set_parameter_int(to, "decompress-threads", 1);

    migrate_set_capability(from, "compress", true);
    migrate_set_capability(to, "compress", true);

    return NULL;
}

static int migrate_postcopy_prepare(QTestState **from_ptr,
                                    QTestState **to_ptr,
                                    MigrateCommon *args)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    QTestState *from, *to;

    if (test_migrate_start(&from, &to, uri, &args->start)) {
        return -1;
    }

    if (args->start_hook) {
        args->postcopy_data = args->start_hook(from, to);
    }

    migrate_set_capability(from, "postcopy-ram", true);
    migrate_set_capability(to, "postcopy-ram", true);
    migrate_set_capability(to, "postcopy-blocktime", true);

    if (args->postcopy_preempt) {
        migrate_set_capability(from, "postcopy-preempt", true);
        migrate_set_capability(to, "postcopy-preempt", true);
    }

    migrate_ensure_non_converge(from);

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    migrate_qmp(from, uri, "{}");

    wait_for_migration_pass(from);

    *from_ptr = from;
    *to_ptr = to;

    return 0;
}

static void migrate_postcopy_complete(QTestState *from, QTestState *to,
                                      MigrateCommon *args)
{
    wait_for_migration_complete(from);

    /* Make sure we get at least one "B" on destination */
    wait_for_serial("dest_serial");

    if (uffd_feature_thread_id) {
        read_blocktime(to);
    }

    if (args->finish_hook) {
        args->finish_hook(from, to, args->postcopy_data);
        args->postcopy_data = NULL;
    }

    test_migrate_end(from, to, true);
}

static void test_postcopy_common(MigrateCommon *args)
{
    QTestState *from, *to;

    if (migrate_postcopy_prepare(&from, &to, args)) {
        return;
    }
    migrate_postcopy_start(from, to);
    migrate_postcopy_complete(from, to, args);
}

static void test_postcopy(void)
{
    MigrateCommon args = { };

    test_postcopy_common(&args);
}

static void test_postcopy_compress(void)
{
    MigrateCommon args = {
        .start_hook = test_migrate_compress_start
    };

    test_postcopy_common(&args);
}

static void test_postcopy_preempt(void)
{
    MigrateCommon args = {
        .postcopy_preempt = true,
    };

    test_postcopy_common(&args);
}

#ifdef CONFIG_GNUTLS
static void test_postcopy_tls_psk(void)
{
    MigrateCommon args = {
        .start_hook = test_migrate_tls_psk_start_match,
        .finish_hook = test_migrate_tls_psk_finish,
    };

    test_postcopy_common(&args);
}

static void test_postcopy_preempt_tls_psk(void)
{
    MigrateCommon args = {
        .postcopy_preempt = true,
        .start_hook = test_migrate_tls_psk_start_match,
        .finish_hook = test_migrate_tls_psk_finish,
    };

    test_postcopy_common(&args);
}
#endif

static void test_postcopy_recovery_common(MigrateCommon *args)
{
    QTestState *from, *to;
    g_autofree char *uri = NULL;

    /* Always hide errors for postcopy recover tests since they're expected */
    args->start.hide_stderr = true;

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

    /* Restore the postcopy bandwidth to unlimited */
    migrate_set_parameter_int(from, "max-postcopy-bandwidth", 0);

    migrate_postcopy_complete(from, to, args);
}

static void test_postcopy_recovery(void)
{
    MigrateCommon args = { };

    test_postcopy_recovery_common(&args);
}

static void test_postcopy_recovery_compress(void)
{
    MigrateCommon args = {
        .start_hook = test_migrate_compress_start
    };

    test_postcopy_recovery_common(&args);
}

#ifdef CONFIG_GNUTLS
static void test_postcopy_recovery_tls_psk(void)
{
    MigrateCommon args = {
        .start_hook = test_migrate_tls_psk_start_match,
        .finish_hook = test_migrate_tls_psk_finish,
    };

    test_postcopy_recovery_common(&args);
}
#endif

static void test_postcopy_preempt_recovery(void)
{
    MigrateCommon args = {
        .postcopy_preempt = true,
    };

    test_postcopy_recovery_common(&args);
}

#ifdef CONFIG_GNUTLS
/* This contains preempt+recovery+tls test altogether */
static void test_postcopy_preempt_all(void)
{
    MigrateCommon args = {
        .postcopy_preempt = true,
        .start_hook = test_migrate_tls_psk_start_match,
        .finish_hook = test_migrate_tls_psk_finish,
    };

    test_postcopy_recovery_common(&args);
}

#endif

static void test_baddest(void)
{
    MigrateStart args = {
        .hide_stderr = true
    };
    QTestState *from, *to;

    if (test_migrate_start(&from, &to, "tcp:127.0.0.1:0", &args)) {
        return;
    }
    migrate_qmp(from, "tcp:127.0.0.1:0", "{}");
    wait_for_migration_fail(from, false);
    test_migrate_end(from, to, false);
}

static void test_precopy_common(MigrateCommon *args)
{
    QTestState *from, *to;
    void *data_hook = NULL;

    if (test_migrate_start(&from, &to, args->listen_uri, &args->start)) {
        return;
    }

    if (args->start_hook) {
        data_hook = args->start_hook(from, to);
    }

    /* Wait for the first serial output from the source */
    if (args->result == MIG_TEST_SUCCEED) {
        wait_for_serial("src_serial");
    }

    if (args->live) {
        /*
         * Testing live migration, we want to ensure that some
         * memory is re-dirtied after being transferred, so that
         * we exercise logic for dirty page handling. We achieve
         * this with a ridiculosly low bandwidth that guarantees
         * non-convergance.
         */
        migrate_ensure_non_converge(from);
    } else {
        /*
         * Testing non-live migration, we allow it to run at
         * full speed to ensure short test case duration.
         * For tests expected to fail, we don't need to
         * change anything.
         */
        if (args->result == MIG_TEST_SUCCEED) {
            qtest_qmp_assert_success(from, "{ 'execute' : 'stop'}");
            if (!got_src_stop) {
                qtest_qmp_eventwait(from, "STOP");
            }
            migrate_ensure_converge(from);
        }
    }

    if (!args->connect_uri) {
        g_autofree char *local_connect_uri =
            migrate_get_socket_address(to, "socket-address");
        migrate_qmp(from, local_connect_uri, "{}");
    } else {
        migrate_qmp(from, args->connect_uri, "{}");
    }


    if (args->result != MIG_TEST_SUCCEED) {
        bool allow_active = args->result == MIG_TEST_FAIL;
        wait_for_migration_fail(from, allow_active);

        if (args->result == MIG_TEST_FAIL_DEST_QUIT_ERR) {
            qtest_set_expected_status(to, EXIT_FAILURE);
        }
    } else {
        if (args->live) {
            if (args->iterations) {
                while (args->iterations--) {
                    wait_for_migration_pass(from);
                }
            } else {
                wait_for_migration_pass(from);
            }

            migrate_ensure_converge(from);

            /*
             * We do this first, as it has a timeout to stop us
             * hanging forever if migration didn't converge
             */
            wait_for_migration_complete(from);

            if (!got_src_stop) {
                qtest_qmp_eventwait(from, "STOP");
            }
        } else {
            wait_for_migration_complete(from);
            /*
             * Must wait for dst to finish reading all incoming
             * data on the socket before issuing 'cont' otherwise
             * it'll be ignored
             */
            wait_for_migration_complete(to);

            qtest_qmp_assert_success(to, "{ 'execute' : 'cont'}");
        }

        if (!got_dst_resume) {
            qtest_qmp_eventwait(to, "RESUME");
        }

        wait_for_serial("dest_serial");
    }

    if (args->finish_hook) {
        args->finish_hook(from, to, data_hook);
    }

    test_migrate_end(from, to, args->result == MIG_TEST_SUCCEED);
}

static void test_precopy_unix_plain(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .listen_uri = uri,
        .connect_uri = uri,
        /*
         * The simplest use case of precopy, covering smoke tests of
         * get-dirty-log dirty tracking.
         */
        .live = true,
    };

    test_precopy_common(&args);
}


static void test_precopy_unix_dirty_ring(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .start = {
            .use_dirty_ring = true,
        },
        .listen_uri = uri,
        .connect_uri = uri,
        /*
         * Besides the precopy/unix basic test, cover dirty ring interface
         * rather than get-dirty-log.
         */
        .live = true,
    };

    test_precopy_common(&args);
}

#ifdef CONFIG_GNUTLS
static void test_precopy_unix_tls_psk(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = uri,
        .start_hook = test_migrate_tls_psk_start_match,
        .finish_hook = test_migrate_tls_psk_finish,
    };

    test_precopy_common(&args);
}

#ifdef CONFIG_TASN1
static void test_precopy_unix_tls_x509_default_host(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .connect_uri = uri,
        .listen_uri = uri,
        .start_hook = test_migrate_tls_x509_start_default_host,
        .finish_hook = test_migrate_tls_x509_finish,
        .result = MIG_TEST_FAIL_DEST_QUIT_ERR,
    };

    test_precopy_common(&args);
}

static void test_precopy_unix_tls_x509_override_host(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = uri,
        .start_hook = test_migrate_tls_x509_start_override_host,
        .finish_hook = test_migrate_tls_x509_finish,
    };

    test_precopy_common(&args);
}
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

#if 0
/* Currently upset on aarch64 TCG */
static void test_ignore_shared(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
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

    if (!got_src_stop) {
        qtest_qmp_eventwait(from, "STOP");
    }

    qtest_qmp_eventwait(to, "RESUME");

    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);

    /* Check whether shared RAM has been really skipped */
    g_assert_cmpint(read_ram_property_int(from, "transferred"), <, 1024 * 1024);

    test_migrate_end(from, to, true);
}
#endif

static void *
test_migrate_xbzrle_start(QTestState *from,
                          QTestState *to)
{
    migrate_set_parameter_int(from, "xbzrle-cache-size", 33554432);

    migrate_set_capability(from, "xbzrle", true);
    migrate_set_capability(to, "xbzrle", true);

    return NULL;
}

static void test_precopy_unix_xbzrle(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = uri,
        .start_hook = test_migrate_xbzrle_start,
        .iterations = 2,
        /*
         * XBZRLE needs pages to be modified when doing the 2nd+ round
         * iteration to have real data pushed to the stream.
         */
        .live = true,
    };

    test_precopy_common(&args);
}

static void test_precopy_unix_compress(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = uri,
        .start_hook = test_migrate_compress_start,
        /*
         * Test that no invalid thread state is left over from
         * the previous iteration.
         */
        .iterations = 2,
        /*
         * We make sure the compressor can always work well even if guest
         * memory is changing.  See commit 34ab9e9743 where we used to fix
         * a bug when only trigger-able with guest memory changing.
         */
        .live = true,
    };

    test_precopy_common(&args);
}

static void test_precopy_unix_compress_nowait(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = uri,
        .start_hook = test_migrate_compress_nowait_start,
        /*
         * Test that no invalid thread state is left over from
         * the previous iteration.
         */
        .iterations = 2,
        /* Same reason for the wait version of precopy compress test */
        .live = true,
    };

    test_precopy_common(&args);
}

static void test_precopy_tcp_plain(void)
{
    MigrateCommon args = {
        .listen_uri = "tcp:127.0.0.1:0",
    };

    test_precopy_common(&args);
}

#ifdef CONFIG_GNUTLS
static void test_precopy_tcp_tls_psk_match(void)
{
    MigrateCommon args = {
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = test_migrate_tls_psk_start_match,
        .finish_hook = test_migrate_tls_psk_finish,
    };

    test_precopy_common(&args);
}

static void test_precopy_tcp_tls_psk_mismatch(void)
{
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = test_migrate_tls_psk_start_mismatch,
        .finish_hook = test_migrate_tls_psk_finish,
        .result = MIG_TEST_FAIL,
    };

    test_precopy_common(&args);
}

#ifdef CONFIG_TASN1
static void test_precopy_tcp_tls_x509_default_host(void)
{
    MigrateCommon args = {
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = test_migrate_tls_x509_start_default_host,
        .finish_hook = test_migrate_tls_x509_finish,
    };

    test_precopy_common(&args);
}

static void test_precopy_tcp_tls_x509_override_host(void)
{
    MigrateCommon args = {
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = test_migrate_tls_x509_start_override_host,
        .finish_hook = test_migrate_tls_x509_finish,
    };

    test_precopy_common(&args);
}

static void test_precopy_tcp_tls_x509_mismatch_host(void)
{
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = test_migrate_tls_x509_start_mismatch_host,
        .finish_hook = test_migrate_tls_x509_finish,
        .result = MIG_TEST_FAIL_DEST_QUIT_ERR,
    };

    test_precopy_common(&args);
}

static void test_precopy_tcp_tls_x509_friendly_client(void)
{
    MigrateCommon args = {
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = test_migrate_tls_x509_start_friendly_client,
        .finish_hook = test_migrate_tls_x509_finish,
    };

    test_precopy_common(&args);
}

static void test_precopy_tcp_tls_x509_hostile_client(void)
{
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = test_migrate_tls_x509_start_hostile_client,
        .finish_hook = test_migrate_tls_x509_finish,
        .result = MIG_TEST_FAIL,
    };

    test_precopy_common(&args);
}

static void test_precopy_tcp_tls_x509_allow_anon_client(void)
{
    MigrateCommon args = {
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = test_migrate_tls_x509_start_allow_anon_client,
        .finish_hook = test_migrate_tls_x509_finish,
    };

    test_precopy_common(&args);
}

static void test_precopy_tcp_tls_x509_reject_anon_client(void)
{
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = test_migrate_tls_x509_start_reject_anon_client,
        .finish_hook = test_migrate_tls_x509_finish,
        .result = MIG_TEST_FAIL,
    };

    test_precopy_common(&args);
}
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

#ifndef _WIN32
static void *test_migrate_fd_start_hook(QTestState *from,
                                        QTestState *to)
{
    int ret;
    int pair[2];

    /* Create two connected sockets for migration */
    ret = qemu_socketpair(PF_LOCAL, SOCK_STREAM, 0, pair);
    g_assert_cmpint(ret, ==, 0);

    /* Send the 1st socket to the target */
    qtest_qmp_fds_assert_success(to, &pair[0], 1,
                                 "{ 'execute': 'getfd',"
                                 "  'arguments': { 'fdname': 'fd-mig' }}");
    close(pair[0]);

    /* Start incoming migration from the 1st socket */
    qtest_qmp_assert_success(to, "{ 'execute': 'migrate-incoming',"
                             "  'arguments': { 'uri': 'fd:fd-mig' }}");

    /* Send the 2nd socket to the target */
    qtest_qmp_fds_assert_success(from, &pair[1], 1,
                                 "{ 'execute': 'getfd',"
                                 "  'arguments': { 'fdname': 'fd-mig' }}");
    close(pair[1]);

    return NULL;
}

static void test_migrate_fd_finish_hook(QTestState *from,
                                        QTestState *to,
                                        void *opaque)
{
    QDict *rsp;
    const char *error_desc;

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
}

static void test_migrate_fd_proto(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .connect_uri = "fd:fd-mig",
        .start_hook = test_migrate_fd_start_hook,
        .finish_hook = test_migrate_fd_finish_hook
    };
    test_precopy_common(&args);
}
#endif /* _WIN32 */

static void do_test_validate_uuid(MigrateStart *args, bool should_fail)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
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
        qtest_set_expected_status(to, EXIT_FAILURE);
        wait_for_migration_fail(from, true);
    } else {
        wait_for_migration_complete(from);
    }

    test_migrate_end(from, to, false);
}

static void test_validate_uuid(void)
{
    MigrateStart args = {
        .opts_source = "-uuid 11111111-1111-1111-1111-111111111111",
        .opts_target = "-uuid 11111111-1111-1111-1111-111111111111",
    };

    do_test_validate_uuid(&args, false);
}

static void test_validate_uuid_error(void)
{
    MigrateStart args = {
        .opts_source = "-uuid 11111111-1111-1111-1111-111111111111",
        .opts_target = "-uuid 22222222-2222-2222-2222-222222222222",
        .hide_stderr = true,
    };

    do_test_validate_uuid(&args, true);
}

static void test_validate_uuid_src_not_set(void)
{
    MigrateStart args = {
        .opts_target = "-uuid 22222222-2222-2222-2222-222222222222",
        .hide_stderr = true,
    };

    do_test_validate_uuid(&args, false);
}

static void test_validate_uuid_dst_not_set(void)
{
    MigrateStart args = {
        .opts_source = "-uuid 11111111-1111-1111-1111-111111111111",
        .hide_stderr = true,
    };

    do_test_validate_uuid(&args, false);
}

/*
 * The way auto_converge works, we need to do too many passes to
 * run this test.  Auto_converge logic is only run once every
 * three iterations, so:
 *
 * - 3 iterations without auto_converge enabled
 * - 3 iterations with pct = 5
 * - 3 iterations with pct = 30
 * - 3 iterations with pct = 55
 * - 3 iterations with pct = 80
 * - 3 iterations with pct = 95 (max(95, 80 + 25))
 *
 * To make things even worse, we need to run the initial stage at
 * 3MB/s so we enter autoconverge even when host is (over)loaded.
 */
static void test_migrate_auto_converge(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateStart args = {};
    QTestState *from, *to;
    int64_t percentage;

    /*
     * We want the test to be stable and as fast as possible.
     * E.g., with 1Gb/s bandwith migration may pass without throttling,
     * so we need to decrease a bandwidth.
     */
    const int64_t init_pct = 5, inc_pct = 25, max_pct = 95;

    if (test_migrate_start(&from, &to, uri, &args)) {
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
    migrate_ensure_non_converge(from);

    /* To check remaining size after precopy */
    migrate_set_capability(from, "pause-before-switchover", true);

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    migrate_qmp(from, uri, "{}");

    /* Wait for throttling begins */
    percentage = 0;
    do {
        percentage = read_migrate_property_int(from, "cpu-throttle-percentage");
        if (percentage != 0) {
            break;
        }
        usleep(20);
        g_assert_false(got_src_stop);
    } while (true);
    /* The first percentage of throttling should be at least init_pct */
    g_assert_cmpint(percentage, >=, init_pct);
    /* Now, when we tested that throttling works, let it converge */
    migrate_ensure_converge(from);

    /*
     * Wait for pre-switchover status to check last throttle percentage
     * and remaining. These values will be zeroed later
     */
    wait_for_migration_status(from, "pre-switchover", NULL);

    /* The final percentage of throttling shouldn't be greater than max_pct */
    percentage = read_migrate_property_int(from, "cpu-throttle-percentage");
    g_assert_cmpint(percentage, <=, max_pct);
    migrate_continue(from, "pre-switchover");

    qtest_qmp_eventwait(to, "RESUME");

    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);

    test_migrate_end(from, to, true);
}

static void *
test_migrate_precopy_tcp_multifd_start_common(QTestState *from,
                                              QTestState *to,
                                              const char *method)
{
    migrate_set_parameter_int(from, "multifd-channels", 16);
    migrate_set_parameter_int(to, "multifd-channels", 16);

    migrate_set_parameter_str(from, "multifd-compression", method);
    migrate_set_parameter_str(to, "multifd-compression", method);

    migrate_set_capability(from, "multifd", true);
    migrate_set_capability(to, "multifd", true);

    /* Start incoming migration from the 1st socket */
    qtest_qmp_assert_success(to, "{ 'execute': 'migrate-incoming',"
                             "  'arguments': { 'uri': 'tcp:127.0.0.1:0' }}");

    return NULL;
}

static void *
test_migrate_precopy_tcp_multifd_start(QTestState *from,
                                       QTestState *to)
{
    return test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
}

static void *
test_migrate_precopy_tcp_multifd_zlib_start(QTestState *from,
                                            QTestState *to)
{
    return test_migrate_precopy_tcp_multifd_start_common(from, to, "zlib");
}

#ifdef CONFIG_ZSTD
static void *
test_migrate_precopy_tcp_multifd_zstd_start(QTestState *from,
                                            QTestState *to)
{
    return test_migrate_precopy_tcp_multifd_start_common(from, to, "zstd");
}
#endif /* CONFIG_ZSTD */

static void test_multifd_tcp_none(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = test_migrate_precopy_tcp_multifd_start,
        /*
         * Multifd is more complicated than most of the features, it
         * directly takes guest page buffers when sending, make sure
         * everything will work alright even if guest page is changing.
         */
        .live = true,
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_zlib(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = test_migrate_precopy_tcp_multifd_zlib_start,
    };
    test_precopy_common(&args);
}

#ifdef CONFIG_ZSTD
static void test_multifd_tcp_zstd(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = test_migrate_precopy_tcp_multifd_zstd_start,
    };
    test_precopy_common(&args);
}
#endif

#ifdef CONFIG_GNUTLS
static void *
test_migrate_multifd_tcp_tls_psk_start_match(QTestState *from,
                                             QTestState *to)
{
    test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
    return test_migrate_tls_psk_start_match(from, to);
}

static void *
test_migrate_multifd_tcp_tls_psk_start_mismatch(QTestState *from,
                                                QTestState *to)
{
    test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
    return test_migrate_tls_psk_start_mismatch(from, to);
}

#ifdef CONFIG_TASN1
static void *
test_migrate_multifd_tls_x509_start_default_host(QTestState *from,
                                                 QTestState *to)
{
    test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
    return test_migrate_tls_x509_start_default_host(from, to);
}

static void *
test_migrate_multifd_tls_x509_start_override_host(QTestState *from,
                                                  QTestState *to)
{
    test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
    return test_migrate_tls_x509_start_override_host(from, to);
}

static void *
test_migrate_multifd_tls_x509_start_mismatch_host(QTestState *from,
                                                  QTestState *to)
{
    test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
    return test_migrate_tls_x509_start_mismatch_host(from, to);
}

static void *
test_migrate_multifd_tls_x509_start_allow_anon_client(QTestState *from,
                                                      QTestState *to)
{
    test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
    return test_migrate_tls_x509_start_allow_anon_client(from, to);
}

static void *
test_migrate_multifd_tls_x509_start_reject_anon_client(QTestState *from,
                                                       QTestState *to)
{
    test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
    return test_migrate_tls_x509_start_reject_anon_client(from, to);
}
#endif /* CONFIG_TASN1 */

static void test_multifd_tcp_tls_psk_match(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = test_migrate_multifd_tcp_tls_psk_start_match,
        .finish_hook = test_migrate_tls_psk_finish,
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_tls_psk_mismatch(void)
{
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .listen_uri = "defer",
        .start_hook = test_migrate_multifd_tcp_tls_psk_start_mismatch,
        .finish_hook = test_migrate_tls_psk_finish,
        .result = MIG_TEST_FAIL,
    };
    test_precopy_common(&args);
}

#ifdef CONFIG_TASN1
static void test_multifd_tcp_tls_x509_default_host(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = test_migrate_multifd_tls_x509_start_default_host,
        .finish_hook = test_migrate_tls_x509_finish,
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_tls_x509_override_host(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = test_migrate_multifd_tls_x509_start_override_host,
        .finish_hook = test_migrate_tls_x509_finish,
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_tls_x509_mismatch_host(void)
{
    /*
     * This has different behaviour to the non-multifd case.
     *
     * In non-multifd case when client aborts due to mismatched
     * cert host, the server has already started trying to load
     * migration state, and so it exits with I/O failure.
     *
     * In multifd case when client aborts due to mismatched
     * cert host, the server is still waiting for the other
     * multifd connections to arrive so hasn't started trying
     * to load migration state, and thus just aborts the migration
     * without exiting.
     */
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .listen_uri = "defer",
        .start_hook = test_migrate_multifd_tls_x509_start_mismatch_host,
        .finish_hook = test_migrate_tls_x509_finish,
        .result = MIG_TEST_FAIL,
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_tls_x509_allow_anon_client(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = test_migrate_multifd_tls_x509_start_allow_anon_client,
        .finish_hook = test_migrate_tls_x509_finish,
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_tls_x509_reject_anon_client(void)
{
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .listen_uri = "defer",
        .start_hook = test_migrate_multifd_tls_x509_start_reject_anon_client,
        .finish_hook = test_migrate_tls_x509_finish,
        .result = MIG_TEST_FAIL,
    };
    test_precopy_common(&args);
}
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

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
    MigrateStart args = {
        .hide_stderr = true,
    };
    QTestState *from, *to, *to2;
    g_autofree char *uri = NULL;

    if (test_migrate_start(&from, &to, "defer", &args)) {
        return;
    }

    migrate_ensure_non_converge(from);

    migrate_set_parameter_int(from, "multifd-channels", 16);
    migrate_set_parameter_int(to, "multifd-channels", 16);

    migrate_set_capability(from, "multifd", true);
    migrate_set_capability(to, "multifd", true);

    /* Start incoming migration from the 1st socket */
    qtest_qmp_assert_success(to, "{ 'execute': 'migrate-incoming',"
                             "  'arguments': { 'uri': 'tcp:127.0.0.1:0' }}");

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    uri = migrate_get_socket_address(to, "socket-address");

    migrate_qmp(from, uri, "{}");

    wait_for_migration_pass(from);

    migrate_cancel(from);

    /* Make sure QEMU process "to" exited */
    qtest_set_expected_status(to, EXIT_FAILURE);
    qtest_wait_qemu(to);

    args = (MigrateStart){
        .only_target = true,
    };

    if (test_migrate_start(&from, &to2, "defer", &args)) {
        return;
    }

    migrate_set_parameter_int(to2, "multifd-channels", 16);

    migrate_set_capability(to2, "multifd", true);

    /* Start incoming migration from the 1st socket */
    qtest_qmp_assert_success(to2, "{ 'execute': 'migrate-incoming',"
                             "  'arguments': { 'uri': 'tcp:127.0.0.1:0' }}");

    g_free(uri);
    uri = migrate_get_socket_address(to2, "socket-address");

    wait_for_migration_status(from, "cancelled", NULL);

    migrate_ensure_converge(from);

    migrate_qmp(from, uri, "{}");

    wait_for_migration_pass(from);

    if (!got_src_stop) {
        qtest_qmp_eventwait(from, "STOP");
    }
    qtest_qmp_eventwait(to2, "RESUME");

    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);
    test_migrate_end(from, to2, true);
}

static void calc_dirty_rate(QTestState *who, uint64_t calc_time)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'calc-dirty-rate',"
                             "'arguments': { "
                             "'calc-time': %" PRIu64 ","
                             "'mode': 'dirty-ring' }}",
                             calc_time);
}

static QDict *query_dirty_rate(QTestState *who)
{
    return qtest_qmp_assert_success_ref(who,
                                        "{ 'execute': 'query-dirty-rate' }");
}

static void dirtylimit_set_all(QTestState *who, uint64_t dirtyrate)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'set-vcpu-dirty-limit',"
                             "'arguments': { "
                             "'dirty-rate': %" PRIu64 " } }",
                             dirtyrate);
}

static void cancel_vcpu_dirty_limit(QTestState *who)
{
    qtest_qmp_assert_success(who,
                             "{ 'execute': 'cancel-vcpu-dirty-limit' }");
}

static QDict *query_vcpu_dirty_limit(QTestState *who)
{
    QDict *rsp;

    rsp = qtest_qmp(who, "{ 'execute': 'query-vcpu-dirty-limit' }");
    g_assert(!qdict_haskey(rsp, "error"));
    g_assert(qdict_haskey(rsp, "return"));

    return rsp;
}

static bool calc_dirtyrate_ready(QTestState *who)
{
    QDict *rsp_return;
    gchar *status;

    rsp_return = query_dirty_rate(who);
    g_assert(rsp_return);

    status = g_strdup(qdict_get_str(rsp_return, "status"));
    g_assert(status);

    return g_strcmp0(status, "measuring");
}

static void wait_for_calc_dirtyrate_complete(QTestState *who,
                                             int64_t time_s)
{
    int max_try_count = 10000;
    usleep(time_s * 1000000);

    while (!calc_dirtyrate_ready(who) && max_try_count--) {
        usleep(1000);
    }

    /*
     * Set the timeout with 10 s(max_try_count * 1000us),
     * if dirtyrate measurement not complete, fail test.
     */
    g_assert_cmpint(max_try_count, !=, 0);
}

static int64_t get_dirty_rate(QTestState *who)
{
    QDict *rsp_return;
    gchar *status;
    QList *rates;
    const QListEntry *entry;
    QDict *rate;
    int64_t dirtyrate;

    rsp_return = query_dirty_rate(who);
    g_assert(rsp_return);

    status = g_strdup(qdict_get_str(rsp_return, "status"));
    g_assert(status);
    g_assert_cmpstr(status, ==, "measured");

    rates = qdict_get_qlist(rsp_return, "vcpu-dirty-rate");
    g_assert(rates && !qlist_empty(rates));

    entry = qlist_first(rates);
    g_assert(entry);

    rate = qobject_to(QDict, qlist_entry_obj(entry));
    g_assert(rate);

    dirtyrate = qdict_get_try_int(rate, "dirty-rate", -1);

    qobject_unref(rsp_return);
    return dirtyrate;
}

static int64_t get_limit_rate(QTestState *who)
{
    QDict *rsp_return;
    QList *rates;
    const QListEntry *entry;
    QDict *rate;
    int64_t dirtyrate;

    rsp_return = query_vcpu_dirty_limit(who);
    g_assert(rsp_return);

    rates = qdict_get_qlist(rsp_return, "return");
    g_assert(rates && !qlist_empty(rates));

    entry = qlist_first(rates);
    g_assert(entry);

    rate = qobject_to(QDict, qlist_entry_obj(entry));
    g_assert(rate);

    dirtyrate = qdict_get_try_int(rate, "limit-rate", -1);

    qobject_unref(rsp_return);
    return dirtyrate;
}

static QTestState *dirtylimit_start_vm(void)
{
    QTestState *vm = NULL;
    g_autofree gchar *cmd = NULL;
    const char *arch = qtest_get_arch();
    g_autofree char *bootpath = NULL;

    assert((strcmp(arch, "x86_64") == 0));
    bootpath = g_strdup_printf("%s/bootsect", tmpfs);
    assert(sizeof(x86_bootsect) == 512);
    init_bootfile(bootpath, x86_bootsect, sizeof(x86_bootsect));

    cmd = g_strdup_printf("-accel kvm,dirty-ring-size=4096 "
                          "-name dirtylimit-test,debug-threads=on "
                          "-m 150M -smp 1 "
                          "-serial file:%s/vm_serial "
                          "-drive file=%s,format=raw ",
                          tmpfs, bootpath);

    vm = qtest_init(cmd);
    return vm;
}

static void dirtylimit_stop_vm(QTestState *vm)
{
    qtest_quit(vm);
    cleanup("bootsect");
    cleanup("vm_serial");
}

static void test_vcpu_dirty_limit(void)
{
    QTestState *vm;
    int64_t origin_rate;
    int64_t quota_rate;
    int64_t rate ;
    int max_try_count = 20;
    int hit = 0;

    /* Start vm for vcpu dirtylimit test */
    vm = dirtylimit_start_vm();

    /* Wait for the first serial output from the vm*/
    wait_for_serial("vm_serial");

    /* Do dirtyrate measurement with calc time equals 1s */
    calc_dirty_rate(vm, 1);

    /* Sleep calc time and wait for calc dirtyrate complete */
    wait_for_calc_dirtyrate_complete(vm, 1);

    /* Query original dirty page rate */
    origin_rate = get_dirty_rate(vm);

    /* VM booted from bootsect should dirty memory steadily */
    assert(origin_rate != 0);

    /* Setup quota dirty page rate at half of origin */
    quota_rate = origin_rate / 2;

    /* Set dirtylimit */
    dirtylimit_set_all(vm, quota_rate);

    /*
     * Check if set-vcpu-dirty-limit and query-vcpu-dirty-limit
     * works literally
     */
    g_assert_cmpint(quota_rate, ==, get_limit_rate(vm));

    /* Sleep a bit to check if it take effect */
    usleep(2000000);

    /*
     * Check if dirtylimit take effect realistically, set the
     * timeout with 20 s(max_try_count * 1s), if dirtylimit
     * doesn't take effect, fail test.
     */
    while (--max_try_count) {
        calc_dirty_rate(vm, 1);
        wait_for_calc_dirtyrate_complete(vm, 1);
        rate = get_dirty_rate(vm);

        /*
         * Assume hitting if current rate is less
         * than quota rate (within accepting error)
         */
        if (rate < (quota_rate + DIRTYLIMIT_TOLERANCE_RANGE)) {
            hit = 1;
            break;
        }
    }

    g_assert_cmpint(hit, ==, 1);

    hit = 0;
    max_try_count = 20;

    /* Check if dirtylimit cancellation take effect */
    cancel_vcpu_dirty_limit(vm);
    while (--max_try_count) {
        calc_dirty_rate(vm, 1);
        wait_for_calc_dirtyrate_complete(vm, 1);
        rate = get_dirty_rate(vm);

        /*
         * Assume dirtylimit be canceled if current rate is
         * greater than quota rate (within accepting error)
         */
        if (rate > (quota_rate + DIRTYLIMIT_TOLERANCE_RANGE)) {
            hit = 1;
            break;
        }
    }

    g_assert_cmpint(hit, ==, 1);
    dirtylimit_stop_vm(vm);
}

static bool kvm_dirty_ring_supported(void)
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

int main(int argc, char **argv)
{
    bool has_kvm, has_tcg;
    bool has_uffd;
    const char *arch;
    g_autoptr(GError) err = NULL;
    int ret;

    g_test_init(&argc, &argv, NULL);

    has_kvm = qtest_has_accel("kvm");
    has_tcg = qtest_has_accel("tcg");

    if (!has_tcg && !has_kvm) {
        g_test_skip("No KVM or TCG accelerator available");
        return 0;
    }

    has_uffd = ufd_version_check();
    arch = qtest_get_arch();

    /*
     * On ppc64, the test only works with kvm-hv, but not with kvm-pr and TCG
     * is touchy due to race conditions on dirty bits (especially on PPC for
     * some reason)
     */
    if (g_str_equal(arch, "ppc64") &&
        (!has_kvm || access("/sys/module/kvm_hv", F_OK))) {
        g_test_message("Skipping test: kvm_hv not available");
        return g_test_run();
    }

    /*
     * Similar to ppc64, s390x seems to be touchy with TCG, so disable it
     * there until the problems are resolved
     */
    if (g_str_equal(arch, "s390x") && !has_kvm) {
        g_test_message("Skipping test: s390x host with KVM is required");
        return g_test_run();
    }

    tmpfs = g_dir_make_tmp("migration-test-XXXXXX", &err);
    if (!tmpfs) {
        g_test_message("Can't create temporary directory in %s: %s",
                       g_get_tmp_dir(), err->message);
    }
    g_assert(tmpfs);

    module_call_init(MODULE_INIT_QOM);

    if (has_uffd) {
        qtest_add_func("/migration/postcopy/plain", test_postcopy);
        qtest_add_func("/migration/postcopy/recovery/plain",
                       test_postcopy_recovery);
        qtest_add_func("/migration/postcopy/preempt/plain", test_postcopy_preempt);
        qtest_add_func("/migration/postcopy/preempt/recovery/plain",
                       test_postcopy_preempt_recovery);
        if (getenv("QEMU_TEST_FLAKY_TESTS")) {
            qtest_add_func("/migration/postcopy/compress/plain",
                           test_postcopy_compress);
            qtest_add_func("/migration/postcopy/recovery/compress/plain",
                           test_postcopy_recovery_compress);
        }
    }

    qtest_add_func("/migration/bad_dest", test_baddest);
    qtest_add_func("/migration/precopy/unix/plain", test_precopy_unix_plain);
    qtest_add_func("/migration/precopy/unix/xbzrle", test_precopy_unix_xbzrle);
    /*
     * Compression fails from time to time.
     * Put test here but don't enable it until everything is fixed.
     */
    if (getenv("QEMU_TEST_FLAKY_TESTS")) {
        qtest_add_func("/migration/precopy/unix/compress/wait",
                       test_precopy_unix_compress);
        qtest_add_func("/migration/precopy/unix/compress/nowait",
                       test_precopy_unix_compress_nowait);
    }
#ifdef CONFIG_GNUTLS
    qtest_add_func("/migration/precopy/unix/tls/psk",
                   test_precopy_unix_tls_psk);

    if (has_uffd) {
        /*
         * NOTE: psk test is enough for postcopy, as other types of TLS
         * channels are tested under precopy.  Here what we want to test is the
         * general postcopy path that has TLS channel enabled.
         */
        qtest_add_func("/migration/postcopy/tls/psk", test_postcopy_tls_psk);
        qtest_add_func("/migration/postcopy/recovery/tls/psk",
                       test_postcopy_recovery_tls_psk);
        qtest_add_func("/migration/postcopy/preempt/tls/psk",
                       test_postcopy_preempt_tls_psk);
        qtest_add_func("/migration/postcopy/preempt/recovery/tls/psk",
                       test_postcopy_preempt_all);
    }
#ifdef CONFIG_TASN1
    qtest_add_func("/migration/precopy/unix/tls/x509/default-host",
                   test_precopy_unix_tls_x509_default_host);
    qtest_add_func("/migration/precopy/unix/tls/x509/override-host",
                   test_precopy_unix_tls_x509_override_host);
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

    qtest_add_func("/migration/precopy/tcp/plain", test_precopy_tcp_plain);
#ifdef CONFIG_GNUTLS
    qtest_add_func("/migration/precopy/tcp/tls/psk/match",
                   test_precopy_tcp_tls_psk_match);
    qtest_add_func("/migration/precopy/tcp/tls/psk/mismatch",
                   test_precopy_tcp_tls_psk_mismatch);
#ifdef CONFIG_TASN1
    qtest_add_func("/migration/precopy/tcp/tls/x509/default-host",
                   test_precopy_tcp_tls_x509_default_host);
    qtest_add_func("/migration/precopy/tcp/tls/x509/override-host",
                   test_precopy_tcp_tls_x509_override_host);
    qtest_add_func("/migration/precopy/tcp/tls/x509/mismatch-host",
                   test_precopy_tcp_tls_x509_mismatch_host);
    qtest_add_func("/migration/precopy/tcp/tls/x509/friendly-client",
                   test_precopy_tcp_tls_x509_friendly_client);
    qtest_add_func("/migration/precopy/tcp/tls/x509/hostile-client",
                   test_precopy_tcp_tls_x509_hostile_client);
    qtest_add_func("/migration/precopy/tcp/tls/x509/allow-anon-client",
                   test_precopy_tcp_tls_x509_allow_anon_client);
    qtest_add_func("/migration/precopy/tcp/tls/x509/reject-anon-client",
                   test_precopy_tcp_tls_x509_reject_anon_client);
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

    /* qtest_add_func("/migration/ignore_shared", test_ignore_shared); */
#ifndef _WIN32
    qtest_add_func("/migration/fd_proto", test_migrate_fd_proto);
#endif
    qtest_add_func("/migration/validate_uuid", test_validate_uuid);
    qtest_add_func("/migration/validate_uuid_error", test_validate_uuid_error);
    qtest_add_func("/migration/validate_uuid_src_not_set",
                   test_validate_uuid_src_not_set);
    qtest_add_func("/migration/validate_uuid_dst_not_set",
                   test_validate_uuid_dst_not_set);
    /*
     * See explanation why this test is slow on function definition
     */
    if (g_test_slow()) {
        qtest_add_func("/migration/auto_converge", test_migrate_auto_converge);
    }
    qtest_add_func("/migration/multifd/tcp/plain/none",
                   test_multifd_tcp_none);
    /*
     * This test is flaky and sometimes fails in CI and otherwise:
     * don't run unless user opts in via environment variable.
     */
    if (getenv("QEMU_TEST_FLAKY_TESTS")) {
        qtest_add_func("/migration/multifd/tcp/plain/cancel",
                       test_multifd_tcp_cancel);
    }
    qtest_add_func("/migration/multifd/tcp/plain/zlib",
                   test_multifd_tcp_zlib);
#ifdef CONFIG_ZSTD
    qtest_add_func("/migration/multifd/tcp/plain/zstd",
                   test_multifd_tcp_zstd);
#endif
#ifdef CONFIG_GNUTLS
    qtest_add_func("/migration/multifd/tcp/tls/psk/match",
                   test_multifd_tcp_tls_psk_match);
    qtest_add_func("/migration/multifd/tcp/tls/psk/mismatch",
                   test_multifd_tcp_tls_psk_mismatch);
#ifdef CONFIG_TASN1
    qtest_add_func("/migration/multifd/tcp/tls/x509/default-host",
                   test_multifd_tcp_tls_x509_default_host);
    qtest_add_func("/migration/multifd/tcp/tls/x509/override-host",
                   test_multifd_tcp_tls_x509_override_host);
    qtest_add_func("/migration/multifd/tcp/tls/x509/mismatch-host",
                   test_multifd_tcp_tls_x509_mismatch_host);
    qtest_add_func("/migration/multifd/tcp/tls/x509/allow-anon-client",
                   test_multifd_tcp_tls_x509_allow_anon_client);
    qtest_add_func("/migration/multifd/tcp/tls/x509/reject-anon-client",
                   test_multifd_tcp_tls_x509_reject_anon_client);
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

    if (g_str_equal(arch, "x86_64") && has_kvm && kvm_dirty_ring_supported()) {
        qtest_add_func("/migration/dirty_ring",
                       test_precopy_unix_dirty_ring);
        qtest_add_func("/migration/vcpu_dirty_limit",
                       test_vcpu_dirty_limit);
    }

    ret = g_test_run();

    g_assert_cmpint(ret, ==, 0);

    ret = rmdir(tmpfs);
    if (ret != 0) {
        g_test_message("unable to rmdir: path (%s): %s",
                       tmpfs, strerror(errno));
    }
    g_free(tmpfs);

    return ret;
}
