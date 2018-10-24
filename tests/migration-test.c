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
#include "qapi/qmp/qjson.h"
#include "qemu/option.h"
#include "qemu/range.h"
#include "qemu/sockets.h"
#include "chardev/char.h"
#include "sysemu/sysemu.h"

#include "migration/migration-test.h"

/* TODO actually test the results and get rid of this */
#define qtest_qmp_discard_response(...) qobject_unref(qtest_qmp(__VA_ARGS__))

unsigned start_address;
unsigned end_address;
bool got_stop;
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

static void init_bootfile(const char *bootpath, void *content)
{
    FILE *bootfile = fopen(bootpath, "wb");

    g_assert_cmpint(fwrite(content, 512, 1, bootfile), ==, 1);
    fclose(bootfile);
}

#include "tests/migration/s390x/a-b-bios.h"

static void init_bootfile_s390x(const char *bootpath)
{
    FILE *bootfile = fopen(bootpath, "wb");
    size_t len = sizeof(s390x_elf);

    g_assert_cmpint(fwrite(s390x_elf, len, 1, bootfile), ==, 1);
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

static void stop_cb(void *opaque, const char *name, QDict *data)
{
    if (!strcmp(name, "STOP")) {
        got_stop = true;
    }
}

/*
 * Events can get in the way of responses we are actually waiting for.
 */
GCC_FMT_ATTR(2, 3)
static QDict *wait_command(QTestState *who, const char *command, ...)
{
    va_list ap;

    va_start(ap, command);
    qtest_qmp_vsend(who, command, ap);
    va_end(ap);

    return qtest_qmp_receive_success(who, stop_cb, NULL);
}

/*
 * Note: caller is responsible to free the returned object via
 * qobject_unref() after use
 */
static QDict *migrate_query(QTestState *who)
{
    return wait_command(who, "{ 'execute': 'query-migrate' }");
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

/*
 * It's tricky to use qemu's migration event capability with qtest,
 * events suddenly appearing confuse the qmp()/hmp() responses.
 */

static uint64_t get_migration_pass(QTestState *who)
{
    QDict *rsp_return, *rsp_ram;
    uint64_t result;

    rsp_return = migrate_query(who);
    if (!qdict_haskey(rsp_return, "ram")) {
        /* Still in setup */
        result = 0;
    } else {
        rsp_ram = qdict_get_qdict(rsp_return, "ram");
        result = qdict_get_try_int(rsp_ram, "dirty-sync-count", 0);
    }
    qobject_unref(rsp_return);
    return result;
}

static void read_blocktime(QTestState *who)
{
    QDict *rsp_return;

    rsp_return = migrate_query(who);
    g_assert(qdict_haskey(rsp_return, "postcopy-blocktime"));
    qobject_unref(rsp_return);
}

static void wait_for_migration_status(QTestState *who,
                                      const char *goal)
{
    while (true) {
        bool completed;
        char *status;

        status = migrate_query_status(who);
        completed = strcmp(status, goal) == 0;
        g_assert_cmpstr(status, !=,  "failed");
        g_free(status);
        if (completed) {
            return;
        }
        usleep(1000);
    }
}

static void wait_for_migration_complete(QTestState *who)
{
    wait_for_migration_status(who, "completed");
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
    bool bad = false;

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
                fprintf(stderr, "Memory content inconsistency at %x"
                                " first_byte = %x last_byte = %x current = %x"
                                " hit_edge = %x\n",
                                address, first_byte, last_byte, b, hit_edge);
                bad = true;
            }
        }
    }
    g_assert_false(bad);
}

static void cleanup(const char *filename)
{
    char *path = g_strdup_printf("%s/%s", tmpfs, filename);

    unlink(path);
    g_free(path);
}

static void migrate_check_parameter(QTestState *who, const char *parameter,
                                    long long value)
{
    QDict *rsp_return;

    rsp_return = wait_command(who,
                              "{ 'execute': 'query-migrate-parameters' }");
    g_assert_cmpint(qdict_get_int(rsp_return, parameter), ==, value);
    qobject_unref(rsp_return);
}

