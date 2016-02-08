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
#include <glib.h>

#include "libqtest.h"
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
    { "mips", "mips", 0x14000000, .bswap = true },
    { "mips", "malta", 0x10000000, .bswap = true },
    { "mips64", "magnum", 0x90000000, .bswap = true },
    { "mips64", "pica61", 0x90000000, .bswap = true },
    { "mips64", "mips", 0x14000000, .bswap = true },
    { "mips64", "malta", 0x10000000, .bswap = true },
    { "mips64el", "fulong2e", 0x1fd00000 },
    { "ppc", "g3beige", 0xfe000000, .bswap = true, .superio = "i82378" },
    { "ppc", "prep", 0x80000000, .bswap = true },
    { "ppc", "bamboo", 0xe8000000, .bswap = true, .superio = "i82378" },
    { "ppc64", "mac99", 0xf2000000, .bswap = true, .superio = "i82378" },
    { "ppc64", "pseries", 0x10080000000ULL,
      .bswap = true, .superio = "i82378" },
    { "sh4", "r2d", 0xfe240000, .superio = "i82378" },
    { "sh4eb", "r2d", 0xfe240000, .bswap = true, .superio = "i82378" },
    { "sparc64", "sun4u", 0x1fe02000000LL, .bswap = true },
    { "x86_64", "pc", -1 },
    {}
};

static uint8_t isa_inb(const TestCase *test, uint16_t addr)
{
    uint8_t value;
    if (test->isa_base == -1) {
        value = inb(addr);
    } else {
        value = readb(test->isa_base + addr);
    }
    return value;
}

static uint16_t isa_inw(const TestCase *test, uint16_t addr)
{
    uint16_t value;
    if (test->isa_base == -1) {
        value = inw(addr);
    } else {
        value = readw(test->isa_base + addr);
    }
    return test->bswap ? bswap16(value) : value;
}

static uint32_t isa_inl(const TestCase *test, uint16_t addr)
{
    uint32_t value;
    if (test->isa_base == -1) {
        value = inl(addr);
    } else {
        value = readl(test->isa_base + addr);
    }
    return test->bswap ? bswap32(value) : value;
}

static void isa_outb(const TestCase *test, uint16_t addr, uint8_t value)
{
    if (test->isa_base == -1) {
        outb(addr, value);
    } else {
        writeb(test->isa_base + addr, value);
    }
}

static void isa_outw(const TestCase *test, uint16_t addr, uint16_t value)
{
    value = test->bswap ? bswap16(value) : value;
    if (test->isa_base == -1) {
        outw(addr, value);
    } else {
        writew(test->isa_base + addr, value);
    }
}

static void isa_outl(const TestCase *test, uint16_t addr, uint32_t value)
{
    value = test->bswap ? bswap32(value) : value;
    if (test->isa_base == -1) {
        outl(addr, value);
    } else {
        writel(test->isa_base + addr, value);
    }
}


