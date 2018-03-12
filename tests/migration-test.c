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
#include "qemu/option.h"
#include "qemu/range.h"
#include "qemu/sockets.h"
#include "chardev/char.h"
#include "sysemu/sysemu.h"
#include "hw/nvram/chrp_nvram.h"

#define MIN_NVRAM_SIZE 8192 /* from spapr_nvram.c */

const unsigned start_address = 1024 * 1024;
const unsigned end_address = 100 * 1024 * 1024;
bool got_stop;

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

/* A simple PC boot sector that modifies memory (1-100MB) quickly
 * outputting a 'B' every so often if it's still running.
 */
#include "tests/migration/x86-a-b-bootblock.h"

static void init_bootfile_x86(const char *bootpath)
{
    FILE *bootfile = fopen(bootpath, "wb");

    g_assert_cmpint(fwrite(x86_bootsect, 512, 1, bootfile), ==, 1);
    fclose(bootfile);
}

static void init_bootfile_ppc(const char *bootpath)
{
    FILE *bootfile;
    char buf[MIN_NVRAM_SIZE];
    ChrpNvramPartHdr *header = (ChrpNvramPartHdr *)buf;

    memset(buf, 0, MIN_NVRAM_SIZE);

    /* Create a "common" partition in nvram to store boot-command property */

    header->signature = CHRP_NVPART_SYSTEM;
    memcpy(header->name, "common", 6);
    chrp_nvram_finish_partition(header, MIN_NVRAM_SIZE);

    /* FW_MAX_SIZE is 4MB, but slof.bin is only 900KB,
     * so let's modify memory between 1MB and 100MB
     * to do like PC bootsector
     */

    sprintf(buf + 16,
            "boot-command=hex .\" _\" begin %x %x do i c@ 1 + i c! 1000 +loop "
            ".\" B\" 0 until", end_address, start_address);

    /* Write partition to the NVRAM file */

    bootfile = fopen(bootpath, "wb");
    g_assert_cmpint(fwrite(buf, MIN_NVRAM_SIZE, 1, bootfile), ==, 1);
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
 * Events can get in the way of responses we are actually waiting for.
 */
static QDict *wait_command(QTestState *who, const char *command)
{
    const char *event_string;
    QDict *response;

    response = qtest_qmp(who, command);

    while (qdict_haskey(response, "event")) {
        /* OK, it was an event */
        event_string = qdict_get_str(response, "event");
        if (!strcmp(event_string, "STOP")) {
            got_stop = true;
        }
        QDECREF(response);
        response = qtest_qmp_receive(who);
    }
    return response;
}


/*
 * It's tricky to use qemu's migration event capability with qtest,
 * events suddenly appearing confuse the qmp()/hmp() responses.
 */

static uint64_t get_migration_pass(QTestState *who)
{
    QDict *rsp, *rsp_return, *rsp_ram;
    uint64_t result;

    rsp = wait_command(who, "{ 'execute': 'query-migrate' }");
    rsp_return = qdict_get_qdict(rsp, "return");
    if (!qdict_haskey(rsp_return, "ram")) {
        /* Still in setup */
        result = 0;
    } else {
        rsp_ram = qdict_get_qdict(rsp_return, "ram");
        result = qdict_get_try_int(rsp_ram, "dirty-sync-count", 0);
    }
    QDECREF(rsp);
    return result;
}

static void wait_for_migration_complete(QTestState *who)
{
    while (true) {
        QDict *rsp, *rsp_return;
        bool completed;
        const char *status;

        rsp = wait_command(who, "{ 'execute': 'query-migrate' }");
        rsp_return = qdict_get_qdict(rsp, "return");
        status = qdict_get_str(rsp_return, "status");
        completed = strcmp(status, "completed") == 0;
        g_assert_cmpstr(status, !=,  "failed");
        QDECREF(rsp);
        if (completed) {
            return;
        }
        usleep(1000);
    }
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
     * 1MB to <100MB in order.
     * This gives us a constraint that any page's byte should be equal or less
     * than the previous pages byte (mod 256); and they should all be equal
     * except for one transition at the point where we meet the incrementer.
     * (We're running this with the guest stopped).
     */
    unsigned address;
    uint8_t first_byte;
    uint8_t last_byte;
    bool hit_edge = false;
    bool bad = false;

    qtest_memread(who, start_address, &first_byte, 1);
    last_byte = first_byte;

    for (address = start_address + 4096; address < end_address; address += 4096)
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
            } else {
                fprintf(stderr, "Memory content inconsistency at %x"
                                " first_byte = %x last_byte = %x current = %x"
                                " hit_edge = %x\n",
                                address, first_byte, last_byte, b, hit_edge);
                bad = true;
            }
        }
        last_byte = b;
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
                                    const char *value)
{
    QDict *rsp, *rsp_return;
    char *result;

    rsp = wait_command(who, "{ 'execute': 'query-migrate-parameters' }");
    rsp_return = qdict_get_qdict(rsp, "return");
    result = g_strdup_printf("%" PRId64,
                             qdict_get_try_int(rsp_return,  parameter, -1));
    g_assert_cmpstr(result, ==, value);
    g_free(result);
    QDECREF(rsp);
}

