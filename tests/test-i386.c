/*
 *  x86 CPU test
 * 
 *  Copyright (c) 2003 Fabrice Bellard
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <sys/ucontext.h>
#include <sys/mman.h>
#include <asm/vm86.h>

#define TEST_CMOV  0
#define TEST_FCOMI 0
//#define LINUX_VM86_IOPL_FIX
//#define TEST_P4_FLAGS

#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)
#define stringify(s)	tostring(s)
#define tostring(s)	#s

#define CC_C   	0x0001
#define CC_P 	0x0004
#define CC_A	0x0010
#define CC_Z	0x0040
#define CC_S    0x0080
#define CC_O    0x0800

#define __init_call	__attribute__ ((unused,__section__ (".initcall.init")))

static void *call_start __init_call = NULL;

#define CC_MASK (CC_C | CC_P | CC_Z | CC_S | CC_O | CC_A)

#define OP add
#include "test-i386.h"

#define OP sub
#include "test-i386.h"

#define OP xor
#include "test-i386.h"

#define OP and
#include "test-i386.h"

#define OP or
#include "test-i386.h"

#define OP cmp
#include "test-i386.h"

#define OP adc
#define OP_CC
#include "test-i386.h"

#define OP sbb
#define OP_CC
#include "test-i386.h"

#define OP inc
#define OP_CC
#define OP1
#include "test-i386.h"

#define OP dec
#define OP_CC
#define OP1
#include "test-i386.h"

#define OP neg
#define OP_CC
#define OP1
#include "test-i386.h"

#define OP not
#define OP_CC
#define OP1
#include "test-i386.h"

#undef CC_MASK
#define CC_MASK (CC_C | CC_P | CC_Z | CC_S | CC_O)

#define OP shl
#include "test-i386-shift.h"

#define OP shr
#include "test-i386-shift.h"

#define OP sar
#include "test-i386-shift.h"

#define OP rol
#include "test-i386-shift.h"

#define OP ror
#include "test-i386-shift.h"

#define OP rcr
#define OP_CC
#include "test-i386-shift.h"

#define OP rcl
#define OP_CC
#include "test-i386-shift.h"

#define OP shld
#define OP_SHIFTD
#define OP_NOBYTE
#include "test-i386-shift.h"

#define OP shrd
#define OP_SHIFTD
#define OP_NOBYTE
#include "test-i386-shift.h"

/* XXX: should be more precise ? */
#undef CC_MASK
#define CC_MASK (CC_C)

#define OP bt
#define OP_NOBYTE
#include "test-i386-shift.h"

#define OP bts
#define OP_NOBYTE
#include "test-i386-shift.h"

#define OP btr
#define OP_NOBYTE
#include "test-i386-shift.h"

#define OP btc
#define OP_NOBYTE
#include "test-i386-shift.h"

/* lea test (modrm support) */
#define TEST_LEA(STR)\
{\
    asm("leal " STR ", %0"\
        : "=r" (res)\
        : "a" (eax), "b" (ebx), "c" (ecx), "d" (edx), "S" (esi), "D" (edi));\
    printf("lea %s = %08x\n", STR, res);\
}

#define TEST_LEA16(STR)\
{\
    asm(".code16 ; .byte 0x67 ; leal " STR ", %0 ; .code32"\
        : "=wq" (res)\
        : "a" (eax), "b" (ebx), "c" (ecx), "d" (edx), "S" (esi), "D" (edi));\
    printf("lea %s = %08x\n", STR, res);\
}


void test_lea(void)
{
    int eax, ebx, ecx, edx, esi, edi, res;
    eax = 0x0001;
    ebx = 0x0002;
    ecx = 0x0004;
    edx = 0x0008;
    esi = 0x0010;
    edi = 0x0020;

    TEST_LEA("0x4000");

    TEST_LEA("(%%eax)");
    TEST_LEA("(%%ebx)");
    TEST_LEA("(%%ecx)");
    TEST_LEA("(%%edx)");
    TEST_LEA("(%%esi)");
    TEST_LEA("(%%edi)");

    TEST_LEA("0x40(%%eax)");
    TEST_LEA("0x40(%%ebx)");
    TEST_LEA("0x40(%%ecx)");
    TEST_LEA("0x40(%%edx)");
    TEST_LEA("0x40(%%esi)");
    TEST_LEA("0x40(%%edi)");

    TEST_LEA("0x4000(%%eax)");
    TEST_LEA("0x4000(%%ebx)");
    TEST_LEA("0x4000(%%ecx)");
    TEST_LEA("0x4000(%%edx)");
    TEST_LEA("0x4000(%%esi)");
    TEST_LEA("0x4000(%%edi)");

    TEST_LEA("(%%eax, %%ecx)");
    TEST_LEA("(%%ebx, %%edx)");
    TEST_LEA("(%%ecx, %%ecx)");
    TEST_LEA("(%%edx, %%ecx)");
    TEST_LEA("(%%esi, %%ecx)");
    TEST_LEA("(%%edi, %%ecx)");

    TEST_LEA("0x40(%%eax, %%ecx)");
    TEST_LEA("0x4000(%%ebx, %%edx)");

    TEST_LEA("(%%ecx, %%ecx, 2)");
    TEST_LEA("(%%edx, %%ecx, 4)");
    TEST_LEA("(%%esi, %%ecx, 8)");

    TEST_LEA("(,%%eax, 2)");
    TEST_LEA("(,%%ebx, 4)");
    TEST_LEA("(,%%ecx, 8)");

    TEST_LEA("0x40(,%%eax, 2)");
    TEST_LEA("0x40(,%%ebx, 4)");
    TEST_LEA("0x40(,%%ecx, 8)");


    TEST_LEA("-10(%%ecx, %%ecx, 2)");
    TEST_LEA("-10(%%edx, %%ecx, 4)");
    TEST_LEA("-10(%%esi, %%ecx, 8)");

    TEST_LEA("0x4000(%%ecx, %%ecx, 2)");
    TEST_LEA("0x4000(%%edx, %%ecx, 4)");
    TEST_LEA("0x4000(%%esi, %%ecx, 8)");

    /* limited 16 bit addressing test */
    TEST_LEA16("0x4000");
    TEST_LEA16("(%%bx)");
    TEST_LEA16("(%%si)");
    TEST_LEA16("(%%di)");
    TEST_LEA16("0x40(%%bx)");
    TEST_LEA16("0x40(%%si)");
    TEST_LEA16("0x40(%%di)");
    TEST_LEA16("0x4000(%%bx)");
    TEST_LEA16("0x4000(%%si)");
    TEST_LEA16("(%%bx,%%si)");
    TEST_LEA16("(%%bx,%%di)");
    TEST_LEA16("0x40(%%bx,%%si)");
    TEST_LEA16("0x40(%%bx,%%di)");
    TEST_LEA16("0x4000(%%bx,%%si)");
    TEST_LEA16("0x4000(%%bx,%%di)");
}

