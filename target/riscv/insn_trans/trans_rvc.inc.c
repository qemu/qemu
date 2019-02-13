/*
 * RISC-V translation routines for the RVC Compressed Instruction Set.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2018 Peer Adelt, peer.adelt@hni.uni-paderborn.de
 *                    Bastian Koppelmann, kbastian@mail.uni-paderborn.de
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

static bool trans_c_addi4spn(DisasContext *ctx, arg_c_addi4spn *a)
{
    if (a->nzuimm == 0) {
        /* Reserved in ISA */
        return false;
    }
    arg_addi arg = { .rd = a->rd, .rs1 = 2, .imm = a->nzuimm };
    return trans_addi(ctx, &arg);
}

static bool trans_c_fld(DisasContext *ctx, arg_c_fld *a)
{
    arg_fld arg = { .rd = a->rd, .rs1 = a->rs1, .imm = a->uimm };
    return trans_fld(ctx, &arg);
}

static bool trans_c_lw(DisasContext *ctx, arg_c_lw *a)
{
    arg_lw arg = { .rd = a->rd, .rs1 = a->rs1, .imm = a->uimm };
    return trans_lw(ctx, &arg);
}

static bool trans_c_flw_ld(DisasContext *ctx, arg_c_flw_ld *a)
{
#ifdef TARGET_RISCV32
    /* C.FLW ( RV32FC-only ) */
    return false;
#else
    /* C.LD ( RV64C/RV128C-only ) */
    return false;
#endif
}

static bool trans_c_fsd(DisasContext *ctx, arg_c_fsd *a)
{
    arg_fsd arg = { .rs1 = a->rs1, .rs2 = a->rs2, .imm = a->uimm };
    return trans_fsd(ctx, &arg);
}

static bool trans_c_sw(DisasContext *ctx, arg_c_sw *a)
{
    arg_sw arg = { .rs1 = a->rs1, .rs2 = a->rs2, .imm = a->uimm };
    return trans_sw(ctx, &arg);
}

static bool trans_c_fsw_sd(DisasContext *ctx, arg_c_fsw_sd *a)
{
#ifdef TARGET_RISCV32
    /* C.FSW ( RV32FC-only ) */
    return false;
#else
    /* C.SD ( RV64C/RV128C-only ) */
    return false;
#endif
}
