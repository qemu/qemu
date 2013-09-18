/*
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef SCSI_SRP_H
#define SCSI_SRP_H

/*
 * Structures and constants for the SCSI RDMA Protocol (SRP) as
 * defined by the INCITS T10 committee.  This file was written using
 * draft Revision 16a of the SRP standard.
 */

enum {

    SRP_LOGIN_REQ = 0x00,
    SRP_TSK_MGMT  = 0x01,
    SRP_CMD       = 0x02,
    SRP_I_LOGOUT  = 0x03,
    SRP_LOGIN_RSP = 0xc0,
    SRP_RSP       = 0xc1,
    SRP_LOGIN_REJ = 0xc2,
    SRP_T_LOGOUT  = 0x80,
    SRP_CRED_REQ  = 0x81,
    SRP_AER_REQ   = 0x82,
    SRP_CRED_RSP  = 0x41,
    SRP_AER_RSP   = 0x42
};

enum {
    SRP_BUF_FORMAT_DIRECT   = 1 << 1,
    SRP_BUF_FORMAT_INDIRECT = 1 << 2
};

enum {
    SRP_NO_DATA_DESC       = 0,
    SRP_DATA_DESC_DIRECT   = 1,
    SRP_DATA_DESC_INDIRECT = 2
};

enum {
    SRP_TSK_ABORT_TASK     = 0x01,
    SRP_TSK_ABORT_TASK_SET = 0x02,
    SRP_TSK_CLEAR_TASK_SET = 0x04,
    SRP_TSK_LUN_RESET      = 0x08,
    SRP_TSK_CLEAR_ACA      = 0x40
};

enum srp_login_rej_reason {
    SRP_LOGIN_REJ_UNABLE_ESTABLISH_CHANNEL   = 0x00010000,
    SRP_LOGIN_REJ_INSUFFICIENT_RESOURCES     = 0x00010001,
    SRP_LOGIN_REJ_REQ_IT_IU_LENGTH_TOO_LARGE = 0x00010002,
    SRP_LOGIN_REJ_UNABLE_ASSOCIATE_CHANNEL   = 0x00010003,
    SRP_LOGIN_REJ_UNSUPPORTED_DESCRIPTOR_FMT = 0x00010004,
    SRP_LOGIN_REJ_MULTI_CHANNEL_UNSUPPORTED  = 0x00010005,
    SRP_LOGIN_REJ_CHANNEL_LIMIT_REACHED      = 0x00010006
};

enum {
    SRP_REV10_IB_IO_CLASS  = 0xff00,
    SRP_REV16A_IB_IO_CLASS = 0x0100
};

enum {
    SRP_TSK_MGMT_COMPLETE       = 0x00,
    SRP_TSK_MGMT_FIELDS_INVALID = 0x02,
    SRP_TSK_MGMT_NOT_SUPPORTED  = 0x04,
    SRP_TSK_MGMT_FAILED         = 0x05
};

struct srp_direct_buf {
    uint64_t    va;
    uint32_t    key;
    uint32_t    len;
};

/*
 * We need the packed attribute because the SRP spec puts the list of
 * descriptors at an offset of 20, which is not aligned to the size of
 * struct srp_direct_buf.  The whole structure must be packed to avoid
 * having the 20-byte structure padded to 24 bytes on 64-bit architectures.
 */
struct srp_indirect_buf {
    struct srp_direct_buf    table_desc;
    uint32_t                 len;
    struct srp_direct_buf    desc_list[0];
} QEMU_PACKED;

enum {
    SRP_MULTICHAN_SINGLE = 0,
    SRP_MULTICHAN_MULTI  = 1
};

struct srp_login_req {
    uint8_t    opcode;
    uint8_t    reserved1[7];
    uint64_t   tag;
    uint32_t   req_it_iu_len;
    uint8_t    reserved2[4];
    uint16_t   req_buf_fmt;
    uint8_t    req_flags;
    uint8_t    reserved3[5];
    uint8_t    initiator_port_id[16];
    uint8_t    target_port_id[16];
};

/*
 * The SRP spec defines the size of the LOGIN_RSP structure to be 52
 * bytes, so it needs to be packed to avoid having it padded to 56
 * bytes on 64-bit architectures.
 */
struct srp_login_rsp {
    uint8_t    opcode;
    uint8_t    reserved1[3];
    uint32_t   req_lim_delta;
    uint64_t   tag;
    uint32_t   max_it_iu_len;
    uint32_t   max_ti_iu_len;
    uint16_t   buf_fmt;
    uint8_t    rsp_flags;
    uint8_t    reserved2[25];
} QEMU_PACKED;

struct srp_login_rej {
    uint8_t    opcode;
    uint8_t    reserved1[3];
    uint32_t   reason;
    uint64_t   tag;
    uint8_t    reserved2[8];
    uint16_t   buf_fmt;
    uint8_t    reserved3[6];
};

struct srp_i_logout {
    uint8_t    opcode;
    uint8_t    reserved[7];
    uint64_t   tag;
};

struct srp_t_logout {
    uint8_t    opcode;
    uint8_t    sol_not;
    uint8_t    reserved[2];
    uint32_t   reason;
    uint64_t   tag;
};

/*
 * We need the packed attribute because the SRP spec only aligns the
 * 8-byte LUN field to 4 bytes.
 */
struct srp_tsk_mgmt {
    uint8_t    opcode;
    uint8_t    sol_not;
    uint8_t    reserved1[6];
    uint64_t   tag;
    uint8_t    reserved2[4];
    uint64_t   lun;
    uint8_t    reserved3[2];
    uint8_t    tsk_mgmt_func;
    uint8_t    reserved4;
    uint64_t   task_tag;
    uint8_t    reserved5[8];
} QEMU_PACKED;

/*
 * We need the packed attribute because the SRP spec only aligns the
 * 8-byte LUN field to 4 bytes.
 */
struct srp_cmd {
    uint8_t    opcode;
    uint8_t    sol_not;
    uint8_t    reserved1[3];
    uint8_t    buf_fmt;
    uint8_t    data_out_desc_cnt;
    uint8_t    data_in_desc_cnt;
    uint64_t   tag;
    uint8_t    reserved2[4];
    uint64_t   lun;
    uint8_t    reserved3;
    uint8_t    task_attr;
    uint8_t    reserved4;
    uint8_t    add_cdb_len;
    uint8_t    cdb[16];
    uint8_t    add_data[0];
} QEMU_PACKED;

enum {
    SRP_RSP_FLAG_RSPVALID = 1 << 0,
    SRP_RSP_FLAG_SNSVALID = 1 << 1,
    SRP_RSP_FLAG_DOOVER   = 1 << 2,
    SRP_RSP_FLAG_DOUNDER  = 1 << 3,
    SRP_RSP_FLAG_DIOVER   = 1 << 4,
    SRP_RSP_FLAG_DIUNDER  = 1 << 5
};

/*
 * The SRP spec defines the size of the RSP structure to be 36 bytes,
 * so it needs to be packed to avoid having it padded to 40 bytes on
 * 64-bit architectures.
 */
struct srp_rsp {
    uint8_t    opcode;
    uint8_t    sol_not;
    uint8_t    reserved1[2];
    uint32_t   req_lim_delta;
    uint64_t   tag;
    uint8_t    reserved2[2];
    uint8_t    flags;
    uint8_t    status;
    uint32_t   data_out_res_cnt;
    uint32_t   data_in_res_cnt;
    uint32_t   sense_data_len;
    uint32_t   resp_data_len;
    uint8_t    data[0];
} QEMU_PACKED;

#endif /* SCSI_SRP_H */
