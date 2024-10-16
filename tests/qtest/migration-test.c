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
#include "crypto/tlscredspsk.h"
#include "qapi/qmp/qlist.h"
#include "ppc-util.h"

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
static QTestMigrationState src_state;
static QTestMigrationState dst_state;

/*
 * An initial 3 MB offset is used as that corresponds
 * to ~1 sec of data transfer with our bandwidth setting.
 */
#define MAGIC_OFFSET_BASE (3 * 1024 * 1024)
/*
 * A further 1k is added to ensure we're not a multiple
 * of TEST_MEM_PAGE_SIZE, thus avoid clash with writes
 * from the migration guest workload.
 */
#define MAGIC_OFFSET_SHUFFLE 1024
#define MAGIC_OFFSET (MAGIC_OFFSET_BASE + MAGIC_OFFSET_SHUFFLE)
#define MAGIC_MARKER 0xFEED12345678CAFEULL

/*
 * Dirtylimit stop working if dirty page rate error
 * value less than DIRTYLIMIT_TOLERANCE_RANGE
 */
#define DIRTYLIMIT_TOLERANCE_RANGE  25  /* MB/s */

#define ANALYZE_SCRIPT "scripts/analyze-migration.py"

#define QEMU_VM_FILE_MAGIC 0x5145564d
#define FILE_TEST_FILENAME "migfile"
#define FILE_TEST_OFFSET 0x1000
#define FILE_TEST_MARKER 'X'
#define QEMU_ENV_SRC "QTEST_QEMU_BINARY_SRC"
#define QEMU_ENV_DST "QTEST_QEMU_BINARY_DST"

typedef enum PostcopyRecoveryFailStage {
    /*
     * "no failure" must be 0 as it's the default.  OTOH, real failure
     * cases must be >0 to make sure they trigger by a "if" test.
     */
    POSTCOPY_FAIL_NONE = 0,
    POSTCOPY_FAIL_CHANNEL_ESTABLISH,
    POSTCOPY_FAIL_RECOVERY,
    POSTCOPY_FAIL_MAX
} PostcopyRecoveryFailStage;

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

    ioctl_mask = 1ULL << _UFFDIO_REGISTER |
                 1ULL << _UFFDIO_UNREGISTER;
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
static char *bootpath;

/* The boot file modifies memory area in [start_address, end_address)
 * repeatedly. It outputs a 'B' at a fixed rate while it's still running.
 */
#include "tests/migration/i386/a-b-bootblock.h"
#include "tests/migration/aarch64/a-b-kernel.h"
#include "tests/migration/ppc64/a-b-kernel.h"
#include "tests/migration/s390x/a-b-bios.h"

static void bootfile_delete(void)
{
    if (!bootpath) {
        return;
    }
    unlink(bootpath);
    g_free(bootpath);
    bootpath = NULL;
}

static void bootfile_create(char *dir, bool suspend_me)
{
    const char *arch = qtest_get_arch();
    unsigned char *content;
    size_t len;

    bootfile_delete();
    bootpath = g_strdup_printf("%s/bootsect", dir);
    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        /* the assembled x86 boot sector should be exactly one sector large */
        g_assert(sizeof(x86_bootsect) == 512);
        x86_bootsect[SYM_suspend_me - SYM_start] = suspend_me;
        content = x86_bootsect;
        len = sizeof(x86_bootsect);
    } else if (g_str_equal(arch, "s390x")) {
        content = s390x_elf;
        len = sizeof(s390x_elf);
    } else if (strcmp(arch, "ppc64") == 0) {
        content = ppc64_kernel;
        len = sizeof(ppc64_kernel);
    } else if (strcmp(arch, "aarch64") == 0) {
        content = aarch64_kernel;
        len = sizeof(aarch64_kernel);
        g_assert(sizeof(aarch64_kernel) <= ARM_TEST_MAX_KERNEL_SIZE);
    } else {
        g_assert_not_reached();
    }

    FILE *bootfile = fopen(bootpath, "wb");

    g_assert_cmpint(fwrite(content, len, 1, bootfile), ==, 1);
    fclose(bootfile);
}

/*
 * Wait for some output in the serial output file,
 * we get an 'A' followed by an endless string of 'B's
 * but on the destination we won't have the A (unless we enabled suspend/resume)
 */
static void wait_for_serial(const char *side)
{
    g_autofree char *serialpath = g_strdup_printf("%s/%s", tmpfs, side);
    FILE *serialfile = fopen(serialpath, "r");

    do {
        int readvalue = fgetc(serialfile);

        switch (readvalue) {
        case 'A':
            /* Fine */
            break;

        case 'B':
            /* It's alive! */
            fclose(serialfile);
            return;

        case EOF:
            fseek(serialfile, 0, SEEK_SET);
            usleep(1000);
            break;

        default:
            fprintf(stderr, "Unexpected %d on %s serial\n", readvalue, side);
            g_assert_not_reached();
        }
    } while (true);
}

static void wait_for_stop(QTestState *who, QTestMigrationState *state)
{
    if (!state->stop_seen) {
        qtest_qmp_eventwait(who, "STOP");
    }
}

static void wait_for_resume(QTestState *who, QTestMigrationState *state)
{
    if (!state->resume_seen) {
        qtest_qmp_eventwait(who, "RESUME");
    }
}

