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

#ifndef __PVRDMA_VERBS_H__
#define __PVRDMA_VERBS_H__

#include "standard-headers/linux/types.h"

union pvrdma_gid {
	uint8_t	raw[16];
	struct {
		uint64_t	subnet_prefix;
		uint64_t	interface_id;
	} global;
};

enum pvrdma_link_layer {
	PVRDMA_LINK_LAYER_UNSPECIFIED,
	PVRDMA_LINK_LAYER_INFINIBAND,
	PVRDMA_LINK_LAYER_ETHERNET,
};

enum pvrdma_mtu {
	PVRDMA_MTU_256  = 1,
	PVRDMA_MTU_512  = 2,
	PVRDMA_MTU_1024 = 3,
	PVRDMA_MTU_2048 = 4,
	PVRDMA_MTU_4096 = 5,
};

enum pvrdma_port_state {
	PVRDMA_PORT_NOP			= 0,
	PVRDMA_PORT_DOWN		= 1,
	PVRDMA_PORT_INIT		= 2,
	PVRDMA_PORT_ARMED		= 3,
	PVRDMA_PORT_ACTIVE		= 4,
	PVRDMA_PORT_ACTIVE_DEFER	= 5,
};

enum pvrdma_port_cap_flags {
	PVRDMA_PORT_SM				= 1 <<  1,
	PVRDMA_PORT_NOTICE_SUP			= 1 <<  2,
	PVRDMA_PORT_TRAP_SUP			= 1 <<  3,
	PVRDMA_PORT_OPT_IPD_SUP			= 1 <<  4,
	PVRDMA_PORT_AUTO_MIGR_SUP		= 1 <<  5,
	PVRDMA_PORT_SL_MAP_SUP			= 1 <<  6,
	PVRDMA_PORT_MKEY_NVRAM			= 1 <<  7,
	PVRDMA_PORT_PKEY_NVRAM			= 1 <<  8,
	PVRDMA_PORT_LED_INFO_SUP		= 1 <<  9,
	PVRDMA_PORT_SM_DISABLED			= 1 << 10,
	PVRDMA_PORT_SYS_IMAGE_GUID_SUP		= 1 << 11,
	PVRDMA_PORT_PKEY_SW_EXT_PORT_TRAP_SUP	= 1 << 12,
	PVRDMA_PORT_EXTENDED_SPEEDS_SUP		= 1 << 14,
	PVRDMA_PORT_CM_SUP			= 1 << 16,
	PVRDMA_PORT_SNMP_TUNNEL_SUP		= 1 << 17,
	PVRDMA_PORT_REINIT_SUP			= 1 << 18,
	PVRDMA_PORT_DEVICE_MGMT_SUP		= 1 << 19,
	PVRDMA_PORT_VENDOR_CLASS_SUP		= 1 << 20,
	PVRDMA_PORT_DR_NOTICE_SUP		= 1 << 21,
	PVRDMA_PORT_CAP_MASK_NOTICE_SUP		= 1 << 22,
	PVRDMA_PORT_BOOT_MGMT_SUP		= 1 << 23,
	PVRDMA_PORT_LINK_LATENCY_SUP		= 1 << 24,
	PVRDMA_PORT_CLIENT_REG_SUP		= 1 << 25,
	PVRDMA_PORT_IP_BASED_GIDS		= 1 << 26,
	PVRDMA_PORT_CAP_FLAGS_MAX		= PVRDMA_PORT_IP_BASED_GIDS,
};

enum pvrdma_port_width {
	PVRDMA_WIDTH_1X		= 1,
	PVRDMA_WIDTH_4X		= 2,
	PVRDMA_WIDTH_8X		= 4,
	PVRDMA_WIDTH_12X	= 8,
};

enum pvrdma_port_speed {
	PVRDMA_SPEED_SDR	= 1,
	PVRDMA_SPEED_DDR	= 2,
	PVRDMA_SPEED_QDR	= 4,
	PVRDMA_SPEED_FDR10	= 8,
	PVRDMA_SPEED_FDR	= 16,
	PVRDMA_SPEED_EDR	= 32,
};

struct pvrdma_port_attr {
	enum pvrdma_port_state	state;
	enum pvrdma_mtu		max_mtu;
	enum pvrdma_mtu		active_mtu;
	uint32_t			gid_tbl_len;
	uint32_t			port_cap_flags;
	uint32_t			max_msg_sz;
	uint32_t			bad_pkey_cntr;
	uint32_t			qkey_viol_cntr;
	uint16_t			pkey_tbl_len;
	uint16_t			lid;
	uint16_t			sm_lid;
	uint8_t			lmc;
	uint8_t			max_vl_num;
	uint8_t			sm_sl;
	uint8_t			subnet_timeout;
	uint8_t			init_type_reply;
	uint8_t			active_width;
	uint8_t			active_speed;
	uint8_t			phys_state;
	uint8_t			reserved[2];
};