static void migrate_set_parameter(QTestState *who, const char *parameter,
                                  long long value)
{
    QDict *rsp;

    rsp = qtest_qmp(who,
                    "{ 'execute': 'migrate-set-parameters',"
                    "'arguments': { %s: %lld } }",
                    parameter, value);
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);
    migrate_check_parameter(who, parameter, value);
}

static void migrate_pause(QTestState *who)
{
    QDict *rsp;

    rsp = wait_command(who, "{ 'execute': 'migrate-pause' }");
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

/*
 * Send QMP command "migrate".
 * Arguments are built from @fmt... (formatted like
 * qobject_from_jsonf_nofail()) with "uri": @uri spliced in.
 */
GCC_FMT_ATTR(3, 4)
static void migrate(QTestState *who, const char *uri, const char *fmt, ...)
{
    va_list ap;
    QDict *args, *rsp;

    va_start(ap, fmt);
    args = qdict_from_vjsonf_nofail(fmt, ap);
    va_end(ap);

    g_assert(!qdict_haskey(args, "uri"));
    qdict_put_str(args, "uri", uri);

    rsp = qmp("{ 'execute': 'migrate', 'arguments': %p}", args);
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

static int test_migrate_start(QTestState **from, QTestState **to,
                               const char *uri, bool hide_stderr)
{
    gchar *cmd_src, *cmd_dst;
    char *bootpath = g_strdup_printf("%s/bootsect", tmpfs);
    const char *arch = qtest_get_arch();
    const char *accel = "kvm:tcg";

    got_stop = false;

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        init_bootfile(bootpath, x86_bootsect);
        cmd_src = g_strdup_printf("-machine accel=%s -m 150M"
                                  " -name source,debug-threads=on"
                                  " -serial file:%s/src_serial"
                                  " -drive file=%s,format=raw",
                                  accel, tmpfs, bootpath);
        cmd_dst = g_strdup_printf("-machine accel=%s -m 150M"
                                  " -name target,debug-threads=on"
                                  " -serial file:%s/dest_serial"
                                  " -drive file=%s,format=raw"
                                  " -incoming %s",
                                  accel, tmpfs, bootpath, uri);
        start_address = X86_TEST_MEM_START;
        end_address = X86_TEST_MEM_END;
    } else if (g_str_equal(arch, "s390x")) {
        init_bootfile_s390x(bootpath);
        cmd_src = g_strdup_printf("-machine accel=%s -m 128M"
                                  " -name source,debug-threads=on"
                                  " -serial file:%s/src_serial -bios %s",
                                  accel, tmpfs, bootpath);
        cmd_dst = g_strdup_printf("-machine accel=%s -m 128M"
                                  " -name target,debug-threads=on"
                                  " -serial file:%s/dest_serial -bios %s"
                                  " -incoming %s",
                                  accel, tmpfs, bootpath, uri);
        start_address = S390_TEST_MEM_START;
        end_address = S390_TEST_MEM_END;
    } else if (strcmp(arch, "ppc64") == 0) {
        cmd_src = g_strdup_printf("-machine accel=%s -m 256M -nodefaults"
                                  " -name source,debug-threads=on"
                                  " -serial file:%s/src_serial"
                                  " -prom-env 'use-nvramrc?=true' -prom-env "
                                  "'nvramrc=hex .\" _\" begin %x %x "
                                  "do i c@ 1 + i c! 1000 +loop .\" B\" 0 "
                                  "until'",  accel, tmpfs, end_address,
                                  start_address);
        cmd_dst = g_strdup_printf("-machine accel=%s -m 256M"
                                  " -name target,debug-threads=on"
                                  " -serial file:%s/dest_serial"
                                  " -incoming %s",
                                  accel, tmpfs, uri);

        start_address = PPC_TEST_MEM_START;
        end_address = PPC_TEST_MEM_END;
    } else if (strcmp(arch, "aarch64") == 0) {
        init_bootfile(bootpath, aarch64_kernel);
        cmd_src = g_strdup_printf("-machine virt,accel=%s,gic-version=max "
                                  "-name vmsource,debug-threads=on -cpu max "
                                  "-m 150M -serial file:%s/src_serial "
                                  "-kernel %s ",
                                  accel, tmpfs, bootpath);
        cmd_dst = g_strdup_printf("-machine virt,accel=%s,gic-version=max "
                                  "-name vmdest,debug-threads=on -cpu max "
                                  "-m 150M -serial file:%s/dest_serial "
                                  "-kernel %s "
                                  "-incoming %s ",
                                  accel, tmpfs, bootpath, uri);

        start_address = ARM_TEST_MEM_START;
        end_address = ARM_TEST_MEM_END;

        g_assert(sizeof(aarch64_kernel) <= ARM_TEST_MAX_KERNEL_SIZE);
    } else {
        g_assert_not_reached();
    }

    g_free(bootpath);

    if (hide_stderr) {
        gchar *tmp;
        tmp = g_strdup_printf("%s 2>/dev/null", cmd_src);
        g_free(cmd_src);
        cmd_src = tmp;

        tmp = g_strdup_printf("%s 2>/dev/null", cmd_dst);
        g_free(cmd_dst);
        cmd_dst = tmp;
    }

    *from = qtest_start(cmd_src);
    g_free(cmd_src);

    *to = qtest_init(cmd_dst);
    g_free(cmd_dst);
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
    migrate_check_parameter(who, "downtime-limit", value * 1000);
}

static void deprecated_set_speed(QTestState *who, long long value)
{
    QDict *rsp;

    rsp = qtest_qmp(who, "{ 'execute': 'migrate_set_speed',"
                          "'arguments': { 'value': %lld } }", value);
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);
    migrate_check_parameter(who, "max-bandwidth", value);
}

