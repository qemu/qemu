/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

#ifndef _INSN_H_
#define _INSN_H_

/*
 *
 * 
 *
 */

#include "cpu.h"
#include "max.h"
#include "global_types.h"
#include "translate.h"

/* Forward declarations */
struct Packet;
struct Instruction;
struct ThreadState;

typedef void (*semantic_insn_t) (CPUHexagonState *env, DisasContext *ctx, struct Instruction * insn);

typedef void (*semantic_pkt_t) (struct ThreadState * T,
								struct Packet * pkt);

/* NOTE: Be careful to keep the Instruction and Packet structures
   as small as possible. The smaller they are, the more simulated
   instructions we can keep in cache. 
*/

/* EJP:
 * For reorg, we should try some things:
 *  * iclass can be determined from opcode
 *  * Redo "part1"
 *  * Is "which_extended" needed? Should be knowable from opcode?
 *  * Is is_load/is_store/is_aia/is_endloop needed? Determine from opcode?
 *  * Remove encoding_offset?
 *  * Remove can_skip_regdep_check (uarch)
 *  * Remove timing_class (uarch)
 *  * Now, all these bits can hopefully fit in a size2u_t
 *  * Remove uarch_* (uarch)
 *  * Have generic uarch pointer (if necessary, maybe per packet?)
 *  We end up with execute pointer, REG_OPERANDS_MAX bytes, size2u_t opcode,
 *     size2u_t flags, IMMEDS_MAX size4s_t, and maybe one more pointer for uarch.
 *  That's reducing  5+5*REG_OPERANDS_MAX bytes, I think, probably >20
 *  For 64 bit arch, our new total would be 8+4+2+2+8+8, or 32 bytes. Nice number! without pointer, 24 bytes
 *  We're currently at 8+4+4+4+8+4+4+16, or 52.
 */



struct Instruction {
	semantic_insn_t generate;	/* pointer to semantic routine */
	size1u_t regno[REG_OPERANDS_MAX];	/* max reg operands including predicates */
	size2u_t opcode;			/* index to instruction information tables */

  	size4u_t rreg;         /* Bitfield for which of the 32 GPRs are read by this instruction */
  	size4u_t wreg;         /* Bitfield for which of the 32 GPRs are written by this instruction */
  	size1u_t rwpreg;       /* Bitfield for which of the 4 pregs are read and written by this instruction,
                              lower nibble is for reads, higher for writes; currently values can be 00, 0F, F0 or FF */
    
	size4u_t iclass:6;
	size4u_t slot:3;
	size4u_t part1:1;			/* cmp-jumps are split into two insns. 
								   This is set for the compare and clear for the jump */
	size4u_t extension_valid:1;	/* Has a constant extender attached */
	size4u_t which_extended:1;	/* If has an extender, which immediate */
	size4u_t is_dcop:1;         /* Is a dcacheop */
  	size4u_t is_dcfetch:1;      /* Has an A_DCFETCH attribute */
	size4u_t is_load:1;			/* Has A_LOAD attribute */
	size4u_t is_store:1;		/* Has A_STORE attribute */
  	size4u_t is_vmem_ld:1;      /* Has an A_LOAD and an A_VMEM attribute */
  	size4u_t is_vmem_st:1;      /* Has an A_STORE and an A_VMEM attribute */
	size4u_t is_scatgath:1;     /* Has an A_CVI_GATHER or A_CVI_SCATTER attribute */
	size4u_t is_memop:1;		/* Has A_MEMOP attribute */
  	size4u_t is_dealloc:1;      /* Is a dealloc return or dealloc frame */
	size4u_t is_aia:1;			/* Is a post increment */
	size4u_t is_endloop:1;		/* This is an end of loop */
	size4u_t is_2nd_jump:1;		/* This is the second jump of a dual-jump packet */
	size4u_t encoding_offset:2;	/* Offset in words from PC to this insn */
	size4u_t new_value_producer_slot:4;	/* For NV insns, where it's coming from */
	size4u_t hvx_resource:8;
	size4s_t immed[IMMEDS_MAX];	/* immediate field */
};

typedef struct Instruction insn_t;


