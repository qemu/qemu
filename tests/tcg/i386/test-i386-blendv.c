/*
 * Check that the legacy BLENDVPS, BLENDVPD and PBLENDVB exist, while the
 * VEX encodings in the same opcodes do not (VBLENDVPS and the others are in
 * map 0F 3A as they add *two* register operands, and the second is encoded
 * in an immediate byte).
 */
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static sigjmp_buf jmp_env;

static void sigill_handler(int sig)
{
    siglongjmp(jmp_env, 1);
}

/* 66 0F 38 xx with modrm = 11 001 001 (xmm1, xmm1 operands).  */
#define SSE_INSN(op) asm volatile(".byte 0x66, 0x0f, 0x38, " #op ", 0xc9" \
                                     : : : "xmm0", "xmm1")

/* Same but encoded with a VEX3 prefix: RXB=111b, mmmmm=2 (0F38), w=0,
 * VVVV=1111b (forced because QEMU does not list an Hx operand),
 * l=0 (128-bit), pp=1 (66h).  */
#define VEX_INSN(op) asm volatile(".byte 0xc4, 0xe2, 0x79, " #op ", 0xc9")

#define CHECK_VEX_ILLEGAL(op)                                         \
    do {                                                              \
        if (sigsetjmp(jmp_env, 1) == 0) {                             \
            VEX_INSN(op);                                             \
            printf("VEX " #op " did not fault\n");                    \
            exit(1);                                                  \
        }                                                             \
    } while (0)

int main(void)
{
    struct sigaction act = { .sa_handler = sigill_handler };

    sigaction(SIGILL, &act, NULL);

    SSE_INSN(0x10);
    SSE_INSN(0x14);
    SSE_INSN(0x15);

    CHECK_VEX_ILLEGAL(0x10);
    CHECK_VEX_ILLEGAL(0x14);
    CHECK_VEX_ILLEGAL(0x15);
    return 0;
}
