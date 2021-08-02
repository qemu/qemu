/*
 * QTest fuzzer-generated testcase for sdcard device
 *
 * Copyright (c) 2021 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqos/libqtest.h"

/*
 * https://gitlab.com/qemu-project/qemu/-/issues/450
 * Used to trigger:
 *  Assertion `wpnum < sd->wpgrps_size' failed.
 */
static void oss_fuzz_29225(void)
{
    QTestState *s;

    s = qtest_init(" -display none -m 512m -nodefaults -nographic"
                   " -device sdhci-pci,sd-spec-version=3"
                   " -device sd-card,drive=d0"
                   " -drive if=none,index=0,file=null-co://,format=raw,id=d0");

    qtest_outl(s, 0xcf8, 0x80001010);
    qtest_outl(s, 0xcfc, 0xd0690);
    qtest_outl(s, 0xcf8, 0x80001003);
    qtest_outl(s, 0xcf8, 0x80001013);
    qtest_outl(s, 0xcfc, 0xffffffff);
    qtest_outl(s, 0xcf8, 0x80001003);
    qtest_outl(s, 0xcfc, 0x3effe00);

    qtest_bufwrite(s, 0xff0d062c, "\xff", 0x1);
    qtest_bufwrite(s, 0xff0d060f, "\xb7", 0x1);
    qtest_bufwrite(s, 0xff0d060a, "\xc9", 0x1);
    qtest_bufwrite(s, 0xff0d060f, "\x29", 0x1);
    qtest_bufwrite(s, 0xff0d060f, "\xc2", 0x1);
    qtest_bufwrite(s, 0xff0d0628, "\xf7", 0x1);
    qtest_bufwrite(s, 0x0, "\xe3", 0x1);
    qtest_bufwrite(s, 0x7, "\x13", 0x1);
    qtest_bufwrite(s, 0x8, "\xe3", 0x1);
    qtest_bufwrite(s, 0xf, "\xe3", 0x1);
    qtest_bufwrite(s, 0xff0d060f, "\x03", 0x1);
    qtest_bufwrite(s, 0xff0d0605, "\x01", 0x1);
    qtest_bufwrite(s, 0xff0d060b, "\xff", 0x1);
    qtest_bufwrite(s, 0xff0d060c, "\xff", 0x1);
    qtest_bufwrite(s, 0xff0d060e, "\xff", 0x1);
    qtest_bufwrite(s, 0xff0d060f, "\x06", 0x1);
    qtest_bufwrite(s, 0xff0d060f, "\x9e", 0x1);

    qtest_quit(s);
}

/*
 * https://gitlab.com/qemu-project/qemu/-/issues/495
 * Used to trigger:
 *  Assertion `wpnum < sd->wpgrps_size' failed.
 */
static void oss_fuzz_36217(void)
{
    QTestState *s;

    s = qtest_init(" -display none -m 32 -nodefaults -nographic"
                   " -device sdhci-pci,sd-spec-version=3 "
                   "-device sd-card,drive=d0 "
                   "-drive if=none,index=0,file=null-co://,format=raw,id=d0");

    qtest_outl(s, 0xcf8, 0x80001010);
    qtest_outl(s, 0xcfc, 0xe0000000);
    qtest_outl(s, 0xcf8, 0x80001004);
    qtest_outw(s, 0xcfc, 0x02);
    qtest_bufwrite(s, 0xe000002c, "\x05", 0x1);
    qtest_bufwrite(s, 0xe000000f, "\x37", 0x1);
    qtest_bufwrite(s, 0xe000000a, "\x01", 0x1);
    qtest_bufwrite(s, 0xe000000f, "\x29", 0x1);
    qtest_bufwrite(s, 0xe000000f, "\x02", 0x1);
    qtest_bufwrite(s, 0xe000000f, "\x03", 0x1);
    qtest_bufwrite(s, 0xe0000005, "\x01", 0x1);
    qtest_bufwrite(s, 0xe000000f, "\x06", 0x1);
    qtest_bufwrite(s, 0xe000000c, "\x05", 0x1);
    qtest_bufwrite(s, 0xe000000e, "\x20", 0x1);
    qtest_bufwrite(s, 0xe000000f, "\x08", 0x1);
    qtest_bufwrite(s, 0xe000000b, "\x3d", 0x1);
    qtest_bufwrite(s, 0xe000000f, "\x1e", 0x1);

    qtest_quit(s);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

   if (strcmp(arch, "i386") == 0) {
        qtest_add_func("fuzz/sdcard/oss_fuzz_29225", oss_fuzz_29225);
        qtest_add_func("fuzz/sdcard/oss_fuzz_36217", oss_fuzz_36217);
   }

   return g_test_run();
}
