/*
 * QTest fuzzer-generated testcase for sb16 audio device
 *
 * Copyright (c) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/*
 * This used to trigger the assert in audio_calloc
 * https://bugs.launchpad.net/qemu/+bug/1910603
 */
static void test_fuzz_sb16_0x1c(void)
{
    QTestState *s = qtest_init("-M q35 "
                               "-device sb16,audiodev=snd0 "
                               "-audiodev none,id=snd0");
    qtest_outw(s, 0x22c, 0x41);
    qtest_outb(s, 0x22c, 0x00);
    qtest_outw(s, 0x22c, 0x1004);
    qtest_outw(s, 0x22c, 0x001c);
    qtest_quit(s);
}

static void test_fuzz_sb16_0x91(void)
{
    QTestState *s = qtest_init("-M pc "
                               "-device sb16,audiodev=none "
                               "-audiodev id=none,driver=none");
    qtest_outw(s, 0x22c, 0xf141);
    qtest_outb(s, 0x22c, 0x00);
    qtest_outb(s, 0x22c, 0x24);
    qtest_outb(s, 0x22c, 0x91);
    qtest_quit(s);
}

/*
 * This used to trigger the assert in audio_calloc
 * through command 0xd4
 */
static void test_fuzz_sb16_0xd4(void)
{
    QTestState *s = qtest_init("-M pc "
                               "-device sb16,audiodev=none "
                               "-audiodev id=none,driver=none");
    qtest_outb(s, 0x22c, 0x41);
    qtest_outb(s, 0x22c, 0x00);
    qtest_outb(s, 0x22c, 0x14);
    qtest_outb(s, 0x22c, 0xd4);
    qtest_quit(s);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    if (qtest_has_machine("q35")) {
        qtest_add_func("fuzz/test_fuzz_sb16/1c", test_fuzz_sb16_0x1c);
    }
    if (qtest_has_machine("pc")) {
        qtest_add_func("fuzz/test_fuzz_sb16/91", test_fuzz_sb16_0x91);
        qtest_add_func("fuzz/test_fuzz_sb16/d4", test_fuzz_sb16_0xd4);
    }

    return g_test_run();
}
