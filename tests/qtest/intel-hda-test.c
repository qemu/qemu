/*
 * QTest testcase for Intel HDA
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define HDA_ID "hda0"
#define CODEC_DEVICES " -device hda-output,bus=" HDA_ID ".0" \
                      " -device hda-micro,bus=" HDA_ID ".0" \
                      " -device hda-duplex,bus=" HDA_ID ".0"

/* Tests only initialization so far. TODO: Replace with functional tests */
static void ich6_test(void)
{
    qtest_start("-machine pc -device intel-hda,id=" HDA_ID CODEC_DEVICES);
    qtest_end();
}

static void ich9_test(void)
{
    qtest_start("-machine q35 -device ich9-intel-hda,bus=pcie.0,addr=1b.0,id="
                HDA_ID CODEC_DEVICES);
    qtest_end();
}

/*
 * https://gitlab.com/qemu-project/qemu/-/issues/542
 * Used to trigger:
 *  AddressSanitizer: stack-overflow
 */
static void test_issue542_ich6(void)
{
    QTestState *s;

    s = qtest_init("-nographic -nodefaults -M pc-q35-6.2 "
                   "-device intel-hda,id=" HDA_ID CODEC_DEVICES);

    qtest_outl(s, 0xcf8, 0x80000804);
    qtest_outw(s, 0xcfc, 0x06);
    qtest_bufwrite(s, 0xff0d060f, "\x03", 1);
    qtest_bufwrite(s, 0x0, "\x12", 1);
    qtest_bufwrite(s, 0x2, "\x2a", 1);
    qtest_writeb(s, 0x0, 0x12);
    qtest_writeb(s, 0x2, 0x2a);
    qtest_outl(s, 0xcf8, 0x80000811);
    qtest_outl(s, 0xcfc, 0x006a4400);
    qtest_bufwrite(s, 0x6a44005a, "\x01", 1);
    qtest_bufwrite(s, 0x6a44005c, "\x02", 1);
    qtest_bufwrite(s, 0x6a442050, "\x00\x00\x44\x6a", 4);
    qtest_bufwrite(s, 0x6a44204a, "\x01", 1);
    qtest_bufwrite(s, 0x6a44204c, "\x02", 1);
    qtest_bufwrite(s, 0x6a44005c, "\x02", 1);
    qtest_bufwrite(s, 0x6a442050, "\x00\x00\x44\x6a", 4);
    qtest_bufwrite(s, 0x6a44204a, "\x01", 1);
    qtest_bufwrite(s, 0x6a44204c, "\x02", 1);
    qtest_quit(s);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    if (qtest_has_machine("pc")) {
        qtest_add_func("/intel-hda/ich6", ich6_test);
    }
    if (qtest_has_machine("q35")) {
        qtest_add_func("/intel-hda/ich9", ich9_test);
        qtest_add_func("/intel-hda/fuzz/issue542", test_issue542_ich6);
    }
    return g_test_run();
}