static void wait_for_suspend(QTestState *who, QTestMigrationState *state)
{
    if (state->suspend_me && !state->suspend_seen) {
        qtest_qmp_eventwait(who, "SUSPEND");
    }
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

/*
 * Wait for two changes in the migration pass count, but bail if we stop.
 */
static void wait_for_migration_pass(QTestState *who)
{
    uint64_t pass, prev_pass = 0, changes = 0;

    while (changes < 2 && !src_state.stop_seen && !src_state.suspend_seen) {
        usleep(1000);
        pass = get_migration_pass(who);
        changes += (pass != prev_pass);
        prev_pass = pass;
    }
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

/*
 * Our goal is to ensure that we run a single full migration
 * iteration, and also dirty memory, ensuring that at least
 * one further iteration is required.
 *
 * We can't directly synchronize with the start of a migration
 * so we have to apply some tricks monitoring memory that is
 * transferred.
 *
 * Initially we set the migration bandwidth to an insanely
 * low value, with tiny max downtime too. This basically
 * guarantees migration will never complete.
 *
 * This will result in a test that is unacceptably slow though,
 * so we can't let the entire migration pass run at this speed.
 * Our intent is to let it run just long enough that we can
 * prove data prior to the marker has been transferred *AND*
 * also prove this transferred data is dirty again.
 *
 * Before migration starts, we write a 64-bit magic marker
 * into a fixed location in the src VM RAM.
 *
 * Then watch dst memory until the marker appears. This is
 * proof that start_address -> MAGIC_OFFSET_BASE has been
 * transferred.
 *
 * Finally we go back to the source and read a byte just
 * before the marker until we see it flip in value. This
 * is proof that start_address -> MAGIC_OFFSET_BASE
 * is now dirty again.
 *
 * IOW, we're guaranteed at least a 2nd migration pass
 * at this point.
 *
 * We can now let migration run at full speed to finish
 * the test
 */
static void migrate_prepare_for_dirty_mem(QTestState *from)
{
    /*
     * The guest workflow iterates from start_address to
     * end_address, writing 1 byte every TEST_MEM_PAGE_SIZE
     * bytes.
     *
     * IOW, if we write to mem at a point which is NOT
     * a multiple of TEST_MEM_PAGE_SIZE, our write won't
     * conflict with the migration workflow.
     *
     * We put in a marker here, that we'll use to determine
     * when the data has been transferred to the dst.
     */
    qtest_writeq(from, start_address + MAGIC_OFFSET, MAGIC_MARKER);
}

static void migrate_wait_for_dirty_mem(QTestState *from,
                                       QTestState *to)
{
    uint64_t watch_address = start_address + MAGIC_OFFSET_BASE;
    uint64_t marker_address = start_address + MAGIC_OFFSET;
    uint8_t watch_byte;

    /*
     * Wait for the MAGIC_MARKER to get transferred, as an
     * indicator that a migration pass has made some known
     * amount of progress.
     */
    do {
        usleep(1000 * 10);
    } while (qtest_readq(to, marker_address) != MAGIC_MARKER);


    /* If suspended, src only iterates once, and watch_byte may never change */
    if (src_state.suspend_me) {
        return;
    }

    /*
     * Now ensure that already transferred bytes are
     * dirty again from the guest workload. Note the
     * guest byte value will wrap around and by chance
     * match the original watch_byte. This is harmless
     * as we'll eventually see a different value if we
     * keep watching
     */
    watch_byte = qtest_readb(from, watch_address);
    do {
        usleep(1000 * 10);
    } while (qtest_readb(from, watch_address) == watch_byte);
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

static void migrate_postcopy_start(QTestState *from, QTestState *to)
{
    qtest_qmp_assert_success(from, "{ 'execute': 'migrate-start-postcopy' }");

    wait_for_stop(from, &src_state);
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
    /* suspend the src before migrating to dest. */
    bool suspend_me;
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

    /*
     * Optional: JSON-formatted list of src QEMU URIs. If a port is
     * defined as '0' in any QDict key a value of '0' will be
     * automatically converted to the correct destination port.
     */
    const char *connect_channels;

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
        /* The QMP command for this migration should fail with an error */
        MIG_TEST_QMP_ERROR,
    } result;

    /*
     * Optional: set number of migration passes to wait for, if live==true.
     * If zero, then merely wait for a few MB of dirty data
     */
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
    PostcopyRecoveryFailStage postcopy_recovery_fail_stage;
} MigrateCommon;

static int test_migrate_start(QTestState **from, QTestState **to,
                              const char *uri, MigrateStart *args)
{
    g_autofree gchar *arch_source = NULL;
    g_autofree gchar *arch_target = NULL;
    /* options for source and target */
    g_autofree gchar *arch_opts = NULL;
    g_autofree gchar *cmd_source = NULL;
    g_autofree gchar *cmd_target = NULL;
    const gchar *ignore_stderr;
    g_autofree char *shmem_opts = NULL;
    g_autofree char *shmem_path = NULL;
    const char *kvm_opts = NULL;
    const char *arch = qtest_get_arch();
    const char *memory_size;
    const char *machine_alias, *machine_opts = "";
    g_autofree char *machine = NULL;

    if (args->use_shmem) {
        if (!g_file_test("/dev/shm", G_FILE_TEST_IS_DIR)) {
            g_test_skip("/dev/shm is not supported");
            return -1;
        }
    }

    dst_state = (QTestMigrationState) { };
    src_state = (QTestMigrationState) { };
    bootfile_create(tmpfs, args->suspend_me);
    src_state.suspend_me = args->suspend_me;

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        memory_size = "150M";

        if (g_str_equal(arch, "i386")) {
            machine_alias = "pc";
        } else {
            machine_alias = "q35";
        }
        arch_opts = g_strdup_printf(
            "-drive if=none,id=d0,file=%s,format=raw "
            "-device ide-hd,drive=d0,secs=1,cyls=1,heads=1", bootpath);
        start_address = X86_TEST_MEM_START;
        end_address = X86_TEST_MEM_END;
    } else if (g_str_equal(arch, "s390x")) {
        memory_size = "128M";
        machine_alias = "s390-ccw-virtio";
        arch_opts = g_strdup_printf("-bios %s", bootpath);
        start_address = S390_TEST_MEM_START;
        end_address = S390_TEST_MEM_END;
    } else if (strcmp(arch, "ppc64") == 0) {
        memory_size = "256M";
        start_address = PPC_TEST_MEM_START;
        end_address = PPC_TEST_MEM_END;
        machine_alias = "pseries";
        machine_opts = "vsmt=8";
        arch_opts = g_strdup_printf(
            "-nodefaults -machine " PSERIES_DEFAULT_CAPABILITIES " "
            "-bios %s", bootpath);
    } else if (strcmp(arch, "aarch64") == 0) {
        memory_size = "150M";
        machine_alias = "virt";
        machine_opts = "gic-version=3";
        arch_opts = g_strdup_printf("-cpu max -kernel %s", bootpath);
        start_address = ARM_TEST_MEM_START;
        end_address = ARM_TEST_MEM_END;
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
    }

    if (args->use_dirty_ring) {
        kvm_opts = ",dirty-ring-size=4096";
    }

    if (!qtest_has_machine(machine_alias)) {
        g_autofree char *msg = g_strdup_printf("machine %s not supported", machine_alias);
        g_test_skip(msg);
        return -1;
    }

    machine = resolve_machine_version(machine_alias, QEMU_ENV_SRC,
                                      QEMU_ENV_DST);

    g_test_message("Using machine type: %s", machine);

    cmd_source = g_strdup_printf("-accel kvm%s -accel tcg "
                                 "-machine %s,%s "
                                 "-name source,debug-threads=on "
                                 "-m %s "
                                 "-serial file:%s/src_serial "
                                 "%s %s %s %s %s",
                                 kvm_opts ? kvm_opts : "",
                                 machine, machine_opts,
                                 memory_size, tmpfs,
                                 arch_opts ? arch_opts : "",
                                 arch_source ? arch_source : "",
                                 shmem_opts ? shmem_opts : "",
                                 args->opts_source ? args->opts_source : "",
                                 ignore_stderr);
    if (!args->only_target) {
        *from = qtest_init_with_env(QEMU_ENV_SRC, cmd_source);
        qtest_qmp_set_event_callback(*from,
                                     migrate_watch_for_events,
                                     &src_state);
    }

    cmd_target = g_strdup_printf("-accel kvm%s -accel tcg "
                                 "-machine %s,%s "
                                 "-name target,debug-threads=on "
                                 "-m %s "
                                 "-serial file:%s/dest_serial "
                                 "-incoming %s "
                                 "%s %s %s %s %s",
                                 kvm_opts ? kvm_opts : "",
                                 machine, machine_opts,
                                 memory_size, tmpfs, uri,
                                 arch_opts ? arch_opts : "",
                                 arch_target ? arch_target : "",
                                 shmem_opts ? shmem_opts : "",
                                 args->opts_target ? args->opts_target : "",
                                 ignore_stderr);
    *to = qtest_init_with_env(QEMU_ENV_DST, cmd_target);
    qtest_qmp_set_event_callback(*to,
                                 migrate_watch_for_events,
                                 &dst_state);

    /*
     * Remove shmem file immediately to avoid memory leak in test failed case.
     * It's valid because QEMU has already opened this file
     */
    if (args->use_shmem) {
        unlink(shmem_path);
    }

    /*
     * Always enable migration events.  Libvirt always uses it, let's try
     * to mimic as closer as that.
     */
    migrate_set_capability(*from, "events", true);
    migrate_set_capability(*to, "events", true);

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

    cleanup("migsocket");
    cleanup("src_serial");
    cleanup("dest_serial");
    cleanup(FILE_TEST_FILENAME);
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
        test_tls_deinit_cert(&servercertreq);
    }

    TLS_CERT_REQ_SIMPLE_SERVER(clientcertreq, cacertreq,
                               data->servercert,
                               args->certhostname,
                               args->certipaddr);
    test_tls_deinit_cert(&clientcertreq);
    test_tls_deinit_cert(&cacertreq);

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

static int migrate_postcopy_prepare(QTestState **from_ptr,
                                    QTestState **to_ptr,
                                    MigrateCommon *args)
{
    QTestState *from, *to;

    if (test_migrate_start(&from, &to, "defer", &args->start)) {
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

    migrate_prepare_for_dirty_mem(from);
    qtest_qmp_assert_success(to, "{ 'execute': 'migrate-incoming',"
                             "  'arguments': { "
                             "      'channels': [ { 'channel-type': 'main',"
                             "      'addr': { 'transport': 'socket',"
                             "                'type': 'inet',"
                             "                'host': '127.0.0.1',"
                             "                'port': '0' } } ] } }");

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");
    wait_for_suspend(from, &src_state);

    migrate_qmp(from, to, NULL, NULL, "{}");

    migrate_wait_for_dirty_mem(from, to);

    *from_ptr = from;
    *to_ptr = to;

    return 0;
}

static void migrate_postcopy_complete(QTestState *from, QTestState *to,
                                      MigrateCommon *args)
{
    wait_for_migration_complete(from);

    if (args->start.suspend_me) {
        /* wakeup succeeds only if guest is suspended */
        qtest_qmp_assert_success(to, "{'execute': 'system_wakeup'}");
    }

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

static void test_postcopy_suspend(void)
{
    MigrateCommon args = {
        .start.suspend_me = true,
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

static void wait_for_postcopy_status(QTestState *one, const char *status)
{
    wait_for_migration_status(one, status,
                              (const char * []) { "failed", "active",
                                                  "completed", NULL });
}

static void postcopy_recover_fail(QTestState *from, QTestState *to,
                                  PostcopyRecoveryFailStage stage)
{
#ifndef _WIN32
    bool fail_early = (stage == POSTCOPY_FAIL_CHANNEL_ESTABLISH);
    int ret, pair1[2], pair2[2];
    char c;

    g_assert(stage > POSTCOPY_FAIL_NONE && stage < POSTCOPY_FAIL_MAX);

    /* Create two unrelated socketpairs */
    ret = qemu_socketpair(PF_LOCAL, SOCK_STREAM, 0, pair1);
    g_assert_cmpint(ret, ==, 0);

    ret = qemu_socketpair(PF_LOCAL, SOCK_STREAM, 0, pair2);
    g_assert_cmpint(ret, ==, 0);

    /*
     * Give the guests unpaired ends of the sockets, so they'll all blocked
     * at reading.  This mimics a wrong channel established.
     */
    qtest_qmp_fds_assert_success(from, &pair1[0], 1,
                                 "{ 'execute': 'getfd',"
                                 "  'arguments': { 'fdname': 'fd-mig' }}");
    qtest_qmp_fds_assert_success(to, &pair2[0], 1,
                                 "{ 'execute': 'getfd',"
                                 "  'arguments': { 'fdname': 'fd-mig' }}");

    /*
     * Write the 1st byte as QEMU_VM_COMMAND (0x8) for the dest socket, to
     * emulate the 1st byte of a real recovery, but stops from there to
     * keep dest QEMU in RECOVER.  This is needed so that we can kick off
     * the recover process on dest QEMU (by triggering the G_IO_IN event).
     *
     * NOTE: this trick is not needed on src QEMUs, because src doesn't
     * rely on an pre-existing G_IO_IN event, so it will always trigger the
     * upcoming recovery anyway even if it can read nothing.
     */
#define QEMU_VM_COMMAND              0x08
    c = QEMU_VM_COMMAND;
    ret = send(pair2[1], &c, 1, 0);
    g_assert_cmpint(ret, ==, 1);

    if (stage == POSTCOPY_FAIL_CHANNEL_ESTABLISH) {
        /*
         * This will make src QEMU to fail at an early stage when trying to
         * resume later, where it shouldn't reach RECOVER stage at all.
         */
        close(pair1[1]);
    }

    migrate_recover(to, "fd:fd-mig");
    migrate_qmp(from, to, "fd:fd-mig", NULL, "{'resume': true}");

    /*
     * Source QEMU has an extra RECOVER_SETUP phase, dest doesn't have it.
     * Make sure it appears along the way.
     */
    migration_event_wait(from, "postcopy-recover-setup");

    if (fail_early) {
        /*
         * When fails at reconnection, src QEMU will automatically goes
         * back to PAUSED state.  Making sure there is an event in this
         * case: Libvirt relies on this to detect early reconnection
         * errors.
         */
        migration_event_wait(from, "postcopy-paused");
    } else {
        /*
         * We want to test "fail later" at RECOVER stage here.  Make sure
         * both QEMU instances will go into RECOVER stage first, then test
         * kicking them out using migrate-pause.
         *
         * Explicitly check the RECOVER event on src, that's what Libvirt
         * relies on, rather than polling.
         */
        migration_event_wait(from, "postcopy-recover");
        wait_for_postcopy_status(from, "postcopy-recover");

        /* Need an explicit kick on src QEMU in this case */
        migrate_pause(from);
    }

    /*
     * For all failure cases, we'll reach such states on both sides now.
     * Check them.
     */
    wait_for_postcopy_status(from, "postcopy-paused");
    wait_for_postcopy_status(to, "postcopy-recover");

    /*
     * Kick dest QEMU out too. This is normally not needed in reality
     * because when the channel is shutdown it should also happen on src.
     * However here we used separate socket pairs so we need to do that
     * explicitly.
     */
    migrate_pause(to);
    wait_for_postcopy_status(to, "postcopy-paused");

    close(pair1[0]);
    close(pair2[0]);
    close(pair2[1]);

    if (stage != POSTCOPY_FAIL_CHANNEL_ESTABLISH) {
        close(pair1[1]);
    }
#endif
}

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
    wait_for_postcopy_status(to, "postcopy-paused");
    wait_for_postcopy_status(from, "postcopy-paused");

    if (args->postcopy_recovery_fail_stage) {
        /*
         * Test when a wrong socket specified for recover, and then the
         * ability to kick it out, and continue with a correct socket.
         */
        postcopy_recover_fail(from, to, args->postcopy_recovery_fail_stage);
        /* continue with a good recovery */
    }

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
    migrate_qmp(from, to, uri, NULL, "{'resume': true}");

    /* Restore the postcopy bandwidth to unlimited */
    migrate_set_parameter_int(from, "max-postcopy-bandwidth", 0);

    migrate_postcopy_complete(from, to, args);
}

static void test_postcopy_recovery(void)
{
    MigrateCommon args = { };

    test_postcopy_recovery_common(&args);
}

static void test_postcopy_recovery_fail_handshake(void)
{
    MigrateCommon args = {
        .postcopy_recovery_fail_stage = POSTCOPY_FAIL_RECOVERY,
    };

    test_postcopy_recovery_common(&args);
}

static void test_postcopy_recovery_fail_reconnect(void)
{
    MigrateCommon args = {
        .postcopy_recovery_fail_stage = POSTCOPY_FAIL_CHANNEL_ESTABLISH,
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
    migrate_qmp(from, to, "tcp:127.0.0.1:0", NULL, "{}");
    wait_for_migration_fail(from, false);
    test_migrate_end(from, to, false);
}

#ifndef _WIN32
static void test_analyze_script(void)
{
    MigrateStart args = {
        .opts_source = "-uuid 11111111-1111-1111-1111-111111111111",
    };
    QTestState *from, *to;
    g_autofree char *uri = NULL;
    g_autofree char *file = NULL;
    int pid, wstatus;
    const char *python = g_getenv("PYTHON");

    if (!python) {
        g_test_skip("PYTHON variable not set");
        return;
    }

    /* dummy url */
    if (test_migrate_start(&from, &to, "tcp:127.0.0.1:0", &args)) {
        return;
    }

    /*
     * Setting these two capabilities causes the "configuration"
     * vmstate to include subsections for them. The script needs to
     * parse those subsections properly.
     */
    migrate_set_capability(from, "validate-uuid", true);
    migrate_set_capability(from, "x-ignore-shared", true);

    file = g_strdup_printf("%s/migfile", tmpfs);
    uri = g_strdup_printf("exec:cat > %s", file);

    migrate_ensure_converge(from);
    migrate_qmp(from, to, uri, NULL, "{}");
    wait_for_migration_complete(from);

    pid = fork();
    if (!pid) {
        close(1);
        open("/dev/null", O_WRONLY);
        execl(python, python, ANALYZE_SCRIPT, "-f", file, NULL);
        g_assert_not_reached();
    }

    g_assert(waitpid(pid, &wstatus, 0) == pid);
    if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
        g_test_message("Failed to analyze the migration stream");
        g_test_fail();
    }
    test_migrate_end(from, to, false);
    cleanup("migfile");
}
#endif

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
        wait_for_suspend(from, &src_state);
    }

    if (args->live) {
        migrate_ensure_non_converge(from);
        migrate_prepare_for_dirty_mem(from);
    } else {
        /*
         * Testing non-live migration, we allow it to run at
         * full speed to ensure short test case duration.
         * For tests expected to fail, we don't need to
         * change anything.
         */
        if (args->result == MIG_TEST_SUCCEED) {
            qtest_qmp_assert_success(from, "{ 'execute' : 'stop'}");
            wait_for_stop(from, &src_state);
            migrate_ensure_converge(from);
        }
    }

    if (args->result == MIG_TEST_QMP_ERROR) {
        migrate_qmp_fail(from, args->connect_uri, args->connect_channels, "{}");
        goto finish;
    }

    migrate_qmp(from, to, args->connect_uri, args->connect_channels, "{}");

    if (args->result != MIG_TEST_SUCCEED) {
        bool allow_active = args->result == MIG_TEST_FAIL;
        wait_for_migration_fail(from, allow_active);

        if (args->result == MIG_TEST_FAIL_DEST_QUIT_ERR) {
            qtest_set_expected_status(to, EXIT_FAILURE);
        }
    } else {
        if (args->live) {
            /*
             * For initial iteration(s) we must do a full pass,
             * but for the final iteration, we need only wait
             * for some dirty mem before switching to converge
             */
            while (args->iterations > 1) {
                wait_for_migration_pass(from);
                args->iterations--;
            }
            migrate_wait_for_dirty_mem(from, to);

            migrate_ensure_converge(from);

            /*
             * We do this first, as it has a timeout to stop us
             * hanging forever if migration didn't converge
             */
            wait_for_migration_complete(from);

            wait_for_stop(from, &src_state);

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

        wait_for_resume(to, &dst_state);

        if (args->start.suspend_me) {
            /* wakeup succeeds only if guest is suspended */
            qtest_qmp_assert_success(to, "{'execute': 'system_wakeup'}");
        }

        wait_for_serial("dest_serial");
    }

finish:
    if (args->finish_hook) {
        args->finish_hook(from, to, data_hook);
    }

    test_migrate_end(from, to, args->result == MIG_TEST_SUCCEED);
}

static void file_dirty_offset_region(void)
{
    g_autofree char *path = g_strdup_printf("%s/%s", tmpfs, FILE_TEST_FILENAME);
    size_t size = FILE_TEST_OFFSET;
    g_autofree char *data = g_new0(char, size);

    memset(data, FILE_TEST_MARKER, size);
    g_assert(g_file_set_contents(path, data, size, NULL));
}

static void file_check_offset_region(void)
{
    g_autofree char *path = g_strdup_printf("%s/%s", tmpfs, FILE_TEST_FILENAME);
    size_t size = FILE_TEST_OFFSET;
    g_autofree char *expected = g_new0(char, size);
    g_autofree char *actual = NULL;
    uint64_t *stream_start;

    /*
     * Ensure the skipped offset region's data has not been touched
     * and the migration stream starts at the right place.
     */

    memset(expected, FILE_TEST_MARKER, size);

    g_assert(g_file_get_contents(path, &actual, NULL, NULL));
    g_assert(!memcmp(actual, expected, size));

    stream_start = (uint64_t *)(actual + size);
    g_assert_cmpint(cpu_to_be64(*stream_start) >> 32, ==, QEMU_VM_FILE_MAGIC);
}

static void test_file_common(MigrateCommon *args, bool stop_src)
{
    QTestState *from, *to;
    void *data_hook = NULL;
    bool check_offset = false;

    if (test_migrate_start(&from, &to, args->listen_uri, &args->start)) {
        return;
    }

    /*
     * File migration is never live. We can keep the source VM running
     * during migration, but the destination will not be running
     * concurrently.
     */
    g_assert_false(args->live);

    if (g_strrstr(args->connect_uri, "offset=")) {
        check_offset = true;
        /*
         * This comes before the start_hook because it's equivalent to
         * a management application creating the file and writing to
         * it so hooks should expect the file to be already present.
         */
        file_dirty_offset_region();
    }

    if (args->start_hook) {
        data_hook = args->start_hook(from, to);
    }

    migrate_ensure_converge(from);
    wait_for_serial("src_serial");

    if (stop_src) {
        qtest_qmp_assert_success(from, "{ 'execute' : 'stop'}");
        wait_for_stop(from, &src_state);
    }

    if (args->result == MIG_TEST_QMP_ERROR) {
        migrate_qmp_fail(from, args->connect_uri, NULL, "{}");
        goto finish;
    }

    migrate_qmp(from, to, args->connect_uri, NULL, "{}");
    wait_for_migration_complete(from);

    /*
     * We need to wait for the source to finish before starting the
     * destination.
     */
    migrate_incoming_qmp(to, args->connect_uri, "{}");
    wait_for_migration_complete(to);

    if (stop_src) {
        qtest_qmp_assert_success(to, "{ 'execute' : 'cont'}");
    }
    wait_for_resume(to, &dst_state);

    wait_for_serial("dest_serial");

    if (check_offset) {
        file_check_offset_region();
    }

finish:
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

static void test_precopy_unix_suspend_live(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .listen_uri = uri,
        .connect_uri = uri,
        /*
         * despite being live, the test is fast because the src
         * suspends immediately.
         */
        .live = true,
        .start.suspend_me = true,
    };

    test_precopy_common(&args);
}

static void test_precopy_unix_suspend_notlive(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .listen_uri = uri,
        .connect_uri = uri,
        .start.suspend_me = true,
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

    migrate_ensure_non_converge(from);
    migrate_prepare_for_dirty_mem(from);

    migrate_set_capability(from, "x-ignore-shared", true);
    migrate_set_capability(to, "x-ignore-shared", true);

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    migrate_qmp(from, to, uri, NULL, "{}");

    migrate_wait_for_dirty_mem(from, to);

    wait_for_stop(from, &src_state);

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

static void test_precopy_file(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
    };

    test_file_common(&args, true);
}

#ifndef _WIN32
static void fdset_add_fds(QTestState *qts, const char *file, int flags,
                          int num_fds, bool direct_io)
{
    for (int i = 0; i < num_fds; i++) {
        int fd;

#ifdef O_DIRECT
        /* only secondary channels can use direct-io */
        if (direct_io && i != 0) {
            flags |= O_DIRECT;
        }
#endif

        fd = open(file, flags, 0660);
        assert(fd != -1);

        qtest_qmp_fds_assert_success(qts, &fd, 1, "{'execute': 'add-fd', "
                                     "'arguments': {'fdset-id': 1}}");
        close(fd);
    }
}

static void *file_offset_fdset_start_hook(QTestState *from, QTestState *to)
{
    g_autofree char *file = g_strdup_printf("%s/%s", tmpfs, FILE_TEST_FILENAME);

    fdset_add_fds(from, file, O_WRONLY, 1, false);
    fdset_add_fds(to, file, O_RDONLY, 1, false);

    return NULL;
}

static void test_precopy_file_offset_fdset(void)
{
    g_autofree char *uri = g_strdup_printf("file:/dev/fdset/1,offset=%d",
                                           FILE_TEST_OFFSET);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .start_hook = file_offset_fdset_start_hook,
    };

    test_file_common(&args, false);
}
#endif

static void test_precopy_file_offset(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s,offset=%d", tmpfs,
                                           FILE_TEST_FILENAME,
                                           FILE_TEST_OFFSET);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
    };

    test_file_common(&args, false);
}

