/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */
#ifndef _ASM_REGDEF_H
#define _ASM_REGDEF_H

#define zero    $r0     /* wired zero */
#define ra      $r1     /* return address */
#define tp      $r2
#define sp      $r3     /* stack pointer */
#define v0      $r4     /* return value - caller saved */
#define v1      $r5
#define a0      $r4     /* argument registers */
#define a1      $r5
#define a2      $r6
#define a3      $r7
#define a4      $r8
#define a5      $r9
#define a6      $r10
#define a7      $r11
#define t0      $r12    /* caller saved */
#define t1      $r13
#define t2      $r14
#define t3      $r15
#define t4      $r16
#define t5      $r17
#define t6      $r18
#define t7      $r19
#define t8      $r20
                        /* $r21: Temporarily reserved */
#define fp      $r22    /* frame pointer */
#define s0      $r23    /* callee saved */
#define s1      $r24
#define s2      $r25
#define s3      $r26
#define s4      $r27
#define s5      $r28
#define s6      $r29
#define s7      $r30
#define s8      $r31

#define gr0     $r0
#define gr1     $r1
#define gr2     $r2
#define gr3     $r3
#define gr4     $r4
#define gr5     $r5
#define gr6     $r6
#define gr7     $r7
#define gr8     $r8
#define gr9     $r9
#define gr10    $r10
#define gr11    $r11
#define gr12    $r12
#define gr13    $r13
#define gr14    $r14
#define gr15    $r15
#define gr16    $r16
#define gr17    $r17
#define gr18    $r18
#define gr19    $r19
#define gr20    $r20
#define gr21    $r21
#define gr22    $r22
#define gr23    $r23
#define gr24    $r24
#define gr25    $r25
#define gr26    $r26
#define gr27    $r27
#define gr28    $r28
#define gr29    $r29
#define gr30    $r30
#define gr31    $r31

#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4
#define STT_COMMON  5
#define STT_TLS     6

#define ASM_NL           ;

#endif /* _ASM_REGDEF_H */
