/*
 * crt0-tc2x.S -- Startup code for GNU/TriCore applications.
 *
 * Copyright (C) 1998-2014 HighTec EDV-Systeme GmbH.
 *
 * This file is part of GCC.
 *
 * GCC is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * GCC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Under Section 7 of GPL version 3, you are granted additional
 * permissions described in the GCC Runtime Library Exception, version
 * 3.1, as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License and
 * a copy of the GCC Runtime Library Exception along with this program;
 * see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
 * <http://www.gnu.org/licenses/>.  */

/* Define the Derivate Name as a hexvalue. This value
 * is built-in defined in tricore-c.c (from tricore-devices.c)
 * the derivate number as a hexvalue (e.g. TC1796 => 0x1796
 * This name will be used in the memory.x Memory description to
 * to confirm that the crt0.o and the memory.x will be get from
 * same directory
 */
    .section ".startup_code", "ax", @progbits
    .global _start
    .type _start,@function

/* default BMI header (only TC2xxx devices) */
    .word   0x00000000
    .word   0xb3590070
    .word   0x00000000
    .word   0x00000000
    .word   0x00000000
    .word   0x00000000
    .word   0x791eb864
    .word   0x86e1479b

_start:
    .code32
    j   _startaddr
    .align  2

_startaddr:
    /*
     * initialize user and interrupt stack pointers
     */
    movh.a  %sp,hi:__USTACK         # load %sp
    lea %sp,[%sp]lo:__USTACK
    movh    %d0,hi:__ISTACK         # load $isp
    addi    %d0,%d0,lo:__ISTACK
    mtcr    $isp,%d0
    isync

#;  install trap handlers

    movh    %d0,hi:first_trap_table     #; load $btv
    addi    %d0,%d0,lo:first_trap_table
    mtcr    $btv,%d0
    isync

    /*
     * initialize call depth counter
     */

    mfcr    %d0,$psw
    or  %d0,%d0,0x7f            # disable call depth counting
    andn    %d0,%d0,0x80            # clear CDE bit
    mtcr    $psw,%d0
    isync

    /*
     * initialize access to system global registers
     */

    mfcr    %d0,$psw
    or  %d0,%d0,0x100           # set GW bit
    mtcr    $psw,%d0
    isync

    /*
     * initialize SDA base pointers
     */
    .global _SMALL_DATA_,_SMALL_DATA2_,_SMALL_DATA3_,_SMALL_DATA4_
    .weak _SMALL_DATA_,_SMALL_DATA2_,_SMALL_DATA3_,_SMALL_DATA4_

    movh.a  %a0,hi:_SMALL_DATA_     # %a0 addresses .sdata/.sbss
    lea %a0,[%a0]lo:_SMALL_DATA_
    movh.a  %a1,hi:_SMALL_DATA2_        # %a1 addresses .sdata2/.sbss2
    lea %a1,[%a1]lo:_SMALL_DATA2_
    movh.a  %a8,hi:_SMALL_DATA3_        # %a8 addresses .sdata3/.sbss3
    lea %a8,[%a8]lo:_SMALL_DATA3_
    movh.a  %a9,hi:_SMALL_DATA4_        # %a9 addresses .sdata4/.sbss4
    lea %a9,[%a9]lo:_SMALL_DATA4_

    /*
     * reset access to system global registers
     */

    mfcr    %d0,$psw
    andn    %d0,%d0,0x100           # clear GW bit
    mtcr    $psw,%d0
    isync

    /*
     * initialize context save areas
     */

    jl  __init_csa



    /*
     * handle clear table (i.e., fill BSS with zeros)
     */

    jl  __clear_table_func


    /*
     * handle copy table (support for romable code)
     */

    jl  __copy_table_func


    /*
     * _exit (main (0, NULL));
     */
    mov %d4,0               # argc = 0
    sub.a   %sp,8
    st.w    [%sp]0,%d4
    st.w    [%sp]4,%d4
    mov.aa  %a4,%sp             # argv

    call    main                # int retval = main (0, NULL);
    mov.a   %a14,%d2        # move exit code to match trap handler
    j   _exit               # _exit (retval);

    debug                   # should never come here


    /*
     * initialize context save areas (CSAs), PCXI, LCX and FCX
     */

    .global __init_csa
    .type __init_csa,function