static void test_precopy_file_offset_bad(void)
{
    /* using a value not supported by qemu_strtosz() */
    g_autofree char *uri = g_strdup_printf("file:%s/%s,offset=0x20M",
                                           tmpfs, FILE_TEST_FILENAME);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .result = MIG_TEST_QMP_ERROR,
    };

    test_file_common(&args, false);
}

static void *test_mode_reboot_start(QTestState *from, QTestState *to)
{
    migrate_set_parameter_str(from, "mode", "cpr-reboot");
    migrate_set_parameter_str(to, "mode", "cpr-reboot");

    migrate_set_capability(from, "x-ignore-shared", true);
    migrate_set_capability(to, "x-ignore-shared", true);

    return NULL;
}

static void *migrate_mapped_ram_start(QTestState *from, QTestState *to)
{
    migrate_set_capability(from, "mapped-ram", true);
    migrate_set_capability(to, "mapped-ram", true);

    return NULL;
}

static void test_mode_reboot(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    MigrateCommon args = {
        .start.use_shmem = true,
        .connect_uri = uri,
        .listen_uri = "defer",
        .start_hook = test_mode_reboot_start
    };

    test_file_common(&args, true);
}

static void test_precopy_file_mapped_ram_live(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .start_hook = migrate_mapped_ram_start,
    };

    test_file_common(&args, false);
}

