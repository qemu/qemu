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

static const uint8_t kernel_pls3adsp1800[] = {
    0xb0, 0x00, 0x84, 0x00,                 /* imm   0x8400 */
    0x30, 0x60, 0x00, 0x04,                 /* addik r3,r0,4 */
    0x30, 0x80, 0x00, 0x54,                 /* addik r4,r0,'T' */
    0xf0, 0x83, 0x00, 0x00,                 /* sbi   r4,r3,0 */
    0xb8, 0x00, 0xff, 0xfc                  /* bri   -4  loop */
};

static const uint8_t kernel_plml605[] = {
    0xe0, 0x83, 0x00, 0xb0,                 /* imm   0x83e0 */
    0x00, 0x10, 0x60, 0x30,                 /* addik r3,r0,0x1000 */
    0x54, 0x00, 0x80, 0x30,                 /* addik r4,r0,'T' */
    0x00, 0x00, 0x83, 0xf0,                 /* sbi   r4,r3,0 */
    0xfc, 0xff, 0x00, 0xb8                  /* bri   -4  loop */
};

static const uint8_t bios_moxiesim[] = {
    0x20, 0x10, 0x00, 0x00, 0x03, 0xf8,     /* ldi.s r1,0x3f8 */
    0x1b, 0x20, 0x00, 0x00, 0x00, 0x54,     /* ldi.b r2,'T' */
    0x1e, 0x12,                             /* st.b  r1,r2 */
    0x1a, 0x00, 0x00, 0x00, 0x10, 0x00      /* jmpa  0x1000 */
};

static const uint8_t bios_raspi2[] = {
    0x08, 0x30, 0x9f, 0xe5,                 /* ldr   r3,[pc,#8]    Get base */
    0x54, 0x20, 0xa0, 0xe3,                 /* mov     r2,#'T' */
    0x00, 0x20, 0xc3, 0xe5,                 /* strb    r2,[r3] */
    0xfb, 0xff, 0xff, 0xea,                 /* b       loop */
    0x00, 0x10, 0x20, 0x3f,                 /* 0x3f201000 = UART0 base addr */
};

static const uint8_t kernel_aarch64[] = {
    0x81, 0x0a, 0x80, 0x52,                 /* mov     w1, #0x54 */
    0x02, 0x20, 0xa1, 0xd2,                 /* mov     x2, #0x9000000 */
    0x41, 0x00, 0x00, 0x39,                 /* strb    w1, [x2] */
    0xfd, 0xff, 0xff, 0x17,                 /* b       -12 (loop) */
};

static const uint8_t kernel_nrf51[] = {
    0x00, 0x00, 0x00, 0x00,                 /* Stack top address */
    0x09, 0x00, 0x00, 0x00,                 /* Reset handler address */
    0x04, 0x4a,                             /* ldr  r2, [pc, #16] Get ENABLE */
    0x04, 0x21,                             /* movs r1, #4 */
    0x11, 0x60,                             /* str  r1, [r2] */
    0x04, 0x4a,                             /* ldr  r2, [pc, #16] Get STARTTX */
    0x01, 0x21,                             /* movs r1, #1 */
    0x11, 0x60,                             /* str  r1, [r2] */
    0x03, 0x4a,                             /* ldr  r2, [pc, #12] Get TXD */
    0x54, 0x21,                             /* movs r1, 'T' */
    0x11, 0x60,                             /* str  r1, [r2] */
    0xfe, 0xe7,                             /* b    . */
    0x00, 0x25, 0x00, 0x40,                 /* 0x40002500 = UART ENABLE */
    0x08, 0x20, 0x00, 0x40,                 /* 0x40002008 = UART STARTTX */
    0x1c, 0x25, 0x00, 0x40                  /* 0x4000251c = UART TXD */
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
    { "ppc", "40p", "-vga none -boot d", "Trying cd:," },
    { "ppc", "g3beige", "", "PowerPC,750" },
    { "ppc", "mac99", "", "PowerPC,G4" },
    { "ppc", "sam460ex", "-m 256", "DRAM:  256 MiB" },
    { "ppc64", "ppce500", "", "U-Boot" },
    { "ppc64", "40p", "-m 192", "Memory: 192M" },
    { "ppc64", "mac99", "", "PowerPC,970FX" },
    { "ppc64", "pseries", "", "Open Firmware" },
    { "ppc64", "powernv", "-cpu POWER8", "OPAL" },
    { "ppc64", "sam460ex", "-device e1000", "8086  100e" },
    { "i386", "isapc", "-cpu qemu32 -device sga", "SGABIOS" },
    { "i386", "pc", "-device sga", "SGABIOS" },
    { "i386", "q35", "-device sga", "SGABIOS" },
    { "x86_64", "isapc", "-cpu qemu32 -device sga", "SGABIOS" },
    { "x86_64", "q35", "-device sga", "SGABIOS" },
    { "sparc", "LX", "", "TMS390S10" },
    { "sparc", "SS-4", "", "MB86904" },
    { "sparc", "SS-600MP", "", "TMS390Z55" },
    { "sparc64", "sun4u", "", "UltraSPARC" },
    { "s390x", "s390-ccw-virtio", "", "virtio device" },
    { "m68k", "mcf5208evb", "", "TT", sizeof(kernel_mcf5208), kernel_mcf5208 },
    { "microblaze", "petalogix-s3adsp1800", "", "TT",
      sizeof(kernel_pls3adsp1800), kernel_pls3adsp1800 },
    { "microblazeel", "petalogix-ml605", "", "TT",
      sizeof(kernel_plml605), kernel_plml605 },
    { "moxie", "moxiesim", "", "TT", sizeof(bios_moxiesim), 0, bios_moxiesim },
    { "arm", "raspi2", "", "TT", sizeof(bios_raspi2), 0, bios_raspi2 },
    { "hppa", "hppa", "", "SeaBIOS wants SYSTEM HALT" },
    { "aarch64", "virt", "-cpu cortex-a57", "TT", sizeof(kernel_aarch64),
      kernel_aarch64 },
    { "arm", "microbit", "", "T", sizeof(kernel_nrf51), kernel_nrf51 },

    { NULL }
};

static bool check_guest_output(const testdef_t *test, int fd)
{
    int i, nbr = 0, pos = 0, ccnt;
    char ch;

    /* Poll serial output... Wait at most 360 seconds */
    for (i = 0; i < 36000; ++i) {
        ccnt = 0;
        while (ccnt++ < 512 && (nbr = read(fd, &ch, 1)) == 1) {
            if (ch == test->expect[pos]) {
                pos += 1;
                if (test->expect[pos] == '\0') {
                    /* We've reached the end of the expected string! */
                    return true;
                }
            } else {
                pos = 0;
            }
        }
        g_assert(nbr >= 0);
        g_usleep(10000);
    }

    return false;
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
    global_qtest = qtest_initf("%s %s -M %s,accel=tcg:kvm "
                               "-chardev file,id=serial0,path=%s "
                               "-no-shutdown -serial chardev:serial0 %s",
                               codeparam, code ? codetmp : "",
                               test->machine, serialtmp, test->extra);
    if (code) {
        unlink(codetmp);
    }

    if (!check_guest_output(test, ser_fd)) {
        g_error("Failed to find expected string. Please check '%s'",
                serialtmp);
    }
    unlink(serialtmp);

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