static void test_endianness(gconstpointer data)
{
    const TestCase *test = data;
    char *args;

    args = g_strdup_printf("-M %s%s%s -device pc-testdev",
                           test->machine,
                           test->superio ? " -device " : "",
                           test->superio ?: "");
    qtest_start(args);
    isa_outl(test, 0xe0, 0x87654321);
    g_assert_cmphex(isa_inl(test, 0xe0), ==, 0x87654321);
    g_assert_cmphex(isa_inw(test, 0xe2), ==, 0x8765);
    g_assert_cmphex(isa_inw(test, 0xe0), ==, 0x4321);
    g_assert_cmphex(isa_inb(test, 0xe3), ==, 0x87);
    g_assert_cmphex(isa_inb(test, 0xe2), ==, 0x65);
    g_assert_cmphex(isa_inb(test, 0xe1), ==, 0x43);
    g_assert_cmphex(isa_inb(test, 0xe0), ==, 0x21);

    isa_outw(test, 0xe2, 0x8866);
    g_assert_cmphex(isa_inl(test, 0xe0), ==, 0x88664321);
    g_assert_cmphex(isa_inw(test, 0xe2), ==, 0x8866);
    g_assert_cmphex(isa_inw(test, 0xe0), ==, 0x4321);
    g_assert_cmphex(isa_inb(test, 0xe3), ==, 0x88);
    g_assert_cmphex(isa_inb(test, 0xe2), ==, 0x66);
    g_assert_cmphex(isa_inb(test, 0xe1), ==, 0x43);
    g_assert_cmphex(isa_inb(test, 0xe0), ==, 0x21);

    isa_outw(test, 0xe0, 0x4422);
    g_assert_cmphex(isa_inl(test, 0xe0), ==, 0x88664422);
    g_assert_cmphex(isa_inw(test, 0xe2), ==, 0x8866);
    g_assert_cmphex(isa_inw(test, 0xe0), ==, 0x4422);
    g_assert_cmphex(isa_inb(test, 0xe3), ==, 0x88);
    g_assert_cmphex(isa_inb(test, 0xe2), ==, 0x66);
    g_assert_cmphex(isa_inb(test, 0xe1), ==, 0x44);
    g_assert_cmphex(isa_inb(test, 0xe0), ==, 0x22);

    isa_outb(test, 0xe3, 0x87);
    g_assert_cmphex(isa_inl(test, 0xe0), ==, 0x87664422);
    g_assert_cmphex(isa_inw(test, 0xe2), ==, 0x8766);
    g_assert_cmphex(isa_inb(test, 0xe3), ==, 0x87);
    g_assert_cmphex(isa_inb(test, 0xe2), ==, 0x66);
    g_assert_cmphex(isa_inb(test, 0xe1), ==, 0x44);
    g_assert_cmphex(isa_inb(test, 0xe0), ==, 0x22);

    isa_outb(test, 0xe2, 0x65);
    g_assert_cmphex(isa_inl(test, 0xe0), ==, 0x87654422);
    g_assert_cmphex(isa_inw(test, 0xe2), ==, 0x8765);
    g_assert_cmphex(isa_inw(test, 0xe0), ==, 0x4422);
    g_assert_cmphex(isa_inb(test, 0xe3), ==, 0x87);
    g_assert_cmphex(isa_inb(test, 0xe2), ==, 0x65);
    g_assert_cmphex(isa_inb(test, 0xe1), ==, 0x44);
    g_assert_cmphex(isa_inb(test, 0xe0), ==, 0x22);

    isa_outb(test, 0xe1, 0x43);
    g_assert_cmphex(isa_inl(test, 0xe0), ==, 0x87654322);
    g_assert_cmphex(isa_inw(test, 0xe2), ==, 0x8765);
    g_assert_cmphex(isa_inw(test, 0xe0), ==, 0x4322);
    g_assert_cmphex(isa_inb(test, 0xe3), ==, 0x87);
    g_assert_cmphex(isa_inb(test, 0xe2), ==, 0x65);
    g_assert_cmphex(isa_inb(test, 0xe1), ==, 0x43);
    g_assert_cmphex(isa_inb(test, 0xe0), ==, 0x22);

    isa_outb(test, 0xe0, 0x21);
    g_assert_cmphex(isa_inl(test, 0xe0), ==, 0x87654321);
    g_assert_cmphex(isa_inw(test, 0xe2), ==, 0x8765);
    g_assert_cmphex(isa_inw(test, 0xe0), ==, 0x4321);
    g_assert_cmphex(isa_inb(test, 0xe3), ==, 0x87);
    g_assert_cmphex(isa_inb(test, 0xe2), ==, 0x65);
    g_assert_cmphex(isa_inb(test, 0xe1), ==, 0x43);
    g_assert_cmphex(isa_inb(test, 0xe0), ==, 0x21);
    qtest_quit(global_qtest);
    g_free(args);
}

