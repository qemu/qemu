/*
 * QTest testcase for dump-guest-memory
 *
 * Generic coverage for the dump-guest-memory QMP command and the
 * query-dump-guest-memory-capability reporting, exercised on a bare
 * machine (no guest OS required).
 *
 * Copyright (c) 2026 Virtuozzo International GmbH
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "qobject/qstring.h"
#include "qemu/bswap.h"
#include "elf.h"

#define KDUMP_RAW_MAGIC     "KDUMP   "
#define KDUMP_FLAT_MAGIC    "makedumpfile"

static QTestState *dump_test_start(void)
{
    return qtest_initf("-machine q35 -accel qtest -m 16");
}

static void assert_file_magic(const char *path, const char *magic, size_t len)
{
    g_autofree char *buf = g_malloc0(len);
    FILE *f = fopen(path, "rb");

    g_assert_nonnull(f);
    g_assert_cmpint(fread(buf, 1, len, f), ==, len);
    fclose(f);
    g_assert_cmpint(memcmp(buf, magic, len), ==, 0);
}

/* validate that the file is a sane x86 ELF core, not just the leading magic */
static void assert_valid_elf_core(const char *path)
{
    unsigned char e[64];
    FILE *f = fopen(path, "rb");
    uint16_t e_type, e_machine, e_phnum;

    g_assert_nonnull(f);
    g_assert_cmpint(fread(e, 1, sizeof(e), f), ==, sizeof(e));
    fclose(f);

    g_assert_cmpint(memcmp(e, ELFMAG, SELFMAG), ==, 0);

    /* e_type and e_machine sit at the same offset for ELF32 and ELF64 */
    e_type = lduw_le_p(e + 16);
    e_machine = lduw_le_p(e + 18);
    g_assert_cmpint(e_type, ==, ET_CORE);
    g_assert(e_machine == EM_386 || e_machine == EM_X86_64);

    /* e_phnum lives at a class-dependent offset */
    if (e[EI_CLASS] == ELFCLASS64) {
        e_phnum = lduw_le_p(e + 56);
    } else {
        e_phnum = lduw_le_p(e + 44);
    }
    g_assert_cmpint(e_phnum, >, 0);
}

/* dump-guest-memory to a fresh temp file; returns the path (caller frees) */
static char *do_dump(QTestState *qts, const char *format)
{
    g_autofree char *tmp = NULL;
    g_autofree char *proto = NULL;
    GError *err = NULL;
    int fd;

    fd = g_file_open_tmp("dump-test-XXXXXX", &tmp, &err);
    g_assert_no_error(err);
    close(fd);
    proto = g_strdup_printf("file:%s", tmp);

    if (format) {
        qtest_qmp_assert_success(qts,
            "{ 'execute': 'dump-guest-memory',"
            "  'arguments': { 'paging': false, 'protocol': %s,"
            "                 'format': %s } }", proto, format);
    } else {
        qtest_qmp_assert_success(qts,
            "{ 'execute': 'dump-guest-memory',"
            "  'arguments': { 'paging': false, 'protocol': %s } }", proto);
    }

    return g_steal_pointer(&tmp);
}

/* query-dump-guest-memory-capability must always advertise at least 'elf' */
static void test_query_capability(void)
{
    QTestState *qts = dump_test_start();
    QDict *resp, *ret;
    QList *formats;
    QListEntry *e;
    bool has_elf = false;

    resp = qtest_qmp(qts,
        "{ 'execute': 'query-dump-guest-memory-capability' }");
    g_assert(qdict_haskey(resp, "return"));
    ret = qdict_get_qdict(resp, "return");
    formats = qdict_get_qlist(ret, "formats");
    g_assert_nonnull(formats);

    QLIST_FOREACH_ENTRY(formats, e) {
        QString *qs = qobject_to(QString, qlist_entry_obj(e));

        if (g_str_equal(qstring_get_str(qs), "elf")) {
            has_elf = true;
        }
    }
    g_assert_true(has_elf);

    qobject_unref(resp);
    qtest_quit(qts);
}

static void test_dump_elf(void)
{
    QTestState *qts = dump_test_start();
    g_autofree char *path = do_dump(qts, NULL);

    assert_valid_elf_core(path);
    unlink(path);
    qtest_quit(qts);
}

/* non-raw kdump is emitted in makedumpfile flattened format */
static void test_dump_kdump_zlib(void)
{
    QTestState *qts = dump_test_start();
    g_autofree char *path = do_dump(qts, "kdump-zlib");

    assert_file_magic(path, KDUMP_FLAT_MAGIC, strlen(KDUMP_FLAT_MAGIC));
    unlink(path);
    qtest_quit(qts);
}

/* raw kdump starts with the on-disk KDUMP header */
static void test_dump_kdump_raw_zlib(void)
{
    QTestState *qts = dump_test_start();
    g_autofree char *path = do_dump(qts, "kdump-raw-zlib");

    assert_file_magic(path, KDUMP_RAW_MAGIC, strlen(KDUMP_RAW_MAGIC));
    unlink(path);
    qtest_quit(qts);
}

/* an unknown protocol must be rejected, not crash the VM */
static void test_dump_invalid_protocol(void)
{
    QTestState *qts = dump_test_start();
    g_autofree char *path = NULL;
    QDict *resp;

    resp = qtest_qmp(qts,
        "{ 'execute': 'dump-guest-memory',"
        "  'arguments': { 'paging': false, 'protocol': 'bogus:/x' } }");
    g_assert(qdict_haskey(resp, "error"));
    qobject_unref(resp);

    /* VM is still alive and dumping still works afterwards */
    path = do_dump(qts, NULL);
    assert_valid_elf_core(path);
    unlink(path);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/dump/query-capability", test_query_capability);
    qtest_add_func("/dump/elf", test_dump_elf);
    qtest_add_func("/dump/kdump-zlib", test_dump_kdump_zlib);
    qtest_add_func("/dump/kdump-raw-zlib", test_dump_kdump_raw_zlib);
    qtest_add_func("/dump/invalid-protocol", test_dump_invalid_protocol);

    return g_test_run();
}
