/*
 *  Copyright(c) 2024-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "crt0.inc"
	.equ TLB_FIXED_ENTRIES, 6

	.org	0x20			/* This must be at address 0x20 */
EventVectorBase:
	.word .EventVectors

/* This can vary based on the revid of the part:
   64, 128, 192.  Most are 128 */
_NumTLBEntries:
	.word 127

TLBMapTable:
	.word UPTE_START

CoreDump:
	.word RegDump

	.subsection 0

	/* Make sure that data and code don't end up in the same L2 cache-line. */
        .p2align 6, 0

 	.global hexagon_start_init
 	.type hexagon_start_init, @function
hexagon_start_init:
.Init:
        /* Clean up house (make sure that R0 is initialized before DCKILL). */
	dckill
	isync
	ickill
	isync

.InitSSR:
	/* SFD = 0, IE = 0, UM = 0, EX = 0, ASID = 0 */
	r0 = #0
	ssr = r0
	isync

	/* Setup events */
.InitVector:
	ReadFrom EventVectorBase
	evb = r0

.InitStack:
	ReadFrom exc_stack_tops
	sgp0 = r0

.InitFramekey:
	r0 = #0
	framekey = r0

	/* Configure cycle counter. */
.InitPcycle:
	r1 = #1
	r0 = syscfg
	r0 = insert (r1, #1, #6)
	syscfg = r0

	/* Configure IMT/DMT. */
.InitDMT:
	r1 = #1
	r0 = syscfg
	r0 = insert (r1, #1, #15)
	syscfg = r0
.InitQoS:
	r1 = #1
	r0 = syscfg
	r0 = insert (r1, #1, #13)
	syscfg = r0
1:
.InitXE:
	r1 = #1
	r0 = ssr
	r0 = insert (r1, #1, #31)
	ssr = r0

        //{ 0x4066, 0x4, 0x7F, 0, 4 }, // v66a_512
	{
		r0 = #0x2c // JTLB size
		r2 = cfgbase
	}
	r1 = asl(r2, #5)
	r0 = memw_phys(r0, r1)
	{
		r0 = add(r0, #-1);
		memw(##_tlbmax) = r0.new
	}

	{
		r0 = #0x40 // L2 Tag size
		r2 = cfgbase
	}
	r0 = memw_phys(r0, r1)
	r1 = #0;
	p0 = cmp.eq(r0, #0x400)
	{
		if (p0) r1 = #5
		if (p0) jump 1f
	}
	p0 = cmp.eq(r0, #0x200)
	{
		if (p0) r1 = #4
		if (p0) jump 1f
	}
	p0 = cmp.eq(r0, #0x100)
	{
		if (p0) r1 = #3
		if (p0) jump 1f
	}
	p0 = cmp.eq(r0, #0x080)
	{
		if (p0) r1 = #2
		if (p0) jump 1f
	}
1:
	memw(##_l2cfg) = r1

/* L2 config sequence:
 *    1	- Disable prefetching by clearing HFd/i bits in ssr/ccr
 */
        r0 = ccr
	r3 = #0
	r0 = insert (r3, #4, #16)	/* Clear HFi, HFd, HFiL2 HFdL2 bits */
	ccr = r0

	/* Configure L2 cache. */
    	r0 = syscfg
	r0 = insert (r3, #3, #16)	/* Set L2 size to 0 via L2CFG. */


/* L2 config sequence:
 *    2	- execute an isync which is aligned to a 32byte boundary.
 */
	.p2alignl 5, 0x7f00c000
	isync

/* L2 config sequence:
 *    3	- execute an syncht insn to insure there are no outstanding
 *        memory transactions.
 */
        syncht

/* L2 config sequence:
 *    4	- Set the desired L2 size for < V4 (set to 0 for >= V4).
 */
	syscfg = r0
	isync

/* L2 config sequence:
 *    5	- Execute the L2KILL insn to initiate the cache.
 */
	l2kill
	syncht

/* L2 config sequence:
 *    6	- Set the desired L2 size.
 */
	r2 = memw(##_l2cfg)
        r3 = #0x5
	r3 = min (r2, r3)	        /* min between desired and hwmax */
	r0 = insert (r3, #4, #16)       /* Set L2 size via L2CFG. */
	syscfg = r0
	isync

	/* Configure L1 caches. */
.InitCache:
	r1 = #0
	r1 = #1
	r2 = syscfg
	r2 = insert (r1, #1, #1)
	r2 = insert (r0, #1, #2)

	r1 = #1
	r2 = insert (r1, #1, #23)

	syscfg = r2
	isync

	/* BEGIN code to turn on translation */
.InitTLB:
	// V65 an later use a table for this stuff, should get a table for all of it!
	r0 = memw(##_tlbmax)

	/* Clear TLB and store the number of TLBs */
	{
		r3:2 = combine(#0,#0)
		memw(##_NumTLBEntries) = r0
	}

	loop0(.InitTLBLoop, r0)
.falign
.InitTLBLoop:
	tlbw(r3:2,r0)
	r0 = add (r0, #-1)
	{}:endloop0
	isync

.InitTLBGlobal:				/* Fixed entry for everything. */
        AddrOf _start, r2
        r2 = lsr (r2, #12)

	AddrOf 0xc3f00000, r1		/* Global, 1-1 mapping. */
	AddrOf 0xf7000000, r0		/* Full perms, fully cacheable WB */
        r1 = or (r1, r2)		/* 1M translation */
        r0 |= asl (r2,#1)
	r0 = setbit(r0,#4)
	r0 = and(r0,#-16)
	r2 = #0
	tlbw(r1:0,r3)

	/* TODO Should there be a TLB entry for TCM too? */

	r0 = syscfg
	r0 = setbit (r0, #0)		/* Turn the MMU on. */
	syscfg = r0
	isync

.InitInt:
	/* Set up rising edge triggered interrupts */
        r0 = #0
	imask = r0
	r1 = #-1
	cswi (r1)

        /* Enable interrupts globally. */
        r0 = ssr
	r0 = setbit (r0, #18)
        ssr= r0

        r0 = syscfg
        r0 = setbit (r0, #4)
        syscfg = r0
        isync

        /* Set up input params to Angel call   */
        r0 = #22
        AddrOf setHeapAngelCallParams, r1
        trap0 (#0)

.PreMain:
        AddrOf hexagon_pre_main, r28
        jumpr r28
        .size hexagon_start_init, . - hexagon_start_init

.global	qdsp6_start_init
.set	qdsp6_start_init, \
	hexagon_start_init

/* (At this point the machine is mostly ready for normal execution */

 /* This code is jumped to when we start a new thread.        */
 /* It reads some values out of memory and uses them          */
 /* to begin execution.                                       */
 /* The code supports going to a function of the type:        */
 /*     void foo (void *arg);                                  */
 /* or                                                        */
 /*     void foo (int arg);                                    */
 /* All we have to do is get the location of "foo", the       */
 /* value for "arg", and set up the stack.                    */
 /* This stuff has been set up for us by thread_create, below.*/
 /* Under the OS, we have no need for this, it is merely for  */
 /* trying multithreaded applications on the raw hardware.    */

	.p2align 4
	.weak thread_stop
	.type thread_stop, @function
thread_stop:
{
	r0 = htid
	r1 = #1
}
	r1 = lsl (r1, r0)
	stop (r1)

	.p2align 4

	.type event_handle_reset, @function

event_handle_reset:
	r1 = htid /* do not alter until final register initialization */

	{
		r28 = ##(start_pc)
		r29 = ##(start_sp)
	}

	r2 = #0 				/* UM = 0 EX = 0 IE = 0 ASID = 0 */
	ssr = r2
	isync
	imask = r2

	r2 = ##(exc_stack_tops)
	r2 = memw (r2+r1<<#2)
	sgp0 = r2

	/* Initialize GP to the start of the global data area. */
	//r2 = ##(_SDA_BASE_)
	//gp = r2

        r2.h = #4
	r2.l = #0
	ssr = r2 /* Turn on interrupts */

	r3 = #1
	r2 = ssr
	r2 = insert (r3, #1, #31)
	ssr = r2

	r2.h = #0x1  /* Enable cache fetching */
	usr  = r2

	r0 = #1
	r2 = #1
	r0 |= asl (r2, #1)
	r2 = ccr
	r2 = insert (r0, #2, #16)
	/* Enable dcfetch and l2fetch. */
	r2 = setbit (r2, #20)
	ccr = r2

	isync

	{
		r2 = ##framekey_tbl
		r3 = ##stack_size
	}
	{
		r2 = memw(r2+r1<<#2) /* load framekey from memory array */
		r3 = memw(r3+r1<<#2) /* load stack_size from memory array */
	}
	{
		framekey = r2 /* store into framekey register */
		r2 = memw (sp+r1<<#2)
	}
	r3 = sub(r2, r3) /* framelimt = sp-stack_size) */
	framelimit = r3 /* store into framelimit register */

	{
		r28 = memw (r28+r1<<#2)
		sp = memw (sp+r1<<#2)
		fp = #0
	}

        {
                r0 = ##(start_param)
	        lr = ##(thread_stop)
        }
	fp = #0
	r1 = htid
	r0 = memw (r0+r1<<#2)

	jump thread_start

	.size event_handle_reset, . - event_handle_reset

        .global __coredump
        .type coredump, @function
        .set __coredump, coredump
coredump:
        r0 = ssr
        r0 = clrbit (r0, #16) /* UM = 0 */
        r0 = clrbit (r0, #17) /* EX = 0 */
        ssr = r0
        isync
        r0 = #0xCD
        trap0 (#0)
        r2 = #-1
        r0 = #-1
        stop (r0)
	.size event_core_dump, . - event_core_dump

        .type event_handle_nmi, @function
event_handle_nmi:
        r0 = #1
        stid = r0
        jump coredump
        .size event_handle_nmi, . - event_handle_nmi

        .type event_handle_error, @function
event_handle_error:
        r0 = #2
        stid = r0
        jump coredump
        .size event_handle_error, . - event_handle_error

        .type event_handle_rsvd, @function
event_handle_rsvd:
        r0.h = #0xdead
        r0.l = #0xbeef
        stid = r0
        jump coredump
        .size event_handle_rsvd, . - event_handle_rsvd

	.global thread_start
	.type thread_start, @function
thread_start:
		jumpr r28
	.size thread_start, . - thread_start

 /* TLB HANDLING                                                  */
 /* There are a few strategies we have tried for TLB handling.    */
 /* The first is just to map every page 1:1 for virtual:physical  */
 /* This means we have nothing to look up but no flexibility      */
 /* The strategy implemented here is to divide memory into        */
 /* a bunch of 1MB pages.  Each page is by default set to the     */
 /* corresponding physical 1M page, but the translation (and the  */
 /* cacheability) can be changed with the add_translation function*/
 /* below.                                                        */
 /* We have to keep the table in memory, and it's down in the data*/
 /* section.                                                      */
 /* The page at address 0 is always kept in the TLB.              */
 /* You will run into problems if the data gets pushed out into   */
 /* another page, because you don't have a translation for the    */
 /* data you need to do the translation!                          */
 /* The solution is to put the translation table (and probably    */
 /* the TLB fill code) in special section (s) that go near address 0 */
 /* You can set that up in the linker script.                     */
 /* TLB miss because of eXecution                                 */
 /* See HEXAGON Architecture System-Level Spec for more information */



 	.subsection 0

	.p2align 6
	.global event_handle_tlbmissx
 	.type event_handle_tlbmissx, @function

event_handle_tlbmissx:
	crswap (sp, sgp0)
	sp = add (sp, #-64)
	/* Save off state */
	{
		memd (sp + #0) = r1:0
		memd (sp + #8) = r3:2
	}
	{
		memd (sp + #16) = r5:4
		memd (sp + #24) = r7:6
	}
	{
		memd (sp + #32) = r9:8
		r9 = p3:0
	}
	r8 = ssr
	r7 = elr
	p1 = tstbit (r8, #0)
	{
		/* Calculate 4K page index */
		r7 = lsr (r7, #12)
		/* Check for next page hit */
		if (!p1) jump 1f
		r0 = ##(__tlb_idx)
	}
	r7 = add (r7, #1)
1:
	{
		r1 = memw(##_tlb_fixed_entries)	/* First non-fixed entry. */
		r3 = memw(##_NumTLBEntries)
	}
	/* Atomically increment index */
	/* NEVER overwrite fixed entries */
1:
	r6 = memw_locked (r0)
	{
		r6 = add (r6, #1)
		/* This was hard coded to p0 = cmp.ge(r6, #NUM_TLB_ENTRIES)
		   Now we are using 2 registers so switch to the equivalent
		   p0 = !cmp.gt(r3, r6) */
		p0 = !cmp.gt (r3, r6)
	}
	/* Will never store a number greater than
           _NumTLBEntries in &__tlb_idx */
	r6 = mux (p0, r1, r6)
	memw_locked (r0, p0) = r6
	if (!p0) jump 1b              /* Retry, lost reservation. */

	{
		r7 = lsr (r7, #8) /* 1M page index */
		r3 = memw (##TLBMapTable)
	}
	r3 = addasl (r3, r7, #1)
	{
		r3 = memh (r3)
		r7 = asl (r7, #8) /* VPN */
	}
	r5 = extractu (r3, #12, #4)
	{
		r4 = extractu (r3, #4, #0)
		r0 = #0x0010 /* 1M */
		r1 = #0
	}
	{
		r4 = asl (r4, #24)
		r1.h = #0xc000
		r0.h = #0xf000
	}
1:
	{
		r1 = or (r1, r7)	/* c000_0000 + VPN */
		r0 |= asl(r5,#9)	/* f000_0000 + PPD */
	}
	r0 = or (r0, r4)
	/* Get Lock */
	tlblock
	r5 = tlbp(r1)
	p0 = tstbit (r5, #31)
	if (!p0) jump 1f

	tlbw(r1:0,r6)
	isync

1:
	tlbunlock

	p3:0 = r9
	{
		r9:8 = memd (sp + #32)
		r7:6 = memd (sp + #24)
	}
	{
		r5:4 = memd (sp + #16)
		r3:2 = memd (sp + #8)
	}
	{
		r1:0 = memd (sp + #0)
		sp = add (sp, #64)
	}
	crswap (sp, sgp0)
	rte

	.size  .event_handle_tlbmissx, . - event_handle_tlbmissx

 /* TLB Miss RW                                            */
 /* Basically the same as TLB MissX, but we get            */
 /* The address from BADVA instead of EVB... see the       */
 /* HEXAGON Architecture System-level Spec for more details. */

	.p2align 6

	.global event_handle_tlbmissrw
 	.type event_handle_tlbmissrw, @function

event_handle_tlbmissrw:
	crswap (sp, sgp0)
	sp = add (sp, #-64)
	{
		memd (sp + #0) = r1:0
		memd (sp + #8) = r3:2
	}
	{
		memd (sp + #16) = r5:4
		memd (sp + #24) = r7:6
	}
	{
		memd (sp + #32) = r9:8
		r8 = ssr
	}
	r7 = badva
	r9 = p3:0
	{
		r0 = ##__tlb_idx
		r1 = memw(##_tlb_fixed_entries)
	}
	{
		r7 = lsr (r7, #20)
		r3 = memw(##_NumTLBEntries) /* 31, 63, 127, or 191 */
	}
	/* Atomically increment index */
	/* NEVER overwrite entry 0 */
1:
	r6 = memw_locked (r0)
	{
		r6 = add (r6, #1)
		/* This was hard coded to p0 = cmp.ge(r6, #NUM_TLB_ENTRIES)
		   Now we are using 2 registers so switch to the equivalent
		   p0 = !cmp.gt(r3, r6) */
		p0 = !cmp.gt (r3, r6)
	}
	/* Will never store a number greater than
           _NumTLBEntries in &__tlb_idx */
	r6 = mux (p0, r1, r6)
	memw_locked (r0, p0) = r6
	if (!p0) jump 1b              /* Retry, lost reservation. */

	r3 = memw (##TLBMapTable)
	r3 = addasl (r3, r7, #1)
	{
		r3 = memh (r3)
		r7 = asl (r7, #8) /* VPN */
	}

	r4 = extractu (r3, #4, #0)
.L_OK:
	{
	        r5 = extractu (r3, #12, #4)
		r0 = #0x0010	/* 1M */
		r1 = #0
	}
	{
		r4 = asl (r4, #24)
		r1.h = #0xc000
		r0.h = #0xf000
	}
1:
	{
		r1 = or (r1, r7) /* R5: VPN | C000_0000 */
		r0 |= asl(r5,#9) /* R4: PPD | F000_0000 */
	}
	r0 = or (r0, r4)

	tlblock
	r5 = tlbp(r1)
	p0 = tstbit (r5, #31)
	if (!p0) jump 1f

	tlbw(r1:0,r6)
	isync
	jump 2f
1:
        // If we take a miss around a user defined page they need to
        // manually create another page or not touch the regions above
        // and below their page within a 1M boundary.
	r4 = memw(##_tlb_fixed_entries)
	p0 = cmp.gt(r4, r5) // r4>r5 == r5<r4, (entryfound < num_fixed)
	if (p0) jump .  // DEAD
2:
	tlbunlock

	p3:0 = r9
	{
		r9:8 = memd (sp + #32)
		r7:6 = memd (sp + #24)
	}
	{
		r5:4 = memd (sp + #16)
		r3:2 = memd (sp + #8)
	}
	{
		r1:0 = memd (sp + #0)
		sp = add (sp, #64)
	}
	crswap (sp, sgp0)
	rte

	.size event_handle_tlbmissrw, . - event_handle_tlbmissrw

/* This code handles the OS-like requests coming   */
/* from the application.                           */

	.p2align 4

 	.type event_handle_trap0, @function

event_handle_trap0:
	crswap (sp, sgp0)
	{
		sp = add (sp, #-40)
		memd (sp + #-40) = r5:4
		r5 = p3:0
		p0 = cmp.eq (r0, #0x40)  /* read (thread) cycles */
	}
	{
		memd (sp + #8) = r3:2
		p1 = cmp.eq (r0, #0x44)  /* read tcycles */
		p2 = cmp.eq (r0, #0x52)  /* read pcycles */
		r4.h = #HI (0x55555555)  /* 1/3 in 0.32 fixed point */
	}

7:
	{
		p3:0 = r5
		r3:2 = memd (sp + #8)
		r5:4 = memd (sp)
		sp = add (sp, #40)
	}
	crswap (sp, sgp0)
        rte

8:
	{
		if (!p2) jump 9f
		r6.l = #38
	}
	{
		p2 = cmp.eq (r1, r6)
		jump 1b
	}

9:
	r1 = memw (##CoreDump)

	jump 1b

        .size event_handle_trap0, . - event_handle_trap0

        .p2align 4

        .type event_handle_trap1, @function

event_handle_trap1:
        r0 = #9
        stid = r0
        jump coredump

        .size event_handle_trap1, . - event_handle_trap1

 /* This is the code jumped to by the interrupt vectors */
 /* (above).  We save context, jump to the function,    */
 /* restore context, and return to where we left off.   */

 	.type event_handle_int, @function

event_handle_int:
	crswap (sp, sgp0)
	allocframe (#160)
	{
		memd (sp + #0) = r1:0
		memd (sp + #8) = r3:2
		r0 = SA0
	}
	{
		memd (sp + #16) = r5:4
		memd (sp + #24) = r7:6
		r1 = LC0
	}
	{
		memd (sp + #32) = r9:8
		memd (sp + #40) = r11:10
		r2 = SA1
	}
	{
		memd (sp + #48) = r13:12
		memd (sp + #56) = r15:14
		r3 = LC1
	}
	{
		memd (sp + #64) = r17:16
		memd (sp + #72) = r19:18
		r6 = p3:0
	}
	{
		memd (sp + #80) = r21:20
		memd (sp + #88) = r23:22
		r5:4 = C7:6 /* M1 and M0 */
	}
	{
		memd (sp + #96) = r25:24
		memd (sp + #104) = r27:26
		r7 = USR
	}
	{
		memd (sp + #112) = r1:0
		memd (sp + #136) = r7:6
		r8 = UGP
	}
	r0 = ssr
	{
		memd (sp + #120) = r3:2
		r2 = r0
		r7 = insert (r0, #8, #16)
	}
	{
		r9 = ELR
		memd (sp + #128) = r5:4
		r0 = and (r0, #0x1f)
		r1 = ##(__IntHandlers)
	}
	{
		r1 = addasl (r1, r0, #2)
	}
	{
		memd (sp + #144) = r9:8
		r1 = memw (r1)
		r3 = #0
		lr = r28
	}
	{
		memd (sp + #152) = lr:fp
		r2 = insert (r3, #3, #16)
		p0 = cmp.eq (r1, #0)
	}
	if (p0) jump 1f // if null, skip a bunch of stuff
	ssr = r2
	crswap (sp, sgp0)
	/* Call interrupt handler */
	callr r1
	/* Ok, we're back... */
	crswap (sp, sgp0)
	/* R7.H is also intnum.. use for ciad */
	/* ciad ... do early to jump over */
	r0 = ssr
	{
		r26.h = #0x0000
		r7:6 = memd (sp + #136)
		r1 = #6  /* EX, IE, !UM */
	}
	{
		r7 = asrh (r7)
		r26.l = #0x0001
		r0 = insert(r1, #3, #16)
	}
	r7 = and (r7, #0x1f)
	r26 = lsl (r26, r7)

	ssr = r0
	ciad (r26)
1:
	{
		lr:fp = memd (sp + #152)
		r9:8 = memd (sp + #144)
	}
	elr = r9
	{
		r7:6 = memd (sp + #136)
		r5:4 = memd (sp + #128)
		UGP = r8
	}
	{
		r3:2 = memd (sp + #120)
		r1:0 = memd (sp + #112)
		usr = r7
		r28 = lr
	}
	{
		r27:26 = memd (sp + #104)
		r25:24 = memd (sp + #96)
		m0 = r4
	}
	{
		r23:22 = memd (sp + #88)
		r21:20 = memd (sp + #80)
		m1 = r5
	}
	{
		r19:18 = memd (sp + #72)
		r17:16 = memd (sp + #64)
		p3:0 = r6
	}
	{
		r15:14 = memd (sp + #56)
		r13:12 = memd (sp + #48)
		lc1 = r3
	}
	{
		r11:10 = memd (sp + #40)
		r9:8   = memd (sp + #32)
		sa1 = r2
	}
	{
		r7:6   = memd (sp + #24)
		r5:4   = memd (sp + #16)
		lc0 = r1
	}
	{
		r3:2   = memd (sp + #8)
		r1:0   = memd (sp + #0)
		sa0 = r0
	}
	deallocframe
	crswap (sp, sgp0)
	rte

	.size event_handle_int, . - event_handle_int

 /* Dummy function for when we don't have code registered for an interrupt.*/

	.p2align 4

 	.type .NoHandler, @function

.NoHandler:
	jumpr lr

	.size .NoHandler, . - .NoHandler

	.text

/* Next we have the event vectors */
/* See the HEXAGON Architecture System-Level Specification  */
/* for more information.*/

	.p2align 12, 0

 	.type .EventVectors, @function

.EventVectors:
	jump event_handle_reset
	jump event_handle_nmi
	jump event_handle_error
	jump event_handle_rsvd
	jump event_handle_tlbmissx
	jump event_handle_rsvd
	jump event_handle_tlbmissrw
	jump event_handle_rsvd
	jump event_handle_trap0
	jump event_handle_trap1
	jump event_handle_rsvd /* 10 */
	jump event_handle_rsvd /* 11 */
	jump event_handle_rsvd /* 12 */
	jump event_handle_rsvd /* 13 */
	jump event_handle_rsvd /* 14 */
	jump event_handle_rsvd /* 15 */
	jump event_handle_int  /* Event number 16, Interrupt 0 */
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int
	jump event_handle_int  /* Event number 47, Interrupt 31 */

	.size .EventVectors, . - .EventVectors

/**************** DATA SECTION ****************/

	/* Here are definitions for some of the data we use above */

	.section .start, "awx"
	.subsection 1

	.p2align 4, 0

	.global __IntHandlers
	.set __IntHandlers, .IntHandlers
.IntHandlers:
	.word .NoHandler	/* 0 */
	.word .NoHandler	/* 1 */
	.word .NoHandler	/* 2 */
	.word .NoHandler	/* 3 */
	.word .NoHandler	/* 4 */
	.word .NoHandler	/* 5 */
	.word .NoHandler	/* 6 */
	.word .NoHandler	/* 7 */
	.word .NoHandler	/* 8 */
	.word .NoHandler	/* 9 */
	.word .NoHandler	/* 10 */
	.word .NoHandler	/* 11 */
	.word .NoHandler	/* 12 */
	.word .NoHandler /* 13 */
	.word .NoHandler /* 14 */
	.word .NoHandler /* 15 */
	.word .NoHandler /* 16 */
	.word .NoHandler /* 17 */
	.word .NoHandler /* 18 */
	.word .NoHandler /* 19 */
	.word .NoHandler /* 20 */
	.word .NoHandler /* 21 */
	.word .NoHandler /* 22 */
	.word .NoHandler /* 23 */
	.word .NoHandler /* 24 */
	.word .NoHandler /* 25 */
	.word .NoHandler /* 26 */
	.word .NoHandler /* 27 */
	.word .NoHandler /* 28 */
	.word .NoHandler /* 29 */
	.word .NoHandler /* 30 */
	.word .NoHandler /* 31 */

	.p2align 5, 0
RegDump:
	.space 4 * (32 + 10 + 29)

    /* This space is used by the supervisor code for saving      */
    /* context for kernel stuff.  It's also used to hold the     */
    /* normal user code registers while we call the user-defined */
    /* interrupt service routine  */
/* Stack tops... enough for a couple context saves... */
	.p2align 3, 0
exc_stack_lim0:	.space 384
exc_stack_top0:	.word 0
	.p2align 3, 0
exc_stack_lim1:	.space 384
exc_stack_top1:	.word 0
	.p2align 3, 0
exc_stack_lim2:	.space 384
exc_stack_top2:	.word 0
	.p2align 3, 0
exc_stack_lim3:	.space 384
exc_stack_top3:	.word 0
	.p2align 3, 0
exc_stack_lim4:	.space 384
exc_stack_top4:	.word 0
	.p2align 3, 0
exc_stack_lim5:	.space 384
exc_stack_top5:	.word 0
	.p2align 3, 0
exc_stack_lim6: .space 384
exc_stack_top6: .word 0
	.p2align 3, 0
exc_stack_lim7: .space 384
exc_stack_top7: .word 0
	.p2align 3, 0
exc_stack_lim8:	.space 384
exc_stack_top8:	.word 0
	.p2align 3, 0
exc_stack_lim9:	.space 384
exc_stack_top9:	.word 0
	.p2align 3, 0
exc_stack_lim10: .space 384
exc_stack_top10: .word 0
	.p2align 3, 0
exc_stack_lim11: .space 384
exc_stack_top11: .word 0
	.p2align 3, 0
exc_stack_lim12: .space 384
exc_stack_top12: .word 0
	.p2align 3, 0
exc_stack_lim13: .space 384
exc_stack_top13: .word 0
	.p2align 3, 0
exc_stack_lim14: .space 384
exc_stack_top14: .word 0
	.p2align 3, 0
exc_stack_lim15: .space 384
exc_stack_top15: .word 0
exc_stack_tops:
	.word exc_stack_top0
	.word exc_stack_top1
	.word exc_stack_top2
	.word exc_stack_top3
	.word exc_stack_top4
	.word exc_stack_top5
	.word exc_stack_top6
	.word exc_stack_top7
	.word exc_stack_top8
	.word exc_stack_top9
	.word exc_stack_top10
	.word exc_stack_top11
	.word exc_stack_top12
	.word exc_stack_top13
	.word exc_stack_top14
	.word exc_stack_top15

	.global __start_pc
	.set __start_pc, start_pc
start_pc:
	jump .
	jump .
	jump .
	jump .
	jump .
	jump .
	jump .
	jump .
	jump .
	jump .
	jump .
	jump .
	jump .
	jump .
	jump .
	jump .


	.global __start_sp
	.set __start_sp, start_sp
start_sp:
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0

	.global __start_param
	.set __start_param, start_param
start_param:
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0

	.global __stack_size
	.set __stack_size, stack_size
stack_size:
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0

	.global __framekey
	.set __framekey, framekey_tbl
framekey_tbl:
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0
	.word 0

_l2cfg:
	.word 0
_tlbmax:
	.word 0

syscfg_l2_table:
        .byte 0x0       /* rev: 0x0xxx: No L2 -> 0k L2 cache */
        .byte 0x2       /* rev: 0x1xxx: 128K L2 -> 128k L2 cache */
        .byte 0x3       /* rev: 0x2xxx: 256K L2 -> 256k L2 cache */
        .byte 0x3       /* rev: 0x3xxx: Not valid at this time */
        .byte 0x4       /* rev: 0x4xxx: 512K L2 -> 512k L2 cache */
        .byte 0x4       /* rev: 0x5xxx: Not valid at this time */
        .byte 0x4       /* rev: 0x6xxx: 768K L2 -> 512k L2 cache */
        .byte 0x4       /* rev: 0x7xxx: Not valid at this time */
        .byte 0x5       /* rev: 0x8xxx: 1024K L2 -> 1024 L2 cache */
        .byte 0x4       /* rev: 0x9xxx: Not valid at this time */
        .byte 0x5       /* rev: 0xAxxx: 1536K L2 -> 1024 L2 cache */
        .byte 0x4       /* rev: 0xBxxx: Not valid at this time */
        .byte 0x4       /* rev: 0xCxxx: Not valid at this time */
        .byte 0x4       /* rev: 0xDxxx: Not valid at this time */
        .byte 0x4       /* rev: 0xExxx: Not valid at this time */
        .byte 0x4       /* rev: 0xFxxx: Not valid at this time */


 /* Data used for TLB refill */

	.p2align 6, 0

	.global __tlb_lock
	.set __tlb_lock, tlb_lock
tlb_lock:
	.word 0
	.global __tlb_idx
	.set __tlb_idx, tlb_idx
tlb_idx:
	.word TLB_FIXED_ENTRIES - 1

	.global _tlb_fixed_entries
_tlb_fixed_entries:
	.word TLB_FIXED_ENTRIES
