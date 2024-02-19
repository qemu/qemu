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
#include "libqos/libqos-spapr.h"

static const uint8_t bios_avr[] = {
    0x88, 0xe0,             /* ldi r24, 0x08   */
    0x80, 0x93, 0xc1, 0x00, /* sts 0x00C1, r24 ; Enable tx */
    0x86, 0xe0,             /* ldi r24, 0x06   */
    0x80, 0x93, 0xc2, 0x00, /* sts 0x00C2, r24 ; Set the data bits to 8 */
    0x84, 0xe5,             /* ldi r24, 0x54   */
    0x80, 0x93, 0xc6, 0x00, /* sts 0x00C6, r24 ; Output 'T' */
};

static const uint8_t kernel_mcf5208[] = {
    0x41, 0xf9, 0xfc, 0x06, 0x00, 0x00,     /* lea 0xfc060000,%a0 */
    0x10, 0x3c, 0x00, 0x54,                 /* move.b #'T',%d0 */
    0x11, 0x7c, 0x00, 0x04, 0x00, 0x08,     /* move.b #4,8(%a0)     Enable TX */
    0x11, 0x40, 0x00, 0x0c,                 /* move.b %d0,12(%a0)   Print 'T' */
    0x60, 0xfa                              /* bra.s  loop */
};

