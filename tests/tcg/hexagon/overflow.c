/*
 *  Copyright(c) 2021-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>

int err;

#include "hex_test.h"

static int32_t satub(int32_t src, int32_t *p, bool *ovf_result)
{
    int32_t result;
    uint32_t usr;

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

bool read_usr_overflow(void)
{
    uint32_t usr;
    asm volatile("%0 = usr\n\t" : "=r"(usr));
    return usr & 1;
}

bool get_usr_overflow(uint32_t usr)
{
    return usr & 1;
}

bool get_usr_fp_invalid(uint32_t usr)
{
    return (usr >> 1) & 1;
}

int32_t get_usr_lpcfg(uint32_t usr)
{
    return (usr >> 8) & 0x3;
}

jmp_buf jmp_env;
bool usr_overflow;

static void sig_segv(int sig, siginfo_t *info, void *puc)
{
    usr_overflow = read_usr_overflow();
    longjmp(jmp_env, 1);
}

static void test_packet(void)
{
    int32_t convres;
    int32_t satres;
    uint32_t usr;

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

    check32(convres, 0xffffffff);
    check32(satres, 0x7f);
    check32(get_usr_overflow(usr), true);
    check32(get_usr_fp_invalid(usr), true);

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

    check32(satres, 0x7f);
    check32(get_usr_overflow(usr), true);
    check32(get_usr_lpcfg(usr), 2);
}

int main()
{
    struct sigaction act;
    bool ovf;

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

    check32(usr_overflow, false);

    test_packet();

    puts(err ? "FAIL" : "PASS");
    return err ? EXIT_FAILURE : EXIT_SUCCESS;
}