__init_csa:
    movh    %d0,0
    mtcr    $pcxi,%d0
    isync
    movh    %d0,hi:__CSA_BEGIN      #; %d0 = begin of CSA
    addi    %d0,%d0,lo:__CSA_BEGIN
    addi    %d0,%d0,63          #; force alignment (2^6)
    andn    %d0,%d0,63
    movh    %d2,hi:__CSA_END        #; %d2 = end of CSA
    addi    %d2,%d2,lo:__CSA_END
    andn    %d2,%d2,63          #; force alignment (2^6)
    sub %d2,%d2,%d0
    sh  %d2,%d2,-6          #; %d2 = number of CSAs
    mov.a   %a3,%d0             #; %a3 = address of first CSA
    extr.u  %d0,%d0,28,4            #; %d0 = segment << 16
    sh  %d0,%d0,16
    lea %a4,0               #; %a4 = previous CSA = 0
    st.a    [%a3],%a4           #; store it in 1st CSA
    mov.aa  %a4,%a3             #; %a4 = current CSA
    lea %a3,[%a3]64         #; %a3 = %a3->nextCSA
    mov.d   %d1,%a3
    extr.u  %d1,%d1,6,16            #; get CSA index
    or  %d1,%d1,%d0         #; add segment number
    mtcr    $lcx,%d1            #; initialize LCX
    add %d2,%d2,-2          #; CSAs to initialize -= 2
    mov.a   %a5,%d2             #; %a5 = loop counter
csa_loop:
    mov.d   %d1,%a4             #; %d1 = current CSA address
    extr.u  %d1,%d1,6,16            #; get CSA index
    or  %d1,%d1,%d0         #; add segment number
    st.w    [%a3],%d1           #; store "nextCSA" pointer
    mov.aa  %a4,%a3             #; %a4 = current CSA address
    lea %a3,[%a3]64         #; %a3 = %a3->nextCSA
    loop    %a5,csa_loop            #; repeat until done

    mov.d   %d1,%a4             #; %d1 = current CSA address
    extr.u  %d1,%d1,6,16            #; get CSA index
    or  %d1,%d1,%d0         #; add segment number
    mtcr    $fcx,%d1            #; initialize FCX
    isync
    ji  %a11




    /*
     * handle clear table (i.e., fill BSS with zeros)
     */
    .global __clear_table_func
    .type __clear_table_func,@function

__clear_table_func:
    mov %d14,0              # %e14 = 0
    mov %d15,0
    movh.a  %a13,hi:__clear_table       # %a13 = &first table entry
    lea %a13,[%a13]lo:__clear_table

__clear_table_next:
    ld.a    %a15,[%a13+]4           # %a15 = current block base
    ld.w    %d3,[%a13+]4            # %d3 = current block length
    jeq %d3,-1,__clear_table_done   # length == -1 => end of table
    sh  %d0,%d3,-3          # %d0 = length / 8 (doublewords)
    and %d1,%d3,7           # %d1 = length % 8 (rem. bytes)
    jz  %d0,__clear_word        # block size < 8 => clear word
    addi    %d0,%d0,-1          # else doublewords -= 1
    mov.a   %a2,%d0             # %a2 = loop counter
__clear_dword:
    st.d    [%a15+]8,%e14           # clear one doubleword
    loop    %a2,__clear_dword
