/*
 * Cross-page TB chaining hazard test.
 *
 * Two adjacent pages of hand-written code.  The last instruction of page A
 * sets the return value and falls through into page B, which returns; a TB
 * always ends at a page boundary, so page A reaches page B through a
 * cross-page goto_tb.
 *
 * Phase 1: run it enough times that QEMU chains TB_A -> TB_B.
 * Phase 2: mprotect page B away. Re-running must fault.
 * Phase 3: map it back and write different code into it. Re-running must
 *          execute the NEW code, not a stale chained translation.
 *
 * With -b, phases 2 and 3 are replaced by a stop at break_here(), where the
 * gdbstub test sets a breakpoint on page B -- after the chain exists -- and
 * checks that re-running the chain still stops on it.  See
 * tests/tcg/multiarch/gdbstub/xpage-bp.py.
 *
 * The code the two pages hold is architecture specific, so each
 * architecture supplies two emitters:
 *
 *   emit_set_ret(p, val) - set the integer return value register to val
 *   emit_ret(p)          - return to the caller
 *
 * both writing at @p and returning the number of bytes written.  Neither
 * may contain a branch: the fall-through from page A into page B is the
 * whole point, and a delay slot must not straddle the boundary.  An
 * architecture that supplies neither skips the test.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>


#if defined(__aarch64__)
#define HAVE_EMITTERS
static size_t emit_set_ret(uint32_t *p, int val)
{
    /* movz w0, #val */
    *p = 0x52800000u | val << 5;
    return 4;
}
static size_t emit_ret(uint32_t *p)
{
    /* ret */
    *p = 0xd65f03c0u;
    return 4;
}
#elif defined(__alpha__)
#define HAVE_EMITTERS
static size_t emit_set_ret(uint32_t *p, int val)
{
    /* lda $0, val($31) */
    *p = 0x201f0000u | val;
    return 4;
}
static size_t emit_ret(uint32_t *p)
{
    /* ret */
    *p = 0x6bfa8001u;
    return 4;
}
#elif defined(__arm__)
#define HAVE_EMITTERS
static size_t emit_set_ret(uint32_t *p, int val)
{
    /* mov r0, #val */
    *p = 0xe3a00000u | val;
    return 4;
}
static size_t emit_ret(uint32_t *p)
{
    /* bx lr */
    *p = 0xe12fff1eu;
    return 4;
}
#elif defined(__hppa__)
#define HAVE_EMITTERS
static size_t emit_set_ret(uint32_t *p, int val)
{
    /* ldi val, %ret0 */
    *p = 0x341c0000u | val << 1;
    return 4;
}
static size_t emit_ret(uint32_t *p)
{
    p[0] = 0xe840c000u;  /* bv %r0(%rp) */
    p[1] = 0x08000240u;  /* nop (delay slot) */
    return 8;
}
#elif defined(__i386__) || defined(__x86_64__)
#define HAVE_EMITTERS
static size_t emit_set_ret(void *p, int val)
{
    /* mov $val, %eax */
    *(unsigned char *)p = 0xb8;
    *(int *)(p + 1) = val;
    return 5;
}
static size_t emit_ret(void *p)
{
    /* ret */
    *(unsigned char *)p = 0xc3;
    return 1;
}
#elif defined(__loongarch64)
#define HAVE_EMITTERS
static size_t emit_set_ret(uint32_t *p, int val)
{
    /* ori $a0, $zero, val */
    *p = 0x03800004u | val << 10;
    return 4;
}
static size_t emit_ret(uint32_t *p)
{
    /* jr $ra */
    *p = 0x4c000020u;
    return 4;
}
#elif defined(__m68k__)
#define HAVE_EMITTERS
static size_t emit_set_ret(void *p, int val)
{
    /* moveq #val, %d0 */
    *(uint16_t *)p = 0x7000u | val;
    return 2;
}
static size_t emit_ret(void *p)
{
    /* rts */
    *(uint16_t *)p = 0x4e75u;
    return 2;
}
#elif defined(__mips__)
#define HAVE_EMITTERS
static size_t emit_set_ret(uint32_t *p, int val)
{
    /* li $v0, val */
    *p = 0x24020000u | val;
    return 4;
}
static size_t emit_ret(uint32_t *p)
{
    p[0] = 0x03e00008u;  /* jr $ra */
    p[1] = 0x00000000u;  /* nop (delay slot) */
    return 8;
}
/*
 * ELFv1 function pointers are descriptors rather than code addresses, so
 * there is nothing to call the raw code through.
 */
#elif defined(__powerpc__) && \
      (!defined(__powerpc64__) || (defined(_CALL_ELF) && _CALL_ELF == 2))