static void test_deprecated(void)
{
    QTestState *from;

    from = qtest_start("-machine none");

    deprecated_set_downtime(from, 0.12345);
    deprecated_set_speed(from, 12345);

    qtest_quit(from);
}

static int migrate_postcopy_prepare(QTestState **from_ptr,
                                     QTestState **to_ptr,
                                     bool hide_error)
{
    char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    QTestState *from, *to;

    if (test_migrate_start(&from, &to, uri, hide_error)) {
        return -1;
    }

    migrate_set_capability(from, "postcopy-ram", true);
    migrate_set_capability(to, "postcopy-ram", true);
    migrate_set_capability(to, "postcopy-blocktime", true);

    /* We want to pick a speed slow enough that the test completes
     * quickly, but that it doesn't complete precopy even on a slow
     * machine, so also set the downtime.
     */
    migrate_set_parameter(from, "max-bandwidth", 100000000);
    migrate_set_parameter(from, "downtime-limit", 1);

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    migrate(from, uri, "{}");
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
    QTestState *from, *to;

    if (migrate_postcopy_prepare(&from, &to, false)) {
        return;
    }
    migrate_postcopy_start(from, to);
    migrate_postcopy_complete(from, to);
}

static void test_postcopy_recovery(void)
{
    QTestState *from, *to;
    char *uri;

    if (migrate_postcopy_prepare(&from, &to, true)) {
        return;
    }

    /* Turn postcopy speed down, 4K/s is slow enough on any machines */
    migrate_set_parameter(from, "max-postcopy-bandwidth", 4096);

    /* Now we start the postcopy */
    migrate_postcopy_start(from, to);

    /*
     * Wait until postcopy is really started; we can only run the
     * migrate-pause command during a postcopy
     */
    wait_for_migration_status(from, "postcopy-active");

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
    wait_for_migration_status(to, "postcopy-paused");

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
    wait_for_migration_status(from, "postcopy-paused");
    migrate(from, uri, "{'resume': true}");
    g_free(uri);

    /* Restore the postcopy bandwidth to unlimited */
    migrate_set_parameter(from, "max-postcopy-bandwidth", 0);

    migrate_postcopy_complete(from, to);
}

