/*
 * Test serial output of some machines.
 *
 * Copyright 2016 Thomas Huth, Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2
 * or later. See the COPYING file in the top-level directory.
 *
 * This test is used to check that the serial output of the firmware
 * (that we provide for some machines) or some small mini-kernels that
 * we provide here contains an expected string. Thus we check that the
 * firmware/kernel still boots at least to a certain point and so we
 * know that the machine is not completely broken.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

static const uint8_t kernel_mcf5208[] = {
    0x41, 0xf9, 0xfc, 0x06, 0x00, 0x00,     /* lea 0xfc060000,%a0 */
    0x10, 0x3c, 0x00, 0x54,                 /* move.b #'T',%d0 */
    0x11, 0x7c, 0x00, 0x04, 0x00, 0x08,     /* move.b #4,8(%a0)     Enable TX */
    0x11, 0x40, 0x00, 0x0c,                 /* move.b %d0,12(%a0)   Print 'T' */
    0x60, 0xfa                              /* bra.s  loop */
};

typedef struct testdef {
    const char *arch;       /* Target architecture */
    const char *machine;    /* Name of the machine */
    const char *extra;      /* Additional parameters */
    const char *expect;     /* Expected string in the serial output */
    size_t codesize;        /* Size of the kernel or bios data */
    const uint8_t *kernel;  /* Set in case we use our own mini kernel */
    const uint8_t *bios;    /* Set in case we use our own mini bios */
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
    { "m68k", "mcf5208evb", "", "TT", sizeof(kernel_mcf5208), kernel_mcf5208 },

    { NULL }
};

static void check_guest_output(const testdef_t *test, int fd)
{
    bool output_ok = false;
    int i, nbr, pos = 0, ccnt;
    char ch;

    /* Poll serial output... Wait at most 60 seconds */
    for (i = 0; i < 6000; ++i) {
        ccnt = 0;
        while ((nbr = read(fd, &ch, 1)) == 1 && ccnt++ < 512) {
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
    char serialtmp[] = "/tmp/qtest-boot-serial-sXXXXXX";
    char codetmp[] = "/tmp/qtest-boot-serial-cXXXXXX";
    const char *codeparam = "";
    const uint8_t *code = NULL;
    int ser_fd;

    ser_fd = mkstemp(serialtmp);
    g_assert(ser_fd != -1);

    if (test->kernel) {
        code = test->kernel;
        codeparam = "-kernel";
    } else if (test->bios) {
        code = test->bios;
        codeparam = "-bios";
    }

    if (code) {
        ssize_t wlen;
        int code_fd;

        code_fd = mkstemp(codetmp);
        g_assert(code_fd != -1);
        wlen = write(code_fd, code, test->codesize);
        g_assert(wlen == test->codesize);
        close(code_fd);
    }

    /*
     * Make sure that this test uses tcg if available: It is used as a
     * fast-enough smoketest for that.
     */
    global_qtest = qtest_startf("%s %s -M %s,accel=tcg:kvm "
                                "-chardev file,id=serial0,path=%s "
                                "-no-shutdown -serial chardev:serial0 %s",
                                codeparam, code ? codetmp : "",
                                test->machine, serialtmp, test->extra);
    unlink(serialtmp);
    if (code) {
        unlink(codetmp);
    }

    check_guest_output(test, ser_fd);
    qtest_quit(global_qtest);

    close(ser_fd);
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
