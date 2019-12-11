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

#ifndef INSN_H
#define INSN_H

#include "cpu.h"
#include "imported/global_types.h"
#include "translate.h"

#define INSTRUCTIONS_MAX 7    /* 2 pairs + loopend */
#define REG_OPERANDS_MAX 5
#define IMMEDS_MAX 2

struct Instruction;

typedef void (*semantic_insn_t)(CPUHexagonState *env,
                                DisasContext *ctx,
                                struct Instruction *insn);

struct Instruction {
    semantic_insn_t generate;            /* pointer to genptr routine */
    size1u_t regno[REG_OPERANDS_MAX];    /* reg operands including predicates */
    size2u_t opcode;

    size4u_t iclass:6;
    size4u_t slot:3;
    size4u_t part1:1;        /*
                              * cmp-jumps are split into two insns.
                              * set for the compare and clear for the jump
                              */
    size4u_t extension_valid:1;   /* Has a constant extender attached */
    size4u_t which_extended:1;    /* If has an extender, which immediate */
    size4u_t is_dcop:1;      /* Is a dcacheop */
    size4u_t is_dcfetch:1;   /* Has an A_DCFETCH attribute */
    size4u_t is_load:1;      /* Has A_LOAD attribute */
    size4u_t is_store:1;     /* Has A_STORE attribute */
    size4u_t is_vmem_ld:1;   /* Has an A_LOAD and an A_VMEM attribute */
    size4u_t is_vmem_st:1;   /* Has an A_STORE and an A_VMEM attribute */
    size4u_t is_scatgath:1;  /* Has an A_CVI_GATHER or A_CVI_SCATTER attr */
    size4u_t is_memop:1;     /* Has A_MEMOP attribute */
    size4u_t is_dealloc:1;   /* Is a dealloc return or dealloc frame */
    size4u_t is_aia:1;       /* Is a post increment */
    size4u_t is_endloop:1;   /* This is an end of loop */
    size4u_t is_2nd_jump:1;  /* This is the second jump of a dual-jump packet */
    size4u_t new_value_producer_slot:4;
    size4u_t hvx_resource:8;
    size4s_t immed[IMMEDS_MAX];    /* immediate field */
};

typedef struct Instruction insn_t;

struct Packet {
    size2u_t num_insns;
    size2u_t encod_pkt_size_in_bytes;

    /* Pre-decodes about LD/ST */
    size8u_t single_load:1;
    size8u_t dual_load:1;
    size8u_t single_store:1;
    size8u_t dual_store:1;
    size8u_t load_and_store:1;
    size8u_t memop_or_nvstore:1;

    /* Pre-decodes about COF */
    size8u_t pkt_has_cof:1;          /* Has any change-of-flow */
    size8u_t pkt_has_dual_jump:1;
    size8u_t pkt_has_initloop:1;
    size8u_t pkt_has_initloop0:1;
    size8u_t pkt_has_initloop1:1;
    size8u_t pkt_has_endloop:1;
    size8u_t pkt_has_endloop0:1;
    size8u_t pkt_has_endloop1:1;
    size8u_t pkt_has_endloop01:1;
    size8u_t pkt_has_call:1;
    size8u_t pkt_has_ras_ret:1;
    size8u_t pkt_has_jumpr:1;
    size8u_t pkt_has_cjump:1;
    size8u_t pkt_has_cjump_dotnew:1;
    size8u_t pkt_has_cjump_dotold:1;
    size8u_t pkt_has_cjump_newval:1;
    size8u_t pkt_has_duplex:1;
    size8u_t pkt_has_payload:1;      /* Contains a constant extender */
    size8u_t pkt_has_dealloc_return:1;
    size8u_t pkt_has_jumpr_return:1;

    /* Pre-decodes about SLOTS */
    size8u_t slot0_valid:1;
    size8u_t slot1_valid:1;
    size8u_t slot2_valid:1;
    size8u_t slot3_valid:1;

    /* When a predicate cancels something, track that */
    size8u_t pkt_has_fp_op:1;
    size8u_t pkt_has_fpsp_op:1;
    size8u_t pkt_has_fpdp_op:1;

    /* Contains a cacheop */
    size8u_t pkt_has_cacheop:1;
    size8u_t pkt_has_dczeroa:1;
    size8u_t pkt_has_ictagop:1;
    size8u_t pkt_has_icflushop:1;
    size8u_t pkt_has_dcflushop:1;
    size8u_t pkt_has_dctagop:1;
    size8u_t pkt_has_l2flushop:1;
    size8u_t pkt_has_l2tagop:1;

    /* load store for slots */
    size8u_t pkt_has_load_s0:1;
    size8u_t pkt_has_load_s1:1;
    size8u_t pkt_has_store_s0:1;
    size8u_t pkt_has_store_s1:1;

    /* Misc */
    size8u_t num_rops:4;            /* Num risc ops in the packet */
    size8u_t pkt_has_vtcm_access:1; /* Is a vmem access going to VTCM */
    size8u_t pkt_access_count:2;    /* Is a vmem access going to VTCM */
    size8u_t pkt_ldaccess_l2:2;     /* vmem ld access to l2 */
    size8u_t pkt_ldaccess_vtcm:2;   /* vmem ld access to vtcm */

    /* Count the types of HVX instructions */
    size8u_t pkt_hvx_va:4;
    size8u_t pkt_hvx_vx:4;
    size8u_t pkt_hvx_vp:4;
    size8u_t pkt_hvx_vs:4;
    size8u_t pkt_hvx_all:4;
    size8u_t pkt_hvx_none:4;

    size8u_t pkt_has_extension:1;

    insn_t insn[INSTRUCTIONS_MAX];
};

typedef struct Packet packet_t;

#endif
