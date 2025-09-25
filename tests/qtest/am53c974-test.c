/*
 * QTest testcase for am53c974
 *
 * Copyright (c) 2021 Mark Cave-Ayland <mark.cave-ayland@ilande.co.uk>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "libqtest.h"


static void test_cmdfifo_underflow_ok(void)
{
    QTestState *s = qtest_init(
        "-device am53c974,id=scsi "
        "-device scsi-hd,drive=disk0 -drive "
        "id=disk0,if=none,file=null-co://,format=raw -nodefaults");
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_outw(s, 0xcfc, 0x01);
    qtest_outl(s, 0xcf8, 0x8000100e);
    qtest_outl(s, 0xcfc, 0x8a000000);
    qtest_outl(s, 0x8a09, 0x42000000);
    qtest_outl(s, 0x8a0d, 0x00);
    qtest_outl(s, 0x8a0b, 0x1000);
    qtest_quit(s);
}

/* Reported as crash_1548bd10e7 */
static void test_cmdfifo_underflow2_ok(void)
{
    QTestState *s = qtest_init(
        "-device am53c974,id=scsi -device scsi-hd,drive=disk0 "
        "-drive id=disk0,if=none,file=null-co://,format=raw -nodefaults");
    qtest_outl(s, 0xcf8, 0x80001010);
    qtest_outl(s, 0xcfc, 0xc000);
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_outw(s, 0xcfc, 0x01);
    qtest_outw(s, 0xc00c, 0x41);
    qtest_outw(s, 0xc00a, 0x00);
    qtest_outl(s, 0xc00a, 0x00);
    qtest_outw(s, 0xc00c, 0x43);
    qtest_outw(s, 0xc00b, 0x00);
    qtest_outw(s, 0xc00b, 0x00);
    qtest_outw(s, 0xc00c, 0x00);
    qtest_outl(s, 0xc00a, 0x00);
    qtest_outw(s, 0xc00a, 0x00);
    qtest_outl(s, 0xc00a, 0x00);
    qtest_outw(s, 0xc00c, 0x00);
    qtest_outl(s, 0xc00a, 0x00);
    qtest_outw(s, 0xc00a, 0x00);
    qtest_outl(s, 0xc00a, 0x00);
    qtest_outw(s, 0xc00c, 0x00);
    qtest_outl(s, 0xc00a, 0x00);
    qtest_outw(s, 0xc00a, 0x00);
    qtest_outl(s, 0xc00a, 0x00);
    qtest_outw(s, 0xc00c, 0x00);
    qtest_outl(s, 0xc00a, 0x00);
    qtest_outl(s, 0xc006, 0x00);
    qtest_outl(s, 0xc00b, 0x00);
    qtest_outw(s, 0xc00b, 0x0800);
    qtest_outw(s, 0xc00b, 0x00);
    qtest_outw(s, 0xc00b, 0x00);
    qtest_outl(s, 0xc006, 0x00);
    qtest_outl(s, 0xc00b, 0x00);
    qtest_outw(s, 0xc00b, 0x0800);
    qtest_outw(s, 0xc00b, 0x00);
    qtest_outw(s, 0xc00b, 0x4100);
    qtest_outw(s, 0xc00a, 0x00);
    qtest_outl(s, 0xc00a, 0x100000);
    qtest_outl(s, 0xc00a, 0x00);
    qtest_outw(s, 0xc00c, 0x43);
    qtest_outl(s, 0xc00a, 0x100000);
    qtest_outl(s, 0xc00a, 0x100000);
    qtest_quit(s);
}

static void test_cmdfifo_overflow_ok(void)
{
    QTestState *s = qtest_init(
        "-device am53c974,id=scsi "
        "-device scsi-hd,drive=disk0 -drive "
        "id=disk0,if=none,file=null-co://,format=raw -nodefaults");
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_outw(s, 0xcfc, 0x01);
    qtest_outl(s, 0xcf8, 0x8000100e);
    qtest_outl(s, 0xcfc, 0x0e000000);
    qtest_outl(s, 0xe40, 0x03);
    qtest_outl(s, 0xe0b, 0x4100);
    qtest_outl(s, 0xe0b, 0x9000);
    qtest_quit(s);
}

/* Reported as crash_530ff2e211 */
static void test_cmdfifo_overflow2_ok(void)
{
    QTestState *s = qtest_init(
        "-device am53c974,id=scsi -device scsi-hd,drive=disk0 "
        "-drive id=disk0,if=none,file=null-co://,format=raw -nodefaults");
    qtest_outl(s, 0xcf8, 0x80001010);
    qtest_outl(s, 0xcfc, 0xc000);
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_outw(s, 0xcfc, 0x01);
    qtest_outl(s, 0xc00b, 0x4100);
    qtest_outw(s, 0xc00b, 0xc200);
    qtest_outl(s, 0xc03f, 0x0300);
    qtest_quit(s);
}

