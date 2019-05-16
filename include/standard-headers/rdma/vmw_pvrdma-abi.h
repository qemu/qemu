/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-2-Clause) */
/*
 * Copyright (c) 2012-2016 VMware, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of EITHER the GNU General Public License
 * version 2 as published by the Free Software Foundation or the BSD
 * 2-Clause License. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License version 2 for more details at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program available in the file COPYING in the main
 * directory of this source tree.
 *
 * The BSD 2-Clause License
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __VMW_PVRDMA_ABI_H__
#define __VMW_PVRDMA_ABI_H__

#include "standard-headers/linux/types.h"

#define PVRDMA_UVERBS_ABI_VERSION	3		/* ABI Version. */
#define PVRDMA_UAR_HANDLE_MASK		0x00FFFFFF	/* Bottom 24 bits. */
#define PVRDMA_UAR_QP_OFFSET		0		/* QP doorbell. */
#define PVRDMA_UAR_QP_SEND		(1 << 30)	/* Send bit. */
#define PVRDMA_UAR_QP_RECV		(1 << 31)	/* Recv bit. */
#define PVRDMA_UAR_CQ_OFFSET		4		/* CQ doorbell. */
#define PVRDMA_UAR_CQ_ARM_SOL		(1 << 29)	/* Arm solicited bit. */
#define PVRDMA_UAR_CQ_ARM		(1 << 30)	/* Arm bit. */
#define PVRDMA_UAR_CQ_POLL		(1 << 31)	/* Poll bit. */
#define PVRDMA_UAR_SRQ_OFFSET		8		/* SRQ doorbell. */
#define PVRDMA_UAR_SRQ_RECV		(1 << 30)	/* Recv bit. */

enum pvrdma_wr_opcode {
	PVRDMA_WR_RDMA_WRITE,
	PVRDMA_WR_RDMA_WRITE_WITH_IMM,
	PVRDMA_WR_SEND,
	PVRDMA_WR_SEND_WITH_IMM,
	PVRDMA_WR_RDMA_READ,
	PVRDMA_WR_ATOMIC_CMP_AND_SWP,
	PVRDMA_WR_ATOMIC_FETCH_AND_ADD,
	PVRDMA_WR_LSO,
	PVRDMA_WR_SEND_WITH_INV,
	PVRDMA_WR_RDMA_READ_WITH_INV,
	PVRDMA_WR_LOCAL_INV,
	PVRDMA_WR_FAST_REG_MR,
	PVRDMA_WR_MASKED_ATOMIC_CMP_AND_SWP,
	PVRDMA_WR_MASKED_ATOMIC_FETCH_AND_ADD,
	PVRDMA_WR_BIND_MW,
	PVRDMA_WR_REG_SIG_MR,
	PVRDMA_WR_ERROR,
};

enum pvrdma_wc_status {
	PVRDMA_WC_SUCCESS,
	PVRDMA_WC_LOC_LEN_ERR,
	PVRDMA_WC_LOC_QP_OP_ERR,
	PVRDMA_WC_LOC_EEC_OP_ERR,
	PVRDMA_WC_LOC_PROT_ERR,
	PVRDMA_WC_WR_FLUSH_ERR,
	PVRDMA_WC_MW_BIND_ERR,
	PVRDMA_WC_BAD_RESP_ERR,
	PVRDMA_WC_LOC_ACCESS_ERR,
	PVRDMA_WC_REM_INV_REQ_ERR,
	PVRDMA_WC_REM_ACCESS_ERR,
	PVRDMA_WC_REM_OP_ERR,
	PVRDMA_WC_RETRY_EXC_ERR,
	PVRDMA_WC_RNR_RETRY_EXC_ERR,
	PVRDMA_WC_LOC_RDD_VIOL_ERR,
	PVRDMA_WC_REM_INV_RD_REQ_ERR,
	PVRDMA_WC_REM_ABORT_ERR,
	PVRDMA_WC_INV_EECN_ERR,
	PVRDMA_WC_INV_EEC_STATE_ERR,
	PVRDMA_WC_FATAL_ERR,
	PVRDMA_WC_RESP_TIMEOUT_ERR,
	PVRDMA_WC_GENERAL_ERR,
};

enum pvrdma_wc_opcode {
	PVRDMA_WC_SEND,
	PVRDMA_WC_RDMA_WRITE,
	PVRDMA_WC_RDMA_READ,
	PVRDMA_WC_COMP_SWAP,
	PVRDMA_WC_FETCH_ADD,
	PVRDMA_WC_BIND_MW,
	PVRDMA_WC_LSO,
	PVRDMA_WC_LOCAL_INV,
	PVRDMA_WC_FAST_REG_MR,
	PVRDMA_WC_MASKED_COMP_SWAP,
	PVRDMA_WC_MASKED_FETCH_ADD,
	PVRDMA_WC_RECV = 1 << 7,
	PVRDMA_WC_RECV_RDMA_WITH_IMM,
};

enum pvrdma_wc_flags {
	PVRDMA_WC_GRH			= 1 << 0,
	PVRDMA_WC_WITH_IMM		= 1 << 1,
	PVRDMA_WC_WITH_INVALIDATE	= 1 << 2,
	PVRDMA_WC_IP_CSUM_OK		= 1 << 3,
	PVRDMA_WC_WITH_SMAC		= 1 << 4,
	PVRDMA_WC_WITH_VLAN		= 1 << 5,
	PVRDMA_WC_WITH_NETWORK_HDR_TYPE	= 1 << 6,
	PVRDMA_WC_FLAGS_MAX		= PVRDMA_WC_WITH_NETWORK_HDR_TYPE,
};

