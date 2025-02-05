/*
 *  Copyright(c) 2019-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Test Purpose:
 * Verify that tlbmissx and tlbmissrw do not set the syscfg.tl bit
 * The HW spec says:
 *     "TLBLOCK is acquired automatically whenever a hardware thread raises a
 *      TLB miss-RW or TLBmiss-X exception."
 * The casual reader would assume that a miss handler would implicitly have
 * the lock, that apparently
 * isn't the case.
 */

.global start
start:
    r0 = ##evb
    evb = r0
    r0 = ##0
    ssr = r0
    jump #setup

#define tlb_index r11
#define stack r29
#define data r18
tlb_index = ##0x00000007

.org 0x100

evb:
    jump #reset
    jump #nmi
    jump #error
    jump #0
    jump #tlbmissx
    jump #0
    jump #tlbmissrw


setup:
    {
        r1 = ##0xc009b800
        r0 = ##0xf7137010
    }
    tlb_index = add(tlb_index, #1)
    tlbw(r1:0,tlb_index)

/* Enable MMU */
    r2 = ##0x0085a07f
    syscfg = r2

/* Test setup */
    r12 = #0x12
    r0 = #0x6
    r7 = ##0x77777777
    r6 = ##0x66666666
    data = ##0xf2000000
    stack = ##0x9ba01000
    jump ##.L_server_loop

/* event vector handlers */
reset:
    r2 = #1
    stop(r0)
nmi:
    r2 = #1
    stop(r0)
error:
    r2 = #1
    stop(r0)


/*
 * Can only handle a single ex fault.
 */
tlbmissx:
  r0 = syscfg
  r1 = #0x800
/*
 * Fail if we automatically start setting SYSCFG:TL again
 */
    r0 = and(r0, r1)
    {
        p0 = cmp.eq(r0, r1); if (p0.new) jump:t .Lfailmissx
    }
    {
        r1 = ##0xc009b900
        r0 = ##0xf7137210
    }
    tlb_index = add(tlb_index, #1)
    tlbw(r1:0,tlb_index)
    tlbunlock
    rte
    stop(r0);
.Lfailmissx:
    r2 = #1
    stop(r2);

/*
 * Can only handle a stack fault and a data fault
 */
tlbmissrw:
    r0 = syscfg
    r1 = #0x800
/*
 * Fail if we automatically start setting SYSCFG:TL again
 */
    r0 = and(r0, r1)
    {
        p0 = cmp.eq(r0, r1); if (p0.new) jump:t .Lfailmissrw
    }
    r0 = badva
    p0 = cmp.eq (stack, r0) // missed the stack
    if (!p0) jump .Ldata
    {
        r1 = ##0xc009ba00
        r0 = ##0xf7137210
    }
  jump #.Ldone
.Ldata:
    {
        r1 = ##0xc00f2000
        r0 = ##0xf71e4010
    }
.Ldone:
    tlb_index = add(tlb_index, #1)
    tlbw(r1:0,tlb_index)
    tlbunlock
    rte
.Lfailmissrw:
    r2 = #1
    stop(r2);



.org 0x100000
  nop
.Lpass:
   r2 = #0
   stop(r0);
   trap0(#0x18)
.L_server_loop:
{
    p0 = cmp.eq(r0,#-0x1)
    if (!p0.new) jump:t .Lpass
    memd(stack) = r7:6; // S1 store to stack will also fault
    memw(data) = r12; // S0 store will fault
}
/*
 * We should not get here:
 */
   r2 = #1
   stop(r0);