#define HAVE_EMITTERS
static size_t emit_set_ret(uint32_t *p, int val)
{
    /* li r3, val */
    *p = 0x38600000u | val;
    return 4;
}
static size_t emit_ret(uint32_t *p)
{
    /* blr */
    *p = 0x4e800020u;
    return 4;
}
#elif defined(__riscv)
#define HAVE_EMITTERS
static size_t emit_set_ret(uint32_t *p, int val)
{
    /* addi a0, zero, val -- the 4 byte form */
    *p = 0x00000513u | val << 20;
    return 4;
}
static size_t emit_ret(uint32_t *p)
{
    /* jalr zero, 0(ra) */
    *p = 0x00008067u;
    return 4;
}
#elif defined(__s390x__)
#define HAVE_EMITTERS
static size_t emit_set_ret(void *p, int val)
{
    /* lghi %r2, val */
    uint16_t *p2 = p;
    p2[0] = 0xa729u;
    p2[1] = val;
    return 4;
}
static size_t emit_ret(void *p)
{
    /* br %r14 */
    *(uint16_t *)p = 0x07feu;
    return 2;
}
#elif defined(__sh__)
#define HAVE_EMITTERS
static size_t emit_set_ret(void *p, int val)
{
    /* mov #val, r0 */
    *(uint16_t *)p = 0xe000u | val;
    return 2;
}
static size_t emit_ret(void *p)
{
    uint16_t *p2 = p;
    p2[0] = 0x000bu;  /* rts */
    p2[1] = 0x0009u;  /* nop (delay slot) */
    return 4;
}
#elif defined(__sparc__)
#define HAVE_EMITTERS
static size_t emit_set_ret(uint32_t *p, int val)
{
    /* mov val, %o0 */
    *p = 0x90102000u | (val & 0x1fff);
    return 4;
}
static size_t emit_ret(uint32_t *p)
{
    p[0] = 0x81c3e008u;  /* retl */
    p[1] = 0x01000000u;  /* nop (delay slot) */
    return 8;
}
#endif

/* Where the fall-through lands, for the gdbstub test to breakpoint on. */
void *page_b_entry;

/* Somewhere for the gdbstub test to stop once the chain is established. */
void __attribute__((noinline)) break_here(void)
{
    asm volatile("");
}

#ifdef HAVE_EMITTERS
static sigjmp_buf jb;
/*
 * Written by the SIGSEGV handler and read by main(), so it must not be
 * cached in a register across the faulting call.
 */
static volatile sig_atomic_t caught;

static void segv(int sig)
{
    caught = 1;
    siglongjmp(jb, 1);
}
#endif

int main(int argc, char **argv)
{
    bool bp_mode = argc > 1 && strcmp(argv[1], "-b") == 0;
#ifndef HAVE_EMITTERS
    printf("SKIP: no code emitters for this architecture\n");
    if (bp_mode) {
        break_here();
    }
    return 0;
#else
    uint32_t tmp[4];
    struct sigaction sa;
    long (*fn)(void);
    size_t setlen, n;
    long ps = sysconf(_SC_PAGESIZE);
    int rc = 0;
    unsigned char *m = mmap(NULL, 2 * ps, PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) {
        perror("mmap");
        return 2;
    }

    void *pb = m + ps;

    /*
     * Page A ends with the store to the return value register, so that the
     * next instruction executed is the first one on page B.
     */
    setlen = emit_set_ret(tmp, 1);
    memcpy(pb - setlen, tmp, setlen);
    emit_ret(pb);
    __builtin___clear_cache((char *)m, (char *)m + 2 * ps);

    page_b_entry = pb;
    fn = (long (*)(void))(pb - setlen);

    for (int i = 0; i < 200000; i++) {
        if (fn() != 1) {
            printf("FAIL: phase 1 wrong result\n");
            return 1;
        }
    }
    printf("phase 1 ok (chained)\n");

    if (bp_mode) {
        /*
         * The chain from page A to page B now exists.  gdb puts a breakpoint
         * on page_b_entry here; the call below has to stop on it rather than
         * jump over it.
         */
        break_here();
        if (fn() != 1) {
            printf("FAIL: bp phase wrong result\n");
            return 1;
        }
        printf("bp phase ok\n");
        return 0;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = segv;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, NULL) != 0) {
        perror("sigaction");
        return 2;
    }
    if (mprotect(pb, ps, PROT_NONE) != 0) {
        perror("mprotect");
        return 2;
    }
    if (sigsetjmp(jb, 1) == 0) {
        fn();
        printf("FAIL: phase 2 executed page B after mprotect(PROT_NONE)\n");
        rc = 1;
    } else if (!caught) {
        printf("FAIL: phase 2 longjmp without entering the handler\n");
        rc = 1;
    } else {
        printf("phase 2 ok (faulted)\n");
    }

    /* Phase 3: map back, overwrite, expect the new code to run. */
    if (mprotect(pb, ps, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        perror("mprotect back");
        return 2;
    }
    n = emit_set_ret(pb, 2);
    emit_ret(pb + n);
    __builtin___clear_cache((char *)pb, (char *)pb + ps);

    long r = fn();
    if (r != 2) {
        printf("FAIL: phase 3 returned %ld, expected 2 (stale chain)\n", r);
        rc = 1;
    } else {
        printf("phase 3 ok (new code ran)\n");
    }
    return rc;
#endif
}