/* Reported as https://issues.oss-fuzz.com/issues/439878564 */
static void test_cmdfifo_overflow3_ok(void)
{
    QTestState *s = qtest_init(
        "-device am53c974,id=scsi -device scsi-hd,drive=disk0 "
        "-drive id=disk0,if=none,file=null-co://,format=raw -nodefaults");
    qtest_outl(s, 0xcf8, 0x80001010);
    qtest_outl(s, 0xcfc, 0xc000);
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_outw(s, 0xcfc, 0x01);
    qtest_outb(s, 0xc00c, 0x43);
    qtest_outl(s, 0xc00b, 0x9100);
    qtest_outl(s, 0xc009, 0x02000000);
    qtest_outl(s, 0xc000, 0x0b);
    qtest_outl(s, 0xc00b, 0x00);
    qtest_outl(s, 0xc00b, 0x00);
    qtest_outl(s, 0xc00b, 0xc200);
    qtest_outl(s, 0xc00b, 0x1000);
    qtest_outl(s, 0xc00b, 0x9000);
    qtest_outb(s, 0xc008, 0x00);
    qtest_outb(s, 0xc008, 0x00);
    qtest_outl(s, 0xc03f, 0x0300);
    qtest_outl(s, 0xc00b, 0x00);
    qtest_outw(s, 0xc00b, 0x4200);
    qtest_outl(s, 0xc00b, 0x00);
    qtest_outw(s, 0xc00b, 0x1200);
    qtest_outl(s, 0xc00b, 0x00);
    qtest_outb(s, 0xc00c, 0x43);
    qtest_outl(s, 0xc00b, 0x00);
    qtest_outl(s, 0xc00b, 0x00);
    qtest_outl(s, 0xc007, 0x00);
    qtest_outl(s, 0xc007, 0x00);
    qtest_outl(s, 0xc007, 0x00);
    qtest_outl(s, 0xc00b, 0x1000);
    qtest_outl(s, 0xc007, 0x00);
    qtest_quit(s);
}

/* Reported as crash_0900379669 */
static void test_fifo_pop_buf(void)
{
    QTestState *s = qtest_init(
        "-device am53c974,id=scsi -device scsi-hd,drive=disk0 "
        "-drive id=disk0,if=none,file=null-co://,format=raw -nodefaults");
    qtest_outl(s, 0xcf8, 0x80001010);
    qtest_outl(s, 0xcfc, 0xc000);
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_outw(s, 0xcfc, 0x01);
    qtest_outb(s, 0xc000, 0x4);
    qtest_outb(s, 0xc008, 0xa0);
    qtest_outl(s, 0xc03f, 0x0300);
    qtest_outl(s, 0xc00b, 0xc300);
    qtest_outw(s, 0xc00b, 0x9000);
    qtest_outl(s, 0xc00b, 0xc300);
    qtest_outl(s, 0xc00b, 0xc300);
    qtest_outl(s, 0xc00b, 0xc300);
    qtest_outw(s, 0xc00b, 0x9000);
    qtest_outw(s, 0xc00b, 0x1000);
    qtest_quit(s);
}

static void test_target_selected_ok(void)
{
    QTestState *s = qtest_init(
        "-device am53c974,id=scsi "
        "-device scsi-hd,drive=disk0 -drive "
        "id=disk0,if=none,file=null-co://,format=raw -nodefaults");
    qtest_outl(s, 0xcf8, 0x80001001);
    qtest_outl(s, 0xcfc, 0x01000000);
    qtest_outl(s, 0xcf8, 0x8000100e);
    qtest_outl(s, 0xcfc, 0xef800000);
    qtest_outl(s, 0xef8b, 0x4100);
    qtest_outw(s, 0xef80, 0x01);
    qtest_outl(s, 0xefc0, 0x03);
    qtest_outl(s, 0xef8b, 0xc100);
    qtest_outl(s, 0xef8b, 0x9000);
    qtest_quit(s);
}

static void test_fifo_underflow_on_write_ok(void)
{
    QTestState *s = qtest_init(
        "-device am53c974,id=scsi "
        "-device scsi-hd,drive=disk0 -drive "
        "id=disk0,if=none,file=null-co://,format=raw -nodefaults");
    qtest_outl(s, 0xcf8, 0x80001010);
    qtest_outl(s, 0xcfc, 0xc000);
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_outw(s, 0xcfc, 0x01);
    qtest_outl(s, 0xc008, 0x0a);
    qtest_outl(s, 0xc009, 0x41000000);
    qtest_outl(s, 0xc009, 0x41000000);
    qtest_outl(s, 0xc00b, 0x1000);
    qtest_quit(s);
}

