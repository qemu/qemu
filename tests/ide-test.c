/*
 * IDE test cases
 *
 * Copyright (c) 2013 Kevin Wolf <kwolf@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <glib.h>

#include "libqtest.h"

#include "qemu-common.h"

#define TEST_IMAGE_SIZE 64 * 1024 * 1024

#define IDE_PCI_DEV     1
#define IDE_PCI_FUNC    1

#define IDE_BASE 0x1f0
#define IDE_PRIMARY_IRQ 14

enum {
    reg_data        = 0x0,
    reg_nsectors    = 0x2,
    reg_lba_low     = 0x3,
    reg_lba_middle  = 0x4,
    reg_lba_high    = 0x5,
    reg_device      = 0x6,
    reg_status      = 0x7,
    reg_command     = 0x7,
};

enum {
    BSY     = 0x80,
    DRDY    = 0x40,
    DF      = 0x20,
    DRQ     = 0x08,
    ERR     = 0x01,
};

enum {
    CMD_IDENTIFY    = 0xec,
};

#define assert_bit_set(data, mask) g_assert_cmphex((data) & (mask), ==, (mask))
#define assert_bit_clear(data, mask) g_assert_cmphex((data) & (mask), ==, 0)

static char tmp_path[] = "/tmp/qtest.XXXXXX";

static void ide_test_start(const char *cmdline_fmt, ...)
{
    va_list ap;
    char *cmdline;

    va_start(ap, cmdline_fmt);
    cmdline = g_strdup_vprintf(cmdline_fmt, ap);
    va_end(ap);

    qtest_start(cmdline);
    qtest_irq_intercept_in(global_qtest, "ioapic");
}

static void ide_test_quit(void)
{
    qtest_quit(global_qtest);
}

static void test_identify(void)
{
    uint8_t data;
    uint16_t buf[256];
    int i;
    int ret;

    ide_test_start(
        "-vnc none "
        "-drive file=%s,if=ide,serial=%s,cache=writeback "
        "-global ide-hd.ver=%s",
        tmp_path, "testdisk", "version");

    /* IDENTIFY command on device 0*/
    outb(IDE_BASE + reg_device, 0);
    outb(IDE_BASE + reg_command, CMD_IDENTIFY);

    /* Read in the IDENTIFY buffer and check registers */
    data = inb(IDE_BASE + reg_device);
    g_assert_cmpint(data & 0x10, ==, 0);

    for (i = 0; i < 256; i++) {
        data = inb(IDE_BASE + reg_status);
        assert_bit_set(data, DRDY | DRQ);
        assert_bit_clear(data, BSY | DF | ERR);

        ((uint16_t*) buf)[i] = inw(IDE_BASE + reg_data);
    }

    data = inb(IDE_BASE + reg_status);
    assert_bit_set(data, DRDY);
    assert_bit_clear(data, BSY | DF | ERR | DRQ);

    /* Check serial number/version in the buffer */
    ret = memcmp(&buf[10], "ettsidks            ", 20);
    g_assert(ret == 0);

    ret = memcmp(&buf[23], "evsroi n", 8);
    g_assert(ret == 0);

    /* Write cache enabled bit */
    assert_bit_set(buf[85], 0x20);

    ide_test_quit();
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();
    int fd;
    int ret;

    /* Check architecture */
    if (strcmp(arch, "i386") && strcmp(arch, "x86_64")) {
        g_test_message("Skipping test for non-x86\n");
        return 0;
    }

    /* Create a temporary raw image */
    fd = mkstemp(tmp_path);
    g_assert(fd >= 0);
    ret = ftruncate(fd, TEST_IMAGE_SIZE);
    g_assert(ret == 0);
    close(fd);

    /* Run the tests */
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/ide/identify", test_identify);

    ret = g_test_run();

    /* Cleanup */
    unlink(tmp_path);

    return ret;
}