/* Again, this can be cleaned up some
 * VA shouldn't exist here, it can be mapped in multiple places
 * num_insns and encod_pkt_size_in_bytes is too big.  Merge with bitfield for space
 * Can we move out bitfields?  Mostly it is there for uarch decisions
 * Do we have to have a bit for every single wierdo instruction here?  Can't we just look for them?
 * Do we need a per-hthread execution count per packet?  Could we change the stats to be global for these things?
 * Why is encoding stored ?!?!
 * Currently ~72 bytes + insn_t's
 * Not as much room for improvement and with 4-6 "insns" per packet that's the dominant storage requirement
 * Still, would be nice to clean up here
 */

struct Packet {
	paddr_t PC_PA;				/* The start address of this packet, physical address. */
	size4u_t PC_VA;				/* The start address of this packet, virtual address. Needed for silver. */ /* EJP: Why? What if it's mapped in two places? */
    size8u_t pktid;

	size4u_t PC_EXCEP;          /*Which PC cause an Exception*/
	size2u_t num_insns;			/* number of instructions within packet */
	size2u_t encod_pkt_size_in_bytes;	/* size of encoded packet */

#if defined(VERIFICATION) || defined(ZEBU_CHECKSUM_TRACE)
	size4u_t words[4];			/* FIXME: MAX_WORDS or something? */
#endif

	/* Possible to multi-register write? (1 flag) */
	size8u_t possible_multi_regwrite:1;

	/* Pre-decodes about LD/ST (10 flags) */
	size8u_t possible_pgxing:1;
	size8u_t double_access:1;
	size8u_t dcfetch_and_access:1;
	size8u_t mem_access:1;
	size8u_t single_load:1;
	size8u_t dual_load:1;
	size8u_t single_store:1;
	size8u_t dual_store:1;
	size8u_t load_and_store:1;
	size8u_t memop_or_nvstore:1;	/* This packet has a memop or NV store */

	/* Pre-decodes about COF (13 flags) */
	size8u_t pkt_has_cof:1;		/* Has any change-of-flow */
	size8u_t pkt_has_dual_jump:1;	/* Has dual jumps */
	size8u_t pkt_has_initloop:1;	/* Has a init loop */
	size8u_t pkt_has_initloop0:1;	/* Has a init loop */
	size8u_t pkt_has_initloop1:1;	/* Has a init loop */
	size8u_t pkt_has_endloop:1;	/* Has an endloop */
	size8u_t pkt_has_endloop0:1;	/* Has an endloop0 */
	size8u_t pkt_has_endloop1:1;	/* Has an endloop0 */
	size8u_t pkt_has_endloop01:1;	/* Has an endloop0 */
	size8u_t pkt_has_call:1;	/* Has a CALL */
	size8u_t pkt_has_ras_ret:1;	/* Has a CALL */
	size8u_t pkt_has_jumpr:1;	/* Has an indirect jumpr */
	size8u_t pkt_has_cjump:1;	/* Has any conditional cof */
	size8u_t pkt_has_cjump_dotnew:1;	/* Conditional cof based on speculative predicate */
	size8u_t pkt_has_cjump_dotold:1;	/* Conditional cof based on speculative predicate */
	size8u_t pkt_has_cjump_newval:1;	/* New value cmp-jumps */
	size8u_t pkt_has_duplex:1;	/* Contains a duplex */
	size8u_t pkt_has_payload:1;	/* Contains a constant extender */
	size8u_t pkt_has_dealloc_return:1;
	size8u_t pkt_has_jumpr_return:1;  /* indirect jump that is a return */

	/* Pre-decodes about SLOTS (4 flags) */
	size8u_t slot0_valid:1;
	size8u_t slot1_valid:1;
	size8u_t slot2_valid:1;
	size8u_t slot3_valid:1;

	/* Pre-decodes about insns-per-pkt (2 flag) */
	size8u_t total_slots_valid_minus_1:2;
    size8u_t total_insns_sans_nop:3;

	/* When a predicate cancels something, track that (8 flags) */
	size8u_t slot_cancelled:4;
	size8u_t pkt_has_stld:1;
	size8u_t pkt_has_fp_op:1;
	size8u_t pkt_has_fpsp_op:1;
	size8u_t pkt_has_fpdp_op:1;

	/* V65: Store new with cancelled source still executes as 0 bytes store */
	size8u_t slot_zero_byte_store:4;
	size8u_t ext_slot_cancelled:4;	/* Extension slot cancelled */
	
