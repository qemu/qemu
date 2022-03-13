/*
 *  Copyright(c) 2021-2022 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>


int err;

static void __check(const char *filename, int line, int x, int expect)
{
    if (x != expect) {
        printf("ERROR %s:%d - %d != %d\n",
               filename, line, x, expect);
        err++;
    }
}

#define check(x, expect) __check(__FILE__, __LINE__, (x), (expect))

static int satub(int src, int *p, int *ovf_result)
{
    int result;
    int usr;

    /*
     * This instruction can set bit 0 (OVF/overflow) in usr
     * Clear the bit first, then return that bit to the caller
     *
     * We also store the src into *p in the same packet, so we
     * can ensure the overflow doesn't get set when an exception
     * is generated.
     */
    asm volatile("r2 = usr\n\t"
                 "r2 = clrbit(r2, #0)\n\t"        /* clear overflow bit */
                 "usr = r2\n\t"
                 "{\n\t"
                 "    %0 = satub(%2)\n\t"
                 "    memw(%3) = %2\n\t"
                 "}\n\t"
                 "%1 = usr\n\t"
                 : "=r"(result), "=r"(usr)
                 : "r"(src), "r"(p)
                 : "r2", "usr", "memory");
  *ovf_result = (usr & 1);
  return result;
}

int read_usr_overflow(void)
{
    int result;
    asm volatile("%0 = usr\n\t" : "=r"(result));
    return result & 1;
}

int get_usr_overflow(int usr)
{
    return usr & 1;
}

int get_usr_fp_invalid(int usr)
{
    return (usr >> 1) & 1;
}

int get_usr_lpcfg(int usr)
{
    return (usr >> 8) & 0x3;
}

jmp_buf jmp_env;
int usr_overflow;

static void sig_segv(int sig, siginfo_t *info, void *puc)
{
    usr_overflow = read_usr_overflow();
    longjmp(jmp_env, 1);
}

static void test_packet(void)
{
    int convres;
    int satres;
    int usr;

    asm("r2 = usr\n\t"
        "r2 = clrbit(r2, #0)\n\t"        /* clear overflow bit */
        "r2 = clrbit(r2, #1)\n\t"        /* clear FP invalid bit */
        "usr = r2\n\t"
        "{\n\t"
        "    %0 = convert_sf2uw(%3):chop\n\t"
        "    %1 = satb(%4)\n\t"
        "}\n\t"
        "%2 = usr\n\t"
        : "=r"(convres), "=r"(satres), "=r"(usr)
        : "r"(0x6a051b86), "r"(0x0410eec0)
        : "r2", "usr");

    check(convres, 0xffffffff);
    check(satres, 0x7f);
    check(get_usr_overflow(usr), 1);
    check(get_usr_fp_invalid(usr), 1);

    asm("r2 = usr\n\t"
        "r2 = clrbit(r2, #0)\n\t"        /* clear overflow bit */
        "usr = r2\n\t"
        "%2 = r2\n\t"
        "p3 = sp3loop0(1f, #1)\n\t"
        "1:\n\t"
        "{\n\t"
        "    %0 = satb(%2)\n\t"
        "}:endloop0\n\t"
        "%1 = usr\n\t"
        : "=r"(satres), "=r"(usr)
        : "r"(0x0410eec0)
        : "r2", "usr", "p3", "sa0", "lc0");

    check(satres, 0x7f);
    check(get_usr_overflow(usr), 1);
    check(get_usr_lpcfg(usr), 2);
}

int main()
{
    struct sigaction act;
    int ovf;

    /* SIGSEGV test */
    act.sa_sigaction = sig_segv;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &act, NULL);
    if (setjmp(jmp_env) == 0) {
        satub(300, 0, &ovf);
    }

    act.sa_handler = SIG_DFL;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    check(usr_overflow, 0);

    test_packet();

    puts(err ? "FAIL" : "PASS");
    return err ? EXIT_FAILURE : EXIT_SUCCESS;
}