static void test_endianness_split(gconstpointer data)
{
    const TestCase *test = data;
    char *args;

    args = g_strdup_printf("-M %s%s%s -device pc-testdev",
                           test->machine,
                           test->superio ? " -device " : "",
                           test->superio ?: "");
    qtest_start(args);
    isa_outl(test, 0xe8, 0x87654321);
    g_assert_cmphex(isa_inl(test, 0xe0), ==, 0x87654321);
    g_assert_cmphex(isa_inw(test, 0xe2), ==, 0x8765);
    g_assert_cmphex(isa_inw(test, 0xe0), ==, 0x4321);

    isa_outw(test, 0xea, 0x8866);
    g_assert_cmphex(isa_inl(test, 0xe0), ==, 0x88664321);
    g_assert_cmphex(isa_inw(test, 0xe2), ==, 0x8866);
    g_assert_cmphex(isa_inw(test, 0xe0), ==, 0x4321);

    isa_outw(test, 0xe8, 0x4422);
    g_assert_cmphex(isa_inl(test, 0xe0), ==, 0x88664422);
    g_assert_cmphex(isa_inw(test, 0xe2), ==, 0x8866);
    g_assert_cmphex(isa_inw(test, 0xe0), ==, 0x4422);

    isa_outb(test, 0xeb, 0x87);
    g_assert_cmphex(isa_inl(test, 0xe0), ==, 0x87664422);
    g_assert_cmphex(isa_inw(test, 0xe2), ==, 0x8766);

    isa_outb(test, 0xea, 0x65);
    g_assert_cmphex(isa_inl(test, 0xe0), ==, 0x87654422);
    g_assert_cmphex(isa_inw(test, 0xe2), ==, 0x8765);
    g_assert_cmphex(isa_inw(test, 0xe0), ==, 0x4422);

    isa_outb(test, 0xe9, 0x43);
    g_assert_cmphex(isa_inl(test, 0xe0), ==, 0x87654322);
    g_assert_cmphex(isa_inw(test, 0xe2), ==, 0x8765);
    g_assert_cmphex(isa_inw(test, 0xe0), ==, 0x4322);

    isa_outb(test, 0xe8, 0x21);
    g_assert_cmphex(isa_inl(test, 0xe0), ==, 0x87654321);
    g_assert_cmphex(isa_inw(test, 0xe2), ==, 0x8765);
    g_assert_cmphex(isa_inw(test, 0xe0), ==, 0x4321);
    qtest_quit(global_qtest);
    g_free(args);
}

static void test_endianness_combine(gconstpointer data)
{
    const TestCase *test = data;
    char *args;

    args = g_strdup_printf("-M %s%s%s -device pc-testdev",
                           test->machine,
                           test->superio ? " -device " : "",
                           test->superio ?: "");
    qtest_start(args);
    isa_outl(test, 0xe0, 0x87654321);
    g_assert_cmphex(isa_inl(test, 0xe8), ==, 0x87654321);
    g_assert_cmphex(isa_inw(test, 0xea), ==, 0x8765);
    g_assert_cmphex(isa_inw(test, 0xe8), ==, 0x4321);

    isa_outw(test, 0xe2, 0x8866);
    g_assert_cmphex(isa_inl(test, 0xe8), ==, 0x88664321);
    g_assert_cmphex(isa_inw(test, 0xea), ==, 0x8866);
    g_assert_cmphex(isa_inw(test, 0xe8), ==, 0x4321);

    isa_outw(test, 0xe0, 0x4422);
    g_assert_cmphex(isa_inl(test, 0xe8), ==, 0x88664422);
    g_assert_cmphex(isa_inw(test, 0xea), ==, 0x8866);
    g_assert_cmphex(isa_inw(test, 0xe8), ==, 0x4422);

    isa_outb(test, 0xe3, 0x87);
    g_assert_cmphex(isa_inl(test, 0xe8), ==, 0x87664422);
    g_assert_cmphex(isa_inw(test, 0xea), ==, 0x8766);

    isa_outb(test, 0xe2, 0x65);
    g_assert_cmphex(isa_inl(test, 0xe8), ==, 0x87654422);
    g_assert_cmphex(isa_inw(test, 0xea), ==, 0x8765);
    g_assert_cmphex(isa_inw(test, 0xe8), ==, 0x4422);

    isa_outb(test, 0xe1, 0x43);
    g_assert_cmphex(isa_inl(test, 0xe8), ==, 0x87654322);
    g_assert_cmphex(isa_inw(test, 0xea), ==, 0x8765);
    g_assert_cmphex(isa_inw(test, 0xe8), ==, 0x4322);

    isa_outb(test, 0xe0, 0x21);
    g_assert_cmphex(isa_inl(test, 0xe8), ==, 0x87654321);
    g_assert_cmphex(isa_inw(test, 0xea), ==, 0x8765);
    g_assert_cmphex(isa_inw(test, 0xe8), ==, 0x4321);
    qtest_quit(global_qtest);
    g_free(args);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();
    int ret;
    int i;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; test_cases[i].arch; i++) {
        gchar *path;
        if (strcmp(test_cases[i].arch, arch) != 0) {
            continue;
        }
        path = g_strdup_printf("endianness/%s",
                               test_cases[i].machine);
        qtest_add_data_func(path, &test_cases[i], test_endianness);

        path = g_strdup_printf("endianness/split/%s",
                               test_cases[i].machine);
        qtest_add_data_func(path, &test_cases[i], test_endianness_split);

        path = g_strdup_printf("endianness/combine/%s",
                               test_cases[i].machine);
        qtest_add_data_func(path, &test_cases[i], test_endianness_combine);
    }

    ret = g_test_run();

    return ret;
}