struct pvrdma_global_route {
	union pvrdma_gid	dgid;
	uint32_t			flow_label;
	uint8_t			sgid_index;
	uint8_t			hop_limit;
	uint8_t			traffic_class;
	uint8_t			reserved;
};

struct pvrdma_grh {
	uint32_t			version_tclass_flow;
	uint16_t			paylen;
	uint8_t			next_hdr;
	uint8_t			hop_limit;
	union pvrdma_gid	sgid;
	union pvrdma_gid	dgid;
};

enum pvrdma_ah_flags {
	PVRDMA_AH_GRH = 1,
};

enum pvrdma_rate {
	PVRDMA_RATE_PORT_CURRENT	= 0,
	PVRDMA_RATE_2_5_GBPS		= 2,
	PVRDMA_RATE_5_GBPS		= 5,
	PVRDMA_RATE_10_GBPS		= 3,
	PVRDMA_RATE_20_GBPS		= 6,
	PVRDMA_RATE_30_GBPS		= 4,
	PVRDMA_RATE_40_GBPS		= 7,
	PVRDMA_RATE_60_GBPS		= 8,
	PVRDMA_RATE_80_GBPS		= 9,
	PVRDMA_RATE_120_GBPS		= 10,
	PVRDMA_RATE_14_GBPS		= 11,
	PVRDMA_RATE_56_GBPS		= 12,
	PVRDMA_RATE_112_GBPS		= 13,
	PVRDMA_RATE_168_GBPS		= 14,
	PVRDMA_RATE_25_GBPS		= 15,
	PVRDMA_RATE_100_GBPS		= 16,
	PVRDMA_RATE_200_GBPS		= 17,
	PVRDMA_RATE_300_GBPS		= 18,
};

struct pvrdma_ah_attr {
	struct pvrdma_global_route	grh;
	uint16_t				dlid;
	uint16_t				vlan_id;
	uint8_t				sl;
	uint8_t				src_path_bits;
	uint8_t				static_rate;
	uint8_t				ah_flags;
	uint8_t				port_num;
	uint8_t				dmac[6];
	uint8_t				reserved;
};

enum pvrdma_cq_notify_flags {
	PVRDMA_CQ_SOLICITED		= 1 << 0,
	PVRDMA_CQ_NEXT_COMP		= 1 << 1,
	PVRDMA_CQ_SOLICITED_MASK	= PVRDMA_CQ_SOLICITED |
					  PVRDMA_CQ_NEXT_COMP,
	PVRDMA_CQ_REPORT_MISSED_EVENTS	= 1 << 2,
};

struct pvrdma_qp_cap {
	uint32_t	max_send_wr;
	uint32_t	max_recv_wr;
	uint32_t	max_send_sge;
	uint32_t	max_recv_sge;
	uint32_t	max_inline_data;
	uint32_t	reserved;
};

enum pvrdma_sig_type {
	PVRDMA_SIGNAL_ALL_WR,
	PVRDMA_SIGNAL_REQ_WR,
};

enum pvrdma_qp_type {
	PVRDMA_QPT_SMI,
	PVRDMA_QPT_GSI,
	PVRDMA_QPT_RC,
	PVRDMA_QPT_UC,
	PVRDMA_QPT_UD,
	PVRDMA_QPT_RAW_IPV6,
	PVRDMA_QPT_RAW_ETHERTYPE,
	PVRDMA_QPT_RAW_PACKET = 8,
	PVRDMA_QPT_XRC_INI = 9,
	PVRDMA_QPT_XRC_TGT,
	PVRDMA_QPT_MAX,
};

enum pvrdma_qp_create_flags {
	PVRDMA_QP_CREATE_IPOPVRDMA_UD_LSO		= 1 << 0,
	PVRDMA_QP_CREATE_BLOCK_MULTICAST_LOOPBACK	= 1 << 1,
};