static void migrate_set_parameter(QTestState *who, const char *parameter,
                                  const char *value)
{
    QDict *rsp;
    gchar *cmd;

    cmd = g_strdup_printf("{ 'execute': 'migrate-set-parameters',"
                          "'arguments': { '%s': %s } }",
                          parameter, value);
    rsp = qtest_qmp(who, cmd);
    g_free(cmd);
    g_assert(qdict_haskey(rsp, "return"));
    QDECREF(rsp);
    migrate_check_parameter(who, parameter, value);
}

static void migrate_set_capability(QTestState *who, const char *capability,
                                   const char *value)
{
    QDict *rsp;
    gchar *cmd;

    cmd = g_strdup_printf("{ 'execute': 'migrate-set-capabilities',"
                          "'arguments': { "
                          "'capabilities': [ { "
                          "'capability': '%s', 'state': %s } ] } }",
                          capability, value);
    rsp = qtest_qmp(who, cmd);
    g_free(cmd);
    g_assert(qdict_haskey(rsp, "return"));
    QDECREF(rsp);
}

static void migrate(QTestState *who, const char *uri)
{
    QDict *rsp;
    gchar *cmd;

    cmd = g_strdup_printf("{ 'execute': 'migrate',"
                          "'arguments': { 'uri': '%s' } }",
                          uri);
    rsp = qtest_qmp(who, cmd);
    g_free(cmd);
    g_assert(qdict_haskey(rsp, "return"));
    QDECREF(rsp);
}

static void migrate_start_postcopy(QTestState *who)
{
    QDict *rsp;

    rsp = wait_command(who, "{ 'execute': 'migrate-start-postcopy' }");
    g_assert(qdict_haskey(rsp, "return"));
    QDECREF(rsp);
}

static void test_migrate_start(QTestState **from, QTestState **to,
                               const char *uri, bool hide_stderr)
{
    gchar *cmd_src, *cmd_dst;
    char *bootpath = g_strdup_printf("%s/bootsect", tmpfs);
    const char *arch = qtest_get_arch();
    const char *accel = "kvm:tcg";

    got_stop = false;

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        init_bootfile_x86(bootpath);
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
    } else if (strcmp(arch, "ppc64") == 0) {

        /* On ppc64, the test only works with kvm-hv, but not with kvm-pr */
        if (access("/sys/module/kvm_hv", F_OK)) {
            accel = "tcg";
        }
        init_bootfile_ppc(bootpath);
        cmd_src = g_strdup_printf("-machine accel=%s -m 256M"
                                  " -name source,debug-threads=on"
                                  " -serial file:%s/src_serial"
                                  " -drive file=%s,if=pflash,format=raw",
                                  accel, tmpfs, bootpath);
        cmd_dst = g_strdup_printf("-machine accel=%s -m 256M"
                                  " -name target,debug-threads=on"
                                  " -serial file:%s/dest_serial"
                                  " -incoming %s",
                                  accel, tmpfs, uri);
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
    gchar *cmd;
    char *expected;
    int64_t result_int;

    cmd = g_strdup_printf("{ 'execute': 'migrate_set_downtime',"
                          "'arguments': { 'value': %g } }", value);
    rsp = qtest_qmp(who, cmd);
    g_free(cmd);
    g_assert(qdict_haskey(rsp, "return"));
    QDECREF(rsp);
    result_int = value * 1000L;
    expected = g_strdup_printf("%" PRId64, result_int);
    migrate_check_parameter(who, "downtime-limit", expected);
    g_free(expected);
}

