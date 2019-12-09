/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

/*
 * Execution functions
 * 
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "qemu/osdep.h"
#include "qemu/log.h"
//#include "iclass.h"
#include "opcodes.h"
#include "decode.h"
#include "insn.h"
//#include "printinsn.h"
#include "utils.h"
#include "isa_constants.h"
#include "macros.h"
#ifdef VERIFICATION
#include "ver_external_api.h"
#endif
#ifdef ZEBU_CHECKSUM_TRACE
#include "zebu_external_api.h"
#endif
#include "mmvec/mmvec.h"
#include "mmvec/decode_ext_mmvec.h"

#define NO_SILVER

enum {
    EXT_IDX_noext = 0,
    EXT_IDX_noext_AFTER = 4,
    EXT_IDX_mmvec = 4,
    EXT_IDX_mmvec_AFTER = 8,
    XX_LAST_EXT_IDX
};

#define snprint_a_pkt(pkt_buf, x, y, z) \
    sprintf(pkt_buf, "FIXME: %s, %d", __FILE__, __LINE__)

enum timing_class_types {
	timing_class_tc_1,
	timing_class_tc_2,
	timing_class_tc_2latepred,
	timing_class_tc_3,
	timing_class_tc_3x,
	timing_class_tc_newvjump,
	timing_class_tc_3stall,
	timing_class_tc_ld,
	timing_class_tc_st,
	timing_class_tc_2early,
	timing_class_tc_4x,
	timing_class_tc_latepredldaia,
	timing_class_tc_latepredstaia,
	timing_class_last_timing_class
};
extern const size1u_t insn_timing_classes[];

#ifdef FIXME
void decode_error(thread_t * thread, exception_info *einfo, unsigned int cause)
{
	einfo->valid = 1;
	einfo->type = EXCEPT_TYPE_PRECISE;
	einfo->cause = cause;
	einfo->badva0 = thread->Regs[REG_BADVA0];
	einfo->badva1 = thread->Regs[REG_BADVA1];
	einfo->bvs = GET_SSR_FIELD(SSR_BVS);
	einfo->bv0 = GET_SSR_FIELD(SSR_V0);
	einfo->bv1 = GET_SSR_FIELD(SSR_V1);
	einfo->elr = thread->Regs[REG_PC];
}
#else
#define decode_error(x, y, z) __decode_error(z)
void __decode_error(unsigned int cause);
void __decode_error(unsigned int cause)
{
    printf("decode_error: %d\n", cause);
}
#endif

#define DEF_REGMAP(NAME,ELEMENTS,...) \
	static const unsigned int DECODE_REGISTER_##NAME[ELEMENTS] = { __VA_ARGS__ };

#include "regmap.h"

#define DECODE_MAPPED_REG(REGNO,NAME) insn->regno[REGNO] = DECODE_REGISTER_##NAME[insn->regno[REGNO]];

int decode_get_regno(insn_t * insn, const char regid);
int decode_get_regno(insn_t * insn, const char regid)
{
	char *idx;
	idx = strchr(opcode_reginfo[insn->opcode], regid);
	if (idx == NULL)
		return -1;
	else
		return (idx - opcode_reginfo[insn->opcode]);
}

typedef struct {
	struct _dectree_table_struct *table_link;
	struct _dectree_table_struct *table_link_b;
	opcode_t opcode;
	enum {
		DECTREE_ENTRY_INVALID,
		DECTREE_TABLE_LINK,
		DECTREE_SUBINSNS,
		DECTREE_EXTSPACE,
		DECTREE_TERMINAL
	} type;
} dectree_entry_t;

typedef struct _dectree_table_struct {
	unsigned int (*lookup_function) (int startbit, int width, size4u_t opcode);
	unsigned int size;
	unsigned int startbit;
	unsigned int width;
	dectree_entry_t table[];
} dectree_table_t;

#ifdef FIXME
static int
decode_finish_ext(thread_t *thread, insn_t *insn, size4u_t opcode)
{
	if (GET_ATTRIB(insn->opcode,A_SHARED_EXTENSION)) {
		return thread->processor_ptr->shared_exttab->ext_decode(thread,insn,opcode);
	} 
	size4u_t active_ext; 
	fCUREXT_WRAP(active_ext);
	return thread->processor_ptr->exttab[active_ext]->ext_decode(thread,insn,opcode);
}
#endif

#define DECODE_NEW_TABLE(TAG,SIZE,WHATNOT) static struct _dectree_table_struct dectree_table_##TAG;
#define TABLE_LINK(TABLE)		/* NOTHING */
#define TERMINAL(TAG,ENC)		/* NOTHING */
#define SUBINSNS(TAG,CLASSA,CLASSB,ENC)	/* NOTHING */
#define EXTSPACE(TAG,ENC)		/* NOTHING */
#define INVALID()				/* NOTHING */
#define DECODE_END_TABLE(...)	/* NOTHING */
#define DECODE_MATCH_INFO(...)	/* NOTHING */
#define DECODE_LEGACY_MATCH_INFO(...)	/* NOTHING */
#define DECODE_OPINFO(...)		/* NOTHING */

#include "dectree.odef"

#undef DECODE_OPINFO
#undef DECODE_MATCH_INFO
#undef DECODE_LEGACY_MATCH_INFO
#undef DECODE_END_TABLE
#undef INVALID
#undef TERMINAL
#undef SUBINSNS
#undef EXTSPACE
#undef TABLE_LINK
#undef DECODE_NEW_TABLE
#undef DECODE_SEPARATOR_BITS