enum pvrdma_qp_attr_mask {
	PVRDMA_QP_STATE			= 1 << 0,
	PVRDMA_QP_CUR_STATE		= 1 << 1,
	PVRDMA_QP_EN_SQD_ASYNC_NOTIFY	= 1 << 2,
	PVRDMA_QP_ACCESS_FLAGS		= 1 << 3,
	PVRDMA_QP_PKEY_INDEX		= 1 << 4,
	PVRDMA_QP_PORT			= 1 << 5,
	PVRDMA_QP_QKEY			= 1 << 6,
	PVRDMA_QP_AV			= 1 << 7,
	PVRDMA_QP_PATH_MTU		= 1 << 8,
	PVRDMA_QP_TIMEOUT		= 1 << 9,
	PVRDMA_QP_RETRY_CNT		= 1 << 10,
	PVRDMA_QP_RNR_RETRY		= 1 << 11,
	PVRDMA_QP_RQ_PSN		= 1 << 12,
	PVRDMA_QP_MAX_QP_RD_ATOMIC	= 1 << 13,
	PVRDMA_QP_ALT_PATH		= 1 << 14,
	PVRDMA_QP_MIN_RNR_TIMER		= 1 << 15,
	PVRDMA_QP_SQ_PSN		= 1 << 16,
	PVRDMA_QP_MAX_DEST_RD_ATOMIC	= 1 << 17,
	PVRDMA_QP_PATH_MIG_STATE	= 1 << 18,
	PVRDMA_QP_CAP			= 1 << 19,
	PVRDMA_QP_DEST_QPN		= 1 << 20,
	PVRDMA_QP_ATTR_MASK_MAX		= PVRDMA_QP_DEST_QPN,
};

enum pvrdma_qp_state {
	PVRDMA_QPS_RESET,
	PVRDMA_QPS_INIT,
	PVRDMA_QPS_RTR,
	PVRDMA_QPS_RTS,
	PVRDMA_QPS_SQD,
	PVRDMA_QPS_SQE,
	PVRDMA_QPS_ERR,
};

enum pvrdma_mig_state {
	PVRDMA_MIG_MIGRATED,
	PVRDMA_MIG_REARM,
	PVRDMA_MIG_ARMED,
};

enum pvrdma_mw_type {
	PVRDMA_MW_TYPE_1 = 1,
	PVRDMA_MW_TYPE_2 = 2,
};

struct pvrdma_srq_attr {
	uint32_t			max_wr;
	uint32_t			max_sge;
	uint32_t			srq_limit;
	uint32_t			reserved;
};

struct pvrdma_qp_attr {
	enum pvrdma_qp_state	qp_state;
	enum pvrdma_qp_state	cur_qp_state;
	enum pvrdma_mtu		path_mtu;
	enum pvrdma_mig_state	path_mig_state;
	uint32_t			qkey;
	uint32_t			rq_psn;
	uint32_t			sq_psn;
	uint32_t			dest_qp_num;
	uint32_t			qp_access_flags;
	uint16_t			pkey_index;
	uint16_t			alt_pkey_index;
	uint8_t			en_sqd_async_notify;
	uint8_t			sq_draining;
	uint8_t			max_rd_atomic;
	uint8_t			max_dest_rd_atomic;
	uint8_t			min_rnr_timer;
	uint8_t			port_num;
	uint8_t			timeout;
	uint8_t			retry_cnt;
	uint8_t			rnr_retry;
	uint8_t			alt_port_num;
	uint8_t			alt_timeout;
	uint8_t			reserved[5];
	struct pvrdma_qp_cap	cap;
	struct pvrdma_ah_attr	ah_attr;
	struct pvrdma_ah_attr	alt_ah_attr;
};

enum pvrdma_send_flags {
	PVRDMA_SEND_FENCE	= 1 << 0,
	PVRDMA_SEND_SIGNALED	= 1 << 1,
	PVRDMA_SEND_SOLICITED	= 1 << 2,
	PVRDMA_SEND_INLINE	= 1 << 3,
	PVRDMA_SEND_IP_CSUM	= 1 << 4,
	PVRDMA_SEND_FLAGS_MAX	= PVRDMA_SEND_IP_CSUM,
};

enum pvrdma_access_flags {
	PVRDMA_ACCESS_LOCAL_WRITE	= 1 << 0,
	PVRDMA_ACCESS_REMOTE_WRITE	= 1 << 1,
	PVRDMA_ACCESS_REMOTE_READ	= 1 << 2,
	PVRDMA_ACCESS_REMOTE_ATOMIC	= 1 << 3,
	PVRDMA_ACCESS_MW_BIND		= 1 << 4,
	PVRDMA_ZERO_BASED		= 1 << 5,
	PVRDMA_ACCESS_ON_DEMAND		= 1 << 6,
	PVRDMA_ACCESS_FLAGS_MAX		= PVRDMA_ACCESS_ON_DEMAND,
};

#endif /* __PVRDMA_VERBS_H__ */