static void test_baddest(void)
{
    QTestState *from, *to;
    QDict *rsp_return;
    char *status;
    bool failed;

    if (test_migrate_start(&from, &to, "tcp:0:0", true)) {
        return;
    }
    migrate(from, "tcp:0:0", "{}");
    do {
        status = migrate_query_status(from);
        g_assert(!strcmp(status, "setup") || !(strcmp(status, "failed")));
        failed = !strcmp(status, "failed");
        g_free(status);
    } while (!failed);

    /* Is the machine currently running? */
    rsp_return = wait_command(from, "{ 'execute': 'query-status' }");
    g_assert(qdict_haskey(rsp_return, "running"));
    g_assert(qdict_get_bool(rsp_return, "running"));
    qobject_unref(rsp_return);

    test_migrate_end(from, to, false);
}

static void test_precopy_unix(void)
{
    char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    QTestState *from, *to;

    if (test_migrate_start(&from, &to, uri, false)) {
        return;
    }

    /* We want to pick a speed slow enough that the test completes
     * quickly, but that it doesn't complete precopy even on a slow
     * machine, so also set the downtime.
     */
    /* 1 ms should make it not converge*/
    migrate_set_parameter(from, "downtime-limit", 1);
    /* 1GB/s */
    migrate_set_parameter(from, "max-bandwidth", 1000000000);

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    migrate(from, uri, "{}");

    wait_for_migration_pass(from);

    /* 300 ms should converge */
    migrate_set_parameter(from, "downtime-limit", 300);

    if (!got_stop) {
        qtest_qmp_eventwait(from, "STOP");
    }

    qtest_qmp_eventwait(to, "RESUME");

    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);

    test_migrate_end(from, to, true);
    g_free(uri);
}

int main(int argc, char **argv)
{
    char template[] = "/tmp/migration-test-XXXXXX";
    int ret;

    g_test_init(&argc, &argv, NULL);

    if (!ufd_version_check()) {
        return 0;
    }

    /*
     * On ppc64, the test only works with kvm-hv, but not with kvm-pr and TCG
     * is touchy due to race conditions on dirty bits (especially on PPC for
     * some reason)
     */
    if (g_str_equal(qtest_get_arch(), "ppc64") &&
        access("/sys/module/kvm_hv", F_OK)) {
        g_test_message("Skipping test: kvm_hv not available");
        return 0;
    }

    /*
     * Similar to ppc64, s390x seems to be touchy with TCG, so disable it
     * there until the problems are resolved
     */
    if (g_str_equal(qtest_get_arch(), "s390x")) {
#if defined(HOST_S390X)
        if (access("/dev/kvm", R_OK | W_OK)) {
            g_test_message("Skipping test: kvm not available");
            return 0;
        }
#else
        g_test_message("Skipping test: Need s390x host to work properly");
        return 0;
#endif
    }

    tmpfs = mkdtemp(template);
    if (!tmpfs) {
        g_test_message("mkdtemp on path (%s): %s\n", template, strerror(errno));
    }
    g_assert(tmpfs);

    module_call_init(MODULE_INIT_QOM);

    qtest_add_func("/migration/postcopy/unix", test_postcopy);
    qtest_add_func("/migration/postcopy/recovery", test_postcopy_recovery);
    qtest_add_func("/migration/deprecated", test_deprecated);
    qtest_add_func("/migration/bad_dest", test_baddest);
    qtest_add_func("/migration/precopy/unix", test_precopy_unix);

    ret = g_test_run();

    g_assert_cmpint(ret, ==, 0);

    ret = rmdir(tmpfs);
    if (ret != 0) {
        g_test_message("unable to rmdir: path (%s): %s\n",
                       tmpfs, strerror(errno));
    }

    return ret;
}
