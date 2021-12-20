/*
 * QTest testcase for ISA endianness
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "libqos/libqtest.h"
#include "qemu/bswap.h"

typedef struct TestCase TestCase;
struct TestCase {
    const char *arch;
    const char *machine;
    uint64_t isa_base;
    bool bswap;
    const char *superio;
};

static const TestCase test_cases[] = {
    { "i386", "pc", -1 },
    { "mips", "malta", 0x10000000, .bswap = true },
    { "mips64", "magnum", 0x90000000, .bswap = true },
    { "mips64", "pica61", 0x90000000, .bswap = true },
    { "mips64", "malta", 0x10000000, .bswap = true },
    { "mips64el", "fuloong2e", 0x1fd00000 },
    { "ppc", "g3beige", 0xfe000000, .bswap = true, .superio = "i82378" },
    { "ppc", "40p", 0x80000000, .bswap = true },
    { "ppc", "bamboo", 0xe8000000, .bswap = true, .superio = "i82378" },
    { "ppc64", "mac99", 0xf2000000, .bswap = true, .superio = "i82378" },
    { "ppc64", "pseries", (1ULL << 45), .bswap = true, .superio = "i82378" },
    { "ppc64", "pseries-2.7", 0x10080000000ULL,
      .bswap = true, .superio = "i82378" },
    { "sh4", "r2d", 0xfe240000, .superio = "i82378" },
    { "sh4eb", "r2d", 0xfe240000, .bswap = true, .superio = "i82378" },
    { "sparc64", "sun4u", 0x1fe02000000LL, .bswap = true },
    { "x86_64", "pc", -1 },
    {}
};

static uint8_t isa_inb(QTestState *qts, const TestCase *test, uint16_t addr)
{
    uint8_t value;
    if (test->isa_base == -1) {
        value = qtest_inb(qts, addr);
    } else {
        value = qtest_readb(qts, test->isa_base + addr);
    }
    return value;
}

static uint16_t isa_inw(QTestState *qts, const TestCase *test, uint16_t addr)
{
    uint16_t value;
    if (test->isa_base == -1) {
        value = qtest_inw(qts, addr);
    } else {
        value = qtest_readw(qts, test->isa_base + addr);
    }
    return test->bswap ? bswap16(value) : value;
}

static uint32_t isa_inl(QTestState *qts, const TestCase *test, uint16_t addr)
{
    uint32_t value;
    if (test->isa_base == -1) {
        value = qtest_inl(qts, addr);
    } else {
        value = qtest_readl(qts, test->isa_base + addr);
    }
    return test->bswap ? bswap32(value) : value;
}

static void isa_outb(QTestState *qts, const TestCase *test, uint16_t addr,
                     uint8_t value)
{
    if (test->isa_base == -1) {
        qtest_outb(qts, addr, value);
    } else {
        qtest_writeb(qts, test->isa_base + addr, value);
    }
}

static void isa_outw(QTestState *qts, const TestCase *test, uint16_t addr,
                     uint16_t value)
{
    value = test->bswap ? bswap16(value) : value;
    if (test->isa_base == -1) {
        qtest_outw(qts, addr, value);
    } else {
        qtest_writew(qts, test->isa_base + addr, value);
    }
}

static void isa_outl(QTestState *qts, const TestCase *test, uint16_t addr,
                     uint32_t value)
{
    value = test->bswap ? bswap32(value) : value;
    if (test->isa_base == -1) {
        qtest_outl(qts, addr, value);
    } else {
        qtest_writel(qts, test->isa_base + addr, value);
    }
}


static void test_endianness(gconstpointer data)
{
    const TestCase *test = data;
    QTestState *qts;

    qts = qtest_initf("-M %s%s%s -device pc-testdev", test->machine,
                      test->superio ? " -device " : "",
                      test->superio ?: "");
    isa_outl(qts, test, 0xe0, 0x87654321);
    g_assert_cmphex(isa_inl(qts, test, 0xe0), ==, 0x87654321);
    g_assert_cmphex(isa_inw(qts, test, 0xe2), ==, 0x8765);
    g_assert_cmphex(isa_inw(qts, test, 0xe0), ==, 0x4321);
    g_assert_cmphex(isa_inb(qts, test, 0xe3), ==, 0x87);
    g_assert_cmphex(isa_inb(qts, test, 0xe2), ==, 0x65);
    g_assert_cmphex(isa_inb(qts, test, 0xe1), ==, 0x43);
    g_assert_cmphex(isa_inb(qts, test, 0xe0), ==, 0x21);

    isa_outw(qts, test, 0xe2, 0x8866);
    g_assert_cmphex(isa_inl(qts, test, 0xe0), ==, 0x88664321);
    g_assert_cmphex(isa_inw(qts, test, 0xe2), ==, 0x8866);
    g_assert_cmphex(isa_inw(qts, test, 0xe0), ==, 0x4321);
    g_assert_cmphex(isa_inb(qts, test, 0xe3), ==, 0x88);
    g_assert_cmphex(isa_inb(qts, test, 0xe2), ==, 0x66);
    g_assert_cmphex(isa_inb(qts, test, 0xe1), ==, 0x43);
    g_assert_cmphex(isa_inb(qts, test, 0xe0), ==, 0x21);

    isa_outw(qts, test, 0xe0, 0x4422);
    g_assert_cmphex(isa_inl(qts, test, 0xe0), ==, 0x88664422);
    g_assert_cmphex(isa_inw(qts, test, 0xe2), ==, 0x8866);
    g_assert_cmphex(isa_inw(qts, test, 0xe0), ==, 0x4422);
    g_assert_cmphex(isa_inb(qts, test, 0xe3), ==, 0x88);
    g_assert_cmphex(isa_inb(qts, test, 0xe2), ==, 0x66);
    g_assert_cmphex(isa_inb(qts, test, 0xe1), ==, 0x44);
    g_assert_cmphex(isa_inb(qts, test, 0xe0), ==, 0x22);

    isa_outb(qts, test, 0xe3, 0x87);
    g_assert_cmphex(isa_inl(qts, test, 0xe0), ==, 0x87664422);
    g_assert_cmphex(isa_inw(qts, test, 0xe2), ==, 0x8766);
    g_assert_cmphex(isa_inb(qts, test, 0xe3), ==, 0x87);
    g_assert_cmphex(isa_inb(qts, test, 0xe2), ==, 0x66);
    g_assert_cmphex(isa_inb(qts, test, 0xe1), ==, 0x44);
    g_assert_cmphex(isa_inb(qts, test, 0xe0), ==, 0x22);

    isa_outb(qts, test, 0xe2, 0x65);
    g_assert_cmphex(isa_inl(qts, test, 0xe0), ==, 0x87654422);
    g_assert_cmphex(isa_inw(qts, test, 0xe2), ==, 0x8765);
    g_assert_cmphex(isa_inw(qts, test, 0xe0), ==, 0x4422);
    g_assert_cmphex(isa_inb(qts, test, 0xe3), ==, 0x87);
    g_assert_cmphex(isa_inb(qts, test, 0xe2), ==, 0x65);
    g_assert_cmphex(isa_inb(qts, test, 0xe1), ==, 0x44);
    g_assert_cmphex(isa_inb(qts, test, 0xe0), ==, 0x22);

    isa_outb(qts, test, 0xe1, 0x43);
    g_assert_cmphex(isa_inl(qts, test, 0xe0), ==, 0x87654322);
    g_assert_cmphex(isa_inw(qts, test, 0xe2), ==, 0x8765);
    g_assert_cmphex(isa_inw(qts, test, 0xe0), ==, 0x4322);
    g_assert_cmphex(isa_inb(qts, test, 0xe3), ==, 0x87);
    g_assert_cmphex(isa_inb(qts, test, 0xe2), ==, 0x65);
    g_assert_cmphex(isa_inb(qts, test, 0xe1), ==, 0x43);
    g_assert_cmphex(isa_inb(qts, test, 0xe0), ==, 0x22);

    isa_outb(qts, test, 0xe0, 0x21);
    g_assert_cmphex(isa_inl(qts, test, 0xe0), ==, 0x87654321);
    g_assert_cmphex(isa_inw(qts, test, 0xe2), ==, 0x8765);
    g_assert_cmphex(isa_inw(qts, test, 0xe0), ==, 0x4321);
    g_assert_cmphex(isa_inb(qts, test, 0xe3), ==, 0x87);
    g_assert_cmphex(isa_inb(qts, test, 0xe2), ==, 0x65);
    g_assert_cmphex(isa_inb(qts, test, 0xe1), ==, 0x43);
    g_assert_cmphex(isa_inb(qts, test, 0xe0), ==, 0x21);
    qtest_quit(qts);
}

static void test_endianness_split(gconstpointer data)
{
    const TestCase *test = data;
    QTestState *qts;

    qts = qtest_initf("-M %s%s%s -device pc-testdev", test->machine,
                      test->superio ? " -device " : "",
                      test->superio ?: "");
    isa_outl(qts, test, 0xe8, 0x87654321);
    g_assert_cmphex(isa_inl(qts, test, 0xe0), ==, 0x87654321);
    g_assert_cmphex(isa_inw(qts, test, 0xe2), ==, 0x8765);
    g_assert_cmphex(isa_inw(qts, test, 0xe0), ==, 0x4321);

    isa_outw(qts, test, 0xea, 0x8866);
    g_assert_cmphex(isa_inl(qts, test, 0xe0), ==, 0x88664321);
    g_assert_cmphex(isa_inw(qts, test, 0xe2), ==, 0x8866);
    g_assert_cmphex(isa_inw(qts, test, 0xe0), ==, 0x4321);

    isa_outw(qts, test, 0xe8, 0x4422);
    g_assert_cmphex(isa_inl(qts, test, 0xe0), ==, 0x88664422);
    g_assert_cmphex(isa_inw(qts, test, 0xe2), ==, 0x8866);
    g_assert_cmphex(isa_inw(qts, test, 0xe0), ==, 0x4422);

    isa_outb(qts, test, 0xeb, 0x87);
    g_assert_cmphex(isa_inl(qts, test, 0xe0), ==, 0x87664422);
    g_assert_cmphex(isa_inw(qts, test, 0xe2), ==, 0x8766);

    isa_outb(qts, test, 0xea, 0x65);
    g_assert_cmphex(isa_inl(qts, test, 0xe0), ==, 0x87654422);
    g_assert_cmphex(isa_inw(qts, test, 0xe2), ==, 0x8765);
    g_assert_cmphex(isa_inw(qts, test, 0xe0), ==, 0x4422);

    isa_outb(qts, test, 0xe9, 0x43);
    g_assert_cmphex(isa_inl(qts, test, 0xe0), ==, 0x87654322);
    g_assert_cmphex(isa_inw(qts, test, 0xe2), ==, 0x8765);
    g_assert_cmphex(isa_inw(qts, test, 0xe0), ==, 0x4322);

    isa_outb(qts, test, 0xe8, 0x21);
    g_assert_cmphex(isa_inl(qts, test, 0xe0), ==, 0x87654321);
    g_assert_cmphex(isa_inw(qts, test, 0xe2), ==, 0x8765);
    g_assert_cmphex(isa_inw(qts, test, 0xe0), ==, 0x4321);
    qtest_quit(qts);
}

static void test_endianness_combine(gconstpointer data)
{
    const TestCase *test = data;
    QTestState *qts;

    qts = qtest_initf("-M %s%s%s -device pc-testdev", test->machine,
                      test->superio ? " -device " : "",
                      test->superio ?: "");
    isa_outl(qts, test, 0xe0, 0x87654321);
    g_assert_cmphex(isa_inl(qts, test, 0xe8), ==, 0x87654321);
    g_assert_cmphex(isa_inw(qts, test, 0xea), ==, 0x8765);
    g_assert_cmphex(isa_inw(qts, test, 0xe8), ==, 0x4321);

    isa_outw(qts, test, 0xe2, 0x8866);
    g_assert_cmphex(isa_inl(qts, test, 0xe8), ==, 0x88664321);
    g_assert_cmphex(isa_inw(qts, test, 0xea), ==, 0x8866);
    g_assert_cmphex(isa_inw(qts, test, 0xe8), ==, 0x4321);

    isa_outw(qts, test, 0xe0, 0x4422);
    g_assert_cmphex(isa_inl(qts, test, 0xe8), ==, 0x88664422);
    g_assert_cmphex(isa_inw(qts, test, 0xea), ==, 0x8866);
    g_assert_cmphex(isa_inw(qts, test, 0xe8), ==, 0x4422);

    isa_outb(qts, test, 0xe3, 0x87);
    g_assert_cmphex(isa_inl(qts, test, 0xe8), ==, 0x87664422);
    g_assert_cmphex(isa_inw(qts, test, 0xea), ==, 0x8766);

    isa_outb(qts, test, 0xe2, 0x65);
    g_assert_cmphex(isa_inl(qts, test, 0xe8), ==, 0x87654422);
    g_assert_cmphex(isa_inw(qts, test, 0xea), ==, 0x8765);
    g_assert_cmphex(isa_inw(qts, test, 0xe8), ==, 0x4422);

    isa_outb(qts, test, 0xe1, 0x43);
    g_assert_cmphex(isa_inl(qts, test, 0xe8), ==, 0x87654322);
    g_assert_cmphex(isa_inw(qts, test, 0xea), ==, 0x8765);
    g_assert_cmphex(isa_inw(qts, test, 0xe8), ==, 0x4322);

    isa_outb(qts, test, 0xe0, 0x21);
    g_assert_cmphex(isa_inl(qts, test, 0xe8), ==, 0x87654321);
    g_assert_cmphex(isa_inw(qts, test, 0xea), ==, 0x8765);
    g_assert_cmphex(isa_inw(qts, test, 0xe8), ==, 0x4321);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();
    int i;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; test_cases[i].arch; i++) {
        gchar *path;

        if (!g_str_equal(test_cases[i].arch, arch) ||
            !qtest_has_machine(test_cases[i].machine) ||
            (test_cases[i].superio && !qtest_has_device(test_cases[i].superio))) {
            continue;
        }
        path = g_strdup_printf("endianness/%s",
                               test_cases[i].machine);
        qtest_add_data_func(path, &test_cases[i], test_endianness);
        g_free(path);

        path = g_strdup_printf("endianness/split/%s",
                               test_cases[i].machine);
        qtest_add_data_func(path, &test_cases[i], test_endianness_split);
        g_free(path);

        path = g_strdup_printf("endianness/combine/%s",
                               test_cases[i].machine);
        qtest_add_data_func(path, &test_cases[i], test_endianness_combine);
        g_free(path);
    }

    return g_test_run();
}