struct pvrdma_alloc_ucontext_resp {
	uint32_t qp_tab_size;
	uint32_t reserved;
};

struct pvrdma_alloc_pd_resp {
	uint32_t pdn;
	uint32_t reserved;
};

struct pvrdma_create_cq {
	uint64_t __attribute__((aligned(8))) buf_addr;
	uint32_t buf_size;
	uint32_t reserved;
};

struct pvrdma_create_cq_resp {
	uint32_t cqn;
	uint32_t reserved;
};

struct pvrdma_resize_cq {
	uint64_t __attribute__((aligned(8))) buf_addr;
	uint32_t buf_size;
	uint32_t reserved;
};

struct pvrdma_create_srq {
	uint64_t __attribute__((aligned(8))) buf_addr;
	uint32_t buf_size;
	uint32_t reserved;
};

struct pvrdma_create_srq_resp {
	uint32_t srqn;
	uint32_t reserved;
};

struct pvrdma_create_qp {
	uint64_t __attribute__((aligned(8))) rbuf_addr;
	uint64_t __attribute__((aligned(8))) sbuf_addr;
	uint32_t rbuf_size;
	uint32_t sbuf_size;
	uint64_t __attribute__((aligned(8))) qp_addr;
};

/* PVRDMA masked atomic compare and swap */
struct pvrdma_ex_cmp_swap {
	uint64_t __attribute__((aligned(8))) swap_val;
	uint64_t __attribute__((aligned(8))) compare_val;
	uint64_t __attribute__((aligned(8))) swap_mask;
	uint64_t __attribute__((aligned(8))) compare_mask;
};

/* PVRDMA masked atomic fetch and add */
struct pvrdma_ex_fetch_add {
	uint64_t __attribute__((aligned(8))) add_val;
	uint64_t __attribute__((aligned(8))) field_boundary;
};

/* PVRDMA address vector. */
struct pvrdma_av {
	uint32_t port_pd;
	uint32_t sl_tclass_flowlabel;
	uint8_t dgid[16];
	uint8_t src_path_bits;
	uint8_t gid_index;
	uint8_t stat_rate;
	uint8_t hop_limit;
	uint8_t dmac[6];
	uint8_t reserved[6];
};

/* PVRDMA scatter/gather entry */
struct pvrdma_sge {
	uint64_t __attribute__((aligned(8))) addr;
	uint32_t   length;
	uint32_t   lkey;
};

/* PVRDMA receive queue work request */
struct pvrdma_rq_wqe_hdr {
	uint64_t __attribute__((aligned(8))) wr_id;		/* wr id */
	uint32_t num_sge;		/* size of s/g array */
	uint32_t total_len;	/* reserved */
};
/* Use pvrdma_sge (ib_sge) for receive queue s/g array elements. */

/* PVRDMA send queue work request */
struct pvrdma_sq_wqe_hdr {
	uint64_t __attribute__((aligned(8))) wr_id;		/* wr id */
	uint32_t num_sge;		/* size of s/g array */
	uint32_t total_len;	/* reserved */
	uint32_t opcode;		/* operation type */
	uint32_t send_flags;	/* wr flags */
	union {
		uint32_t imm_data;
		uint32_t invalidate_rkey;
	} ex;
	uint32_t reserved;
	union {
		struct {
			uint64_t __attribute__((aligned(8))) remote_addr;
			uint32_t rkey;
			uint8_t reserved[4];
		} rdma;
		struct {
			uint64_t __attribute__((aligned(8))) remote_addr;
			uint64_t __attribute__((aligned(8))) compare_add;
			uint64_t __attribute__((aligned(8))) swap;
			uint32_t rkey;
			uint32_t reserved;
		} atomic;
		struct {
			uint64_t __attribute__((aligned(8))) remote_addr;
			uint32_t log_arg_sz;
			uint32_t rkey;
			union {
				struct pvrdma_ex_cmp_swap  cmp_swap;
				struct pvrdma_ex_fetch_add fetch_add;
			} wr_data;
		} masked_atomics;
		struct {
			uint64_t __attribute__((aligned(8))) iova_start;
			uint64_t __attribute__((aligned(8))) pl_pdir_dma;
			uint32_t page_shift;
			uint32_t page_list_len;
			uint32_t length;
			uint32_t access_flags;
			uint32_t rkey;
			uint32_t reserved;
		} fast_reg;
		struct {
			uint32_t remote_qpn;
			uint32_t remote_qkey;
			struct pvrdma_av av;
		} ud;
	} wr;
};
/* Use pvrdma_sge (ib_sge) for send queue s/g array elements. */

/* Completion queue element. */
struct pvrdma_cqe {
	uint64_t __attribute__((aligned(8))) wr_id;
	uint64_t __attribute__((aligned(8))) qp;
	uint32_t opcode;
	uint32_t status;
	uint32_t byte_len;
	uint32_t imm_data;
	uint32_t src_qp;
	uint32_t wc_flags;
	uint32_t vendor_err;
	uint16_t pkey_index;
	uint16_t slid;
	uint8_t sl;
	uint8_t dlid_path_bits;
	uint8_t port_num;
	uint8_t smac[6];
	uint8_t network_hdr_type;
	uint8_t reserved2[6]; /* Pad to next power of 2 (64). */
};

#endif /* __VMW_PVRDMA_ABI_H__ */
