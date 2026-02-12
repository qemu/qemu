/*
 * Test DIEBR and DIDBR instructions.
 *
 * Most inputs were discovered by fuzzing and exercise various corner cases in
 * the helpers.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <asm/ucontext.h>

static void sigfpe_handler(int sig, siginfo_t *info, void *puc)
{
    struct ucontext *uc = puc;
    unsigned short *xr_insn;
    int r;

    xr_insn = (unsigned short *)(uc->uc_mcontext.regs.psw.addr - 6);
    r = *xr_insn & 0xf;
    uc->uc_mcontext.regs.gprs[r] = sig;
}

#define DIVIDE_TO_INTEGER(name, floatN)                                        \
static inline __attribute__((__always_inline__)) int                           \
name(floatN *r1, floatN r2, floatN *r3, int m4, int *sig)                      \
{                                                                              \
    int cc;                                                                    \
                                                                               \
    asm(/* Make the initial CC predictable for suppression tests */            \
        "xr %[sig],%[sig]\n"                                                   \
        #name " %[r1],%[r3],%[r2],%[m4]\n"                                     \
        "ipm %[cc]\n"                                                          \
        "srl %[cc],28"                                                         \
        /*                                                                     \
         * Use earlyclobbers to prevent the compiler from reusing floating     \
         * point registers. This instruction doesn't like it.                  \
         */                                                                    \
        : [r1] "+&f" (*r1), [r3] "+&f" (*r3), [sig] "=r" (*sig), [cc] "=d" (cc)\
        : [r2] "f" (r2), [m4] "i" (m4)                                         \
        : "cc");                                                               \
                                                                               \
    return cc;                                                                 \
}

DIVIDE_TO_INTEGER(diebr, float)
DIVIDE_TO_INTEGER(didbr, double)

#define TEST_DIVIDE_TO_INTEGER(name, intN, int_fmt, floatN, float_fmt)         \
static inline __attribute__((__always_inline__)) int                           \
test_ ## name(unsigned intN r1i, unsigned intN r2i, int m4, int fpc,           \
              unsigned intN r1o, unsigned intN r3o, int cco, unsigned int fpco,\
              int sigo)                                                        \
{                                                                              \
    union {                                                                    \
        floatN f;                                                              \
        unsigned intN i;                                                       \
    } r1, r2, r3;                                                              \
    int cc, err = 0, sig;                                                      \
                                                                               \
    r1.i = r1i;                                                                \
    r2.i = r2i;                                                                \
    r3.i = 0x12345678;                                                         \
    printf("[ RUN      ] %" float_fmt "(0x%" int_fmt                           \
           ") / %" float_fmt "(0x%" int_fmt ")\n", r1.f, r1.i, r2.f, r2.i);    \
    asm volatile("sfpc %[fpc]" : : [fpc] "r" (fpc));                           \
    cc = name(&r1.f, r2.f, &r3.f, m4, &sig);                                   \
    asm volatile("stfpc %[fpc]" : [fpc] "=Q" (fpc));                           \
    if (r1.i != r1o) {                                                         \
        printf("[  FAILED  ] remainder 0x%" int_fmt                            \
               " != expected 0x%" int_fmt "\n", r1.i, r1o);                    \
        err += 1;                                                              \
    }                                                                          \
    if (r3.i != r3o) {                                                         \
        printf("[  FAILED  ] quotient 0x%" int_fmt                             \
               " != expected 0x%" int_fmt "\n", r3.i, r3o);                    \
        err += 1;                                                              \
    }                                                                          \
    if (cc != cco) {                                                           \
        printf("[  FAILED  ] cc %d != expected %d\n", cc, cco);                \
        err += 1;                                                              \
    }                                                                          \
    if (fpc != fpco) {                                                         \
        printf("[  FAILED  ] fpc 0x%x != expected 0x%x\n", fpc, fpco);         \
        err += 1;                                                              \
    }                                                                          \
    if (sig != sigo) {                                                         \
        printf("[  FAILED  ] signal 0x%x != expected 0x%x\n", sig, sigo);      \
        err += 1;                                                              \
    }                                                                          \
                                                                               \
    return err;                                                                \
}