static void test_cancelled_request_ok(void)
{
    QTestState *s = qtest_init(
        "-device am53c974,id=scsi "
        "-device scsi-hd,drive=disk0 -drive "
        "id=disk0,if=none,file=null-co://,format=raw -nodefaults");
    qtest_outl(s, 0xcf8, 0x80001010);
    qtest_outl(s, 0xcfc, 0xc000);
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_outw(s, 0xcfc, 0x05);
    qtest_outb(s, 0xc046, 0x02);
    qtest_outl(s, 0xc00b, 0xc100);
    qtest_outl(s, 0xc040, 0x03);
    qtest_outl(s, 0xc040, 0x03);
    qtest_bufwrite(s, 0x0, "\x41", 0x1);
    qtest_outl(s, 0xc00b, 0xc100);
    qtest_outw(s, 0xc040, 0x02);
    qtest_outw(s, 0xc040, 0x81);
    qtest_outl(s, 0xc00b, 0x9000);
    qtest_quit(s);
}

static void test_inflight_cancel_ok(void)
{
    QTestState *s = qtest_init(
        "-device am53c974,id=scsi "
        "-device scsi-hd,drive=disk0 -drive "
        "id=disk0,if=none,file=null-co://,format=raw -nodefaults");
    qtest_outl(s, 0xcf8, 0x80001000);
    qtest_inw(s, 0xcfc);
    qtest_outl(s, 0xcf8, 0x80001010);
    qtest_outl(s, 0xcfc, 0xffffffff);
    qtest_outl(s, 0xcf8, 0x80001010);
    qtest_inl(s, 0xcfc);
    qtest_outl(s, 0xcf8, 0x80001010);
    qtest_outl(s, 0xcfc, 0xc001);
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_inw(s, 0xcfc);
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_outw(s, 0xcfc, 0x7);
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_inw(s, 0xcfc);
    qtest_inb(s, 0xc000);
    qtest_outb(s, 0xc008, 0x8);
    qtest_outw(s, 0xc00b, 0x4100);
    qtest_outb(s, 0xc009, 0x0);
    qtest_outb(s, 0xc009, 0x0);
    qtest_outw(s, 0xc00b, 0xc212);
    qtest_outl(s, 0xc042, 0x2c2c5a88);
    qtest_outw(s, 0xc00b, 0xc212);
    qtest_outw(s, 0xc00b, 0x415a);
    qtest_outl(s, 0xc03f, 0x3060303);
    qtest_outl(s, 0xc00b, 0x5afa9054);
    qtest_quit(s);
}

static void test_reset_before_transfer_ok(void)
{
    QTestState *s = qtest_init(
        "-device am53c974,id=scsi "
        "-device scsi-hd,drive=disk0 -drive "
        "id=disk0,if=none,file=null-co://,format=raw -nodefaults");

    qtest_outl(s, 0xcf8, 0x80001010);
    qtest_outl(s, 0xcfc, 0xc000);
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_outw(s, 0xcfc, 0x01);
    qtest_outl(s, 0xc007, 0x2500);
    qtest_outl(s, 0xc00a, 0x410000);
    qtest_outl(s, 0xc00a, 0x410000);
    qtest_outw(s, 0xc00b, 0x0200);
    qtest_outw(s, 0xc040, 0x03);
    qtest_outw(s, 0xc009, 0x00);
    qtest_outw(s, 0xc00b, 0x00);
    qtest_outw(s, 0xc009, 0x00);
    qtest_outw(s, 0xc00b, 0x00);
    qtest_outw(s, 0xc009, 0x00);
    qtest_outw(s, 0xc003, 0x1000);
    qtest_outw(s, 0xc00b, 0x1000);
    qtest_outl(s, 0xc00b, 0x9000);
    qtest_outw(s, 0xc00b, 0x1000);
    qtest_quit(s);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0) {
        qtest_add_func("am53c974/test_cmdfifo_underflow_ok",
                       test_cmdfifo_underflow_ok);
        qtest_add_func("am53c974/test_cmdfifo_underflow2_ok",
                       test_cmdfifo_underflow2_ok);
        qtest_add_func("am53c974/test_cmdfifo_overflow_ok",
                       test_cmdfifo_overflow_ok);
        qtest_add_func("am53c974/test_cmdfifo_overflow2_ok",
                       test_cmdfifo_overflow2_ok);
        qtest_add_func("am53c974/test_cmdfifo_overflow3_ok",
                       test_cmdfifo_overflow3_ok);
        qtest_add_func("am53c974/test_fifo_pop_buf",
                       test_fifo_pop_buf);
        qtest_add_func("am53c974/test_target_selected_ok",
                       test_target_selected_ok);
        qtest_add_func("am53c974/test_fifo_underflow_on_write_ok",
                       test_fifo_underflow_on_write_ok);
        qtest_add_func("am53c974/test_cancelled_request_ok",
                       test_cancelled_request_ok);
        qtest_add_func("am53c974/test_inflight_cancel_ok",
                       test_inflight_cancel_ok);
        qtest_add_func("am53c974/test_reset_before_transfer_ok",
                       test_reset_before_transfer_ok);
    }

    return g_test_run();
}