#define TEST_JCC(JCC, v1, v2)\
{\
    int res;\
    asm("movl $1, %0\n\t"\
        "cmpl %2, %1\n\t"\
        "j" JCC " 1f\n\t"\
        "movl $0, %0\n\t"\
        "1:\n\t"\
        : "=r" (res)\
        : "r" (v1), "r" (v2));\
    printf("%-10s %d\n", "j" JCC, res);\
\
    asm("movl $0, %0\n\t"\
        "cmpl %2, %1\n\t"\
        "set" JCC " %b0\n\t"\
        : "=r" (res)\
        : "r" (v1), "r" (v2));\
    printf("%-10s %d\n", "set" JCC, res);\
 if (TEST_CMOV) {\
    asm("movl $0x12345678, %0\n\t"\
        "cmpl %2, %1\n\t"\
        "cmov" JCC "l %3, %0\n\t"\
        : "=r" (res)\
        : "r" (v1), "r" (v2), "m" (1));\
        printf("%-10s R=0x%08x\n", "cmov" JCC "l", res);\
    asm("movl $0x12345678, %0\n\t"\
        "cmpl %2, %1\n\t"\
        "cmov" JCC "w %w3, %w0\n\t"\
        : "=r" (res)\
        : "r" (v1), "r" (v2), "r" (1));\
        printf("%-10s R=0x%08x\n", "cmov" JCC "w", res);\
 } \
}

/* various jump tests */
void test_jcc(void)
{
    TEST_JCC("ne", 1, 1);
    TEST_JCC("ne", 1, 0);

    TEST_JCC("e", 1, 1);
    TEST_JCC("e", 1, 0);

    TEST_JCC("l", 1, 1);
    TEST_JCC("l", 1, 0);
    TEST_JCC("l", 1, -1);

    TEST_JCC("le", 1, 1);
    TEST_JCC("le", 1, 0);
    TEST_JCC("le", 1, -1);

    TEST_JCC("ge", 1, 1);
    TEST_JCC("ge", 1, 0);
    TEST_JCC("ge", -1, 1);

    TEST_JCC("g", 1, 1);
    TEST_JCC("g", 1, 0);
    TEST_JCC("g", 1, -1);

    TEST_JCC("b", 1, 1);
    TEST_JCC("b", 1, 0);
    TEST_JCC("b", 1, -1);

    TEST_JCC("be", 1, 1);
    TEST_JCC("be", 1, 0);
    TEST_JCC("be", 1, -1);

    TEST_JCC("ae", 1, 1);
    TEST_JCC("ae", 1, 0);
    TEST_JCC("ae", 1, -1);

    TEST_JCC("a", 1, 1);
    TEST_JCC("a", 1, 0);
    TEST_JCC("a", 1, -1);


    TEST_JCC("p", 1, 1);
    TEST_JCC("p", 1, 0);

    TEST_JCC("np", 1, 1);
    TEST_JCC("np", 1, 0);

    TEST_JCC("o", 0x7fffffff, 0);
    TEST_JCC("o", 0x7fffffff, -1);

    TEST_JCC("no", 0x7fffffff, 0);
    TEST_JCC("no", 0x7fffffff, -1);

    TEST_JCC("s", 0, 1);
    TEST_JCC("s", 0, -1);
    TEST_JCC("s", 0, 0);

    TEST_JCC("ns", 0, 1);
    TEST_JCC("ns", 0, -1);
    TEST_JCC("ns", 0, 0);
}

#undef CC_MASK
#ifdef TEST_P4_FLAGS
#define CC_MASK (CC_C | CC_P | CC_Z | CC_S | CC_O | CC_A)
#else
#define CC_MASK (CC_O | CC_C)
#endif

#define OP mul
#include "test-i386-muldiv.h"

#define OP imul
#include "test-i386-muldiv.h"

void test_imulw2(int op0, int op1) 
{
    int res, s1, s0, flags;
    s0 = op0;
    s1 = op1;
    res = s0;
    flags = 0;
    asm ("push %4\n\t"
         "popf\n\t"
         "imulw %w2, %w0\n\t" 
         "pushf\n\t"
         "popl %1\n\t"
         : "=q" (res), "=g" (flags)
         : "q" (s1), "0" (res), "1" (flags));
    printf("%-10s A=%08x B=%08x R=%08x CC=%04x\n",
           "imulw", s0, s1, res, flags & CC_MASK);
}

void test_imull2(int op0, int op1) 
{
    int res, s1, s0, flags;
    s0 = op0;
    s1 = op1;
    res = s0;
    flags = 0;
    asm ("push %4\n\t"
         "popf\n\t"
         "imull %2, %0\n\t" 
         "pushf\n\t"
         "popl %1\n\t"
         : "=q" (res), "=g" (flags)
         : "q" (s1), "0" (res), "1" (flags));
    printf("%-10s A=%08x B=%08x R=%08x CC=%04x\n",
           "imull", s0, s1, res, flags & CC_MASK);
}

#define TEST_IMUL_IM(size, size1, op0, op1)\
{\
    int res, flags;\
    flags = 0;\
    res = 0;\
    asm ("push %3\n\t"\
         "popf\n\t"\
         "imul" size " $" #op0 ", %" size1 "2, %" size1 "0\n\t" \
         "pushf\n\t"\
         "popl %1\n\t"\
         : "=r" (res), "=g" (flags)\
         : "r" (op1), "1" (flags), "0" (res));\
    printf("%-10s A=%08x B=%08x R=%08x CC=%04x\n",\
           "imul" size, op0, op1, res, flags & CC_MASK);\
}


#undef CC_MASK
#define CC_MASK (0)

#define OP div
#include "test-i386-muldiv.h"

#define OP idiv
#include "test-i386-muldiv.h"

