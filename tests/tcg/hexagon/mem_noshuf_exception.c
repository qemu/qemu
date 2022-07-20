/*
 *  Copyright(c) 2022 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>

int err;
int segv_caught;

#define SHOULD_NOT_CHANGE_VAL        5
int should_not_change = SHOULD_NOT_CHANGE_VAL;

#define OK_TO_CHANGE_VAL        13
int ok_to_change = OK_TO_CHANGE_VAL;

static void __check(const char *filename, int line, int x, int expect)
{
    if (x != expect) {
        printf("ERROR %s:%d - %d != %d\n",
               filename, line, x, expect);
        err++;
    }
}

#define check(x, expect) __check(__FILE__, __LINE__, (x), (expect))

static void __chk_error(const char *filename, int line, int ret)
{
    if (ret < 0) {
        printf("ERROR %s:%d - %d\n", filename, line, ret);
        err++;
    }
}

#define chk_error(ret) __chk_error(__FILE__, __LINE__, (ret))

jmp_buf jmp_env;

static void sig_segv(int sig, siginfo_t *info, void *puc)
{
    check(sig, SIGSEGV);
    segv_caught = 1;
    longjmp(jmp_env, 1);
}

int main()
{
    struct sigaction act;
    int dummy32;
    long long dummy64;
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

    check(segv_caught, 1);
    check(should_not_change, SHOULD_NOT_CHANGE_VAL);

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

    check(ok_to_change, 7);

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

    check(ok_to_change, 9);
    check((int)p, (int)NULL);

    puts(err ? "FAIL" : "PASS");
    return err ? EXIT_FAILURE : EXIT_SUCCESS;
}