static void test_precopy_file_mapped_ram(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .start_hook = migrate_mapped_ram_start,
    };

    test_file_common(&args, true);
}

static void *migrate_multifd_mapped_ram_start(QTestState *from, QTestState *to)
{
    migrate_mapped_ram_start(from, to);

    migrate_set_parameter_int(from, "multifd-channels", 4);
    migrate_set_parameter_int(to, "multifd-channels", 4);

    migrate_set_capability(from, "multifd", true);
    migrate_set_capability(to, "multifd", true);

    return NULL;
}

static void test_multifd_file_mapped_ram_live(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .start_hook = migrate_multifd_mapped_ram_start,
    };

    test_file_common(&args, false);
}

static void test_multifd_file_mapped_ram(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .start_hook = migrate_multifd_mapped_ram_start,
    };

    test_file_common(&args, true);
}

static void *multifd_mapped_ram_dio_start(QTestState *from, QTestState *to)
{
    migrate_multifd_mapped_ram_start(from, to);

    migrate_set_parameter_bool(from, "direct-io", true);
    migrate_set_parameter_bool(to, "direct-io", true);

    return NULL;
}

static void test_multifd_file_mapped_ram_dio(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .start_hook = multifd_mapped_ram_dio_start,
    };

    if (!probe_o_direct_support(tmpfs)) {
        g_test_skip("Filesystem does not support O_DIRECT");
        return;
    }

    test_file_common(&args, true);
}

