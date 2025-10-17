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

#include <stdio.h>
#include <signal.h>
#include <time.h>

void sig_user(int sig, siginfo_t *info, void *puc)
{
    asm("r7 = #0\n\t"
        "p0 = r7\n\t"
        "p1 = r7\n\t"
        "p2 = r7\n\t"
        "p3 = r7\n\t"
        "r6 = #0x12345678\n\t"
        "cs0 = r6\n\t"
        "r6 = #0x87654321\n\t"
        "cs1 = r6\n\t"
        : : : "r6", "r7", "p0", "p1", "p2", "p3", "cs0", "cs1");
}

int main()
{
    int err = 0;
    unsigned int i = 100000;
    struct sigaction act;
    struct itimerspec it;
    timer_t tid;
    struct sigevent sev;

    act.sa_sigaction = sig_user;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGUSR1, &act, NULL);
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGUSR1;
    sev.sigev_value.sival_ptr = &tid;
    timer_create(CLOCK_REALTIME, &sev, &tid);
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_nsec = 100000;
    it.it_value.tv_sec = 0;
    it.it_value.tv_nsec = 100000;
    timer_settime(tid, 0, &it, NULL);

    asm("loop0(1f, %1)\n\t"
        "1: r9 = #0xdeadbeef\n\t"
        "   cs0 = r9\n\t"
        "   r9 = #0xbadc0fee\n\t"
        "   cs1 = r9\n\t"
        "   r8 = #0xff\n\t"
        "   p0 = r8\n\t"
        "   p1 = r8\n\t"
        "   p2 = r8\n\t"
        "   p3 = r8\n\t"
        "   jump 3f\n\t"
        "2: memb(%0) = #1\n\t"
        "   jump 4f\n\t"
        "3:\n\t"
        "   r8 = p0\n\t"
        "   p0 = cmp.eq(r8, #0xff)\n\t"
        "   if (!p0) jump 2b\n\t"
        "   r8 = p1\n\t"
        "   p0 = cmp.eq(r8, #0xff)\n\t"
        "   if (!p0) jump 2b\n\t"
        "   r8 = p2\n\t"
        "   p0 = cmp.eq(r8, #0xff)\n\t"
        "   if (!p0) jump 2b\n\t"
        "   r8 = p3\n\t"
        "   p0 = cmp.eq(r8, #0xff)\n\t"
        "   if (!p0) jump 2b\n\t"
        "   r8 = cs0\n\t"
        "   r9 = #0xdeadbeef\n\t"
        "   p0 = cmp.eq(r8, r9)\n\t"
        "   if (!p0) jump 2b\n\t"
        "   r8 = cs1\n\t"
        "   r9 = #0xbadc0fee\n\t"
        "   p0 = cmp.eq(r8, r9)\n\t"
        "   if (!p0) jump 2b\n\t"
        "4: {}: endloop0\n\t"
        :
        : "r"(&err), "r"(i)
        : "memory", "r8", "r9", "p0", "p1", "p2", "p3", "cs0", "cs1", "lc0",
          "sa0");

    puts(err ? "FAIL" : "PASS");
    return err;
}
