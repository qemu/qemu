/*
 * CSKY syscall header.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/* this struct defines the way the registers are stored on the
   stack during a system call. */
struct target_pt_regs {
    unsigned long   pc;
    long            r1;
    /*
     * When syscall fails, we must recover syscall arg (r2, modified when
     * syscall return), Modified by Li Chunqiang  20050626.
     */
    long            syscallr2;
    unsigned long   sr; /* psr */
    long            r2;
    long            r3;
    long            r4;
    long            r5;
    long            r6;
    long            r7;
    long            r8;
    long            r9;
    long            r10;
    long            r11;
    long            r12;
    long            r13;
    long            r14;
    long            r15;
#ifdef TARGET_CSKYV2
    long            r16;
    long            r17;
    long            r18;
    long            r19;
    long            r20;
    long            r21;
    long            r22;
    long            r23;
    long            r24;
    long            r25;
    long            r26;
    long            r27;
    long            r28;
    long            r29;
    long            r30;
    long            r31;
#endif
    /* FIXME  add by shangyh */
    long            r0;
};

#define UNAME_MINIMUM_RELEASE "2.6.32"

#define TARGET_MINSIGSTKSZ 2048
#define TARGET_MLOCKALL_MCL_CURRENT 1
#define TARGET_MLOCKALL_MCL_FUTURE  2