TEST_DIVIDE_TO_INTEGER(diebr, int, "x", float, "f")
TEST_DIVIDE_TO_INTEGER(didbr, long, "lx", double, "lf")

int main(void)
{
    struct sigaction act = {
        .sa_sigaction = sigfpe_handler,
        .sa_flags = SA_SIGINFO,
    };
    int err = 0;

    /* Set up SIG handler */
    if (sigaction(SIGFPE, &act, NULL)) {
        printf("[  FAILED  ] sigaction(SIGFPE) failed\n");
        return EXIT_FAILURE;
    }

    /* 451 / 460 */
    err += test_diebr(0x43e1f1f1, 0x43e61616, 7, 0,
                      0x43e1f1f1, 0, 0, 0, 0);

    /* 480 / 0 */
    err += test_diebr(0x43f00000, 0, 0, 0,
                      0x7fc00000, 0x7fc00000, 1, 0x800000, 0);

    /* QNaN / QNaN */
    err += test_diebr(0xffffffff, 0xffffffff, 0, 0,
                      0xffffffff, 0xffffffff, 1, 0, 0);

    /* -2.08E-8 / -2.08E-8 */
    err += test_diebr(0xb2b2b2b2, 0xb2b2b2b2, 0, 0,
                      0x80000000, 0x3f800000, 0, 0, 0);

    /*
     * Test partial remainder without quotient scaling (cc2).
     *
     * a = 12401981 / 268435456
     * b = -5723991 / 72057594037927936
     * q = a / b = -3329131425038336 / 5723991 =~ -581610178.1
     * n = round(q, float32, nearest_even) = -581610176
     * r_precise = a - b * n = 189155 / 1125899906842624
     * r = round(r_precise, float32, nearest_even) = r_precise
     */
    err += test_diebr(0x3d3d3d3d, 0xaeaeaeae, 0, 0,
                      0x2f38b8c0, 0xce0aaaab, 2, 0, 0);

    /* 1.07E-31 / 2.19 */
    err += test_diebr(0x0c0c0c0c, 0x400c0c0c, 6, 0,
                      0xc00c0c0c, 0x3f800000, 0, 0x80000, 0);

    /*
     * Test partial remainder with quotient scaling (cc3).
     *
     * a = 298343530578310714772108083200
     * b = -592137/10384593717069655257060992658440192
     * q = a / b
     *   = -1032725451057301340137043014721780674141077289604872315653324800 /
     *     197379
     *   =~ -5232195173029052432817285601415452880707052369324357280426.6
     * n = round(q, float32, nearest_even)
     *   = -5232194943010009439437691768433469154159343131709361094656
     * n / 2^192 = -6992213 / 8388608
     * r_precise = a - b * n = 13115851209189604982784
     * r = round(r_precise, float32, nearest_even) = r_precise
     */
    err += test_diebr(0x7070ffff, 0x90909090, 0, 0,
                      0x6431c0c0, 0xbf5562aa, 3, 0, 0);

    /*
     * Test large, but representable quotient.
     *
     * a = -12040119 / 549755813888
     * b = 1 / 38685626227668133590597632
     * q = a / b = -847248053779631702016
     * n = round(q, float32, to_odd) = q
     * r_precise = a - b * n = -0
     * r = round(r_precise, float32, nearest_even) = -0
     */
    err += test_diebr(0xb7b7b7b7, 0x15000000, 7, 0,
                      0x80000000, 0xe237b7b7, 0, 0, 0);

    /* 0 / 0 */
    err += test_diebr(0, 0, 1, 0,
                      0x7fc00000, 0x7fc00000, 1, 0x800000, 0);

    /* 4.3E-33 / -2.08E-8 with SIGFPE */
    err += test_diebr(0x09b2b2b2, 0xb2b2b2b2, 0, 0xfc000007,
                      0xb2b2b2b1, 0xbf800000, 0, 0xfc000807, SIGFPE);

    /*
     * Test tiny remainder scaling when FPC Underflow Mask is set.
     *
     * 1.19E-39 / -1.28E-9 = { r = 1.19E-39 * 2^192, n = -0 }
     */
    err += test_diebr(0x000d0100, 0xb0b0b0b0, 6, 0xfc000000,
                      0x5ed01000, 0x80000000, 0, 0xfc001000, SIGFPE);

    /*
     * Test "inexact and incremented" DXC.
     *
     * a = 53555504
     * b = -520849213389117849600
     * q = a / b = -3347219 / 32553075836819865600
     * n = round(q, float32, to_odd) = -1
     * r_precise = a - b * n = -520849213389064294096
     * r = round(r_precise, float32, to_odd) = -520849213389117849600
     * abs(r) - abs(r_precise) = 53555504
     */
    err += test_diebr(0x4c4c4c4c, 0xe1e1e1e1, 0, 0xfc000007,
                      0xe1e1e1e1, 0xbf800000, 0, 0xfc000c07, SIGFPE);

    /* 0 / 0 with SIGFPE */
    err += test_diebr(0, 0, 0, 0xfc000007,
                      0, 0x12345678, 0, 0xfc008007, SIGFPE);

    /* 5.76E-16 / 5.39E+34 */
    err += test_diebr(0x26262626, 0x79262626, 6, 0,
                      0xf9262626, 0x3f800000, 0, 0x80000, 0);

    /* -4.97E+17 / 2.03E-38 */
    err += test_diebr(0xdcdcdcdc, 0x00dcdcdc, 7, 0xfc000000,
                      0x80000000, 0xbb800000, 1, 0xfc000000, 0);

    /* -1.23E+17 / SNaN */
    err += test_diebr(0xdbdb240b, 0xffac73ff, 4, 0,
                      0xffec73ff, 0xffec73ff, 1, 0x800000, 0);

    /* 2.34E-38 / 3.27E-33 with SIGFPE */
    err += test_diebr(0x00ff0987, 0x0987c6f6, 6, 0x08000000,
                      0x8987c6b6, 0x3f800000, 0, 0x8000800, SIGFPE);

    /* -5.93E+11 / -2.7E+4 */
    err += test_diebr(0xd30a0040, 0xc6d30a00, 0, 0xc4000000,
                      0xc74a4400, 0x4ba766c6, 2, 0xc4000000, 0);

    /* 9.86E-32 / -inf */
    err += test_diebr(0x0c000029, 0xff800000, 0, 0,
                      0xc000029, 0x80000000, 0, 0, 0);

    /* QNaN / SNaN */
    err += test_diebr(0xffff94ff, 0xff94ff24, 4, 7,
                      0xffd4ff24, 0xffd4ff24, 1, 0x800007, 0);

    /* 2.8E-43 / -inf */
    err += test_diebr(0x000000c8, 0xff800000, 0, 0x7c000007,
                      0x000000c8, 0x80000000, 0, 0x7c000007, 0);

    /* -1.7E+38 / -inf */
    err += test_diebr(0xff00003d, 0xff800000, 0, 0,
                      0xff00003d, 0, 0, 0, 0);

    /* 1.94E-304 / 1.94E-304 */
    err += test_didbr(0x00e100e100e100e1, 0x00e100e100e100e1, 0, 1,
                      0, 0x3ff0000000000000, 0, 1, 0);

    /* 4.82E-299 / 5.29E-308 */
    err += test_didbr(0x0200230200230200, 0x0023020023020023, 0, 0,
                      0x8001a017d247b3f4, 0x41cb2aa05f000000, 0, 0, 0);

    /* -1.38E-75 / -3.77E+208 */
    err += test_didbr(0xb063eb3d63b063eb, 0xeb3d63b063eb3d63, 3, 0xe8000000,
                      0x6b3d63b063eb3d63, 0x3ff0000000000000, 0, 0xe8000c00,
                      SIGFPE);

    /* 4.78E-299 / 6.88E-315 */
    err += test_didbr(0x0200000000000000, 0x0000000053020000, 0, 0,
                      0x8000000020820000, 0x4338ac20dd47c6c1, 0, 0, 0);

    return err ? EXIT_FAILURE : EXIT_SUCCESS;
}