#ifndef _WIN32
static void multifd_mapped_ram_fdset_end(QTestState *from, QTestState *to,
                                         void *opaque)
{
    QDict *resp;
    QList *fdsets;

    /*
     * Remove the fdsets after migration, otherwise a second migration
     * would fail due fdset reuse.
     */
    qtest_qmp_assert_success(from, "{'execute': 'remove-fd', "
                             "'arguments': { 'fdset-id': 1}}");

    /*
     * Make sure no fdsets are left after migration, otherwise a
     * second migration would fail due fdset reuse.
     */
    resp = qtest_qmp(from, "{'execute': 'query-fdsets', "
                     "'arguments': {}}");
    g_assert(qdict_haskey(resp, "return"));
    fdsets = qdict_get_qlist(resp, "return");
    g_assert(fdsets && qlist_empty(fdsets));
    qobject_unref(resp);
}

static void *multifd_mapped_ram_fdset_dio(QTestState *from, QTestState *to)
{
    g_autofree char *file = g_strdup_printf("%s/%s", tmpfs, FILE_TEST_FILENAME);

    fdset_add_fds(from, file, O_WRONLY, 2, true);
    fdset_add_fds(to, file, O_RDONLY, 2, true);

    migrate_multifd_mapped_ram_start(from, to);
    migrate_set_parameter_bool(from, "direct-io", true);
    migrate_set_parameter_bool(to, "direct-io", true);

    return NULL;
}

static void *multifd_mapped_ram_fdset(QTestState *from, QTestState *to)
{
    g_autofree char *file = g_strdup_printf("%s/%s", tmpfs, FILE_TEST_FILENAME);

    fdset_add_fds(from, file, O_WRONLY, 2, false);
    fdset_add_fds(to, file, O_RDONLY, 2, false);

    migrate_multifd_mapped_ram_start(from, to);

    return NULL;
}

static void test_multifd_file_mapped_ram_fdset(void)
{
    g_autofree char *uri = g_strdup_printf("file:/dev/fdset/1,offset=%d",
                                           FILE_TEST_OFFSET);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .start_hook = multifd_mapped_ram_fdset,
        .finish_hook = multifd_mapped_ram_fdset_end,
    };

    test_file_common(&args, true);
}

static void test_multifd_file_mapped_ram_fdset_dio(void)
{
    g_autofree char *uri = g_strdup_printf("file:/dev/fdset/1,offset=%d",
                                           FILE_TEST_OFFSET);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .start_hook = multifd_mapped_ram_fdset_dio,
        .finish_hook = multifd_mapped_ram_fdset_end,
    };

    if (!probe_o_direct_support(tmpfs)) {
        g_test_skip("Filesystem does not support O_DIRECT");
        return;
    }

    test_file_common(&args, true);
}
#endif /* !_WIN32 */

static void test_precopy_tcp_plain(void)
{
    MigrateCommon args = {
        .listen_uri = "tcp:127.0.0.1:0",
    };

    test_precopy_common(&args);
}

static void *test_migrate_switchover_ack_start(QTestState *from, QTestState *to)
{

    migrate_set_capability(from, "return-path", true);
    migrate_set_capability(to, "return-path", true);

    migrate_set_capability(from, "switchover-ack", true);
    migrate_set_capability(to, "switchover-ack", true);

    return NULL;
}

static void test_precopy_tcp_switchover_ack(void)
{
    MigrateCommon args = {
        .listen_uri = "tcp:127.0.0.1:0",
        .start_hook = test_migrate_switchover_ack_start,
        /*
         * Source VM must be running in order to consider the switchover ACK
         * when deciding to do switchover or not.
         */
        .live = true,
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
    migrate_incoming_qmp(to, "fd:fd-mig", "{}");

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

static void test_migrate_precopy_fd_socket(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .connect_uri = "fd:fd-mig",
        .start_hook = test_migrate_fd_start_hook,
        .finish_hook = test_migrate_fd_finish_hook
    };
    test_precopy_common(&args);
}

static void *migrate_precopy_fd_file_start(QTestState *from, QTestState *to)
{
    g_autofree char *file = g_strdup_printf("%s/%s", tmpfs, FILE_TEST_FILENAME);
    int src_flags = O_CREAT | O_RDWR;
    int dst_flags = O_CREAT | O_RDWR;
    int fds[2];

    fds[0] = open(file, src_flags, 0660);
    assert(fds[0] != -1);

    fds[1] = open(file, dst_flags, 0660);
    assert(fds[1] != -1);


    qtest_qmp_fds_assert_success(to, &fds[0], 1,
                                 "{ 'execute': 'getfd',"
                                 "  'arguments': { 'fdname': 'fd-mig' }}");

    qtest_qmp_fds_assert_success(from, &fds[1], 1,
                                 "{ 'execute': 'getfd',"
                                 "  'arguments': { 'fdname': 'fd-mig' }}");

    close(fds[0]);
    close(fds[1]);

    return NULL;
}

static void test_migrate_precopy_fd_file(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .connect_uri = "fd:fd-mig",
        .start_hook = migrate_precopy_fd_file_start,
        .finish_hook = test_migrate_fd_finish_hook
    };
    test_file_common(&args, true);
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

    migrate_qmp(from, to, uri, NULL, "{}");

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

static void do_test_validate_uri_channel(MigrateCommon *args)
{
    QTestState *from, *to;

    if (test_migrate_start(&from, &to, args->listen_uri, &args->start)) {
        return;
    }

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    /*
     * 'uri' and 'channels' validation is checked even before the migration
     * starts.
     */
    migrate_qmp_fail(from, args->connect_uri, args->connect_channels, "{}");
    test_migrate_end(from, to, false);
}

static void test_validate_uri_channels_both_set(void)
{
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .listen_uri = "defer",
        .connect_uri = "tcp:127.0.0.1:0",
        .connect_channels = "[ { 'channel-type': 'main',"
                            "    'addr': { 'transport': 'socket',"
                            "              'type': 'inet',"
                            "              'host': '127.0.0.1',"
                            "              'port': '0' } } ]",
    };

    do_test_validate_uri_channel(&args);
}

