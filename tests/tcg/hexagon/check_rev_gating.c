/*
 * Test that instructions from a newer revision than the running CPU
 * are rejected with SIGILL.
 *
 * Compiled with -mv66 so that e_flags selects CPU v66. The test embeds
 * instructions from v68 through v73 via .word encoding: the assembler
 * enforces the selected CPU's own minimum version, so none of these --
 * including ones it otherwise knows how to assemble at their own
 * target, such as callrh or unpause -- can be written as themselves in
 * a file built for v66. The revision-gated decoder must reject every
 * one of them, and linux-user must deliver SIGILL.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void *resume_pc;
static int signals_handled;
static int expected_signals;

static void handle_sigill(int sig, siginfo_t *info, void *puc)
{
    ucontext_t *uc = (ucontext_t *)puc;

    if (sig != SIGILL) {
        _exit(EXIT_FAILURE);
    }

    uc->uc_mcontext.r0 = SIGILL;
    uc->uc_mcontext.pc = (unsigned long)resume_pc;
    signals_handled++;
}

/*
 * Try to execute an instruction introduced after v66
 * On a v66 CPU this must raise SIGILL.
 *
 * Since we are building for v66, the assembler will reject
 * the instructions, so introduce them with .word.
 */
#define TRY_FUNC(NAME, WORD) \
static int try_##NAME(void) \
{ \
    int sig; \
    expected_signals++; \
    asm volatile( \
        "r0 = #0\n" \
        "r1 = ##1f\n" \
        "memw(%1) = r1\n" \
        WORD \
        "1:\n" \
        "%0 = r0\n" \
        : "=r"(sig) \
        : "r"(&resume_pc) \
        : "r0", "r1", "memory"); \
    return sig; \
}

TRY_FUNC(v68_loadw_aq,
         ".word 0x9200c800    /* { r0 = memw_aq(r0) } */\n")
TRY_FUNC(v68_loadd_aq,
         ".word 0x9201d800    /* r1:0 = memd_aq(r1) */\n")
TRY_FUNC(v68_release_at,
         ".word 0xa0e0c00c    /* release(r0):at */\n")
TRY_FUNC(v68_release_st,
         ".word 0xa0e0c02c    /* release(r0):st */\n")
TRY_FUNC(v68_storew_rl_at,
         ".word 0xa0a0c108    /* memw_rl(r0):at = r1 */\n")
TRY_FUNC(v68_stored_rl_at,
         ".word 0xa0e2c008    /* memd_rl(r2):at = r1:0 */\n")
TRY_FUNC(v68_storew_rl_st,
         ".word 0xa0a0c128    /* memw_rl(r0):st = r1 */\n")
TRY_FUNC(v68_stored_rl_st,
         ".word 0xa0e2c028    /* memd_rl(r2):st = r1:0 */\n")

TRY_FUNC(v68hvx_v6mpy,
         ".word 0x1f42e424    /* v5:4.w = v6mpy(v5:4.ub, v3:2.b, #1):v */\n")

TRY_FUNC(v69hvx_vasrvuhubrndsat,
         ".word 0x1d06c465    /* v5.ub = vasr(v5:4.uh, v6.ub):rnd:sat */\n")
TRY_FUNC(v69hvx_vasrvuhubsat,
         ".word 0x1d06c445    /* v5.ub = vasr(v5:4.uh, v6.ub):sat */\n")
TRY_FUNC(v69hvx_vasrvwuhrndsat,
         ".word 0x1d06c425    /* v5.uh = vasr(v5:4.w, v6.uh):rnd:sat */\n")
TRY_FUNC(v69hvx_vasrvwuhsat,
         ".word 0x1d06c405    /* v5.uh = vasr(v5:4.w, v6.uh):sat */\n")
TRY_FUNC(v69hvx_vassign_tmp,
         ".word 0x1e014dcc    /* { v12.tmp = v13 */\n"
         ".word 0x1c43cc04    /*    v4.w = vadd(v12.w, v3.w) } */\n")
TRY_FUNC(v69hvx_vcombine_tmp,
         ".word 0x1eae4fec    /* { v13:12.tmp = vcombine(v15, v14) */\n"
         ".word 0x1c434c04    /*   v4.w = vadd(v12.w, v3.w) */\n"
         ".word 0x1e03edf0    /*   v16 = v13 } */\n")
TRY_FUNC(v69hvx_vmpyuhvs,
         ".word 0x1fc5e4e4    /* v4.uh = vmpy(V4.uh, v5.uh):>>16 */\n")

TRY_FUNC(v73_callrh,
         ".word 0x50c5c000    /* callrh r5 */\n")
TRY_FUNC(v73_jumprh,
         ".word 0x52c0c000    /* jumprh r0 */\n")
TRY_FUNC(v73_unpause,
         ".word 0x57e0d000    /* unpause */\n")

int main(void)
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handle_sigill;
    act.sa_flags = SA_SIGINFO;
    assert(sigaction(SIGILL, &act, NULL) == 0);

    assert(try_v68_loadw_aq() == SIGILL);
    assert(try_v68_loadd_aq() == SIGILL);
    assert(try_v68_release_at() == SIGILL);
    assert(try_v68_release_st() == SIGILL);
    assert(try_v68_storew_rl_at() == SIGILL);
    assert(try_v68_stored_rl_at() == SIGILL);
    assert(try_v68_storew_rl_st() == SIGILL);
    assert(try_v68_stored_rl_st() == SIGILL);

    assert(try_v68hvx_v6mpy() == SIGILL);

    assert(try_v69hvx_vasrvuhubrndsat() == SIGILL);
    assert(try_v69hvx_vasrvuhubsat() == SIGILL);
    assert(try_v69hvx_vasrvwuhrndsat() == SIGILL);
    assert(try_v69hvx_vasrvwuhsat() == SIGILL);
    assert(try_v69hvx_vassign_tmp() == SIGILL);
    assert(try_v69hvx_vcombine_tmp() == SIGILL);
    assert(try_v69hvx_vmpyuhvs() == SIGILL);

    assert(try_v73_callrh() == SIGILL);
    assert(try_v73_jumprh() == SIGILL);
    assert(try_v73_unpause() == SIGILL);

    assert(signals_handled == expected_signals);

    puts("PASS");
    return EXIT_SUCCESS;
}