static void deprecated_set_speed(QTestState *who, const char *value)
{
    QDict *rsp;
    gchar *cmd;

    cmd = g_strdup_printf("{ 'execute': 'migrate_set_speed',"
                          "'arguments': { 'value': %s } }", value);
    rsp = qtest_qmp(who, cmd);
    g_free(cmd);
    g_assert(qdict_haskey(rsp, "return"));
    QDECREF(rsp);
    migrate_check_parameter(who, "max-bandwidth", value);
}

static void test_deprecated(void)
{
    QTestState *from;

    from = qtest_start("");

    deprecated_set_downtime(from, 0.12345);
    deprecated_set_speed(from, "12345");

    qtest_quit(from);
}

static void test_migrate(void)
{
    char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    QTestState *from, *to;

    test_migrate_start(&from, &to, uri, false);

    migrate_set_capability(from, "postcopy-ram", "true");
    migrate_set_capability(to, "postcopy-ram", "true");

    /* We want to pick a speed slow enough that the test completes
     * quickly, but that it doesn't complete precopy even on a slow
     * machine, so also set the downtime.
     */
    migrate_set_parameter(from, "max-bandwidth", "100000000");
    migrate_set_parameter(from, "downtime-limit", "1");

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    migrate(from, uri);

    wait_for_migration_pass(from);

    migrate_start_postcopy(from);

    if (!got_stop) {
        qtest_qmp_eventwait(from, "STOP");
    }

    qtest_qmp_eventwait(to, "RESUME");

    wait_for_serial("dest_serial");
    wait_for_migration_complete(from);

    g_free(uri);

    test_migrate_end(from, to, true);
}

static void test_baddest(void)
{
    QTestState *from, *to;
    QDict *rsp, *rsp_return;
    const char *status;
    bool failed;

    test_migrate_start(&from, &to, "tcp:0:0", true);
    migrate(from, "tcp:0:0");
    do {
        rsp = wait_command(from, "{ 'execute': 'query-migrate' }");
        rsp_return = qdict_get_qdict(rsp, "return");

        status = qdict_get_str(rsp_return, "status");

        g_assert(!strcmp(status, "setup") || !(strcmp(status, "failed")));
        failed = !strcmp(status, "failed");
        QDECREF(rsp);
    } while (!failed);

    /* Is the machine currently running? */
    rsp = wait_command(from, "{ 'execute': 'query-status' }");
    g_assert(qdict_haskey(rsp, "return"));
    rsp_return = qdict_get_qdict(rsp, "return");
    g_assert(qdict_haskey(rsp_return, "running"));
    g_assert(qdict_get_bool(rsp_return, "running"));
    QDECREF(rsp);

    test_migrate_end(from, to, false);
}

int main(int argc, char **argv)
{
    char template[] = "/tmp/migration-test-XXXXXX";
    int ret;

    g_test_init(&argc, &argv, NULL);

    if (!ufd_version_check()) {
        return 0;
    }

    tmpfs = mkdtemp(template);
    if (!tmpfs) {
        g_test_message("mkdtemp on path (%s): %s\n", template, strerror(errno));
    }
    g_assert(tmpfs);

    module_call_init(MODULE_INIT_QOM);

    qtest_add_func("/migration/postcopy/unix", test_migrate);
    qtest_add_func("/migration/deprecated", test_deprecated);
    qtest_add_func("/migration/bad_dest", test_baddest);

    ret = g_test_run();

    g_assert_cmpint(ret, ==, 0);

    ret = rmdir(tmpfs);
    if (ret != 0) {
        g_test_message("unable to rmdir: path (%s): %s\n",
                       tmpfs, strerror(errno));
    }

    return ret;
}