static void test_validate_uri_channels_none_set(void)
{
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
        },
        .listen_uri = "defer",
    };

    do_test_validate_uri_channel(&args);
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
     * E.g., with 1Gb/s bandwidth migration may pass without throttling,
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

    migrate_qmp(from, to, uri, NULL, "{}");

    /* Wait for throttling begins */
    percentage = 0;
    do {
        percentage = read_migrate_property_int(from, "cpu-throttle-percentage");
        if (percentage != 0) {
            break;
        }
        usleep(20);
        g_assert_false(src_state.stop_seen);
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
    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", "{}");

    return NULL;
}

static void *
test_migrate_precopy_tcp_multifd_start(QTestState *from,
                                       QTestState *to)
{
    return test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
}

static void *
test_migrate_precopy_tcp_multifd_start_zero_page_legacy(QTestState *from,
                                                        QTestState *to)
{
    test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
    migrate_set_parameter_str(from, "zero-page-detection", "legacy");
    return NULL;
}

static void *
test_migration_precopy_tcp_multifd_start_no_zero_page(QTestState *from,
                                                      QTestState *to)
{
    test_migrate_precopy_tcp_multifd_start_common(from, to, "none");
    migrate_set_parameter_str(from, "zero-page-detection", "none");
    return NULL;
}

static void *
test_migrate_precopy_tcp_multifd_zlib_start(QTestState *from,
                                            QTestState *to)
{
    /*
     * Overloading this test to also check that set_parameter does not error.
     * This is also done in the tests for the other compression methods.
     */
    migrate_set_parameter_int(from, "multifd-zlib-level", 2);
    migrate_set_parameter_int(to, "multifd-zlib-level", 2);

    return test_migrate_precopy_tcp_multifd_start_common(from, to, "zlib");
}

#ifdef CONFIG_ZSTD
static void *
test_migrate_precopy_tcp_multifd_zstd_start(QTestState *from,
                                            QTestState *to)
{
    migrate_set_parameter_int(from, "multifd-zstd-level", 2);
    migrate_set_parameter_int(to, "multifd-zstd-level", 2);

    return test_migrate_precopy_tcp_multifd_start_common(from, to, "zstd");
}
#endif /* CONFIG_ZSTD */

#ifdef CONFIG_QATZIP
static void *
test_migrate_precopy_tcp_multifd_qatzip_start(QTestState *from,
                                              QTestState *to)
{
    migrate_set_parameter_int(from, "multifd-qatzip-level", 2);
    migrate_set_parameter_int(to, "multifd-qatzip-level", 2);

    return test_migrate_precopy_tcp_multifd_start_common(from, to, "qatzip");
}
#endif

#ifdef CONFIG_QPL
static void *
test_migrate_precopy_tcp_multifd_qpl_start(QTestState *from,
                                            QTestState *to)
{
    return test_migrate_precopy_tcp_multifd_start_common(from, to, "qpl");
}
#endif /* CONFIG_QPL */
#ifdef CONFIG_UADK
static void *
test_migrate_precopy_tcp_multifd_uadk_start(QTestState *from,
                                            QTestState *to)
{
    return test_migrate_precopy_tcp_multifd_start_common(from, to, "uadk");
}
#endif /* CONFIG_UADK */

static void test_multifd_tcp_uri_none(void)
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

static void test_multifd_tcp_zero_page_legacy(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = test_migrate_precopy_tcp_multifd_start_zero_page_legacy,
        /*
         * Multifd is more complicated than most of the features, it
         * directly takes guest page buffers when sending, make sure
         * everything will work alright even if guest page is changing.
         */
        .live = true,
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_no_zero_page(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = test_migration_precopy_tcp_multifd_start_no_zero_page,
        /*
         * Multifd is more complicated than most of the features, it
         * directly takes guest page buffers when sending, make sure
         * everything will work alright even if guest page is changing.
         */
        .live = true,
    };
    test_precopy_common(&args);
}

static void test_multifd_tcp_channels_none(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = test_migrate_precopy_tcp_multifd_start,
        .live = true,
        .connect_channels = "[ { 'channel-type': 'main',"
                            "    'addr': { 'transport': 'socket',"
                            "              'type': 'inet',"
                            "              'host': '127.0.0.1',"
                            "              'port': '0' } } ]",
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

#ifdef CONFIG_QATZIP
static void test_multifd_tcp_qatzip(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = test_migrate_precopy_tcp_multifd_qatzip_start,
    };
    test_precopy_common(&args);
}
#endif

#ifdef CONFIG_QPL
static void test_multifd_tcp_qpl(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = test_migrate_precopy_tcp_multifd_qpl_start,
    };
    test_precopy_common(&args);
}
#endif