__clear_word:
    jz  %d1,__clear_table_next
    sh  %d0,%d1,-2          # %d0 = length / 4 (words)
    and %d1,%d1,3           # %d1 = length % 4 (rem. bytes)
    jz  %d0,__clear_hword       # block size < 4 => clear hword
    st.w    [%a15+]4,%d15           # clear one word
__clear_hword:
    jz  %d1,__clear_table_next
    sh  %d0,%d1,-1          # %d0 = length / 2 (halfwords)
    and %d1,%d1,1           # %d1 = length % 2 (rem. bytes)
    jz  %d0,__clear_byte        # block size < 2 => clear byte
    st.h    [%a15+]2,%d15           # clear one halfword
__clear_byte:
    jz  %d1,__clear_table_next
    st.b    [%a15],%d15         # clear one byte
    j   __clear_table_next      # handle next clear table entry
__clear_table_done:

    ji  %a11



    /*
     * handle copy table (support for romable code)
     */
    .global __copy_table_func
    .type __copy_table_func,@function

__copy_table_func:
    movh.a  %a13,hi:__copy_table        # %a13 = &first table entry
    lea %a13,[%a13]lo:__copy_table

__copy_table_next:
    ld.a    %a15,[%a13+]4           # %a15 = src address
    ld.a    %a14,[%a13+]4           # %a14 = dst address
    ld.w    %d3,[%a13+]4            # %d3 = block length
    jeq %d3,-1,__copy_table_done    # length == -1 => end of table
    sh  %d0,%d3,-3          # %d0 = length / 8 (doublewords)
    and %d1,%d3,7           # %d1 = length % 8 (rem. bytes)
    jz  %d0,__copy_word         # block size < 8 => copy word
    addi    %d0,%d0,-1          # else doublewords -= 1
    mov.a   %a2,%d0             # %a2 = loop counter
__copy_dword:
    ld.d    %e14,[%a15+]8           # copy one doubleword
    st.d    [%a14+]8,%e14
    loop    %a2,__copy_dword
__copy_word:
    jz  %d1,__copy_table_next
    sh  %d0,%d1,-2          # %d0 = length / 4 (words)
    and %d1,%d1,3           # %d1 = length % 4 (rem. bytes)
    jz  %d0,__copy_hword        # block size < 4 => copy hword
    ld.w    %d14,[%a15+]4           # copy one word
    st.w    [%a14+]4,%d14
__copy_hword:
    jz  %d1,__copy_table_next
    sh  %d0,%d1,-1          # %d0 = length / 2 (halfwords)
    and %d1,%d1,1           # %d1 = length % 2 (rem. bytes)
    jz  %d0,__copy_byte         # block size < 2 => copy byte
    ld.h    %d14,[%a15+]2           # copy one halfword
    st.h    [%a14+]2,%d14
__copy_byte:
    jz  %d1,__copy_table_next
    ld.b    %d14,[%a15]0            # copy one byte
    st.b    [%a14],%d14
    j   __copy_table_next       # handle next copy table entry
__copy_table_done:

    ji  %a11

_exit:
    movh.a %a15, hi:__TESTDEVICE
    lea %a15,[%a15]lo:__TESTDEVICE
    mov.d %d2, %a14
    st.w [%a15], %d2 # write exit code to testdevice
    debug

/*============================================================================*
 * Exception handlers (exceptions in startup code)
 *
 * This is a minimal trap vector table, which consists of eight
 * entries, each consisting of eight words (32 bytes).
 *============================================================================*/


#;  .section .traptab, "ax", @progbits

.macro trapentry from=0, to=7
    mov.u   %d14, \from << 8
    add %d14,%d14,%d15
    mov.a   %a14,%d14
    addih.a %a14,%a14,0 # if we trap, we fail
    j   _exit
0:
    j   0b
    nop
    rfe
    .align 5

    .if \to-\from
    trapentry "(\from+1)",\to
    .endif
.endm

    .align 8
    .global first_trap_table
first_trap_table:
    trapentry 0, 7