void test_mul(void)
{
    test_imulb(0x1234561d, 4);
    test_imulb(3, -4);
    test_imulb(0x80, 0x80);
    test_imulb(0x10, 0x10);

    test_imulw(0, 0x1234001d, 45);
    test_imulw(0, 23, -45);
    test_imulw(0, 0x8000, 0x8000);
    test_imulw(0, 0x100, 0x100);

    test_imull(0, 0x1234001d, 45);
    test_imull(0, 23, -45);
    test_imull(0, 0x80000000, 0x80000000);
    test_imull(0, 0x10000, 0x10000);

    test_mulb(0x1234561d, 4);
    test_mulb(3, -4);
    test_mulb(0x80, 0x80);
    test_mulb(0x10, 0x10);

    test_mulw(0, 0x1234001d, 45);
    test_mulw(0, 23, -45);
    test_mulw(0, 0x8000, 0x8000);
    test_mulw(0, 0x100, 0x100);

    test_mull(0, 0x1234001d, 45);
    test_mull(0, 23, -45);
    test_mull(0, 0x80000000, 0x80000000);
    test_mull(0, 0x10000, 0x10000);

    test_imulw2(0x1234001d, 45);
    test_imulw2(23, -45);
    test_imulw2(0x8000, 0x8000);
    test_imulw2(0x100, 0x100);

    test_imull2(0x1234001d, 45);
    test_imull2(23, -45);
    test_imull2(0x80000000, 0x80000000);
    test_imull2(0x10000, 0x10000);

    TEST_IMUL_IM("w", "w", 45, 0x1234);
    TEST_IMUL_IM("w", "w", -45, 23);
    TEST_IMUL_IM("w", "w", 0x8000, 0x80000000);
    TEST_IMUL_IM("w", "w", 0x7fff, 0x1000);

    TEST_IMUL_IM("l", "", 45, 0x1234);
    TEST_IMUL_IM("l", "", -45, 23);
    TEST_IMUL_IM("l", "", 0x8000, 0x80000000);
    TEST_IMUL_IM("l", "", 0x7fff, 0x1000);

    test_idivb(0x12341678, 0x127e);
    test_idivb(0x43210123, -5);
    test_idivb(0x12340004, -1);

    test_idivw(0, 0x12345678, 12347);
    test_idivw(0, -23223, -45);
    test_idivw(0, 0x12348000, -1);
    test_idivw(0x12343, 0x12345678, 0x81238567);

    test_idivl(0, 0x12345678, 12347);
    test_idivl(0, -233223, -45);
    test_idivl(0, 0x80000000, -1);
    test_idivl(0x12343, 0x12345678, 0x81234567);

    test_divb(0x12341678, 0x127e);
    test_divb(0x43210123, -5);
    test_divb(0x12340004, -1);

    test_divw(0, 0x12345678, 12347);
    test_divw(0, -23223, -45);
    test_divw(0, 0x12348000, -1);
    test_divw(0x12343, 0x12345678, 0x81238567);

    test_divl(0, 0x12345678, 12347);
    test_divl(0, -233223, -45);
    test_divl(0, 0x80000000, -1);
    test_divl(0x12343, 0x12345678, 0x81234567);
}