#define DECODE_SEPARATOR_BITS(START,WIDTH) NULL,START,WIDTH
#define DECODE_NEW_TABLE_HELPER(TAG,SIZE,FN,START,WIDTH) \
static dectree_table_t dectree_table_##TAG = { \
	.size = SIZE, .lookup_function = FN, .startbit = START, .width = WIDTH, \
	.table = {
#define DECODE_NEW_TABLE(TAG,SIZE,WHATNOT) DECODE_NEW_TABLE_HELPER(TAG,SIZE,WHATNOT)

#define TABLE_LINK(TABLE) { .type = DECTREE_TABLE_LINK, .table_link = &dectree_table_##TABLE },
#define TERMINAL(TAG,ENC) { .type = DECTREE_TERMINAL, .opcode = TAG  },
#define SUBINSNS(TAG,CLASSA,CLASSB,ENC) { .type = DECTREE_SUBINSNS, .table_link = &dectree_table_DECODE_SUBINSN_##CLASSA, .table_link_b = &dectree_table_DECODE_SUBINSN_##CLASSB },
#define EXTSPACE(TAG,ENC) { .type = DECTREE_EXTSPACE },
#define INVALID() { .type = DECTREE_ENTRY_INVALID, .opcode = XX_LAST_OPCODE },

#define DECODE_END_TABLE(...) } };

#define DECODE_MATCH_INFO(...)	/* NOTHING */
#define DECODE_LEGACY_MATCH_INFO(...)	/* NOTHING */
#define DECODE_OPINFO(...)		/* NOTHING */

#include "dectree.odef"

#undef DECODE_OPINFO
#undef DECODE_MATCH_INFO
#undef DECODE_LEGACY_MATCH_INFO
#undef DECODE_END_TABLE
#undef INVALID
#undef TERMINAL
#undef SUBINSNS
#undef EXTSPACE
#undef TABLE_LINK
#undef DECODE_NEW_TABLE
#undef DECODE_NEW_TABLE_HELPER
#undef DECODE_SEPARATOR_BITS

static dectree_table_t dectree_table_DECODE_EXT_EXT_noext = {
	.size = 1, .lookup_function = NULL, .startbit = 0, .width = 0,
	.table = {
		{ .type = DECTREE_ENTRY_INVALID, .opcode = XX_LAST_OPCODE },
        }
};

static dectree_table_t *ext_trees[XX_LAST_EXT_IDX];

static void decode_ext_init(void)
{
	int i;
    for (i = EXT_IDX_noext; i < EXT_IDX_noext_AFTER; i++) {
        ext_trees[i] = &dectree_table_DECODE_EXT_EXT_noext;
    }
    for (i = EXT_IDX_mmvec; i < EXT_IDX_mmvec_AFTER; i++) {
        ext_trees[i] = &dectree_table_DECODE_EXT_EXT_mmvec;
    }
}

typedef struct {
	size4u_t mask;
	size4u_t match;
} decode_itable_entry_t;

#define DECODE_NEW_TABLE(TAG,SIZE,WHATNOT)	/* NOTHING */
#define TABLE_LINK(TABLE)		/* NOTHING */
#define TERMINAL(TAG,ENC)		/* NOTHING */
#define SUBINSNS(TAG,CLASSA,CLASSB,ENC)	/* NOTHING */
#define EXTSPACE(TAG,ENC)		/* NOTHING */
#define INVALID()				/* NOTHING */
#define DECODE_END_TABLE(...)	/* NOTHING */
#define DECODE_OPINFO(...)		/* NOTHING */

#define DECODE_MATCH_INFO_NORMAL(TAG,MASK,MATCH) \
    [TAG] = { \
        .mask = MASK, \
        .match = MATCH, \
    },

#define DECODE_MATCH_INFO_NULL(TAG,MASK,MATCH) \
    [TAG] = { .match = ~0 },

#define DECODE_MATCH_INFO(...) DECODE_MATCH_INFO_NORMAL(__VA_ARGS__)
#define DECODE_LEGACY_MATCH_INFO(...) /* NOTHING */

static const decode_itable_entry_t decode_itable[XX_LAST_OPCODE] =
{
#include "dectree.odef"
};

#undef DECODE_MATCH_INFO
#define DECODE_MATCH_INFO(...) DECODE_MATCH_INFO_NULL(__VA_ARGS__)

#undef DECODE_LEGACY_MATCH_INFO
#define DECODE_LEGACY_MATCH_INFO(...) DECODE_MATCH_INFO_NORMAL(__VA_ARGS__)

static const decode_itable_entry_t decode_legacy_itable[XX_LAST_OPCODE] =
{
#include "dectree.odef"
};

#undef DECODE_OPINFO
#undef DECODE_MATCH_INFO
#undef DECODE_LEGACY_MATCH_INFO
#undef DECODE_END_TABLE
#undef INVALID
#undef TERMINAL
#undef SUBINSNS
#undef EXTSPACE
#undef TABLE_LINK
#undef DECODE_NEW_TABLE
#undef DECODE_SEPARATOR_BITS

void decode_init(void);
void decode_init(void)
{
	decode_ext_init();
}

void decode_send_insn_to(packet_t * packet, int start, int newloc)
{
	insn_t tmpinsn;
	int direction;
	int i;
	if (start == newloc)
		return;
	if (start < newloc) {
		/* Move towards end */
		direction = 1;
	} else {
		/* move towards beginning */
		direction = -1;
	}
	for (i = start; i != newloc; i += direction) {
		tmpinsn = packet->insn[i];
		packet->insn[i] = packet->insn[i + direction];
		packet->insn[i + direction] = tmpinsn;
	}
}

/* Fill newvalue registers with the correct regno
 */
static int
#ifdef FIXME
decode_fill_newvalue_regno(thread_t * thread, packet_t * packet, exception_info *einfo)
#else
decode_fill_newvalue_regno(packet_t * packet)
#endif
{
	int i, def_regnum, use_regidx, def_idx;
	size2u_t def_opcode, use_opcode;
	char *dststr;

	for (i = 1; i < packet->num_insns; i++) {
		if (GET_ATTRIB(packet->insn[i].opcode, A_DOTNEWVALUE) &&  !GET_ATTRIB(packet->insn[i].opcode, A_EXTENSION)) {

			use_opcode = packet->insn[i].opcode;

			/* It's a store, so we're adjusting the Nt field */
			if (GET_ATTRIB(use_opcode, A_STORE)) {
				use_regidx =
					strchr(opcode_reginfo[use_opcode],
						   't') - opcode_reginfo[use_opcode];
			} else {			/* It's a Jump, so we're adjusting the Ns field */
				use_regidx =
					strchr(opcode_reginfo[use_opcode],
						   's') - opcode_reginfo[use_opcode];
			}

			/* What's encoded at the N-field is the offset to who's producing the value.
			   Shift off the LSB which indicates odd/even register.
			 */
			def_idx = i - ((packet->insn[i].regno[use_regidx]) >> 1);

			/* Check for a badly encoded N-field which points to an instruction
			   out-of-range */
			if ((def_idx < 0) || (def_idx > (packet->num_insns - 1))) {
				warn("[NEWDEFINED] A new-value consumer has no valid producer!\n");
#ifdef FIXME
				thread->exception_msg = "Bad Packet Grouping";
#endif
				decode_error(thread,einfo,PRECISE_CAUSE_INVALID_PACKET);
				return 1;
			}

			/* previous insn is the producer */
			def_opcode = packet->insn[def_idx].opcode;
			if (NULL != (dststr = strstr(opcode_wregs[def_opcode],"Rd"))) {
				dststr = strchr(opcode_reginfo[def_opcode],'d');
			} else if (NULL != (dststr = strstr(opcode_wregs[def_opcode],"Rx"))) {
				dststr = strchr(opcode_reginfo[def_opcode],'x');
			} else if (NULL != (dststr = strstr(opcode_wregs[def_opcode],"Re"))) {
				dststr = strchr(opcode_reginfo[def_opcode],'e');
			} else if (NULL != (dststr = strstr(opcode_wregs[def_opcode],"Ry"))) {
				dststr = strchr(opcode_reginfo[def_opcode],'y');
			} else {
#ifdef FIXME
				thread->exception_msg = "Bad Packet Grouping";
#endif
				decode_error(thread,einfo,PRECISE_CAUSE_INVALID_PACKET);
				warn("[NEWDEFINED] A new-value consumer has no valid producer!\n");
				return 1;
			}
			if (dststr == NULL) fatal("Didn't find register in opcode_reginfo");
			def_regnum=packet->insn[def_idx].regno[dststr-opcode_reginfo[def_opcode]];

			/* Now patch up the consumer with the register number */
			packet->insn[i].regno[use_regidx] = def_regnum;
			/* We need to remember who produces this value to later check if it was
			   dynamically cancelled */
			packet->insn[i].new_value_producer_slot =
				packet->insn[def_idx].slot;
		}
	}
	return 0;
}

/* Split CJ into a compare and a jump
 */
#ifdef FIXME
static int decode_split_cmpjump(thread_t *thread, packet_t * pkt, exception_info *einfo)
#else
static int decode_split_cmpjump(packet_t * pkt)
#endif
{
	int last, i;
	int numinsns = pkt->num_insns;

	/* First, split all compare-jumps.
	   The compare is sent to the end as a new instruction.
	   Do it this way so we don't reorder dual jumps. Those need to stay in
	   original order
	 */
	for (i = 0; i < numinsns; i++) {
		/* It's a cmp-jump */
		if (GET_ATTRIB(pkt->insn[i].opcode, A_NEWCMPJUMP)) {
			last = pkt->num_insns;
			pkt->insn[last] = pkt->insn[i];	// copy the instruction
			pkt->insn[last].part1 = 1;	// last instruction does the CMP
			pkt->insn[i].part1 = 0;	// existing instruction does the JUMP
			pkt->num_insns++;
		}
	}

	/* Now re-shuffle all the compares back to the beginning */
	for (i = 0; i < pkt->num_insns; i++) {
		if (pkt->insn[i].part1) {
			decode_send_insn_to(pkt, i, 0);
		}
	}
	return 0;
}

static inline int decode_opcode_can_jump(int opcode)
{

	if ((GET_ATTRIB(opcode, A_JUMP)) ||
		(GET_ATTRIB(opcode, A_CALL)) ||
		(opcode == J2_trap0) ||
		(opcode == J2_trap1) ||
		(opcode == J2_rte) ||
		(opcode == J2_pause)) {
	    /* Exception to A_JUMP attribute. Also, any one know
	       why hintjr has A_JUMP attribute set? */
	    if(opcode == J4_hintjumpr) return (0);
		return (1);
	}

	return (0);
}

static inline int decode_opcode_ends_loop(int opcode)
{
	return GET_ATTRIB(opcode, A_HWLOOP0_END) || GET_ATTRIB(opcode, A_HWLOOP1_END);
}

static int decode_possible_multiwrite(packet_t * pkt);

/* Set the is_* fields in each instruction 
 */
#ifdef FIXME
static int decode_set_insn_attr_fields(thread_t *thread, packet_t * pkt, exception_info *einfo)
#else
static int decode_set_insn_attr_fields(packet_t * pkt)
#endif
{
	int i;
	int numinsns = pkt->num_insns;
	size2u_t opcode;
	int loads = 0;
	int stores = 0;
	int canjump;
	int total_slots_valid = 0;
    int total_insns_sans_nop = 0;

	pkt->num_rops = 0;
	pkt->pkt_has_cof = 0;
	pkt->pkt_has_call = 0;
	pkt->pkt_has_jumpr = 0;
	pkt->pkt_has_cjump = 0;
	pkt->pkt_has_cjump_dotnew = 0;
	pkt->pkt_has_cjump_dotold = 0;
	pkt->pkt_has_cjump_newval = 0;
	pkt->pkt_has_endloop = 0;
	pkt->pkt_has_endloop0 = 0;
	pkt->pkt_has_endloop01 = 0;
	pkt->pkt_has_endloop1 = 0;
	pkt->pkt_has_cacheop = 0;
	pkt->memop_or_nvstore = 0;
	pkt->pkt_has_valid_slot0_non_mem = 0;
	pkt->pkt_has_valid_slot1_non_mem = 0;
	pkt->pkt_has_valid_slot01_non_mem = 0;
	pkt->pkt_has_dczeroa = 0;
	pkt->dcfetch_and_access = 0;
	pkt->pkt_has_dealloc_return = 0;
    pkt->pkt_has_jumpr_return = 0;
    pkt->pkt_has_ras_ret  = 0;
	pkt->pkt_not_logged_for_timing = 0;
	pkt->native_pkt = 1;
	pkt->total_memop = 0;
	//pkt->pkt_has_extension = 0;
    pkt->pkt_nonvmem_st_ct = 0;
    pkt->pkt_memport_ct = 0;
    pkt->pkt_memport_s0 = 0;
    pkt->pkt_memport_s1 = 0;

    pkt->pkt_has_dword_store = 0;
    pkt->pkt_has_dword_load = 0;

#ifdef VERIFICATION
	thread->last_pkt_to_silver = 0;
#endif	
	for (i = 0; i < numinsns; i++) {

		opcode = pkt->insn[i].opcode;
		if (pkt->insn[i].part1)
			continue;			/* Skip compare of cmp-jumps */

		if (GET_ATTRIB(opcode, A_EXTENSION)) {
			pkt->pkt_has_vecx = 1;
#ifdef VERIFICATION
			// TB issues with Silver: Could be an Silver instruction or 
			// scalar store to L1S
			thread->last_pkt_to_silver = 1;
#endif	
		}
		if (GET_ATTRIB(opcode, A_NO_TIMING_LOG)) {
			pkt->pkt_not_logged_for_timing = 1;
		}
		
		if (GET_ATTRIB(opcode, A_ROPS_3)) {
			pkt->num_rops += 3;
		} else if (GET_ATTRIB(opcode, A_ROPS_2)) {
			pkt->num_rops += 2;
		} else {
			pkt->num_rops++;
		}
		if (pkt->insn[i].extension_valid) {
			pkt->num_rops += 2;
		}

		if (GET_ATTRIB(opcode, A_MEMOP) || GET_ATTRIB(opcode, A_NVSTORE)) {
			pkt->memop_or_nvstore = 1;
		}
		if (GET_ATTRIB(opcode, A_CACHEOP)) {
			pkt->pkt_has_cacheop = 1;
			if (GET_ATTRIB(opcode, A_DCZEROA)) {
				pkt->pkt_has_dczeroa = 1;
			}
			if (GET_ATTRIB(opcode, A_ICTAGOP)) {
				pkt->pkt_has_ictagop = 1;
			}
			if (GET_ATTRIB(opcode, A_ICFLUSHOP)) {
				pkt->pkt_has_icflushop = 1;
			}
			if (GET_ATTRIB(opcode, A_DCTAGOP)) {
				pkt->pkt_has_dctagop = 1;
			}
			if (GET_ATTRIB(opcode, A_DCFLUSHOP)) {
				pkt->pkt_has_dcflushop = 1;
			}
			if (GET_ATTRIB(opcode, A_L2TAGOP)) {
				pkt->pkt_has_l2tagop = 1;
			}
			if (GET_ATTRIB(opcode, A_L2FLUSHOP)) {
				pkt->pkt_has_l2flushop = 1;
			}
		}

        if(GET_ATTRIB(opcode, A_DEALLOCRET)) {
            pkt->pkt_has_dealloc_return = 1;
        }

		if (GET_ATTRIB(opcode, A_STORE)) {
			pkt->insn[i].is_store = 1;
            if (GET_ATTRIB(opcode, A_VMEM)) 
                pkt->insn[i].is_vmem_st = 1;
            else
                pkt->pkt_nonvmem_st_ct++;

			if (pkt->insn[i].slot == 0)
				pkt->pkt_has_store_s0 = 1;
			else
				pkt->pkt_has_store_s1 = 1;

            if (GET_ATTRIB(opcode, A_MEMSIZE_8B))
                pkt->pkt_has_dword_store = 1;
		}
		if (GET_ATTRIB(opcode, A_DCFETCH)) {
			pkt->insn[i].is_dcfetch = 1;
        }
		if (GET_ATTRIB(opcode, A_LOAD)) {
			pkt->insn[i].is_load = 1;
            if (GET_ATTRIB(opcode, A_VMEM)) 
                pkt->insn[i].is_vmem_ld = 1;

			if (pkt->insn[i].slot == 0)
				pkt->pkt_has_load_s0 = 1;
			else
				pkt->pkt_has_load_s1 = 1;

            if (GET_ATTRIB(opcode, A_MEMSIZE_8B))
                pkt->pkt_has_dword_load = 1;
		}
		if (GET_ATTRIB(opcode, A_VMEMU)) 		   
				pkt->pkt_has_vmemu = 1;	
		if (GET_ATTRIB(opcode, A_CVI_GATHER) || GET_ATTRIB(opcode, A_CVI_SCATTER) ) {
			pkt->insn[i].is_scatgath = 1;		
			pkt->pkt_has_scatgath = 1;			
		}
		
		if (GET_ATTRIB(opcode, A_MEMOP)) {
			pkt->insn[i].is_memop = 1;
			pkt->total_memop++;
		} 
        if (GET_ATTRIB(opcode, A_DEALLOCRET) || GET_ATTRIB(opcode, A_DEALLOCFRAME)) {
          	pkt->insn[i].is_dealloc = 1;
        }
        if (GET_ATTRIB(opcode, A_DCFLUSHOP) || GET_ATTRIB(opcode, A_DCTAGOP)) {
          	pkt->insn[i].is_dcop = 1;
        }
        
		if ((pkt->insn[i].is_load) ||
			(pkt->insn[i].iclass == ICLASS_M) ||
			(pkt->insn[i].iclass == ICLASS_ALU64)) {
			pkt->pkt_has_long_latency_insn = 1;
		}

		if ((pkt->mem_access) && (GET_ATTRIB(opcode, A_DCFETCH))) {
			pkt->dcfetch_and_access = 1;
		}

        if(pkt->mem_access && GET_ATTRIB(opcode, A_CIRCADDR)) {
//            fINSERT_BITS(pkt->pkt_has_circular, 1, pkt->insn[i].slot, 1);
            pkt->pkt_has_circular = 1;
        }

		pkt->pkt_has_call |= GET_ATTRIB(opcode, A_CALL);
		pkt->pkt_has_jumpr |= GET_ATTRIB(opcode, A_INDIRECT) && !(GET_ATTRIB(opcode,A_HINTJR));
		pkt->pkt_has_cjump |= GET_ATTRIB(opcode, A_CJUMP);
		pkt->pkt_has_cjump_dotnew |= (GET_ATTRIB(opcode, A_DOTNEW)
									  && GET_ATTRIB(opcode, A_CJUMP));
		pkt->pkt_has_cjump_dotold |= (GET_ATTRIB(opcode, A_DOTOLD)
									  && GET_ATTRIB(opcode, A_CJUMP));
		pkt->pkt_has_cjump_newval |= (GET_ATTRIB(opcode, A_DOTNEWVALUE)
									  && GET_ATTRIB(opcode, A_CJUMP));

		/* Look for indirect jump that is not a return */
		pkt->pkt_has_jumpr_return |=  GET_ATTRIB(opcode,A_INDIRECT)
			                              && !(GET_ATTRIB(opcode,A_RET)||GET_ATTRIB(opcode,A_HINTJR)||GET_ATTRIB(opcode,A_CALL)) 
										  && ((!GET_ATTRIB(opcode,A_CJUMP) && (pkt->insn[i].regno[0] == 31)) || (GET_ATTRIB(opcode,A_CJUMP) && (pkt->insn[i].regno[1] == 31)));
		
        pkt->pkt_has_ras_ret |=  (GET_ATTRIB(opcode,A_RET_TYPE)) ||
                                    ((GET_ATTRIB(opcode,A_INDIRECT)) && 
									   ((!GET_ATTRIB(opcode,A_CJUMP) && (pkt->insn[i].regno[0] == 31)) 
									   ||(GET_ATTRIB(opcode,A_CJUMP) && (pkt->insn[i].regno[1] == 31)))
									);

		canjump = decode_opcode_can_jump(opcode);

		if (pkt->pkt_has_cof) {
			if (canjump) {
				pkt->pkt_has_dual_jump = 1;
				pkt->insn[i].is_2nd_jump = 1;
			}
		} else {
			pkt->pkt_has_cof |= canjump;
		}

		pkt->insn[i].is_endloop = decode_opcode_ends_loop(opcode);

		pkt->pkt_has_endloop |= pkt->insn[i].is_endloop;
		pkt->pkt_has_endloop0 |= (GET_ATTRIB(opcode,A_HWLOOP0_END) && !GET_ATTRIB(opcode,A_HWLOOP1_END));
		pkt->pkt_has_endloop01 |= (GET_ATTRIB(opcode,A_HWLOOP0_END) && GET_ATTRIB(opcode,A_HWLOOP1_END));
		pkt->pkt_has_endloop1 |= (GET_ATTRIB(opcode,A_HWLOOP1_END) && !GET_ATTRIB(opcode,A_HWLOOP0_END));

		pkt->pkt_has_cof |= pkt->pkt_has_endloop;

		/* Now create slot valids */
		if (pkt->insn[i].is_endloop)	/* Don't count endloops */
			continue;

		switch (pkt->insn[i].slot) {
		case 0:
			pkt->slot0_valid = 1;
			break;
		case 1:
			pkt->slot1_valid = 1;
			break;
		case 2:
			pkt->slot2_valid = 1;
			break;
		case 3:
			pkt->slot3_valid = 1;
			break;
		}
		total_slots_valid++;
        if(!GET_ATTRIB(pkt->insn[i].opcode, A_IT_NOP)) {
            total_insns_sans_nop++;
        }

		/* And track #loads/stores */
		if (pkt->insn[i].is_store) {
			stores++;
            if (pkt->insn[i].is_vmem_st)
              pkt->pkt_vmem_st_ct++;
		} else if (pkt->insn[i].is_load) {
			loads++;
            if (pkt->insn[i].is_vmem_ld)
              pkt->pkt_vmem_ld_ct++;
		}

#ifdef FIXME
        //In RTL, MemPort = (attr:A_MEMLIKE and !iclass:NCJ) and !(iclass:COPROC_VMEM and attr:A_LOAD) and !attr:A_CVI_SCATTER;
        if ((GET_ATTRIB(opcode, A_MEMLIKE) && (pkt->insn[i].iclass != ICLASS_NCJ)) && !((pkt->insn[i].iclass == ICLASS_COPROC_VMEM) && GET_ATTRIB(opcode, A_LOAD)) && !(GET_ATTRIB(opcode, A_CVI_SCATTER))) {
            pkt->pkt_memport_ct++;
            if (pkt->insn[i].slot == 0)
                pkt->pkt_memport_s0 = 1;
            else if (pkt->insn[i].slot == 1)
                pkt->pkt_memport_s1 = 1;
        }
#endif

	}
	/* Track non memory slot0,1 valids */
	if ((pkt->slot0_valid) &&
		(!pkt->pkt_has_load_s0) && (!pkt->pkt_has_store_s0)) {
		pkt->pkt_has_valid_slot0_non_mem = 1;
	}

	if ((pkt->slot1_valid) &&
		(!pkt->pkt_has_load_s1) && (!pkt->pkt_has_store_s1)) {
		pkt->pkt_has_valid_slot1_non_mem = 1;
	}

	if ((pkt->slot0_valid && pkt->slot1_valid) &&
		((loads == 0) && (stores == 0))) {
		pkt->pkt_has_valid_slot01_non_mem = 1;
	}

	if (stores == 2) {
		pkt->dual_store = 1;
	} else if (loads == 2) {
		pkt->dual_load = 1;
	} else if ((loads == 1) && (stores == 1)) {
		pkt->load_and_store = 1;
	} else if (loads == 1) {
		pkt->single_load = 1;
	} else if (stores == 1) {
		pkt->single_store = 1;
	}

	pkt->total_slots_valid_minus_1 = total_slots_valid - 1;
    pkt->total_insns_sans_nop = total_insns_sans_nop;

	pkt->possible_multi_regwrite = decode_possible_multiwrite(pkt);

#ifdef FIXME
	if (thread->processor_ptr->features->QDSP6_TINY_CORE && thread->processor_ptr->arch_proc_options->tiny_core_exception_checks)
		pkt->native_pkt = is_native_tinycore_packet(thread, pkt);
	
    if (!thread->processor_ptr->arch_proc_options->dstats) {
        for (i = 0; i < pkt->num_insns; i++) {
            switch (insn_timing_classes[pkt->insn[i].opcode] & 0xf) {
                case timing_class_tc_3:      pkt->pkt_has_tc_3_instruction |= 1; break;
                case timing_class_tc_3x:     pkt->pkt_has_tc_3x_instruction |= 1; break;
                case timing_class_tc_3stall: pkt->pkt_has_tc_3stall_instruction |= 1; break;
                case timing_class_tc_ld:     pkt->pkt_has_tc_ld_instruction |= 1; break;
                case timing_class_tc_st:     pkt->pkt_has_tc_st_instruction |= 1; break;
            }
        }
    }
#endif

	return 0;
}

/* Shuffle for execution
 * Move stores to end (in same order as encoding) 
 * Move compares to beginning (for use by .new insns)
 */
#ifdef FIXME
static int decode_shuffle_for_execution(thread_t *thread, packet_t * packet, exception_info *einfo)
#else
static int decode_shuffle_for_execution(packet_t * packet)
#endif
{
	int changed = 0;
	int i;
	int flag;					/* flag means we've seen a non-memory instruction */
	int n_mems;
    int last_insn = packet->num_insns - 1;
    // Skip end loops, somehow an end loop is getting in and messing up the order
    if (decode_opcode_ends_loop(packet->insn[last_insn].opcode)) {
                    // Skip end loops
                    last_insn--;
                    } 
	do {
		changed = 0;
		/* Stores go last, must not reorder */
		/* V5: cannot shuffle stores past loads, either */
		/* Iterate backwards.  If we see a non-memory instruction, then a store, 
		 * shuffle the store to the front.  Don't shuffle stores wrt each other 
		 * or a load. */
		/* I think the assembler should be doing this for us, now? */

		for (flag = n_mems = 0, i = last_insn; i >= 0; i--) {
            
			if (flag && GET_ATTRIB(packet->insn[i].opcode, A_STORE)) {
				/* Don't reorder coproc loads and stores... WHY???  */
				decode_send_insn_to(packet, i, last_insn - n_mems );
				n_mems++;
				changed = 1;
				//break;
			} else if (GET_ATTRIB(packet->insn[i].opcode, A_STORE)) {
				n_mems++;
			} else if (GET_ATTRIB(packet->insn[i].opcode, A_LOAD)) {
				/* Don't set flag, since we don't want to shuffle a store past
				 * a load in v5 */
				n_mems++;
			} else if (GET_ATTRIB(packet->insn[i].opcode,A_DOTNEWVALUE)) {
				/* Don't set flag, since we don't want to shuffle past a .new value */
			} 
            else {
				flag = 1;
			}
		}
		if (changed)
			continue;
		/* Compares go first, may be reordered wrt each other */
		for (flag = 0, i = 0; i < last_insn+1; i++) {
			if (((strstr(opcode_wregs[packet->insn[i].opcode], "Pd4") != NULL) ||
				(strstr(opcode_wregs[packet->insn[i].opcode],"Pe4") != NULL))
				&& (GET_ATTRIB(packet->insn[i].opcode, A_STORE) == 0)) {
				/* This should be a compare (not a store conditional) */
				if (flag) {
					decode_send_insn_to(packet, i, 0);
					changed = 1;
					continue;
				}
			} else if (GET_ATTRIB(packet->insn[i].opcode, A_IMPLICIT_WRITES_P3)
					&& !decode_opcode_ends_loop(packet->insn[i].opcode)) {
				/* spNloop instruction */
				/* Don't reorder endloops; they are not valid for .new uses, and we want to match HW */
				if (flag) {
					decode_send_insn_to(packet, i, 0);
					changed = 1;
					continue;
				}
			} else if (GET_ATTRIB
					(packet->insn[i].opcode, A_IMPLICIT_WRITES_P0)
					&& !GET_ATTRIB(packet->insn[i].opcode, A_NEWCMPJUMP)) {
				/* CABAC instruction */
				if (flag) {
					decode_send_insn_to(packet, i, 0);
					changed = 1;
					continue;
				}
			} else {
				flag = 1;
			}
		}
		if (changed)
			continue;

	} while (changed);
	/* If we have a .new register compare/branch, move that to the very
	 * very end, past stores */
	/* EJP: Note: If we come up with a problem for moving it past stores,
	 * we can likely not move it past the stores, because the stores should
	 * only be generating values on post-increment like things, and that
	 * will likely not be allowed for the .new comparison. 
	 */
	for (i = 0; i < last_insn; i++) {
		if (GET_ATTRIB(packet->insn[i].opcode, A_DOTNEWVALUE)) {
			decode_send_insn_to(packet, i, last_insn);
			break;
		}
	}
	/* And at the very very very end, move any RTE's, since they update
	 * user/supervisor mode and it's easier to shuffle than to make SSR go
	 * in a reg log and not break everything.  EJP: don't we log this anyway now? */
	for (i = 0; i < last_insn; i++) {
		if ((packet->insn[i].opcode == J2_rte)) {
			decode_send_insn_to(packet, i, last_insn);
			break;
		}
	}
	return 0;
}

/* Actually check two writes / instruction case */
#ifdef FIXME
static inline int check_twowrite(thread_t * thread, insn_t * insn)
#else
static inline int check_twowrite(insn_t * insn)
#endif
{
	int n_dests = 0;
	size4u_t dmask = 1;
	size4u_t xmask = 1;
	size2u_t opcode = insn->opcode;
	char buf[128];
	if (strstr(opcode_wregs[opcode], "Rd") != NULL)
		n_dests++;
	if (strstr(opcode_wregs[opcode], "Rx") != NULL)
		n_dests++;
	if (n_dests < 2)
		return 0;
	if (strstr(opcode_wregs[opcode], "Rdd") != NULL)
		dmask = 3;
	if (strstr(opcode_wregs[opcode], "Rxx") != NULL)
		xmask = 3;
/* 	dmask = dmask << insn->regno[index(opcode_reginfo[opcode],'d')-opcode_reginfo[opcode]]; */
/* 	xmask = xmask << insn->regno[index(opcode_reginfo[opcode],'x')-opcode_reginfo[opcode]]; */
	dmask =
		dmask << insn->regno[strchr(opcode_reginfo[opcode], 'd') -
							 opcode_reginfo[opcode]];
	xmask =
		xmask << insn->regno[strchr(opcode_reginfo[opcode], 'x') -
							 opcode_reginfo[opcode]];

	if (dmask & xmask) {
#ifdef FIXME
		snprintinsn(buf, 128, insn);
#else
		sprintf(buf, "FIXME: %s, %d", __FILE__, __LINE__);
#endif
		warn("[UNDEFINED] Overlapping regs? %s", buf);
	}
	return 0;
}

#define JUST_GENISET_SUPPORT
#undef JUST_GENISET_SUPPORT

#if 0
/* Check to see whether it was OK to skip over a slot N */
static int
#ifdef FIXME
decode_assembler_check_skipped_slot(thread_t * thread, packet_t * pkt, int slot, exception_info *einfo)
#else
decode_assembler_check_skipped_slot(packet_t * pkt, int slot)
#endif
{
	int i;
	const char *valid_slot_str;
	char pkt_buf[1024];
	for (i = 0; i < pkt->num_insns; i++) {
		if (decode_opcode_ends_loop(pkt->insn[i].opcode))
			continue;
		if (pkt->insn[i].slot > slot)
			continue;			/* already in a higher slot */
		if (pkt->insn[i].slot == slot) {
			snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
			fatal("slot %d not empty? insn=%d pkt=%s", slot, i, pkt_buf);
		}
		valid_slot_str =
			find_iclass_slots(pkt->insn[i].opcode, pkt->insn[i].iclass);
		if (strchr(valid_slot_str, '0' + slot)) {
			decode_error(thread, einfo, PRECISE_CAUSE_INVALID_PACKET);
			snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
			warn("[NEWDEFINED] Slot%d empty, could be filled with insn in slot%d: <SLOT%s> %s", slot, pkt->insn[i].slot, valid_slot_str, pkt_buf);
			return 1;
		}
	}
	return 0;
}
#endif

#if 0
/* Check all the slot ordering restrictions */
#ifdef FIXME
static int decode_assembler_check_slots(thread_t * thread, packet_t * pkt, exception_info *einfo)
#else
static int decode_assembler_check_slots(packet_t * pkt)
#endif
{
	int slot;
	int i, j;
	const char *valid_slot_str;
	char pkt_buf[1024];
	unsigned int skipped_slots = 0;
	/* Check to make sure they are grouped in decreasing order */
	for (i = 0, slot = 3; i < pkt->num_insns; i++) {
		if (decode_opcode_ends_loop(pkt->insn[i].opcode))
			continue;
		valid_slot_str =
			find_iclass_slots(pkt->insn[i].opcode, pkt->insn[i].iclass);
		if (slot < 0) {
			decode_error(thread, einfo, PRECISE_CAUSE_INVALID_PACKET);
			snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
			warn("[NEWDEFINED] Can't map insns to slots: %s", pkt_buf);
			return 1;
		}
		while (strchr(valid_slot_str, '0' + slot) == NULL) {
			if (slot <= 0) {
				decode_error(thread, einfo, PRECISE_CAUSE_INVALID_PACKET);
				snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
				warn("[NEWDEFINED] Can't map insns to slots: %s", pkt_buf);
				return 1;
			}
			skipped_slots |= (1 << slot);
			slot--;
		}
		/*   comment this out b/c 'decode_set_slot_number' should do this.                
		   pkt->insn[i].slot = slot;
		 */
		slot--;
	}

	/* Check to make sure insns grouped as high as possible */
	if (skipped_slots) {
		for (slot = 3; slot >= 0; slot--) {
			if (skipped_slots & (1 << slot)) {
#ifdef FIXME
				decode_assembler_check_skipped_slot(thread, pkt, slot, einfo);
#else
				decode_assembler_check_skipped_slot(pkt, slot);
#endif
			}
		}
	}

	/* Check single-mem-last */
	for (i = 0; i < pkt->num_insns; i++) {
		int saw_mem = 0;
		int slot0_alu32 = 0;
		if (decode_opcode_ends_loop(pkt->insn[i].opcode))
			continue;
		if (GET_ATTRIB(pkt->insn[i].opcode, A_MEMLIKE)||GET_ATTRIB(pkt->insn[i].opcode, A_MEMLIKE_PACKET_RULES))
			saw_mem = 1;
		if ((pkt->insn[i].slot == 0) &&
			(!GET_ATTRIB(pkt->insn[i].opcode, A_MEMLIKE) && !GET_ATTRIB(pkt->insn[i].opcode, A_MEMLIKE_PACKET_RULES)))
			slot0_alu32 = 1;
		if (saw_mem && slot0_alu32) {
//          warn("[UNDEFINED] single mem in slot1: %s", pkt_buf);
			snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
			warn("single mem in slot1: %s", pkt_buf);
			decode_error(thread,einfo, PRECISE_CAUSE_INVALID_PACKET);
		}
	}

	/* Check noslot1 restriction */
	for (i = 0; i < pkt->num_insns; i++) {
		int saw_slot1_store = 0;
		int need_restriction = 0;
		if (GET_ATTRIB(pkt->insn[i].opcode, A_RESTRICT_NOSLOT1_STORE)) {
			need_restriction = 1;
		}
		if (GET_ATTRIB(pkt->insn[i].opcode, A_STORE) && (pkt->insn[i].slot == 1)) {
			saw_slot1_store = 1;
		}

		if (saw_slot1_store && need_restriction) {
//          warn("[UNDEFINED] slot1 store not allowed: %s", pkt_buf);
			snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
			warn("slot1 store not allowed: %s", pkt_buf);
			decode_error(thread,einfo,PRECISE_CAUSE_INVALID_PACKET);
			return 1;
		}
	}
	
	/* Check noslot0 restriction */
	for (i = 0; i < pkt->num_insns; i++) {
		int saw_slot0_load = 0;
		int need_restriction = 0;
		if (GET_ATTRIB(pkt->insn[i].opcode, A_RESTRICT_NOSLOT0_LOAD) ) {
			need_restriction = 1;
		}
		if (GET_ATTRIB(pkt->insn[i].opcode, A_LOAD) && (pkt->insn[i].slot == 0)) {
			saw_slot0_load = 1;
		}

		if (saw_slot0_load && need_restriction) {
//          warn("[UNDEFINED] slot1 store not allowed: %s", pkt_buf);
			snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
			warn("slot0 load not allowed: %s", pkt_buf);
			decode_error(thread,einfo,PRECISE_CAUSE_INVALID_PACKET);
			return 1;
		}
	}

	/* Check solo insns */
	for (i = 0; i < pkt->num_insns; i++) {
		if ((GET_ATTRIB(pkt->insn[i].opcode, A_RESTRICT_NOPACKET))
			&& (pkt->num_insns > 1)) {
//          warn("[UNDEFINED] insn %d solo, but in a packet: %s", i, pkt_buf);
			snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
			warn("insn %d solo, but in a packet: %s", i, pkt_buf);
			decode_error(thread,einfo,PRECISE_CAUSE_INVALID_PACKET);
			return 1;
		}
	}

	/* Check slot1 empty insns */
	for (i = 0; i < pkt->num_insns; i++) {
		if ((GET_ATTRIB(pkt->insn[i].opcode, A_RESTRICT_NOSLOT1))
			&& (skipped_slots & 2)) {
			for (j = 0; j < pkt->num_insns; j++) {
				if (i == j)
					continue;
				if ((pkt->insn[j].slot == 1)
					&& !GET_ATTRIB(pkt->insn[j].opcode, A_IT_NOP)) {
					decode_error(thread, einfo,PRECISE_CAUSE_INVALID_PACKET);
					snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
					warn("[NEWDEFINED] slot1 not empty/nop: %s", pkt_buf);
					return 1;
				}
			}
		}
	}

	/* Check slot1 empty insns */
	for (i = 0; i < pkt->num_insns; i++) {
		if ((GET_ATTRIB(pkt->insn[i].opcode, A_RESTRICT_SLOT1_AOK))
			&& (skipped_slots & 2)) {
			for (j = 0; j < pkt->num_insns; j++) {
				if (i == j)
					continue;
				if ((pkt->insn[j].slot == 1)
					&& (GET_ATTRIB(pkt->insn[j].opcode, A_LOAD) ||
						GET_ATTRIB(pkt->insn[j].opcode, A_STORE))) {
					decode_error(thread, einfo,PRECISE_CAUSE_INVALID_PACKET);
					snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
					warn("[NEWDEFINED] slot1 not A-type: %s", pkt_buf);
					return 1;
				}
			}
		}
	}
	

	/* no slot 2 mpy for tiny core dmac */
	for (i = 0; i < pkt->num_insns; i++) {
		if (GET_ATTRIB(pkt->insn[i].opcode, A_RESTRICT_NOSLOT2_MPY)) {
			for (j = 0; j < pkt->num_insns; j++) {
				if (GET_ATTRIB(pkt->insn[j].opcode, A_MPY) && (pkt->insn[j].slot == 2)) {
					decode_error(thread, einfo,PRECISE_CAUSE_INVALID_PACKET);
					snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
					warn("[NEWDEFINED] slot 2 has a mpy with dmac: %s", pkt_buf);
					return 1;
				}
			}
		}
	}
	
	
	return 0;
}
#endif

static int
#ifdef FIXME
decode_assembler_check_branching(thread_t * thread, packet_t * pkt, exception_info *einfo)
#else
decode_assembler_check_branching(packet_t * pkt)
#endif
{
	char pkt_buf[1024];
	int i;
	unsigned int n_branchadders = 0;
	unsigned int n_cofs = 0;
	unsigned int relax1 = 0;
	unsigned int relax2 = 0;
	for (i = 0; i < pkt->num_insns; i++) {
		if (GET_ATTRIB(pkt->insn[i].opcode, A_BRANCHADDER))
			n_branchadders++;
		if (GET_ATTRIB(pkt->insn[i].opcode, A_COF))
			n_cofs++;
		if ((relax1 == 0)
			&& (GET_ATTRIB(pkt->insn[i].opcode, A_RELAX_COF_1ST))) {
			relax1 = 1;
		} else if ((relax1 == 1)
				   && (GET_ATTRIB(pkt->insn[i].opcode, A_RELAX_COF_2ND))) {
			relax2 = 1;
		}
	}
	if (n_cofs == 2) {
		if (relax1 && relax2) {
			return 0;
		}
	}
	if (n_branchadders > 2) {
		decode_error(thread, einfo,PRECISE_CAUSE_INVALID_PACKET);
		snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
		warn("[NEWDEFINED] n_branchadders = %d > 2: %s",
			 n_branchadders, pkt_buf);
		return 1;
	}
	if (n_cofs > 1) {
		decode_error(thread, einfo,PRECISE_CAUSE_INVALID_PACKET);
		snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
		warn("[NEWDEFINED] n_cofs = %d > 1: %s", n_cofs, pkt_buf);
		return 1;
	}
	return 0;
}

static int
#ifdef FIXME
decode_assembler_check_srmove(thread_t * thread, packet_t * pkt, exception_info *einfo)
#else
decode_assembler_check_srmove(packet_t * pkt)
#endif
{
	char pkt_buf[1024];
	unsigned int saw_srmove = 0;
	unsigned int saw_nosrmove = 0;
	int i;
	for (i = 0; i < pkt->num_insns; i++) {
		if (GET_ATTRIB(pkt->insn[i].opcode, A_RESTRICT_NOSRMOVE))
			saw_nosrmove++;
		if ((pkt->insn[i].opcode == A2_tfrrcr) &&
			(pkt->insn[i].regno[0] == 8))
			saw_srmove = 1;
	}
	if (saw_srmove && saw_nosrmove) {
		decode_error(thread, einfo,PRECISE_CAUSE_INVALID_PACKET);
		snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
		warn("[NEWDEFINED] 'USR=R' not allowed with SR update: %s",
			 pkt_buf);
		return 1;
	}
	return 0;
}

static int
#ifdef FIXME
decode_assembler_check_loopla(thread_t * thread, packet_t * pkt, exception_info *einfo)
#else
decode_assembler_check_loopla(packet_t * pkt)
#endif
{
	char pkt_buf[1024];
	int is_endloop0 = 0;
	int is_endloop1 = 0;
	int i;
	/* Find what loops we might be the end of */
	for (i = 0; i < pkt->num_insns; i++) {
		if (GET_ATTRIB(pkt->insn[i].opcode, A_HWLOOP0_END)) {
			is_endloop0 = 1;
		}
		if (GET_ATTRIB(pkt->insn[i].opcode, A_HWLOOP1_END)) {
			is_endloop1 = 1;
		}
	}
	if (!is_endloop0 && !is_endloop1)
		return 0;					/* Nothing more to do */
	for (i = 0; i < pkt->num_insns; i++) {
		size2u_t opcode = pkt->insn[i].opcode;
		if (GET_ATTRIB(opcode, A_COF))
			continue;			/* This is the endloop */
		if (is_endloop0) {
			if ((strstr(opcode_wregs[opcode], "SA0")) ||
				(strstr(opcode_wregs[opcode], "LC0")) ||
				((opcode == A2_tfrrcr) && (pkt->insn[i].regno[0] == 0)) ||
				((opcode == A2_tfrrcr) && (pkt->insn[i].regno[0] == 1))) {
				decode_error(thread, einfo,PRECISE_CAUSE_INVALID_PACKET);
				snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
				warn("[NEWDEFINED] Writes SA0/LC0 in endloop0: %s",
					 pkt_buf);
				return 1;
			}
		}
		if (is_endloop1) {
			if ((strstr(opcode_wregs[opcode], "SA1")) ||
				(strstr(opcode_wregs[opcode], "LC1")) ||
				((opcode == A2_tfrrcr) && (pkt->insn[i].regno[0] == 2)) ||
				((opcode == A2_tfrrcr) && (pkt->insn[i].regno[0] == 3))) {
				decode_error(thread, einfo,PRECISE_CAUSE_INVALID_PACKET);
				snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
				warn("[NEWDEFINED] Writes SA1/LC1 in endloop1: %s",
					 pkt_buf);
				return 1;
			}
		}
	}
	return 0;
}

#if 0
#ifdef FIXME
static int decode_assembler_check_sc(thread_t * thread, packet_t * pkt, exception_info *einfo)
#else
static int decode_assembler_check_sc(packet_t * pkt)
#endif
{
	int i;
	int has_sc = 0;
	char pkt_buf[1024];
	enum {
#define DEF_PP_ICLASS32(TYPE,SLOTS,UNITS) ICLASS_PP_TYPE_##TYPE,
#define DEF_EE_ICLASS32(TYPE,SLOTS,UNITS)	/* NOTHING */
#include "iclass.def"
#undef DEF_EE_ICLASS32
#undef DEF_PP_ICLASS32
#define DEF_PP_ICLASS32(TYPE,SLOTS,UNITS)	/* NOTHING */
#define DEF_EE_ICLASS32(TYPE,SLOTS,UNITS) ICLASS_EE_TYPE_##TYPE,
#include "iclass.def"
#undef DEF_EE_ICLASS32
#undef DEF_PP_ICLASS32
	};
	for (i = 0; i < pkt->num_insns; i++) {
		if (pkt->insn[i].opcode == S2_storew_locked)
			has_sc = 1;
	}
	if (!has_sc)
		return 0;
	for (i = 0; i < pkt->num_insns; i++) {
		if (pkt->insn[i].opcode == S2_storew_locked)
			continue;
		if (decode_opcode_ends_loop(pkt->insn[i].opcode)) {
			decode_error(thread, einfo,PRECISE_CAUSE_INVALID_PACKET);
			snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
			warn("[NEWDEFINED] memw_locked store can only be grouped with A32/X: %s", pkt_buf);
			return 1;
		}
		switch (pkt->insn[i].iclass) {
		case ICLASS_PP_TYPE_ALU32_2op:
		case ICLASS_PP_TYPE_ALU32_3op:
		case ICLASS_PP_TYPE_ALU32_ADDI:
		case ICLASS_PP_TYPE_S_2op:
		case ICLASS_PP_TYPE_S_3op:
		case ICLASS_PP_TYPE_ALU64:
		case ICLASS_PP_TYPE_M:
			break;
		default:
			decode_error(thread, einfo,PRECISE_CAUSE_INVALID_PACKET);
			snprint_a_pkt(pkt_buf, 1024, pkt, NULL);
			warn("[NEWDEFINED] memw_locked store can only be grouped with A32/X: %s", pkt_buf);
			return 1;
		}
	}
	return 0;
}
#endif

#ifdef FIXME
static int decode_assembler_check_fpops(thread_t * thread, packet_t * pkt, exception_info *einfo)
#else
static int decode_assembler_check_fpops(packet_t * pkt)
#endif
{
	int i;
	for (i = 0; i < pkt->num_insns; i++) {
		if (GET_ATTRIB(pkt->insn[i].opcode, A_FPOP)) {
			pkt->pkt_has_fp_op = 1;
		}
		if (GET_ATTRIB(pkt->insn[i].opcode, A_FPDOUBLE)) {
			pkt->pkt_has_fpdp_op = 1;
		} else if (GET_ATTRIB(pkt->insn[i].opcode, A_FPSINGLE)) {
			pkt->pkt_has_fpsp_op = 1;
		}
	}
	return 0;
}

#ifdef FIXME
static int decode_assembler_checks(thread_t * thread, packet_t * pkt, exception_info *einfo)
#else
static int decode_assembler_checks(packet_t * pkt)
#endif
{
	int errors = 0;
	// XXX: FIXME: remove this when assembler and tests working for slot23 cvi
	//if (GET_SSR_FIELD(SSR_XE)) return 0;
#ifdef FIXME
	errors += decode_assembler_check_fpops(thread, pkt,einfo);
	errors += decode_assembler_check_slots(thread, pkt,einfo);
	errors += decode_assembler_check_branching(thread, pkt,einfo);
	errors += decode_assembler_check_srmove(thread, pkt,einfo);
	errors += decode_assembler_check_loopla(thread, pkt,einfo);
	errors += decode_assembler_check_sc(thread, pkt,einfo);
#else
	errors += decode_assembler_check_fpops(pkt);
//	errors += decode_assembler_check_slots(pkt);
	errors += decode_assembler_check_branching(pkt);
	errors += decode_assembler_check_srmove(pkt);
	errors += decode_assembler_check_loopla(pkt);
//	errors += decode_assembler_check_sc(pkt);
#endif
	return errors;
}

#ifdef FIXME
static int decode_audio_extensions(thread_t * thread, packet_t * pkt, exception_info *einfo)
#else
static int decode_audio_extensions(packet_t * pkt)
#endif
{
	int i;
	for (i = 0; i < pkt->num_insns; i++) {
#ifdef FIXME
		if (GET_ATTRIB(pkt->insn[i].opcode, A_EXTENSION_AUDIO) && !thread->processor_ptr->arch_proc_options->audio_ext_enable) {
			warn("Decode fail for audio extension instruction on a non-audio (tiny) core");
			decode_error(thread, einfo, PRECISE_CAUSE_NO_COPROC2_ENABLE); // Maybe coproc packet
			return 1;
		}
#endif
	}
	return 0;
}

#ifdef FIXME
static int decode_native_packet(thread_t * thread, packet_t * pkt, exception_info *einfo)
#else
static int decode_native_packet(packet_t * pkt)
#endif
{
#ifdef FIXME
	if ((thread->processor_ptr->features->QDSP6_TINY_CORE) && (!pkt->native_pkt) && thread->processor_ptr->arch_proc_options->tiny_core_exception_checks) {
		warn("Not native packet, can't crack");
		decode_error(thread, einfo, PRECISE_CAUSE_INVALID_PACKET);
		return 1;	
	}
#endif
	return 0;
}

static int
#ifdef FIXME
apply_extender(thread_t * thread, packet_t * pkt, int i, size4u_t extender, exception_info *einfo)
#else
apply_extender(packet_t * pkt, int i, size4u_t extender)
#endif
{
	int immed_num;
	size4u_t base_immed;

	if (i == pkt->num_insns) {
//      warn("[UNDEFINED] Extenders at end-of-packet");
		warn("Extenders at end-of-packet, taking error exception");
		decode_error(thread,einfo,PRECISE_CAUSE_INVALID_PACKET);
		return 1;
	}
	if (GET_ATTRIB(pkt->insn[i].opcode, A_IT_EXTENDER)) {
		/* Another extender word... */
		//return apply_extender(thread,pkt,i+1,immediate,whichimm);
//      warn("[UNDEFINED] two extenders in a row");
		warn("two extenders in a row, taking error exception");
		decode_error(thread,einfo,PRECISE_CAUSE_INVALID_PACKET);
		return 1;
	}
	if (!GET_ATTRIB(pkt->insn[i].opcode, A_EXTENDABLE)) {
		warn("Instruction not extendable: %s",
			 opcode_names[pkt->insn[i].opcode]);
		decode_error(thread,einfo,PRECISE_CAUSE_INVALID_PACKET);
		return 1;
	}
	immed_num =
#ifdef FIXME
		opcode_which_immediate_is_extended(thread, pkt->insn[i].opcode);
#else
		opcode_which_immediate_is_extended(pkt->insn[i].opcode);
#endif
	base_immed = pkt->insn[i].immed[immed_num];

	pkt->insn[i].immed[immed_num] = extender | fZXTN(6, 32, base_immed);
	return 0;
}

#ifdef FIXME
static int decode_apply_extenders(thread_t * thread, packet_t * packet, exception_info *einfo)
#else
static int decode_apply_extenders(packet_t * packet)
#endif
{
	int i;
	for (i = 0; i < packet->num_insns; i++) {
		if (GET_ATTRIB(packet->insn[i].opcode, A_IT_EXTENDER)) {
			packet->insn[i+1].extension_valid = 1;
			packet->pkt_has_payload = 1;
#ifdef FIXME
			apply_extender(thread, packet, i + 1, 
				packet->insn[i].immed[0], einfo);
#else
			apply_extender(packet, i + 1, 
				packet->insn[i].immed[0]);
#endif
		}
	}
	return 0;
}

#ifdef FIXME
static int decode_remove_extenders(thread_t * thread, packet_t * packet, exception_info *einfo)
#else
static int decode_remove_extenders(packet_t * packet)
#endif
{
	int i, j;
	for (i = 0; i < packet->num_insns; i++) {
		if (GET_ATTRIB(packet->insn[i].opcode, A_IT_EXTENDER)) {
			for (j = i; (j< packet->num_insns-1)&&(j<INSTRUCTIONS_MAX-1); j++) {
				packet->insn[j] = packet->insn[j + 1];
			}
			packet->num_insns--;
		}
	}
	return 0;
}

#ifdef FIXME
static int decode_check_latepred(thread_t * thread, packet_t * packet, exception_info *einfo)
#else
static int decode_check_latepred(packet_t * packet)
#endif
{
	int i;
	unsigned int pred_newreads = 0;
	unsigned int latepred_writes = 0;
	int opc;
	int regno;
	insn_t *insn;
	for (i = 0; i < packet->num_insns; i++) {
		insn = &packet->insn[i];
		opc = insn->opcode;
		if (GET_ATTRIB(opc, A_RESTRICT_LATEPRED)) {
			if (GET_ATTRIB(opc, A_IMPLICIT_WRITES_P0)) {
				latepred_writes |= 1;
				continue;
			}
			if (GET_ATTRIB(opc, A_IMPLICIT_WRITES_P1)) {
				latepred_writes |= 2;
				continue;
			}
			if (GET_ATTRIB(opc, A_IMPLICIT_WRITES_P2)) {
				latepred_writes |= 4;
				continue;
			}
			if (GET_ATTRIB(opc, A_IMPLICIT_WRITES_P3)) {
				/* Ignore loopend0/loopend1 because they might not write late */
				if (GET_ATTRIB(opc, A_HWLOOP0_END)) continue;
				latepred_writes |= 8;
				continue;
			}
			if (strstr(opcode_wregs[opc], "Pd") != NULL) {
				regno = insn->regno[decode_get_regno(insn, 'd')];
				latepred_writes |= (1 << regno);
			}
			if (strstr(opcode_wregs[opc], "Pe") != NULL) {
				regno = insn->regno[decode_get_regno(insn, 'e')];
				latepred_writes |= (1 << regno);
			}
		}
		if (GET_ATTRIB(opc, A_DOTNEW)) {
			if (GET_ATTRIB(opc, A_IMPLICIT_READS_P0)) {
				pred_newreads |= 1;
			}
			if (GET_ATTRIB(opc, A_IMPLICIT_READS_P1)) {
				pred_newreads |= 2;
			}
			if (strstr(opcode_rregs[opc], "Ps") != NULL) {
				regno = insn->regno[decode_get_regno(insn, 's')];
				pred_newreads |= (1 << regno);
			}
			if (strstr(opcode_rregs[opc], "Pt") != NULL) {
				regno = insn->regno[decode_get_regno(insn, 't')];
				pred_newreads |= (1 << regno);
			}
			if (strstr(opcode_rregs[opc], "Pu") != NULL) {
				regno = insn->regno[decode_get_regno(insn, 'u')];
				pred_newreads |= (1 << regno);
			}
			if (strstr(opcode_rregs[opc], "Pv") != NULL) {
				regno = insn->regno[decode_get_regno(insn, 'v')];
				pred_newreads |= (1 << regno);
			}
		}
	}
	if ((pred_newreads & latepred_writes) != 0) {
		warn(".new predicate read of a late-generated predicate!");
		warn("newreads: %x latewrites: %x", pred_newreads,
			 latepred_writes);
#ifdef FIXME
		thread->exception_msg = ".new pred read, late pred generation";
#endif
		decode_error(thread,einfo,PRECISE_CAUSE_INVALID_PACKET);
		return 1;
	}
	return 0;
}

#if 0
static const char *
#ifdef FIXME
get_valid_slot_str(const thread_t *thread, const packet_t *pkt, unsigned int slot)
#else
get_valid_slot_str(const packet_t *pkt, unsigned int slot)
#endif
{
	if (GET_ATTRIB(pkt->insn[slot].opcode, A_EXTENSION)) {
		/* Extensions might map to different slot numbers */
#ifdef FIXME
		size4u_t active_ext; 
		fCUREXT_WRAP(active_ext);
		return thread->processor_ptr->exttab[active_ext]->ext_decode_find_iclass_slots(pkt->insn[slot].opcode);
#else
		// FIXME Only dealing with HVX for now
		return mmvec_ext_decode_find_iclass_slots(pkt->insn[slot].opcode);

#endif
	} else {
		/* First fix the general slot numbers */
		return find_iclass_slots(pkt->insn[slot].opcode,  pkt->insn[slot].iclass);
	}
}
#endif

static int decode_possible_multiwrite(packet_t * pkt)
{
	int i;
	size8u_t flag;
	size8u_t mask = 0;
	int rnum;

	if (pkt->num_insns == 1) {
		return 0;
	}
	for (i = 0; i < pkt->num_insns; i++) {
		if (GET_ATTRIB(pkt->insn[i].opcode, A_CONDEXEC)) {
			return 1;
		}
		if (GET_ATTRIB(pkt->insn[i].opcode, A_IMPLICIT_WRITES_P3) ||
			GET_ATTRIB(pkt->insn[i].opcode, A_IMPLICIT_WRITES_P2) ||
			GET_ATTRIB(pkt->insn[i].opcode, A_IMPLICIT_WRITES_P1) ||
			GET_ATTRIB(pkt->insn[i].opcode, A_IMPLICIT_WRITES_P0)) {
			return 1;
		}
		if (GET_ATTRIB(pkt->insn[i].opcode, A_RESTRICT_LATEPRED)) {
			return 1;
		}

		/* Regno zero is usually the destination.
		   If we see multiple insns writing to the same regno[0],
		   mark it as a possible collision.
		   Acutal collision is checked in exec.c. This is just used
		   to determine when to check 
		 */
		rnum = pkt->insn[i].regno[0];
		if (rnum < 63) {
			flag = (1ULL << rnum);
			if (flag & mask) {
				return 1;
			}
			mask |= flag;
		}
	}
	return 0;
}

#ifdef FIXME
static int decode_handle_stores(thread_t * thread, packet_t * pkt, exception_info *einfo)
#else
static int decode_handle_stores(packet_t * pkt)
#endif
{
	int i, n_stores, is_stld = 0;
	for (i = n_stores = 0; i < pkt->num_insns; i++) {
		if (GET_ATTRIB(pkt->insn[i].opcode, A_STORE)) {
			n_stores++;
		} else if (GET_ATTRIB(pkt->insn[i].opcode, A_LOAD)) {
			if (n_stores > 0)
				is_stld = 1;
		}
	}
	if ((is_stld == 0) && (n_stores <= 1))
		return 0;

	if (is_stld)
		pkt->pkt_has_stld = 1;
	/* TBD: insert load with storebuf search for A_LOAD instructions */
	return 0;
}

#include "q6v_decode.c"

#ifdef FIXME
packet_t *decode_stuffed(thread_t *thread)
#else
packet_t *decode_stuffed(size4u_t *words, packet_t *decode_pkt);
packet_t *decode_stuffed(size4u_t *words, packet_t *decode_pkt)
#endif
{
	int ret;
#ifdef FIXME
	exception_info einfo;
	memset(&einfo,0,sizeof(einfo));
	ret = do_decode_packet(thread, 0xdeadadd0, 1,
		&thread->processor_ptr->global_regs[REG_STFINST],&thread->decode_packet);
#else
	ret = do_decode_packet(0xdeadadd0, 1,
		words,decode_pkt);
#endif
	if (ret <= 0) /* ERROR or BAD PARSE */ return NULL;
#ifdef FIXME
	if (einfo.valid) {
		/* Is this right? Guessing so, it used to do this */
		register_einfo(thread,&einfo);
		return NULL;
	}
#endif
#ifdef VERIFICATION
    thread->decode_packet.words[0] = thread->processor_ptr->global_regs[REG_STFINST];
#endif
#ifdef FIXME
	return &thread->decode_packet;
#else
	return decode_pkt;
#endif

}

#ifdef FIXME
packet_t *decode_this(thread_t *thread, size4u_t *words, packet_t *decode_pkt)
#else
packet_t *decode_this(size4u_t *words, packet_t *decode_pkt)
#endif
{
	int ret;
#ifdef FIXME
	exception_info einfo;
	memset(&einfo,0,sizeof(einfo));
	ret = do_decode_packet(thread, 0xdeadadd0, 4, words,decode_pkt,&einfo);
#else
	ret = do_decode_packet(0xdeadadd0, 4, words,decode_pkt);
#endif
	if (ret <= 0) /* ERROR or BAD PARSE */ return NULL;
#ifdef FIXME
	if (einfo.valid) return NULL;
#endif
	return decode_pkt;
}

#ifdef FIXME
#include "uarch/udecode.c"
#endif
