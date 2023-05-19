/*
 *  Copyright(c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Test the VLIW semantics of exceptions with mem_noshuf
 *
 * When a packet has the :mem_noshuf attribute, the semantics dictate
 * That the load will get the data from the store if the addresses overlap.
 * To accomplish this, we perform the store first.  However, we have to
 * handle the case where the store raises an exception.  In that case, the
 * store should not alter the machine state.
 *
 * We test this with a mem_noshuf packet with a store to a global variable,
 * "should_not_change" and a load from NULL.  After the SIGSEGV is caught,
 * we check * that the "should_not_change" value is the same.
 *
 * We also check that a predicated load where the predicate is false doesn't
 * raise an exception and allows the store to happen.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>

int err;

#include "hex_test.h"

bool segv_caught;

#define SHOULD_NOT_CHANGE_VAL        5
int32_t should_not_change = SHOULD_NOT_CHANGE_VAL;

#define OK_TO_CHANGE_VAL        13
int32_t ok_to_change = OK_TO_CHANGE_VAL;

jmp_buf jmp_env;

static void sig_segv(int sig, siginfo_t *info, void *puc)
{
    check32(sig, SIGSEGV);
    segv_caught = true;
    longjmp(jmp_env, 1);
}

int main()
{
    struct sigaction act;
    int32_t dummy32;
    int64_t dummy64;
    void *p;

    /* SIGSEGV test */
    act.sa_sigaction = sig_segv;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    chk_error(sigaction(SIGSEGV, &act, NULL));
    if (setjmp(jmp_env) == 0) {
        asm volatile("r18 = ##should_not_change\n\t"
                     "r19 = #0\n\t"
                     "{\n\t"
                     "    memw(r18) = #7\n\t"
                     "    %0 = memw(r19)\n\t"
                     "}:mem_noshuf\n\t"
                      : "=r"(dummy32) : : "r18", "r19", "memory");
    }

    act.sa_handler = SIG_DFL;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    chk_error(sigaction(SIGSEGV, &act, NULL));

    check32(segv_caught, true);
    check32(should_not_change, SHOULD_NOT_CHANGE_VAL);

    /*
     * Check that a predicated load where the predicate is false doesn't
     * raise an exception and allows the store to happen.
     */
    asm volatile("r18 = ##ok_to_change\n\t"
                 "r19 = #0\n\t"
                 "p0 = cmp.gt(r0, r0)\n\t"
                 "{\n\t"
                 "    memw(r18) = #7\n\t"
                 "    if (p0) %0 = memw(r19)\n\t"
                 "}:mem_noshuf\n\t"
                  : "=r"(dummy32) : : "r18", "r19", "p0", "memory");

    check32(ok_to_change, 7);

    /*
     * Also check that the post-increment doesn't happen when the
     * predicate is false.
     */
    ok_to_change = OK_TO_CHANGE_VAL;
    p = NULL;
    asm volatile("r18 = ##ok_to_change\n\t"
                 "p0 = cmp.gt(r0, r0)\n\t"
                 "{\n\t"
                 "    memw(r18) = #9\n\t"
                 "    if (p0) %1 = memd(%0 ++ #8)\n\t"
                 "}:mem_noshuf\n\t"
                  : "+r"(p), "=r"(dummy64) : : "r18", "p0", "memory");

    check32(ok_to_change, 9);
    check32((int)p, (int)NULL);

    puts(err ? "FAIL" : "PASS");
    return err ? EXIT_FAILURE : EXIT_SUCCESS;
}