#ifdef CONFIG_UADK
static void test_multifd_tcp_uadk(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start_hook = test_migrate_precopy_tcp_multifd_uadk_start,
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

    if (test_migrate_start(&from, &to, "defer", &args)) {
        return;
    }

    migrate_ensure_non_converge(from);
    migrate_prepare_for_dirty_mem(from);

    migrate_set_parameter_int(from, "multifd-channels", 16);
    migrate_set_parameter_int(to, "multifd-channels", 16);

    migrate_set_capability(from, "multifd", true);
    migrate_set_capability(to, "multifd", true);

    /* Start incoming migration from the 1st socket */
    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", "{}");

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    migrate_qmp(from, to, NULL, NULL, "{}");

    migrate_wait_for_dirty_mem(from, to);

    migrate_cancel(from);

    /* Make sure QEMU process "to" exited */
    qtest_set_expected_status(to, EXIT_FAILURE);
    qtest_wait_qemu(to);
    qtest_quit(to);

    /*
     * Ensure the source QEMU finishes its cancellation process before we
     * proceed with the setup of the next migration. The test_migrate_start()
     * function and others might want to interact with the source in a way that
     * is not possible while the migration is not canceled properly. For
     * example, setting migration capabilities when the migration is still
     * running leads to an error.
     */
    wait_for_migration_status(from, "cancelled", NULL);

    args = (MigrateStart){
        .only_target = true,
    };

    if (test_migrate_start(&from, &to2, "defer", &args)) {
        return;
    }

    migrate_set_parameter_int(to2, "multifd-channels", 16);

    migrate_set_capability(to2, "multifd", true);

    /* Start incoming migration from the 1st socket */
    migrate_incoming_qmp(to2, "tcp:127.0.0.1:0", "{}");

    migrate_ensure_non_converge(from);

    migrate_qmp(from, to2, NULL, NULL, "{}");

    migrate_wait_for_dirty_mem(from, to2);

    migrate_ensure_converge(from);

    wait_for_stop(from, &src_state);
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
    const char *status;
    bool ready;

    rsp_return = query_dirty_rate(who);
    g_assert(rsp_return);

    status = qdict_get_str(rsp_return, "status");
    g_assert(status);
    ready = g_strcmp0(status, "measuring");
    qobject_unref(rsp_return);

    return ready;
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
    const char *status;
    QList *rates;
    const QListEntry *entry;
    QDict *rate;
    int64_t dirtyrate;

    rsp_return = query_dirty_rate(who);
    g_assert(rsp_return);

    status = qdict_get_str(rsp_return, "status");
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

    bootfile_create(tmpfs, false);
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

static void migrate_dirty_limit_wait_showup(QTestState *from,
                                            const int64_t period,
                                            const int64_t value)
{
    /* Enable dirty limit capability */
    migrate_set_capability(from, "dirty-limit", true);

    /* Set dirty limit parameters */
    migrate_set_parameter_int(from, "x-vcpu-dirty-limit-period", period);
    migrate_set_parameter_int(from, "vcpu-dirty-limit", value);

    /* Make sure migrate can't converge */
    migrate_ensure_non_converge(from);

    /* To check limit rate after precopy */
    migrate_set_capability(from, "pause-before-switchover", true);

    /* Wait for the serial output from the source */
    wait_for_serial("src_serial");
}

/*
 * This test does:
 *  source                          destination
 *  start vm
 *                                  start incoming vm
 *  migrate
 *  wait dirty limit to begin
 *  cancel migrate
 *  cancellation check
 *                                  restart incoming vm
 *  migrate
 *  wait dirty limit to begin
 *  wait pre-switchover event
 *  convergence condition check
 *
 * And see if dirty limit migration works correctly.
 * This test case involves many passes, so it runs in slow mode only.
 */
static void test_migrate_dirty_limit(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    QTestState *from, *to;
    int64_t remaining;
    uint64_t throttle_us_per_full;
    /*
     * We want the test to be stable and as fast as possible.
     * E.g., with 1Gb/s bandwidth migration may pass without dirty limit,
     * so we need to decrease a bandwidth.
     */
    const int64_t dirtylimit_period = 1000, dirtylimit_value = 50;
    const int64_t max_bandwidth = 400000000; /* ~400Mb/s */
    const int64_t downtime_limit = 250; /* 250ms */
    /*
     * We migrate through unix-socket (> 500Mb/s).
     * Thus, expected migration speed ~= bandwidth limit (< 500Mb/s).
     * So, we can predict expected_threshold
     */
    const int64_t expected_threshold = max_bandwidth * downtime_limit / 1000;
    int max_try_count = 10;
    MigrateCommon args = {
        .start = {
            .hide_stderr = true,
            .use_dirty_ring = true,
        },
        .listen_uri = uri,
        .connect_uri = uri,
    };

    /* Start src, dst vm */
    if (test_migrate_start(&from, &to, args.listen_uri, &args.start)) {
        return;
    }

    /* Prepare for dirty limit migration and wait src vm show up */
    migrate_dirty_limit_wait_showup(from, dirtylimit_period, dirtylimit_value);

    /* Start migrate */
    migrate_qmp(from, to, args.connect_uri, NULL, "{}");

    /* Wait for dirty limit throttle begin */
    throttle_us_per_full = 0;
    while (throttle_us_per_full == 0) {
        throttle_us_per_full =
        read_migrate_property_int(from, "dirty-limit-throttle-time-per-round");
        usleep(100);
        g_assert_false(src_state.stop_seen);
    }

    /* Now cancel migrate and wait for dirty limit throttle switch off */
    migrate_cancel(from);
    wait_for_migration_status(from, "cancelled", NULL);

    /* Check if dirty limit throttle switched off, set timeout 1ms */
    do {
        throttle_us_per_full =
        read_migrate_property_int(from, "dirty-limit-throttle-time-per-round");
        usleep(100);
        g_assert_false(src_state.stop_seen);
    } while (throttle_us_per_full != 0 && --max_try_count);

    /* Assert dirty limit is not in service */
    g_assert_cmpint(throttle_us_per_full, ==, 0);

    args = (MigrateCommon) {
        .start = {
            .only_target = true,
            .use_dirty_ring = true,
        },
        .listen_uri = uri,
        .connect_uri = uri,
    };

    /* Restart dst vm, src vm already show up so we needn't wait anymore */
    if (test_migrate_start(&from, &to, args.listen_uri, &args.start)) {
        return;
    }

    /* Start migrate */
    migrate_qmp(from, to, args.connect_uri, NULL, "{}");

    /* Wait for dirty limit throttle begin */
    throttle_us_per_full = 0;
    while (throttle_us_per_full == 0) {
        throttle_us_per_full =
        read_migrate_property_int(from, "dirty-limit-throttle-time-per-round");
        usleep(100);
        g_assert_false(src_state.stop_seen);
    }

    /*
     * The dirty limit rate should equals the return value of
     * query-vcpu-dirty-limit if dirty limit cap set
     */
    g_assert_cmpint(dirtylimit_value, ==, get_limit_rate(from));

    /* Now, we have tested if dirty limit works, let it converge */
    migrate_set_parameter_int(from, "downtime-limit", downtime_limit);
    migrate_set_parameter_int(from, "max-bandwidth", max_bandwidth);

    /*
     * Wait for pre-switchover status to check if migration
     * satisfy the convergence condition
     */
    wait_for_migration_status(from, "pre-switchover", NULL);

    remaining = read_ram_property_int(from, "remaining");
    g_assert_cmpint(remaining, <,
                    (expected_threshold + expected_threshold / 100));

    migrate_continue(from, "pre-switchover");

    qtest_qmp_eventwait(to, "RESUME");

    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);

    test_migrate_end(from, to, true);
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
    bool has_uffd, is_x86;
    const char *arch;
    g_autoptr(GError) err = NULL;
    const char *qemu_src = getenv(QEMU_ENV_SRC);
    const char *qemu_dst = getenv(QEMU_ENV_DST);
    int ret;

    g_test_init(&argc, &argv, NULL);

    /*
     * The default QTEST_QEMU_BINARY must always be provided because
     * that is what helpers use to query the accel type and
     * architecture.
     */
    if (qemu_src && qemu_dst) {
        g_test_message("Only one of %s, %s is allowed",
                       QEMU_ENV_SRC, QEMU_ENV_DST);
        exit(1);
    }

    has_kvm = qtest_has_accel("kvm");
    has_tcg = qtest_has_accel("tcg");

    if (!has_tcg && !has_kvm) {
        g_test_skip("No KVM or TCG accelerator available");
        return 0;
    }

    has_uffd = ufd_version_check();
    arch = qtest_get_arch();
    is_x86 = !strcmp(arch, "i386") || !strcmp(arch, "x86_64");

    tmpfs = g_dir_make_tmp("migration-test-XXXXXX", &err);
    if (!tmpfs) {
        g_test_message("Can't create temporary directory in %s: %s",
                       g_get_tmp_dir(), err->message);
    }
    g_assert(tmpfs);

    module_call_init(MODULE_INIT_QOM);

    migration_test_add("/migration/bad_dest", test_baddest);
#ifndef _WIN32
    migration_test_add("/migration/analyze-script", test_analyze_script);
#endif

    if (is_x86) {
        migration_test_add("/migration/precopy/unix/suspend/live",
                           test_precopy_unix_suspend_live);
        migration_test_add("/migration/precopy/unix/suspend/notlive",
                           test_precopy_unix_suspend_notlive);
    }

    if (has_uffd) {
        migration_test_add("/migration/postcopy/plain", test_postcopy);
        migration_test_add("/migration/postcopy/recovery/plain",
                           test_postcopy_recovery);
        migration_test_add("/migration/postcopy/preempt/plain",
                           test_postcopy_preempt);
        migration_test_add("/migration/postcopy/preempt/recovery/plain",
                           test_postcopy_preempt_recovery);
        migration_test_add("/migration/postcopy/recovery/double-failures/handshake",
                           test_postcopy_recovery_fail_handshake);
        migration_test_add("/migration/postcopy/recovery/double-failures/reconnect",
                           test_postcopy_recovery_fail_reconnect);
        if (is_x86) {
            migration_test_add("/migration/postcopy/suspend",
                               test_postcopy_suspend);
        }
    }

    migration_test_add("/migration/precopy/unix/plain",
                       test_precopy_unix_plain);
    if (g_test_slow()) {
        migration_test_add("/migration/precopy/unix/xbzrle",
                           test_precopy_unix_xbzrle);
    }
    migration_test_add("/migration/precopy/file",
                       test_precopy_file);
    migration_test_add("/migration/precopy/file/offset",
                       test_precopy_file_offset);
#ifndef _WIN32
    migration_test_add("/migration/precopy/file/offset/fdset",
                       test_precopy_file_offset_fdset);
#endif
    migration_test_add("/migration/precopy/file/offset/bad",
                       test_precopy_file_offset_bad);

    /*
     * Our CI system has problems with shared memory.
     * Don't run this test until we find a workaround.
     */
    if (getenv("QEMU_TEST_FLAKY_TESTS")) {
        migration_test_add("/migration/mode/reboot", test_mode_reboot);
    }

    migration_test_add("/migration/precopy/file/mapped-ram",
                       test_precopy_file_mapped_ram);
    migration_test_add("/migration/precopy/file/mapped-ram/live",
                       test_precopy_file_mapped_ram_live);

    migration_test_add("/migration/multifd/file/mapped-ram",
                       test_multifd_file_mapped_ram);
    migration_test_add("/migration/multifd/file/mapped-ram/live",
                       test_multifd_file_mapped_ram_live);

    migration_test_add("/migration/multifd/file/mapped-ram/dio",
                       test_multifd_file_mapped_ram_dio);

#ifndef _WIN32
    migration_test_add("/migration/multifd/file/mapped-ram/fdset",
                       test_multifd_file_mapped_ram_fdset);
    migration_test_add("/migration/multifd/file/mapped-ram/fdset/dio",
                       test_multifd_file_mapped_ram_fdset_dio);
#endif

#ifdef CONFIG_GNUTLS
    migration_test_add("/migration/precopy/unix/tls/psk",
                       test_precopy_unix_tls_psk);

    if (has_uffd) {
        /*
         * NOTE: psk test is enough for postcopy, as other types of TLS
         * channels are tested under precopy.  Here what we want to test is the
         * general postcopy path that has TLS channel enabled.
         */
        migration_test_add("/migration/postcopy/tls/psk",
                           test_postcopy_tls_psk);
        migration_test_add("/migration/postcopy/recovery/tls/psk",
                           test_postcopy_recovery_tls_psk);
        migration_test_add("/migration/postcopy/preempt/tls/psk",
                           test_postcopy_preempt_tls_psk);
        migration_test_add("/migration/postcopy/preempt/recovery/tls/psk",
                           test_postcopy_preempt_all);
    }
#ifdef CONFIG_TASN1
    migration_test_add("/migration/precopy/unix/tls/x509/default-host",
                       test_precopy_unix_tls_x509_default_host);
    migration_test_add("/migration/precopy/unix/tls/x509/override-host",
                       test_precopy_unix_tls_x509_override_host);
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

    migration_test_add("/migration/precopy/tcp/plain", test_precopy_tcp_plain);

    migration_test_add("/migration/precopy/tcp/plain/switchover-ack",
                       test_precopy_tcp_switchover_ack);

#ifdef CONFIG_GNUTLS
    migration_test_add("/migration/precopy/tcp/tls/psk/match",
                       test_precopy_tcp_tls_psk_match);
    migration_test_add("/migration/precopy/tcp/tls/psk/mismatch",
                       test_precopy_tcp_tls_psk_mismatch);
#ifdef CONFIG_TASN1
    migration_test_add("/migration/precopy/tcp/tls/x509/default-host",
                       test_precopy_tcp_tls_x509_default_host);
    migration_test_add("/migration/precopy/tcp/tls/x509/override-host",
                       test_precopy_tcp_tls_x509_override_host);
    migration_test_add("/migration/precopy/tcp/tls/x509/mismatch-host",
                       test_precopy_tcp_tls_x509_mismatch_host);
    migration_test_add("/migration/precopy/tcp/tls/x509/friendly-client",
                       test_precopy_tcp_tls_x509_friendly_client);
    migration_test_add("/migration/precopy/tcp/tls/x509/hostile-client",
                       test_precopy_tcp_tls_x509_hostile_client);
    migration_test_add("/migration/precopy/tcp/tls/x509/allow-anon-client",
                       test_precopy_tcp_tls_x509_allow_anon_client);
    migration_test_add("/migration/precopy/tcp/tls/x509/reject-anon-client",
                       test_precopy_tcp_tls_x509_reject_anon_client);
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

    /* migration_test_add("/migration/ignore_shared", test_ignore_shared); */
#ifndef _WIN32
    migration_test_add("/migration/precopy/fd/tcp",
                       test_migrate_precopy_fd_socket);
    migration_test_add("/migration/precopy/fd/file",
                       test_migrate_precopy_fd_file);
#endif
    migration_test_add("/migration/validate_uuid", test_validate_uuid);
    migration_test_add("/migration/validate_uuid_error",
                       test_validate_uuid_error);
    migration_test_add("/migration/validate_uuid_src_not_set",
                       test_validate_uuid_src_not_set);
    migration_test_add("/migration/validate_uuid_dst_not_set",
                       test_validate_uuid_dst_not_set);
    migration_test_add("/migration/validate_uri/channels/both_set",
                       test_validate_uri_channels_both_set);
    migration_test_add("/migration/validate_uri/channels/none_set",
                       test_validate_uri_channels_none_set);
    /*
     * See explanation why this test is slow on function definition
     */
    if (g_test_slow()) {
        migration_test_add("/migration/auto_converge",
                           test_migrate_auto_converge);
        if (g_str_equal(arch, "x86_64") &&
            has_kvm && kvm_dirty_ring_supported()) {
            migration_test_add("/migration/dirty_limit",
                               test_migrate_dirty_limit);
        }
    }
    migration_test_add("/migration/multifd/tcp/uri/plain/none",
                       test_multifd_tcp_uri_none);
    migration_test_add("/migration/multifd/tcp/channels/plain/none",
                       test_multifd_tcp_channels_none);
    migration_test_add("/migration/multifd/tcp/plain/zero-page/legacy",
                       test_multifd_tcp_zero_page_legacy);
    migration_test_add("/migration/multifd/tcp/plain/zero-page/none",
                       test_multifd_tcp_no_zero_page);
    migration_test_add("/migration/multifd/tcp/plain/cancel",
                       test_multifd_tcp_cancel);
    migration_test_add("/migration/multifd/tcp/plain/zlib",
                       test_multifd_tcp_zlib);
#ifdef CONFIG_ZSTD
    migration_test_add("/migration/multifd/tcp/plain/zstd",
                       test_multifd_tcp_zstd);
#endif
#ifdef CONFIG_QATZIP
    migration_test_add("/migration/multifd/tcp/plain/qatzip",
                test_multifd_tcp_qatzip);
#endif
#ifdef CONFIG_QPL
    migration_test_add("/migration/multifd/tcp/plain/qpl",
                       test_multifd_tcp_qpl);
#endif
#ifdef CONFIG_UADK
    migration_test_add("/migration/multifd/tcp/plain/uadk",
                       test_multifd_tcp_uadk);
#endif
#ifdef CONFIG_GNUTLS
    migration_test_add("/migration/multifd/tcp/tls/psk/match",
                       test_multifd_tcp_tls_psk_match);
    migration_test_add("/migration/multifd/tcp/tls/psk/mismatch",
                       test_multifd_tcp_tls_psk_mismatch);
#ifdef CONFIG_TASN1
    migration_test_add("/migration/multifd/tcp/tls/x509/default-host",
                       test_multifd_tcp_tls_x509_default_host);
    migration_test_add("/migration/multifd/tcp/tls/x509/override-host",
                       test_multifd_tcp_tls_x509_override_host);
    migration_test_add("/migration/multifd/tcp/tls/x509/mismatch-host",
                       test_multifd_tcp_tls_x509_mismatch_host);
    migration_test_add("/migration/multifd/tcp/tls/x509/allow-anon-client",
                       test_multifd_tcp_tls_x509_allow_anon_client);
    migration_test_add("/migration/multifd/tcp/tls/x509/reject-anon-client",
                       test_multifd_tcp_tls_x509_reject_anon_client);
#endif /* CONFIG_TASN1 */
#endif /* CONFIG_GNUTLS */

    if (g_str_equal(arch, "x86_64") && has_kvm && kvm_dirty_ring_supported()) {
        migration_test_add("/migration/dirty_ring",
                           test_precopy_unix_dirty_ring);
        if (qtest_has_machine("pc") && g_test_slow()) {
            migration_test_add("/migration/vcpu_dirty_limit",
                               test_vcpu_dirty_limit);
        }
    }

    ret = g_test_run();

    g_assert_cmpint(ret, ==, 0);

    bootfile_delete();
    ret = rmdir(tmpfs);
    if (ret != 0) {
        g_test_message("unable to rmdir: path (%s): %s",
                       tmpfs, strerror(errno));
    }
    g_free(tmpfs);

    return ret;
}
