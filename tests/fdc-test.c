/*
 * Floppy test cases.
 *
 * Copyright (c) 2012 Kevin Wolf <kwolf@redhat.com>
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

#define TEST_IMAGE_SIZE 1440 * 1024

#define FLOPPY_BASE 0x3f0
#define FLOPPY_IRQ 6

enum {
    reg_sra         = 0x0,
    reg_srb         = 0x1,
    reg_dor         = 0x2,
    reg_msr         = 0x4,
    reg_dsr         = 0x4,
    reg_fifo        = 0x5,
    reg_dir         = 0x7,
};

enum {
    CMD_SENSE_INT   = 0x08,
    CMD_SEEK        = 0x0f,
};

enum {
    RQM     = 0x80,
    DIO     = 0x40,

    DSKCHG  = 0x80,
};

char test_image[] = "/tmp/qtest.XXXXXX";

#define assert_bit_set(data, mask) g_assert_cmphex((data) & (mask), ==, (mask))
#define assert_bit_clear(data, mask) g_assert_cmphex((data) & (mask), ==, 0)

static uint8_t base = 0x70;

enum {
    CMOS_FLOPPY     = 0x10,
};

static void floppy_send(uint8_t byte)
{
    uint8_t msr;

    msr = inb(FLOPPY_BASE + reg_msr);
    assert_bit_set(msr, RQM);
    assert_bit_clear(msr, DIO);

    outb(FLOPPY_BASE + reg_fifo, byte);
}

static uint8_t floppy_recv(void)
{
    uint8_t msr;

    msr = inb(FLOPPY_BASE + reg_msr);
    assert_bit_set(msr, RQM | DIO);

    return inb(FLOPPY_BASE + reg_fifo);
}

static void ack_irq(void)
{
    g_assert(get_irq(FLOPPY_IRQ));
    floppy_send(CMD_SENSE_INT);
    floppy_recv();
    floppy_recv();
    g_assert(!get_irq(FLOPPY_IRQ));
}

static void send_step_pulse(void)
{
    int drive = 0;
    int head = 0;
    static int cyl = 0;

    floppy_send(CMD_SEEK);
    floppy_send(head << 2 | drive);
    g_assert(!get_irq(FLOPPY_IRQ));
    floppy_send(cyl);
    ack_irq();

    cyl = (cyl + 1) % 4;
}

static uint8_t cmos_read(uint8_t reg)
{
    outb(base + 0, reg);
    return inb(base + 1);
}

static void test_cmos(void)
{
    uint8_t cmos;

    cmos = cmos_read(CMOS_FLOPPY);
    g_assert(cmos == 0x40);
}

static void test_no_media_on_start(void)
{
    uint8_t dir;

    /* Media changed bit must be set all time after start if there is
     * no media in drive. */
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
    send_step_pulse();
    send_step_pulse();
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
}

static void test_media_change(void)
{
    uint8_t dir;

    /* Insert media in drive. DSKCHK should not be reset until a step pulse
     * is sent. */
    qmp("{'execute':'change', 'arguments':{ 'device':'floppy0', "
        "'target': '%s' }}", test_image);
    qmp(""); /* ignore event (FIXME open -> open transition?!) */
    qmp(""); /* ignore event */

    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);

    send_step_pulse();
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_clear(dir, DSKCHG);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_clear(dir, DSKCHG);

    /* Eject the floppy and check that DSKCHG is set. Reading it out doesn't
     * reset the bit. */
    qmp("{'execute':'eject', 'arguments':{ 'device':'floppy0' }}");
    qmp(""); /* ignore event */

    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);

    send_step_pulse();
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
    dir = inb(FLOPPY_BASE + reg_dir);
    assert_bit_set(dir, DSKCHG);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();
    char *cmdline;
    int fd;
    int ret;

    /* Check architecture */
    if (strcmp(arch, "i386") && strcmp(arch, "x86_64")) {
        g_test_message("Skipping test for non-x86\n");
        return 0;
    }

    /* Create a temporary raw image */
    fd = mkstemp(test_image);
    g_assert(fd >= 0);
    ret = ftruncate(fd, TEST_IMAGE_SIZE);
    g_assert(ret == 0);
    close(fd);

    /* Run the tests */
    g_test_init(&argc, &argv, NULL);

    cmdline = g_strdup_printf("-vnc none ");

    qtest_start(cmdline);
    qtest_irq_intercept_in(global_qtest, "ioapic");
    qtest_add_func("/fdc/cmos", test_cmos);
    qtest_add_func("/fdc/no_media_on_start", test_no_media_on_start);
    qtest_add_func("/fdc/media_change", test_media_change);

    ret = g_test_run();

    /* Cleanup */
    qtest_quit(global_qtest);
    unlink(test_image);

    return ret;
}
