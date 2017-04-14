/*
 * Test serial output of some machines.
 *
 * Copyright 2016 Thomas Huth, Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2
 * or later. See the COPYING file in the top-level directory.
 *
 * This test is used to check that the serial output of the firmware
 * (that we provide for some machines) contains an expected string.
 * Thus we check that the firmware still boots at least to a certain
 * point and so we know that the machine is not completely broken.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

typedef struct testdef {
    const char *arch;       /* Target architecture */
    const char *machine;    /* Name of the machine */
    const char *extra;      /* Additional parameters */
    const char *expect;     /* Expected string in the serial output */
} testdef_t;

static testdef_t tests[] = {
    { "alpha", "clipper", "", "PCI:" },
    { "ppc", "ppce500", "", "U-Boot" },
    { "ppc", "prep", "", "Open Hack'Ware BIOS" },
    { "ppc64", "ppce500", "", "U-Boot" },
    { "ppc64", "prep", "", "Open Hack'Ware BIOS" },
    { "ppc64", "pseries", "", "Open Firmware" },
    { "ppc64", "powernv", "-cpu POWER8", "SkiBoot" },
    { "i386", "isapc", "-cpu qemu32 -device sga", "SGABIOS" },
    { "i386", "pc", "-device sga", "SGABIOS" },
    { "i386", "q35", "-device sga", "SGABIOS" },
    { "x86_64", "isapc", "-cpu qemu32 -device sga", "SGABIOS" },
    { "x86_64", "q35", "-device sga", "SGABIOS" },
    { "s390x", "s390-ccw-virtio",
      "-nodefaults -device sclpconsole,chardev=serial0", "virtio device" },
    { NULL }
};

static void check_guest_output(const testdef_t *test, int fd)
{
    bool output_ok = false;
    int i, nbr, pos = 0;
    char ch;

    /* Poll serial output... Wait at most 60 seconds */
    for (i = 0; i < 6000; ++i) {
        while ((nbr = read(fd, &ch, 1)) == 1) {
            if (ch == test->expect[pos]) {
                pos += 1;
                if (test->expect[pos] == '\0') {
                    /* We've reached the end of the expected string! */
                    output_ok = true;
                    goto done;
                }
            } else {
                pos = 0;
            }
        }
        g_assert(nbr >= 0);
        g_usleep(10000);
    }

done:
    g_assert(output_ok);
}

static void test_machine(const void *data)
{
    const testdef_t *test = data;
    char *args;
    char tmpname[] = "/tmp/qtest-boot-serial-XXXXXX";
    int fd;

    fd = mkstemp(tmpname);
    g_assert(fd != -1);

    args = g_strdup_printf("-M %s,accel=tcg -chardev file,id=serial0,path=%s"
                           " -no-shutdown -serial chardev:serial0 %s",
                           test->machine, tmpname, test->extra);

    qtest_start(args);
    unlink(tmpname);

    check_guest_output(test, fd);
    qtest_quit(global_qtest);

    g_free(args);
    close(fd);
}

int main(int argc, char *argv[])
{
    const char *arch = qtest_get_arch();
    int i;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; tests[i].arch != NULL; i++) {
        if (strcmp(arch, tests[i].arch) == 0) {
            char *name = g_strdup_printf("boot-serial/%s", tests[i].machine);
            qtest_add_data_func(name, &tests[i], test_machine);
            g_free(name);
        }
    }

    return g_test_run();
}