	/* Contains a cacheop (8 flags) */
	size8u_t pkt_has_cacheop:1;
	size8u_t pkt_has_dczeroa:1;
	size8u_t pkt_has_ictagop:1;
	size8u_t pkt_has_icflushop:1;
	size8u_t pkt_has_dcflushop:1;
	size8u_t pkt_has_dctagop:1;
	size8u_t pkt_has_l2flushop:1;
	size8u_t pkt_has_l2tagop:1;

	/* load store for slots. (4 flags) */
	size8u_t pkt_has_load_s0:1;
	size8u_t pkt_has_load_s1:1;
	size8u_t pkt_has_store_s0:1;
	size8u_t pkt_has_store_s1:1;

	/* Misc (8 flags) */
	size8u_t num_rops:4;		/* Num risc ops in the packet */
	size8u_t pkt_has_long_latency_insn:1;
	size8u_t pkt_page_is_stable:1;	/* If this PA is 'stable' from the host */
	size8u_t pkt_has_vecx:1;	/* Is a packet with SILVER isntructions */
	size8u_t pkt_has_l1s_scalar:1; /* Is there a scalar load store going to l1s */
    size8u_t pkt_has_vtcm_access:1; /* Is a vmem access going to VTCM */
    size8u_t pkt_has_vmemu_access:1; /* VMEMU access, different from double access */
    size8u_t pkt_access_count:2; /* Is a vmem access going to VTCM */
    size8u_t pkt_ldaccess_l2:2; /* vmem ld access to l2 */
    size8u_t pkt_ldaccess_vtcm:2; /* vmem ld access to vtcm */
    size8u_t double_access_vec:1; /* double vector access for v and z load */
  	size8u_t pkt_vmem_ld_ct:2; /* pkt has how many vmem loads */
  	size8u_t pkt_vmem_st_ct:2; /* pkt has how many vmem stores */
	size8u_t pkt_has_scatgath:1; /* pkt has scatter gather */
	size8u_t pkt_has_vmemu:1; /* pkt has unaligned vmem, this is different from pkt_has_vmemu_access which is runtime  */
    size8u_t pkt_nonvmem_st_ct:2; /* pkt has how many non vmem stores */
    size1u_t pkt_memport_ct:2; /*pkt use number of mem ports*/
    size1u_t pkt_memport_s0:1; /*pkt use mem port by instruction on slot 0*/
    size1u_t pkt_memport_s1:1; /*pkt use mem port by instruction on slot 1*/
	
    size1u_t pkt_has_dword_store:1;
    size1u_t pkt_has_dword_load:1;
	
	size8u_t pkt_hvx_va:4;	
	size8u_t pkt_hvx_vx:4;	
	size8u_t pkt_hvx_vp:4;	
	size8u_t pkt_hvx_vs:4;	
	size8u_t pkt_hvx_all:4;	
	size8u_t pkt_hvx_none:4;	
	
	/* non memory operations (3 flags) */
	size8u_t pkt_has_valid_slot0_non_mem:1;
	size8u_t pkt_has_valid_slot1_non_mem:1;
	size8u_t pkt_has_valid_slot01_non_mem:1;

	/* Timing class information (5 flags) */
	size8u_t pkt_has_tc_3_instruction:1;
	size8u_t pkt_has_tc_3x_instruction:1;
	size8u_t pkt_has_tc_3stall_instruction:1;
	size8u_t pkt_has_tc_ld_instruction:1;
	size8u_t pkt_has_tc_st_instruction:1;

	/* Circular addressing and overflows (2 flags) */
	size8u_t pkt_has_circular:1;
	size8u_t pkt_has_circular_ovf:1;

	size8u_t pkt_has_extension:1;
	size8u_t pkt_has_shared_extension:1; /* For global extentions like HMX that are not context (XA) based (at least for now) */
	size8u_t pkt_not_logged_for_timing:1; /* BQ - Usually has solo instruction that doesn't go to timing like k0lock */
	
	size4u_t exec_count[THREADS_MAX];	/* how many times it executed */

	size4u_t native_pkt:1;
	size4u_t total_memop:2;
	struct Packet *taken_ptr;	/* predicted next packet */
	struct Packet *fallthrough_ptr;	/* predicted fall-through */

	/* This MUST be the last thing in this structure */
	insn_t insn[INSTRUCTIONS_MAX];

    size1u_t pkt_num_tc1;
    size1u_t pkt_num_tc2;
    size1u_t pkt_num_tc3;
    size1u_t pkt_num_tc4;
};

typedef struct Packet packet_t;



#endif							/* #ifndef _INSN_H_ */
