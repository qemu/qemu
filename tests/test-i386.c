#include <stdlib.h>
#include <stdio.h>
#include <math.h>

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

/* XXX: currently no A flag */
#define CC_MASK (CC_C | CC_P | CC_Z | CC_S | CC_O)

#define __init_call	__attribute__ ((unused,__section__ (".initcall.init")))

static void *call_start __init_call = NULL;

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
    asm("movl $1, %0\n\t"\
        "cmpl %2, %1\n\t"\
        JCC " 1f\n\t"\
        "movl $0, %0\n\t"\
        "1:\n\t"\
        : "=r" (res)\
        : "r" (v1), "r" (v2));\
    printf("%-10s %d\n", JCC, res);\
}

/* various jump tests */
void test_jcc(void)
{
    int res;

    TEST_JCC("jne", 1, 1);
    TEST_JCC("jne", 1, 0);

    TEST_JCC("je", 1, 1);
    TEST_JCC("je", 1, 0);

    TEST_JCC("jl", 1, 1);
    TEST_JCC("jl", 1, 0);
    TEST_JCC("jl", 1, -1);

    TEST_JCC("jle", 1, 1);
    TEST_JCC("jle", 1, 0);
    TEST_JCC("jle", 1, -1);

    TEST_JCC("jge", 1, 1);
    TEST_JCC("jge", 1, 0);
    TEST_JCC("jge", -1, 1);

    TEST_JCC("jg", 1, 1);
    TEST_JCC("jg", 1, 0);
    TEST_JCC("jg", 1, -1);

    TEST_JCC("jb", 1, 1);
    TEST_JCC("jb", 1, 0);
    TEST_JCC("jb", 1, -1);

    TEST_JCC("jbe", 1, 1);
    TEST_JCC("jbe", 1, 0);
    TEST_JCC("jbe", 1, -1);

    TEST_JCC("jae", 1, 1);
    TEST_JCC("jae", 1, 0);
    TEST_JCC("jae", 1, -1);

    TEST_JCC("ja", 1, 1);
    TEST_JCC("ja", 1, 0);
    TEST_JCC("ja", 1, -1);


    TEST_JCC("jp", 1, 1);
    TEST_JCC("jp", 1, 0);

    TEST_JCC("jnp", 1, 1);
    TEST_JCC("jnp", 1, 0);

    TEST_JCC("jo", 0x7fffffff, 0);
    TEST_JCC("jo", 0x7fffffff, -1);

    TEST_JCC("jno", 0x7fffffff, 0);
    TEST_JCC("jno", 0x7fffffff, -1);

    TEST_JCC("js", 0, 1);
    TEST_JCC("js", 0, -1);
    TEST_JCC("js", 0, 0);

    TEST_JCC("jns", 0, 1);
    TEST_JCC("jns", 0, -1);
    TEST_JCC("jns", 0, 0);
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
    test_lea();
    test_jcc();
    return 0;
}