#define TEST_BSX(op, size, op0)\
{\
    int res, val, resz;\
    val = op0;\
    asm("xorl %1, %1\n"\
        "movl $0x12345678, %0\n"\
        #op " %" size "2, %" size "0 ; setz %b1" \
        : "=r" (res), "=q" (resz)\
        : "g" (val));\
    printf("%-10s A=%08x R=%08x %d\n", #op, val, res, resz);\
}

void test_bsx(void)
{
    TEST_BSX(bsrw, "w", 0);
    TEST_BSX(bsrw, "w", 0x12340128);
    TEST_BSX(bsrl, "", 0);
    TEST_BSX(bsrl, "", 0x00340128);
    TEST_BSX(bsfw, "w", 0);
    TEST_BSX(bsfw, "w", 0x12340128);
    TEST_BSX(bsfl, "", 0);
    TEST_BSX(bsfl, "", 0x00340128);
}

/**********************************************/

void test_fops(double a, double b)
{
    printf("a=%f b=%f a+b=%f\n", a, b, a + b);
    printf("a=%f b=%f a-b=%f\n", a, b, a - b);
    printf("a=%f b=%f a*b=%f\n", a, b, a * b);
    printf("a=%f b=%f a/b=%f\n", a, b, a / b);
    printf("a=%f b=%f fmod(a, b)=%f\n", a, b, fmod(a, b));
    printf("a=%f sqrt(a)=%f\n", a, sqrt(a));
    printf("a=%f sin(a)=%f\n", a, sin(a));
    printf("a=%f cos(a)=%f\n", a, cos(a));
    printf("a=%f tan(a)=%f\n", a, tan(a));
    printf("a=%f log(a)=%f\n", a, log(a));
    printf("a=%f exp(a)=%f\n", a, exp(a));
    printf("a=%f b=%f atan2(a, b)=%f\n", a, b, atan2(a, b));
    /* just to test some op combining */
    printf("a=%f asin(sin(a))=%f\n", a, asin(sin(a)));
    printf("a=%f acos(cos(a))=%f\n", a, acos(cos(a)));
    printf("a=%f atan(tan(a))=%f\n", a, atan(tan(a)));

}

void test_fcmp(double a, double b)
{
    printf("(%f<%f)=%d\n",
           a, b, a < b);
    printf("(%f<=%f)=%d\n",
           a, b, a <= b);
    printf("(%f==%f)=%d\n",
           a, b, a == b);
    printf("(%f>%f)=%d\n",
           a, b, a > b);
    printf("(%f<=%f)=%d\n",
           a, b, a >= b);
    if (TEST_FCOMI) {
        unsigned int eflags;
        /* test f(u)comi instruction */
        asm("fcomi %2, %1\n"
            "pushf\n"
            "pop %0\n"
            : "=r" (eflags)
            : "t" (a), "u" (b));
        printf("fcomi(%f %f)=%08x\n", a, b, eflags & (CC_Z | CC_P | CC_C));
    }
}

void test_fcvt(double a)
{
    float fa;
    long double la;
    int16_t fpuc;
    int i;
    int64_t lla;
    int ia;
    int16_t wa;
    double ra;

    fa = a;
    la = a;
    printf("(float)%f = %f\n", a, fa);
    printf("(long double)%f = %Lf\n", a, la);
    printf("a=%016Lx\n", *(long long *)&a);
    printf("la=%016Lx %04x\n", *(long long *)&la, 
           *(unsigned short *)((char *)(&la) + 8));

    /* test all roundings */
    asm volatile ("fstcw %0" : "=m" (fpuc));
    for(i=0;i<4;i++) {
        asm volatile ("fldcw %0" : : "m" ((fpuc & ~0x0c00) | (i << 10)));
        asm volatile ("fist %0" : "=m" (wa) : "t" (a));
        asm volatile ("fistl %0" : "=m" (ia) : "t" (a));
        asm volatile ("fistpll %0" : "=m" (lla) : "t" (a) : "st");
        asm volatile ("frndint ; fstl %0" : "=m" (ra) : "t" (a));
        asm volatile ("fldcw %0" : : "m" (fpuc));
        printf("(short)a = %d\n", wa);
        printf("(int)a = %d\n", ia);
        printf("(int64_t)a = %Ld\n", lla);
        printf("rint(a) = %f\n", ra);
    }
}

#define TEST(N) \
    asm("fld" #N : "=t" (a)); \
    printf("fld" #N "= %f\n", a);

void test_fconst(void)
{
    double a;
    TEST(1);
    TEST(l2t);
    TEST(l2e);
    TEST(pi);
    TEST(lg2);
    TEST(ln2);
    TEST(z);
}

void test_fbcd(double a)
{
    unsigned short bcd[5];
    double b;

    asm("fbstp %0" : "=m" (bcd[0]) : "t" (a) : "st");
    asm("fbld %1" : "=t" (b) : "m" (bcd[0]));
    printf("a=%f bcd=%04x%04x%04x%04x%04x b=%f\n", 
           a, bcd[4], bcd[3], bcd[2], bcd[1], bcd[0], b);
}

#define TEST_ENV(env, save, restore)\
{\
    memset((env), 0xaa, sizeof(*(env)));\
    for(i=0;i<5;i++)\
        asm volatile ("fldl %0" : : "m" (dtab[i]));\
    asm(save " %0\n" : : "m" (*(env)));\
    asm(restore " %0\n": : "m" (*(env)));\
    for(i=0;i<5;i++)\
        asm volatile ("fstpl %0" : "=m" (rtab[i]));\
    for(i=0;i<5;i++)\
        printf("res[%d]=%f\n", i, rtab[i]);\
    printf("fpuc=%04x fpus=%04x fptag=%04x\n",\
           (env)->fpuc,\
           (env)->fpus & 0xff00,\
           (env)->fptag);\
}

void test_fenv(void)
{
    struct __attribute__((packed)) {
        uint16_t fpuc;
        uint16_t dummy1;
        uint16_t fpus;
        uint16_t dummy2;
        uint16_t fptag;
        uint16_t dummy3;
        uint32_t ignored[4];
        long double fpregs[8];
    } float_env32;
    struct __attribute__((packed)) {
        uint16_t fpuc;
        uint16_t fpus;
        uint16_t fptag;
        uint16_t ignored[4];
        long double fpregs[8];
    } float_env16;
    double dtab[8];
    double rtab[8];
    int i;

    for(i=0;i<8;i++)
        dtab[i] = i + 1;

    TEST_ENV(&float_env16, "data16 fnstenv", "data16 fldenv");
    TEST_ENV(&float_env16, "data16 fnsave", "data16 frstor");
    TEST_ENV(&float_env32, "fnstenv", "fldenv");
    TEST_ENV(&float_env32, "fnsave", "frstor");

    /* test for ffree */
    for(i=0;i<5;i++)
        asm volatile ("fldl %0" : : "m" (dtab[i]));
    asm volatile("ffree %st(2)");
    asm volatile ("fnstenv %0\n" : : "m" (float_env32));
    asm volatile ("fninit");
    printf("fptag=%04x\n", float_env32.fptag);
}


#define TEST_FCMOV(a, b, eflags, CC)\
{\
    double res;\
    asm("push %3\n"\
        "popf\n"\
        "fcmov" CC " %2, %0\n"\
        : "=t" (res)\
        : "0" (a), "u" (b), "g" (eflags));\
    printf("fcmov%s eflags=0x%04x-> %f\n", \
           CC, eflags, res);\
}

void test_fcmov(void)
{
    double a, b;
    int eflags, i;

    a = 1.0;
    b = 2.0;
    for(i = 0; i < 4; i++) {
        eflags = 0;
        if (i & 1)
            eflags |= CC_C;
        if (i & 2)
            eflags |= CC_Z;
        TEST_FCMOV(a, b, eflags, "b");
        TEST_FCMOV(a, b, eflags, "e");
        TEST_FCMOV(a, b, eflags, "be");
        TEST_FCMOV(a, b, eflags, "nb");
        TEST_FCMOV(a, b, eflags, "ne");
        TEST_FCMOV(a, b, eflags, "nbe");
    }
    TEST_FCMOV(a, b, 0, "u");
    TEST_FCMOV(a, b, CC_P, "u");
    TEST_FCMOV(a, b, 0, "nu");
    TEST_FCMOV(a, b, CC_P, "nu");
}

void test_floats(void)
{
    test_fops(2, 3);
    test_fops(1.4, -5);
    test_fcmp(2, -1);
    test_fcmp(2, 2);
    test_fcmp(2, 3);
    test_fcvt(0.5);
    test_fcvt(-0.5);
    test_fcvt(1.0/7.0);
    test_fcvt(-1.0/9.0);
    test_fcvt(32768);
    test_fcvt(-1e20);
    test_fconst();
    test_fbcd(1234567890123456);
    test_fbcd(-123451234567890);
    test_fenv();
    if (TEST_CMOV) {
        test_fcmov();
    }
}

/**********************************************/

#define TEST_BCD(op, op0, cc_in, cc_mask)\
{\
    int res, flags;\
    res = op0;\
    flags = cc_in;\
    asm ("push %3\n\t"\
         "popf\n\t"\
         #op "\n\t"\
         "pushf\n\t"\
         "popl %1\n\t"\
        : "=a" (res), "=g" (flags)\
        : "0" (res), "1" (flags));\
    printf("%-10s A=%08x R=%08x CCIN=%04x CC=%04x\n",\
           #op, op0, res, cc_in, flags & cc_mask);\
}

void test_bcd(void)
{
    TEST_BCD(daa, 0x12340503, CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(daa, 0x12340506, CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(daa, 0x12340507, CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(daa, 0x12340559, CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(daa, 0x12340560, CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(daa, 0x1234059f, CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(daa, 0x123405a0, CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(daa, 0x12340503, 0, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(daa, 0x12340506, 0, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(daa, 0x12340503, CC_C, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(daa, 0x12340506, CC_C, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(daa, 0x12340503, CC_C | CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(daa, 0x12340506, CC_C | CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));

    TEST_BCD(das, 0x12340503, CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(das, 0x12340506, CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(das, 0x12340507, CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(das, 0x12340559, CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(das, 0x12340560, CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(das, 0x1234059f, CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(das, 0x123405a0, CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(das, 0x12340503, 0, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(das, 0x12340506, 0, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(das, 0x12340503, CC_C, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(das, 0x12340506, CC_C, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(das, 0x12340503, CC_C | CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));
    TEST_BCD(das, 0x12340506, CC_C | CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_A));

    TEST_BCD(aaa, 0x12340205, CC_A, (CC_C | CC_A));
    TEST_BCD(aaa, 0x12340306, CC_A, (CC_C | CC_A));
    TEST_BCD(aaa, 0x1234040a, CC_A, (CC_C | CC_A));
    TEST_BCD(aaa, 0x123405fa, CC_A, (CC_C | CC_A));
    TEST_BCD(aaa, 0x12340205, 0, (CC_C | CC_A));
    TEST_BCD(aaa, 0x12340306, 0, (CC_C | CC_A));
    TEST_BCD(aaa, 0x1234040a, 0, (CC_C | CC_A));
    TEST_BCD(aaa, 0x123405fa, 0, (CC_C | CC_A));
    
    TEST_BCD(aas, 0x12340205, CC_A, (CC_C | CC_A));
    TEST_BCD(aas, 0x12340306, CC_A, (CC_C | CC_A));
    TEST_BCD(aas, 0x1234040a, CC_A, (CC_C | CC_A));
    TEST_BCD(aas, 0x123405fa, CC_A, (CC_C | CC_A));
    TEST_BCD(aas, 0x12340205, 0, (CC_C | CC_A));
    TEST_BCD(aas, 0x12340306, 0, (CC_C | CC_A));
    TEST_BCD(aas, 0x1234040a, 0, (CC_C | CC_A));
    TEST_BCD(aas, 0x123405fa, 0, (CC_C | CC_A));

    TEST_BCD(aam, 0x12340547, CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_O | CC_A));
    TEST_BCD(aad, 0x12340407, CC_A, (CC_C | CC_P | CC_Z | CC_S | CC_O | CC_A));
}

#define TEST_XCHG(op, size, opconst)\
{\
    int op0, op1;\
    op0 = 0x12345678;\
    op1 = 0xfbca7654;\
    asm(#op " %" size "0, %" size "1" \
        : "=q" (op0), opconst (op1) \
        : "0" (op0), "1" (op1));\
    printf("%-10s A=%08x B=%08x\n",\
           #op, op0, op1);\
}

#define TEST_CMPXCHG(op, size, opconst, eax)\
{\
    int op0, op1;\
    op0 = 0x12345678;\
    op1 = 0xfbca7654;\
    asm(#op " %" size "0, %" size "1" \
        : "=q" (op0), opconst (op1) \
        : "0" (op0), "1" (op1), "a" (eax));\
    printf("%-10s EAX=%08x A=%08x C=%08x\n",\
           #op, eax, op0, op1);\
}

void test_xchg(void)
{
    TEST_XCHG(xchgl, "", "=q");
    TEST_XCHG(xchgw, "w", "=q");
    TEST_XCHG(xchgb, "b", "=q");

    TEST_XCHG(xchgl, "", "=m");
    TEST_XCHG(xchgw, "w", "=m");
    TEST_XCHG(xchgb, "b", "=m");

    TEST_XCHG(xaddl, "", "=q");
    TEST_XCHG(xaddw, "w", "=q");
    TEST_XCHG(xaddb, "b", "=q");

    {
        int res;
        res = 0x12345678;
        asm("xaddl %1, %0" : "=r" (res) : "0" (res));
        printf("xaddl same res=%08x\n", res);
    }

    TEST_XCHG(xaddl, "", "=m");
    TEST_XCHG(xaddw, "w", "=m");
    TEST_XCHG(xaddb, "b", "=m");

    TEST_CMPXCHG(cmpxchgl, "", "=q", 0xfbca7654);
    TEST_CMPXCHG(cmpxchgw, "w", "=q", 0xfbca7654);
    TEST_CMPXCHG(cmpxchgb, "b", "=q", 0xfbca7654);

    TEST_CMPXCHG(cmpxchgl, "", "=q", 0xfffefdfc);
    TEST_CMPXCHG(cmpxchgw, "w", "=q", 0xfffefdfc);
    TEST_CMPXCHG(cmpxchgb, "b", "=q", 0xfffefdfc);

    TEST_CMPXCHG(cmpxchgl, "", "=m", 0xfbca7654);
    TEST_CMPXCHG(cmpxchgw, "w", "=m", 0xfbca7654);
    TEST_CMPXCHG(cmpxchgb, "b", "=m", 0xfbca7654);

    TEST_CMPXCHG(cmpxchgl, "", "=m", 0xfffefdfc);
    TEST_CMPXCHG(cmpxchgw, "w", "=m", 0xfffefdfc);
    TEST_CMPXCHG(cmpxchgb, "b", "=m", 0xfffefdfc);

    {
        uint64_t op0, op1, op2;
        int i, eflags;

        for(i = 0; i < 2; i++) {
            op0 = 0x123456789abcd;
            if (i == 0)
                op1 = 0xfbca765423456;
            else
                op1 = op0;
            op2 = 0x6532432432434;
            asm("cmpxchg8b %1\n" 
                "pushf\n"
                "popl %2\n"
                : "=A" (op0), "=m" (op1), "=g" (eflags)
                : "0" (op0), "m" (op1), "b" ((int)op2), "c" ((int)(op2 >> 32)));
            printf("cmpxchg8b: op0=%016llx op1=%016llx CC=%02x\n", 
                    op0, op1, eflags & CC_Z);
        }
    }
}

/**********************************************/
/* segmentation tests */

#include <asm/ldt.h>
#include <linux/unistd.h>
#include <linux/version.h>

_syscall3(int, modify_ldt, int, func, void *, ptr, unsigned long, bytecount)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 66)
#define modify_ldt_ldt_s user_desc
#endif

uint8_t seg_data1[4096];
uint8_t seg_data2[4096];

#define MK_SEL(n) (((n) << 3) | 7)

#define TEST_LR(op, size, seg, mask)\
{\
    int res, res2;\
    res = 0x12345678;\
    asm (op " %" size "2, %" size "0\n" \
         "movl $0, %1\n"\
         "jnz 1f\n"\
         "movl $1, %1\n"\
         "1:\n"\
         : "=r" (res), "=r" (res2) : "m" (seg), "0" (res));\
    printf(op ": Z=%d %08x\n", res2, res & ~(mask));\
}

/* NOTE: we use Linux modify_ldt syscall */
void test_segs(void)
{
    struct modify_ldt_ldt_s ldt;
    long long ldt_table[3];
    int res, res2;
    char tmp;
    struct {
        uint32_t offset;
        uint16_t seg;
    } __attribute__((packed)) segoff;

    ldt.entry_number = 1;
    ldt.base_addr = (unsigned long)&seg_data1;
    ldt.limit = (sizeof(seg_data1) + 0xfff) >> 12;
    ldt.seg_32bit = 1;
    ldt.contents = MODIFY_LDT_CONTENTS_DATA;
    ldt.read_exec_only = 0;
    ldt.limit_in_pages = 1;
    ldt.seg_not_present = 0;
    ldt.useable = 1;
    modify_ldt(1, &ldt, sizeof(ldt)); /* write ldt entry */

    ldt.entry_number = 2;
    ldt.base_addr = (unsigned long)&seg_data2;
    ldt.limit = (sizeof(seg_data2) + 0xfff) >> 12;
    ldt.seg_32bit = 1;
    ldt.contents = MODIFY_LDT_CONTENTS_DATA;
    ldt.read_exec_only = 0;
    ldt.limit_in_pages = 1;
    ldt.seg_not_present = 0;
    ldt.useable = 1;
    modify_ldt(1, &ldt, sizeof(ldt)); /* write ldt entry */

    modify_ldt(0, &ldt_table, sizeof(ldt_table)); /* read ldt entries */
#if 0
    {
        int i;
        for(i=0;i<3;i++)
            printf("%d: %016Lx\n", i, ldt_table[i]);
    }
#endif
    /* do some tests with fs or gs */
    asm volatile ("movl %0, %%fs" : : "r" (MK_SEL(1)));

    seg_data1[1] = 0xaa;
    seg_data2[1] = 0x55;

    asm volatile ("fs movzbl 0x1, %0" : "=r" (res));
    printf("FS[1] = %02x\n", res);

    asm volatile ("pushl %%gs\n"
                  "movl %1, %%gs\n"
                  "gs movzbl 0x1, %0\n"
                  "popl %%gs\n"
                  : "=r" (res)
                  : "r" (MK_SEL(2)));
    printf("GS[1] = %02x\n", res);

    /* tests with ds/ss (implicit segment case) */
    tmp = 0xa5;
    asm volatile ("pushl %%ebp\n\t"
                  "pushl %%ds\n\t"
                  "movl %2, %%ds\n\t"
                  "movl %3, %%ebp\n\t"
                  "movzbl 0x1, %0\n\t"
                  "movzbl (%%ebp), %1\n\t"
                  "popl %%ds\n\t"
                  "popl %%ebp\n\t"
                  : "=r" (res), "=r" (res2)
                  : "r" (MK_SEL(1)), "r" (&tmp));
    printf("DS[1] = %02x\n", res);
    printf("SS[tmp] = %02x\n", res2);

    segoff.seg = MK_SEL(2);
    segoff.offset = 0xabcdef12;
    asm volatile("lfs %2, %0\n\t" 
                 "movl %%fs, %1\n\t"
                 : "=r" (res), "=g" (res2) 
                 : "m" (segoff));
    printf("FS:reg = %04x:%08x\n", res2, res);

    TEST_LR("larw", "w", MK_SEL(2), 0x0100);
    TEST_LR("larl", "", MK_SEL(2), 0x0100);
    TEST_LR("lslw", "w", MK_SEL(2), 0);
    TEST_LR("lsll", "", MK_SEL(2), 0);

    TEST_LR("larw", "w", 0xfff8, 0);
    TEST_LR("larl", "", 0xfff8, 0);
    TEST_LR("lslw", "w", 0xfff8, 0);
    TEST_LR("lsll", "", 0xfff8, 0);
}

/* 16 bit code test */
extern char code16_start, code16_end;
extern char code16_func1;
extern char code16_func2;
extern char code16_func3;

void test_code16(void)
{
    struct modify_ldt_ldt_s ldt;
    int res, res2;

    /* build a code segment */
    ldt.entry_number = 1;
    ldt.base_addr = (unsigned long)&code16_start;
    ldt.limit = &code16_end - &code16_start;
    ldt.seg_32bit = 0;
    ldt.contents = MODIFY_LDT_CONTENTS_CODE;
    ldt.read_exec_only = 0;
    ldt.limit_in_pages = 0;
    ldt.seg_not_present = 0;
    ldt.useable = 1;
    modify_ldt(1, &ldt, sizeof(ldt)); /* write ldt entry */

    /* call the first function */
    asm volatile ("lcall %1, %2" 
                  : "=a" (res)
                  : "i" (MK_SEL(1)), "i" (&code16_func1): "memory", "cc");
    printf("func1() = 0x%08x\n", res);
    asm volatile ("lcall %2, %3" 
                  : "=a" (res), "=c" (res2)
                  : "i" (MK_SEL(1)), "i" (&code16_func2): "memory", "cc");
    printf("func2() = 0x%08x spdec=%d\n", res, res2);
    asm volatile ("lcall %1, %2" 
                  : "=a" (res)
                  : "i" (MK_SEL(1)), "i" (&code16_func3): "memory", "cc");
    printf("func3() = 0x%08x\n", res);
}

extern char func_lret32;
extern char func_iret32;

void test_misc(void)
{
    char table[256];
    int res, i;

    for(i=0;i<256;i++) table[i] = 256 - i;
    res = 0x12345678;
    asm ("xlat" : "=a" (res) : "b" (table), "0" (res));
    printf("xlat: EAX=%08x\n", res);

    asm volatile ("pushl %%cs ; call %1" 
                  : "=a" (res)
                  : "m" (func_lret32): "memory", "cc");
    printf("func_lret32=%x\n", res);

    asm volatile ("pushfl ; pushl %%cs ; call %1" 
                  : "=a" (res)
                  : "m" (func_iret32): "memory", "cc");
    printf("func_iret32=%x\n", res);

    /* specific popl test */
    asm volatile ("pushl $12345432 ; pushl $0x9abcdef ; popl (%%esp) ; popl %0"
                  : "=g" (res));
    printf("popl esp=%x\n", res);

    /* specific popw test */
    asm volatile ("pushl $12345432 ; pushl $0x9abcdef ; popw (%%esp) ; addl $2, %%esp ; popl %0"
                  : "=g" (res));
    printf("popw esp=%x\n", res);
}

uint8_t str_buffer[4096];

#define TEST_STRING1(OP, size, DF, REP)\
{\
    int esi, edi, eax, ecx, eflags;\
\
    esi = (long)(str_buffer + sizeof(str_buffer) / 2);\
    edi = (long)(str_buffer + sizeof(str_buffer) / 2) + 16;\
    eax = 0x12345678;\
    ecx = 17;\
\
    asm volatile ("pushl $0\n\t"\
                  "popf\n\t"\
                  DF "\n\t"\
                  REP #OP size "\n\t"\
                  "cld\n\t"\
                  "pushf\n\t"\
                  "popl %4\n\t"\
                  : "=S" (esi), "=D" (edi), "=a" (eax), "=c" (ecx), "=g" (eflags)\
                  : "0" (esi), "1" (edi), "2" (eax), "3" (ecx));\
    printf("%-10s ESI=%08x EDI=%08x EAX=%08x ECX=%08x EFL=%04x\n",\
           REP #OP size, esi, edi, eax, ecx,\
           eflags & (CC_C | CC_P | CC_Z | CC_S | CC_O | CC_A));\
}

#define TEST_STRING(OP, REP)\
    TEST_STRING1(OP, "b", "", REP);\
    TEST_STRING1(OP, "w", "", REP);\
    TEST_STRING1(OP, "l", "", REP);\
    TEST_STRING1(OP, "b", "std", REP);\
    TEST_STRING1(OP, "w", "std", REP);\
    TEST_STRING1(OP, "l", "std", REP)

void test_string(void)
{
    int i;
    for(i = 0;i < sizeof(str_buffer); i++)
        str_buffer[i] = i + 0x56;
   TEST_STRING(stos, "");
   TEST_STRING(stos, "rep ");
   TEST_STRING(lods, ""); /* to verify stos */
   TEST_STRING(lods, "rep "); 
   TEST_STRING(movs, "");
   TEST_STRING(movs, "rep ");
   TEST_STRING(lods, ""); /* to verify stos */

   /* XXX: better tests */
   TEST_STRING(scas, "");
   TEST_STRING(scas, "repz ");
   TEST_STRING(scas, "repnz ");
   TEST_STRING(cmps, "");
   TEST_STRING(cmps, "repz ");
   TEST_STRING(cmps, "repnz ");
}

/* VM86 test */

static inline void set_bit(uint8_t *a, unsigned int bit)
{
    a[bit / 8] |= (1 << (bit % 8));
}

static inline uint8_t *seg_to_linear(unsigned int seg, unsigned int reg)
{
    return (uint8_t *)((seg << 4) + (reg & 0xffff));
}

static inline void pushw(struct vm86_regs *r, int val)
{
    r->esp = (r->esp & ~0xffff) | ((r->esp - 2) & 0xffff);
    *(uint16_t *)seg_to_linear(r->ss, r->esp) = val;
}

#undef __syscall_return
#define __syscall_return(type, res) \
do { \
	return (type) (res); \
} while (0)

_syscall2(int, vm86, int, func, struct vm86plus_struct *, v86)

extern char vm86_code_start;
extern char vm86_code_end;

#define VM86_CODE_CS 0x100
#define VM86_CODE_IP 0x100

void test_vm86(void)
{
    struct vm86plus_struct ctx;
    struct vm86_regs *r;
    uint8_t *vm86_mem;
    int seg, ret;

    vm86_mem = mmap((void *)0x00000000, 0x110000, 
                    PROT_WRITE | PROT_READ | PROT_EXEC, 
                    MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
    if (vm86_mem == MAP_FAILED) {
        printf("ERROR: could not map vm86 memory");
        return;
    }
    memset(&ctx, 0, sizeof(ctx));

    /* init basic registers */
    r = &ctx.regs;
    r->eip = VM86_CODE_IP;
    r->esp = 0xfffe;
    seg = VM86_CODE_CS;
    r->cs = seg;
    r->ss = seg;
    r->ds = seg;
    r->es = seg;
    r->fs = seg;
    r->gs = seg;
    r->eflags = VIF_MASK;

    /* move code to proper address. We use the same layout as a .com
       dos program. */
    memcpy(vm86_mem + (VM86_CODE_CS << 4) + VM86_CODE_IP, 
           &vm86_code_start, &vm86_code_end - &vm86_code_start);

    /* mark int 0x21 as being emulated */
    set_bit((uint8_t *)&ctx.int_revectored, 0x21);

    for(;;) {
        ret = vm86(VM86_ENTER, &ctx);
        switch(VM86_TYPE(ret)) {
        case VM86_INTx:
            {
                int int_num, ah, v;
                
                int_num = VM86_ARG(ret);
                if (int_num != 0x21)
                    goto unknown_int;
                ah = (r->eax >> 8) & 0xff;
                switch(ah) {
                case 0x00: /* exit */
                    goto the_end;
                case 0x02: /* write char */
                    {
                        uint8_t c = r->edx;
                        putchar(c);
                    }
                    break;
                case 0x09: /* write string */
                    {
                        uint8_t c, *ptr;
                        ptr = seg_to_linear(r->ds, r->edx);
                        for(;;) {
                            c = *ptr++;
                            if (c == '$')
                                break;
                            putchar(c);
                        }
                        r->eax = (r->eax & ~0xff) | '$';
                    }
                    break;
                case 0xff: /* extension: write eflags number in edx */
                    v = (int)r->edx;
#ifndef LINUX_VM86_IOPL_FIX
                    v &= ~0x3000;
#endif
                    printf("%08x\n", v);
                    break;
                default:
                unknown_int:
                    printf("unsupported int 0x%02x\n", int_num);
                    goto the_end;
                }
            }
            break;
        case VM86_SIGNAL:
            /* a signal came, we just ignore that */
            break;
        case VM86_STI:
            break;
        default:
            printf("ERROR: unhandled vm86 return code (0x%x)\n", ret);
            goto the_end;
        }
    }
 the_end:
    printf("VM86 end\n");
    munmap(vm86_mem, 0x110000);
}

/* exception tests */
#ifndef REG_EAX
#define REG_EAX EAX
#define REG_EBX EBX
#define REG_ECX ECX
#define REG_EDX EDX
#define REG_ESI ESI
#define REG_EDI EDI
#define REG_EBP EBP
#define REG_ESP ESP
#define REG_EIP EIP
#define REG_EFL EFL
#define REG_TRAPNO TRAPNO
#define REG_ERR ERR
#endif

jmp_buf jmp_env;
int v1;
int tab[2];

void sig_handler(int sig, siginfo_t *info, void *puc)
{
    struct ucontext *uc = puc;

    printf("si_signo=%d si_errno=%d si_code=%d",
           info->si_signo, info->si_errno, info->si_code);
    printf(" si_addr=0x%08lx",
           (unsigned long)info->si_addr);
    printf("\n");

    printf("trapno=0x%02x err=0x%08x",
           uc->uc_mcontext.gregs[REG_TRAPNO],
           uc->uc_mcontext.gregs[REG_ERR]);
    printf(" EIP=0x%08x", uc->uc_mcontext.gregs[REG_EIP]);
    printf("\n");
    longjmp(jmp_env, 1);
}

void test_exceptions(void)
{
    struct modify_ldt_ldt_s ldt;
    struct sigaction act;
    volatile int val;
    
    act.sa_sigaction = sig_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGFPE, &act, NULL);
    sigaction(SIGILL, &act, NULL);
    sigaction(SIGSEGV, &act, NULL);
    sigaction(SIGBUS, &act, NULL);
    sigaction(SIGTRAP, &act, NULL);

    /* test division by zero reporting */
    printf("DIVZ exception:\n");
    if (setjmp(jmp_env) == 0) {
        /* now divide by zero */
        v1 = 0;
        v1 = 2 / v1;
    }

    printf("BOUND exception:\n");
    if (setjmp(jmp_env) == 0) {
        /* bound exception */
        tab[0] = 1;
        tab[1] = 10;
        asm volatile ("bound %0, %1" : : "r" (11), "m" (tab[0]));
    }

    printf("segment exceptions:\n");
    if (setjmp(jmp_env) == 0) {
        /* load an invalid segment */
        asm volatile ("movl %0, %%fs" : : "r" ((0x1234 << 3) | 1));
    }
    if (setjmp(jmp_env) == 0) {
        /* null data segment is valid */
        asm volatile ("movl %0, %%fs" : : "r" (3));
        /* null stack segment */
        asm volatile ("movl %0, %%ss" : : "r" (3));
    }

    ldt.entry_number = 1;
    ldt.base_addr = (unsigned long)&seg_data1;
    ldt.limit = (sizeof(seg_data1) + 0xfff) >> 12;
    ldt.seg_32bit = 1;
    ldt.contents = MODIFY_LDT_CONTENTS_DATA;
    ldt.read_exec_only = 0;
    ldt.limit_in_pages = 1;
    ldt.seg_not_present = 1;
    ldt.useable = 1;
    modify_ldt(1, &ldt, sizeof(ldt)); /* write ldt entry */

    if (setjmp(jmp_env) == 0) {
        /* segment not present */
        asm volatile ("movl %0, %%fs" : : "r" (MK_SEL(1)));
    }

    /* test SEGV reporting */
    printf("PF exception:\n");
    if (setjmp(jmp_env) == 0) {
        val = 1;
        /* we add a nop to test a weird PC retrieval case */
        asm volatile ("nop");
        /* now store in an invalid address */
        *(char *)0x1234 = 1;
    }

    /* test SEGV reporting */
    printf("PF exception:\n");
    if (setjmp(jmp_env) == 0) {
        val = 1;
        /* read from an invalid address */
        v1 = *(char *)0x1234;
    }
    
    /* test illegal instruction reporting */
    printf("UD2 exception:\n");
    if (setjmp(jmp_env) == 0) {
        /* now execute an invalid instruction */
        asm volatile("ud2");
    }
    printf("lock nop exception:\n");
    if (setjmp(jmp_env) == 0) {
        /* now execute an invalid instruction */
        asm volatile("lock nop");
    }
    
    printf("INT exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("int $0xfd");
    }
    if (setjmp(jmp_env) == 0) {
        asm volatile ("int $0x01");
    }
    if (setjmp(jmp_env) == 0) {
        asm volatile (".byte 0xcd, 0x03");
    }
    if (setjmp(jmp_env) == 0) {
        asm volatile ("int $0x04");
    }
    if (setjmp(jmp_env) == 0) {
        asm volatile ("int $0x05");
    }

    printf("INT3 exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("int3");
    }

    printf("CLI exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("cli");
    }

    printf("STI exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("cli");
    }

    printf("INTO exception:\n");
    if (setjmp(jmp_env) == 0) {
        /* overflow exception */
        asm volatile ("addl $1, %0 ; into" : : "r" (0x7fffffff));
    }

    printf("OUTB exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("outb %%al, %%dx" : : "d" (0x4321), "a" (0));
    }

    printf("INB exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("inb %%dx, %%al" : "=a" (val) : "d" (0x4321));
    }

    printf("REP OUTSB exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("rep outsb" : : "d" (0x4321), "S" (tab), "c" (1));
    }

    printf("REP INSB exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("rep insb" : : "d" (0x4321), "D" (tab), "c" (1));
    }

    printf("HLT exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("hlt");
    }

    printf("single step exception:\n");
    val = 0;
    if (setjmp(jmp_env) == 0) {
        asm volatile ("pushf\n"
                      "orl $0x00100, (%%esp)\n"
                      "popf\n"
                      "movl $0xabcd, %0\n" 
                      "movl $0x0, %0\n" : "=m" (val) : : "cc", "memory");
    }
    printf("val=0x%x\n", val);
}

/* specific precise single step test */
void sig_trap_handler(int sig, siginfo_t *info, void *puc)
{
    struct ucontext *uc = puc;
    printf("EIP=0x%08x\n", uc->uc_mcontext.gregs[REG_EIP]);
}

const uint8_t sstep_buf1[4] = { 1, 2, 3, 4};
uint8_t sstep_buf2[4];

void test_single_step(void)
{
    struct sigaction act;
    volatile int val;
    int i;

    val = 0;
    act.sa_sigaction = sig_trap_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGTRAP, &act, NULL);
    asm volatile ("pushf\n"
                  "orl $0x00100, (%%esp)\n"
                  "popf\n"
                  "movl $0xabcd, %0\n" 

                  /* jmp test */
                  "movl $3, %%ecx\n"
                  "1:\n"
                  "addl $1, %0\n"
                  "decl %%ecx\n"
                  "jnz 1b\n"

                  /* movsb: the single step should stop at each movsb iteration */
                  "movl $sstep_buf1, %%esi\n"
                  "movl $sstep_buf2, %%edi\n"
                  "movl $0, %%ecx\n"
                  "rep movsb\n"
                  "movl $3, %%ecx\n"
                  "rep movsb\n"
                  "movl $1, %%ecx\n"
                  "rep movsb\n"

                  /* cmpsb: the single step should stop at each cmpsb iteration */
                  "movl $sstep_buf1, %%esi\n"
                  "movl $sstep_buf2, %%edi\n"
                  "movl $0, %%ecx\n"
                  "rep cmpsb\n"
                  "movl $4, %%ecx\n"
                  "rep cmpsb\n"
                  
                  /* getpid() syscall: single step should skip one
                     instruction */
                  "movl $20, %%eax\n"
                  "int $0x80\n"
                  "movl $0, %%eax\n"
                  
                  /* when modifying SS, trace is not done on the next
                     instruction */
                  "movl %%ss, %%ecx\n"
                  "movl %%ecx, %%ss\n"
                  "addl $1, %0\n"
                  "movl $1, %%eax\n"
                  "movl %%ecx, %%ss\n"
                  "jmp 1f\n"
                  "addl $1, %0\n"
                  "1:\n"
                  "movl $1, %%eax\n"
                  "pushl %%ecx\n"
                  "popl %%ss\n"
                  "addl $1, %0\n"
                  "movl $1, %%eax\n"
                  
                  "pushf\n"
                  "andl $~0x00100, (%%esp)\n"
                  "popf\n"
                  : "=m" (val) 
                  : 
                  : "cc", "memory", "eax", "ecx", "esi", "edi");
    printf("val=%d\n", val);
    for(i = 0; i < 4; i++)
        printf("sstep_buf2[%d] = %d\n", i, sstep_buf2[i]);
}

/* self modifying code test */
uint8_t code[] = {
    0xb8, 0x1, 0x00, 0x00, 0x00, /* movl $1, %eax */
    0xc3, /* ret */
};

asm("smc_code2:\n"
    "movl 4(%esp), %eax\n"
    "movl %eax, smc_patch_addr2 + 1\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "smc_patch_addr2:\n"
    "movl $1, %eax\n"
    "ret\n");

typedef int FuncType(void);
extern int smc_code2(int);
void test_self_modifying_code(void)
{
    int i;

    printf("self modifying code:\n");
    printf("func1 = 0x%x\n", ((FuncType *)code)());
    for(i = 2; i <= 4; i++) {
        code[1] = i;
        printf("func%d = 0x%x\n", i, ((FuncType *)code)());
    }

    /* more difficult test : the modified code is just after the
       modifying instruction. It is forbidden in Intel specs, but it
       is used by old DOS programs */
    for(i = 2; i <= 4; i++) {
        printf("smc_code2(%d) = %d\n", i, smc_code2(i));
    }
}
    
static void *call_end __init_call = NULL;

int main(int argc, char **argv)
{
    void **ptr;
    void (*func)(void);

    ptr = &call_start + 1;
    while (*ptr != NULL) {
        func = *ptr++;
        func();
    }
    test_bsx();
    test_mul();
    test_jcc();
    test_floats();
    test_bcd();
    test_xchg();
    test_string();
    test_misc();
    test_lea();
    test_segs();
    test_code16();
    test_vm86();
    test_exceptions();
    test_self_modifying_code();
    test_single_step();
    return 0;
}