static const uint8_t bios_nextcube[] = {
    0x06, 0x00, 0x00, 0x00,                 /* Initial SP */
    0x01, 0x00, 0x00, 0x08,                 /* Initial PC */
    0x41, 0xf9, 0x02, 0x11, 0x80, 0x00,     /* lea 0x02118000,%a0 */
    0x10, 0x3c, 0x00, 0x54,                 /* move.b #'T',%d0 */
    0x11, 0x7c, 0x00, 0x05, 0x00, 0x01,     /* move.b #5,1(%a0)    Sel TXCTRL */
    0x11, 0x7c, 0x00, 0x68, 0x00, 0x01,     /* move.b #0x68,1(%a0) Enable TX */
    0x11, 0x40, 0x00, 0x03,                 /* move.b %d0,3(%a0)   Print 'T' */
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

static const uint8_t kernel_stm32vldiscovery[] = {
    0x00, 0x00, 0x00, 0x00,                 /* Stack top address */
    0x1d, 0x00, 0x00, 0x00,                 /* Reset handler address */
    0x00, 0x00, 0x00, 0x00,                 /* NMI */
    0x00, 0x00, 0x00, 0x00,                 /* Hard fault */
    0x00, 0x00, 0x00, 0x00,                 /* Memory management fault */
    0x00, 0x00, 0x00, 0x00,                 /* Bus fault */
    0x00, 0x00, 0x00, 0x00,                 /* Usage fault */
    0x0b, 0x4b,                             /* ldr  r3, [pc, #44] Get RCC */
    0x44, 0xf2, 0x04, 0x02,                 /* movw r2, #16388 */
    0x1a, 0x60,                             /* str  r2, [r3] */
    0x0a, 0x4b,                             /* ldr  r3, [pc, #40] Get GPIOA */
    0x1a, 0x68,                             /* ldr  r2, [r3] */
    0x22, 0xf0, 0xf0, 0x02,                 /* bic  r2, r2, #240 */
    0x1a, 0x60,                             /* str  r2, [r3] */
    0x1a, 0x68,                             /* ldr  r2, [r3] */
    0x42, 0xf0, 0xb0, 0x02,                 /* orr  r2, r2, #176 */
    0x1a, 0x60,                             /* str  r2, [r3] */
    0x07, 0x4b,                             /* ldr  r3, [pc, #26] Get BAUD */
    0x45, 0x22,                             /* movs r2, #69 */
    0x1a, 0x60,                             /* str  r2, [r3] */
    0x06, 0x4b,                             /* ldr  r3, [pc, #24] Get ENABLE */
    0x42, 0xf2, 0x08, 0x02,                 /* movw r2, #8200 */
    0x1a, 0x60,                             /* str  r2, [r3] */
    0x05, 0x4b,                             /* ldr  r3, [pc, #20] Get TXD */
    0x54, 0x22,                             /* movs r2, 'T' */
    0x1a, 0x60,                             /* str  r2, [r3] */
    0xfe, 0xe7,                             /* b    . */
    0x18, 0x10, 0x02, 0x40,                 /* 0x40021018 = RCC */
    0x04, 0x08, 0x01, 0x40,                 /* 0x40010804 = GPIOA */
    0x08, 0x38, 0x01, 0x40,                 /* 0x40013808 = USART1 BAUD */
    0x0c, 0x38, 0x01, 0x40,                 /* 0x4001380c = USART1 ENABLE */
    0x04, 0x38, 0x01, 0x40                  /* 0x40013804 = USART1 TXD */
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

static const testdef_t tests[] = {
    { "alpha", "clipper", "", "PCI:" },
    { "avr", "arduino-duemilanove", "", "T", sizeof(bios_avr), NULL, bios_avr },
    { "avr", "arduino-mega-2560-v3", "", "T", sizeof(bios_avr), NULL, bios_avr},
    { "ppc", "ppce500", "", "U-Boot" },
    { "ppc", "40p", "-vga none -boot d", "Trying cd:," },
    { "ppc", "g3beige", "", "PowerPC,750" },
    { "ppc", "mac99", "", "PowerPC,G4" },
    { "ppc", "sam460ex", "-m 256", "DRAM:  256 MiB" },
    { "ppc64", "ppce500", "", "U-Boot" },
    { "ppc64", "40p", "-m 192", "Memory: 192M" },
    { "ppc64", "mac99", "", "PowerPC,970FX" },
    { "ppc64", "pseries",
      "-machine " PSERIES_DEFAULT_CAPABILITIES,
      "Open Firmware" },
    { "ppc64", "powernv8", "", "OPAL" },
    { "ppc64", "powernv9", "", "OPAL" },
    { "ppc64", "sam460ex", "-device pci-bridge,chassis_nr=2", "1b36  0001" },
    { "i386", "isapc", "-cpu qemu32 -M graphics=off", "SeaBIOS" },
    { "i386", "pc", "-M graphics=off", "SeaBIOS" },
    { "i386", "q35", "-M graphics=off", "SeaBIOS" },
    { "x86_64", "isapc", "-cpu qemu32 -M graphics=off", "SeaBIOS" },
    { "x86_64", "q35", "-M graphics=off", "SeaBIOS" },
    { "sparc", "LX", "", "TMS390S10" },
    { "sparc", "SS-4", "", "MB86904" },
    { "sparc", "SS-600MP", "", "TMS390Z55" },
    { "sparc64", "sun4u", "", "UltraSPARC" },
    { "s390x", "s390-ccw-virtio", "", "device" },
    { "m68k", "mcf5208evb", "", "TT", sizeof(kernel_mcf5208), kernel_mcf5208 },
    { "m68k", "next-cube", "", "TT", sizeof(bios_nextcube), 0, bios_nextcube },
    { "microblaze", "petalogix-s3adsp1800", "", "TT",
      sizeof(kernel_pls3adsp1800), kernel_pls3adsp1800 },
    { "microblazeel", "petalogix-ml605", "", "TT",
      sizeof(kernel_plml605), kernel_plml605 },
    { "arm", "raspi2b", "", "TT", sizeof(bios_raspi2), 0, bios_raspi2 },
    /* For hppa, force bios to output to serial by disabling graphics. */
    { "hppa", "hppa", "-vga none", "SeaBIOS wants SYSTEM HALT" },
    { "aarch64", "virt", "-cpu max", "TT", sizeof(kernel_aarch64),
      kernel_aarch64 },
    { "arm", "microbit", "", "T", sizeof(kernel_nrf51), kernel_nrf51 },
    { "arm", "stm32vldiscovery", "", "T",
      sizeof(kernel_stm32vldiscovery), kernel_stm32vldiscovery },

    { NULL }
};

static bool check_guest_output(QTestState *qts, const testdef_t *test, int fd)
{
    int nbr = 0, pos = 0, ccnt;
    time_t now, start = time(NULL);
    char ch;

    /* Poll serial output... */
    while (1) {
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
        /* Wait only if the child is still alive.  */
        if (!qtest_probe_child(qts)) {
            break;
        }
        /* Wait at most 360 seconds.  */
        now = time(NULL);
        if (now - start >= 360) {
            break;
        }
        g_usleep(10000);
    }

    return false;
}

static void test_machine(const void *data)
{
    const testdef_t *test = data;
    g_autofree char *serialtmp = NULL;
    g_autofree char *codetmp = NULL;
    const char *codeparam = "";
    const uint8_t *code = NULL;
    QTestState *qts;
    int ser_fd;

    ser_fd = g_file_open_tmp("qtest-boot-serial-sXXXXXX", &serialtmp, NULL);
    g_assert(ser_fd != -1);
    close(ser_fd);

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

        code_fd = g_file_open_tmp("qtest-boot-serial-cXXXXXX", &codetmp, NULL);
        g_assert(code_fd != -1);
        wlen = write(code_fd, code, test->codesize);
        g_assert(wlen == test->codesize);
        close(code_fd);
    }

    /*
     * Make sure that this test uses tcg if available: It is used as a
     * fast-enough smoketest for that.
     */
    qts = qtest_initf("%s %s -M %s -no-shutdown "
                      "-chardev file,id=serial0,path=%s "
                      "-serial chardev:serial0 -accel tcg -accel kvm %s",
                      codeparam, code ? codetmp : "", test->machine,
                      serialtmp, test->extra);
    if (code) {
        unlink(codetmp);
    }

    ser_fd = open(serialtmp, O_RDONLY);
    g_assert(ser_fd != -1);
    if (!check_guest_output(qts, test, ser_fd)) {
        g_error("Failed to find expected string. Please check '%s'",
                serialtmp);
    }
    unlink(serialtmp);

    qtest_quit(qts);

    close(ser_fd);
}

int main(int argc, char *argv[])
{
    const char *arch = qtest_get_arch();
    int i;

    g_test_init(&argc, &argv, NULL);

    if (!qtest_has_accel("tcg") && !qtest_has_accel("kvm")) {
        g_test_skip("No KVM or TCG accelerator available");
        return 0;
    }

    for (i = 0; tests[i].arch != NULL; i++) {
        if (g_str_equal(arch, tests[i].arch) &&
            qtest_has_machine(tests[i].machine)) {
            char *name = g_strdup_printf("boot-serial/%s", tests[i].machine);
            qtest_add_data_func(name, &tests[i], test_machine);
            g_free(name);
        }
    }

    return g_test_run();
}
