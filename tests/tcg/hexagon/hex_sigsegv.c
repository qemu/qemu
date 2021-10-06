/*
 *  Copyright(c) 2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
 * Test the VLIW semantics of two stores in a packet
 *
 * When a packet has 2 stores, either both commit or neither commit.
 * We test this with a packet that does stores to both NULL and a global
 * variable, "should_not_change".  After the SIGSEGV is caught, we check
 * that the "should_not_change" value is the same.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>

typedef unsigned char uint8_t;

int err;
int segv_caught;

#define SHOULD_NOT_CHANGE_VAL        5
int should_not_change = SHOULD_NOT_CHANGE_VAL;

#define BUF_SIZE        300
unsigned char buf[BUF_SIZE];


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
                     "    memw(r19) = #0\n\t"
                     "}\n\t"
                      : : : "r18", "r19", "memory");
    }

    act.sa_handler = SIG_DFL;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    chk_error(sigaction(SIGSEGV, &act, NULL));

    check(segv_caught, 1);
    check(should_not_change, SHOULD_NOT_CHANGE_VAL);

    puts(err ? "FAIL" : "PASS");
    return err ? EXIT_FAILURE : EXIT_SUCCESS;
}
