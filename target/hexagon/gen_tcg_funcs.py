#!/usr/bin/env python3

##
##  Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
##
##  This program is free software; you can redistribute it and/or modify
##  it under the terms of the GNU General Public License as published by
##  the Free Software Foundation; either version 2 of the License, or
##  (at your option) any later version.
##
##  This program is distributed in the hope that it will be useful,
##  but WITHOUT ANY WARRANTY; without even the implied warranty of
##  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
##  GNU General Public License for more details.
##
##  You should have received a copy of the GNU General Public License
##  along with this program; if not, see <http://www.gnu.org/licenses/>.
##

import sys
import re
import string
import hex_common


##
## Helpers for gen_tcg_func
##
def gen_decl_ea_tcg(f, tag):
    f.write("    TCGv EA G_GNUC_UNUSED = tcg_temp_new();\n")


def genptr_decl_pair_writable(f, tag, regtype, regid, regno):
    regN = f"{regtype}{regid}N"
    if regtype == "R":
        f.write(f"    const int {regN} = insn->regno[{regno}];\n")
    elif regtype == "C":
        f.write(f"    const int {regN} = insn->regno[{regno}] + HEX_REG_SA0;\n")
    else:
        hex_common.bad_register(regtype, regid)
    f.write(f"    TCGv_i64 {regtype}{regid}V = " f"get_result_gpr_pair(ctx, {regN});\n")


def genptr_decl_writable(f, tag, regtype, regid, regno):
    regN = f"{regtype}{regid}N"
    if regtype == "R":
        f.write(f"    const int {regN} = insn->regno[{regno}];\n")
        f.write(f"    TCGv {regtype}{regid}V = get_result_gpr(ctx, {regN});\n")
    elif regtype == "C":
        f.write(f"    const int {regN} = insn->regno[{regno}] + HEX_REG_SA0;\n")
        f.write(f"    TCGv {regtype}{regid}V = get_result_gpr(ctx, {regN});\n")
    elif regtype == "P":
        f.write(f"    const int {regN} = insn->regno[{regno}];\n")
        f.write(f"    TCGv {regtype}{regid}V = tcg_temp_new();\n")
    else:
        hex_common.bad_register(regtype, regid)


def genptr_decl(f, tag, regtype, regid, regno):
    regN = f"{regtype}{regid}N"
    if regtype == "R":
        if regid in {"ss", "tt"}:
            f.write(f"    TCGv_i64 {regtype}{regid}V = tcg_temp_new_i64();\n")
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
        elif regid in {"dd", "ee", "xx", "yy"}:
            genptr_decl_pair_writable(f, tag, regtype, regid, regno)
        elif regid in {"s", "t", "u", "v"}:
            f.write(
                f"    TCGv {regtype}{regid}V = " f"hex_gpr[insn->regno[{regno}]];\n"
            )
        elif regid in {"d", "e", "x", "y"}:
            genptr_decl_writable(f, tag, regtype, regid, regno)
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "P":
        if regid in {"s", "t", "u", "v"}:
            f.write(
                f"    TCGv {regtype}{regid}V = " f"hex_pred[insn->regno[{regno}]];\n"
            )
        elif regid in {"d", "e", "x"}:
            genptr_decl_writable(f, tag, regtype, regid, regno)
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "C":
        if regid == "ss":
            f.write(f"    TCGv_i64 {regtype}{regid}V = " f"tcg_temp_new_i64();\n")
            f.write(f"    const int {regN} = insn->regno[{regno}] + " "HEX_REG_SA0;\n")
        elif regid == "dd":
            genptr_decl_pair_writable(f, tag, regtype, regid, regno)
        elif regid == "s":
            f.write(f"    TCGv {regtype}{regid}V = tcg_temp_new();\n")
            f.write(
                f"    const int {regtype}{regid}N = insn->regno[{regno}] + "
                "HEX_REG_SA0;\n"
            )
        elif regid == "d":
            genptr_decl_writable(f, tag, regtype, regid, regno)
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "M":
        if regid == "u":
            f.write(f"    const int {regtype}{regid}N = " f"insn->regno[{regno}];\n")
            f.write(
                f"    TCGv {regtype}{regid}V = hex_gpr[{regtype}{regid}N + "
                "HEX_REG_M0];\n"
            )
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "V":
        if regid in {"dd"}:
            f.write(f"    const int {regtype}{regid}N = " f"insn->regno[{regno}];\n")
            f.write(f"    const intptr_t {regtype}{regid}V_off =\n")
            if hex_common.is_tmp_result(tag):
                f.write(
                    f"        ctx_tmp_vreg_off(ctx, {regtype}{regid}N, 2, " "true);\n"
                )
            else:
                f.write(f"        ctx_future_vreg_off(ctx, {regtype}{regid}N,")
                f.write(" 2, true);\n")
            if not hex_common.skip_qemu_helper(tag):
                f.write(f"    TCGv_ptr {regtype}{regid}V = " "tcg_temp_new_ptr();\n")
                f.write(
                    f"    tcg_gen_addi_ptr({regtype}{regid}V, tcg_env, "
                    f"{regtype}{regid}V_off);\n"
                )
        elif regid in {"uu", "vv", "xx"}:
            f.write(f"    const int {regtype}{regid}N = " f"insn->regno[{regno}];\n")
            f.write(f"    const intptr_t {regtype}{regid}V_off =\n")
            f.write(f"        offsetof(CPUHexagonState, {regtype}{regid}V);\n")
            if not hex_common.skip_qemu_helper(tag):
                f.write(f"    TCGv_ptr {regtype}{regid}V = " "tcg_temp_new_ptr();\n")
                f.write(
                    f"    tcg_gen_addi_ptr({regtype}{regid}V, tcg_env, "
                    f"{regtype}{regid}V_off);\n"
                )
        elif regid in {"s", "u", "v", "w"}:
            f.write(f"    const int {regtype}{regid}N = " f"insn->regno[{regno}];\n")
            f.write(f"    const intptr_t {regtype}{regid}V_off =\n")
            f.write(f"        vreg_src_off(ctx, {regtype}{regid}N);\n")
            if not hex_common.skip_qemu_helper(tag):
                f.write(f"    TCGv_ptr {regtype}{regid}V = " "tcg_temp_new_ptr();\n")
        elif regid in {"d", "x", "y"}:
            f.write(f"    const int {regtype}{regid}N = " f"insn->regno[{regno}];\n")
            f.write(f"    const intptr_t {regtype}{regid}V_off =\n")
            if regid == "y":
                f.write("        offsetof(CPUHexagonState, vtmp);\n")
            elif hex_common.is_tmp_result(tag):
                f.write(
                    f"        ctx_tmp_vreg_off(ctx, {regtype}{regid}N, 1, " "true);\n"
                )
            else:
                f.write(f"        ctx_future_vreg_off(ctx, {regtype}{regid}N,")
                f.write(" 1, true);\n")

            if not hex_common.skip_qemu_helper(tag):
                f.write(f"    TCGv_ptr {regtype}{regid}V = " "tcg_temp_new_ptr();\n")
                f.write(
                    f"    tcg_gen_addi_ptr({regtype}{regid}V, tcg_env, "
                    f"{regtype}{regid}V_off);\n"
                )
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "Q":
        if regid in {"d", "e", "x"}:
            f.write(f"    const int {regtype}{regid}N = " f"insn->regno[{regno}];\n")
            f.write(f"    const intptr_t {regtype}{regid}V_off =\n")
            f.write(f"        get_result_qreg(ctx, {regtype}{regid}N);\n")
            if not hex_common.skip_qemu_helper(tag):
                f.write(f"    TCGv_ptr {regtype}{regid}V = " "tcg_temp_new_ptr();\n")
                f.write(
                    f"    tcg_gen_addi_ptr({regtype}{regid}V, tcg_env, "
                    f"{regtype}{regid}V_off);\n"
                )
        elif regid in {"s", "t", "u", "v"}:
            f.write(f"    const int {regtype}{regid}N = " f"insn->regno[{regno}];\n")
            f.write(f"    const intptr_t {regtype}{regid}V_off =\n")
            f.write(
                f"        offsetof(CPUHexagonState, " f"QRegs[{regtype}{regid}N]);\n"
            )
            if not hex_common.skip_qemu_helper(tag):
                f.write(f"    TCGv_ptr {regtype}{regid}V = " "tcg_temp_new_ptr();\n")
        else:
            hex_common.bad_register(regtype, regid)
    else:
        hex_common.bad_register(regtype, regid)


def genptr_decl_new(f, tag, regtype, regid, regno):
    if regtype == "N":
        if regid in {"s", "t"}:
            f.write(
                f"    TCGv {regtype}{regid}N = "
                f"get_result_gpr(ctx, insn->regno[{regno}]);\n"
            )
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "P":
        if regid in {"t", "u", "v"}:
            f.write(
                f"    TCGv {regtype}{regid}N = "
                f"ctx->new_pred_value[insn->regno[{regno}]];\n"
            )
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "O":
        if regid == "s":
            f.write(
                f"    const intptr_t {regtype}{regid}N_num = "
                f"insn->regno[{regno}];\n"
            )
            if hex_common.skip_qemu_helper(tag):
                f.write(f"    const intptr_t {regtype}{regid}N_off =\n")
                f.write("         ctx_future_vreg_off(ctx, " f"{regtype}{regid}N_num,")
                f.write(" 1, true);\n")
            else:
                f.write(
                    f"    TCGv {regtype}{regid}N = "
                    f"tcg_constant_tl({regtype}{regid}N_num);\n"
                )
        else:
            hex_common.bad_register(regtype, regid)
    else:
        hex_common.bad_register(regtype, regid)


def genptr_decl_opn(f, tag, regtype, regid, i):
    if hex_common.is_pair(regid):
        genptr_decl(f, tag, regtype, regid, i)
    elif hex_common.is_single(regid):
        if hex_common.is_old_val(regtype, regid, tag):
            genptr_decl(f, tag, regtype, regid, i)
        elif hex_common.is_new_val(regtype, regid, tag):
            genptr_decl_new(f, tag, regtype, regid, i)
        else:
            hex_common.bad_register(regtype, regid)
    else:
        hex_common.bad_register(regtype, regid)


def genptr_decl_imm(f, immlett):
    if immlett.isupper():
        i = 1
    else:
        i = 0
    f.write(f"    int {hex_common.imm_name(immlett)} = insn->immed[{i}];\n")


def genptr_src_read(f, tag, regtype, regid):
    if regtype == "R":
        if regid in {"ss", "tt", "xx", "yy"}:
            f.write(
                f"    tcg_gen_concat_i32_i64({regtype}{regid}V, "
                f"hex_gpr[{regtype}{regid}N],\n"
            )
            f.write(
                f"                                 hex_gpr[{regtype}"
                f"{regid}N + 1]);\n"
            )
        elif regid in {"x", "y"}:
            ## For read/write registers, we need to get the original value into
            ## the result TCGv.  For conditional instructions, this is done in
            ## gen_start_packet.  For unconditional instructions, we do it here.
            if "A_CONDEXEC" not in hex_common.attribdict[tag]:
                f.write(
                    f"    tcg_gen_mov_tl({regtype}{regid}V, "
                    f"hex_gpr[{regtype}{regid}N]);\n"
                )
        elif regid not in {"s", "t", "u", "v"}:
            hex_common.bad_register(regtype, regid)
    elif regtype == "P":
        if regid == "x":
            f.write(
                f"    tcg_gen_mov_tl({regtype}{regid}V, "
                f"hex_pred[{regtype}{regid}N]);\n"
            )
        elif regid not in {"s", "t", "u", "v"}:
            hex_common.bad_register(regtype, regid)
    elif regtype == "C":
        if regid == "ss":
            f.write(
                f"    gen_read_ctrl_reg_pair(ctx, {regtype}{regid}N, "
                f"{regtype}{regid}V);\n"
            )
        elif regid == "s":
            f.write(
                f"    gen_read_ctrl_reg(ctx, {regtype}{regid}N, "
                f"{regtype}{regid}V);\n"
            )
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "M":
        if regid != "u":
            hex_common.bad_register(regtype, regid)
    elif regtype == "V":
        if regid in {"uu", "vv", "xx"}:
            f.write(f"    tcg_gen_gvec_mov(MO_64, {regtype}{regid}V_off,\n")
            f.write(f"        vreg_src_off(ctx, {regtype}{regid}N),\n")
            f.write("        sizeof(MMVector), sizeof(MMVector));\n")
            f.write("    tcg_gen_gvec_mov(MO_64,\n")
            f.write(f"        {regtype}{regid}V_off + sizeof(MMVector),\n")
            f.write(f"        vreg_src_off(ctx, {regtype}{regid}N ^ 1),\n")
            f.write("        sizeof(MMVector), sizeof(MMVector));\n")
        elif regid in {"s", "u", "v", "w"}:
            if not hex_common.skip_qemu_helper(tag):
                f.write(
                    f"    tcg_gen_addi_ptr({regtype}{regid}V, tcg_env, "
                    f"{regtype}{regid}V_off);\n"
                )
        elif regid in {"x", "y"}:
            f.write(f"    tcg_gen_gvec_mov(MO_64, {regtype}{regid}V_off,\n")
            f.write(f"        vreg_src_off(ctx, {regtype}{regid}N),\n")
            f.write("        sizeof(MMVector), sizeof(MMVector));\n")
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "Q":
        if regid in {"s", "t", "u", "v"}:
            if not hex_common.skip_qemu_helper(tag):
                f.write(
                    f"    tcg_gen_addi_ptr({regtype}{regid}V, tcg_env, "
                    f"{regtype}{regid}V_off);\n"
                )
        elif regid in {"x"}:
            f.write(f"    tcg_gen_gvec_mov(MO_64, {regtype}{regid}V_off,\n")
            f.write(
                f"        offsetof(CPUHexagonState, " f"QRegs[{regtype}{regid}N]),\n"
            )
            f.write("        sizeof(MMQReg), sizeof(MMQReg));\n")
        else:
            hex_common.bad_register(regtype, regid)
    else:
        hex_common.bad_register(regtype, regid)


def genptr_src_read_new(f, regtype, regid):
    if regtype == "N":
        if regid not in {"s", "t"}:
            hex_common.bad_register(regtype, regid)
    elif regtype == "P":
        if regid not in {"t", "u", "v"}:
            hex_common.bad_register(regtype, regid)
    elif regtype == "O":
        if regid != "s":
            hex_common.bad_register(regtype, regid)
    else:
        hex_common.bad_register(regtype, regid)


def genptr_src_read_opn(f, regtype, regid, tag):
    if hex_common.is_pair(regid):
        genptr_src_read(f, tag, regtype, regid)
    elif hex_common.is_single(regid):
        if hex_common.is_old_val(regtype, regid, tag):
            genptr_src_read(f, tag, regtype, regid)
        elif hex_common.is_new_val(regtype, regid, tag):
            genptr_src_read_new(f, regtype, regid)
        else:
            hex_common.bad_register(regtype, regid)
    else:
        hex_common.bad_register(regtype, regid)


def gen_helper_call_opn(f, tag, regtype, regid, i):
    if i > 0:
        f.write(", ")
    if hex_common.is_pair(regid):
        f.write(f"{regtype}{regid}V")
    elif hex_common.is_single(regid):
        if hex_common.is_old_val(regtype, regid, tag):
            f.write(f"{regtype}{regid}V")
        elif hex_common.is_new_val(regtype, regid, tag):
            f.write(f"{regtype}{regid}N")
        else:
            hex_common.bad_register(regtype, regid)
    else:
        hex_common.bad_register(regtype, regid)


def gen_helper_decl_imm(f, immlett):
    f.write(
        f"    TCGv tcgv_{hex_common.imm_name(immlett)} = "
        f"tcg_constant_tl({hex_common.imm_name(immlett)});\n"
    )


def gen_helper_call_imm(f, immlett):
    f.write(f", tcgv_{hex_common.imm_name(immlett)}")


def genptr_dst_write_pair(f, tag, regtype, regid):
    f.write(f"    gen_log_reg_write_pair(ctx, {regtype}{regid}N, "
            f"{regtype}{regid}V);\n")


def genptr_dst_write(f, tag, regtype, regid):
    if regtype == "R":
        if regid in {"dd", "xx", "yy"}:
            genptr_dst_write_pair(f, tag, regtype, regid)
        elif regid in {"d", "e", "x", "y"}:
            f.write(
                f"    gen_log_reg_write(ctx, {regtype}{regid}N, "
                f"{regtype}{regid}V);\n"
            )
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "P":
        if regid in {"d", "e", "x"}:
            f.write(
                f"    gen_log_pred_write(ctx, {regtype}{regid}N, "
                f"{regtype}{regid}V);\n"
            )
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "C":
        if regid == "dd":
            f.write(
                f"    gen_write_ctrl_reg_pair(ctx, {regtype}{regid}N, "
                f"{regtype}{regid}V);\n"
            )
        elif regid == "d":
            f.write(
                f"    gen_write_ctrl_reg(ctx, {regtype}{regid}N, "
                f"{regtype}{regid}V);\n"
            )
        else:
            hex_common.bad_register(regtype, regid)
    else:
        hex_common.bad_register(regtype, regid)


def genptr_dst_write_ext(f, tag, regtype, regid, newv="EXT_DFL"):
    if regtype == "V":
        if regid in {"xx"}:
            f.write(
                f"    gen_log_vreg_write_pair(ctx, {regtype}{regid}V_off, "
                f"{regtype}{regid}N, {newv});\n"
            )
        elif regid in {"y"}:
            f.write(
                f"    gen_log_vreg_write(ctx, {regtype}{regid}V_off, "
                f"{regtype}{regid}N, {newv});\n"
            )
        elif regid not in {"dd", "d", "x"}:
            hex_common.bad_register(regtype, regid)
    elif regtype == "Q":
        if regid not in {"d", "e", "x"}:
            hex_common.bad_register(regtype, regid)
    else:
        hex_common.bad_register(regtype, regid)


def genptr_dst_write_opn(f, regtype, regid, tag):
    if hex_common.is_pair(regid):
        if hex_common.is_hvx_reg(regtype):
            if hex_common.is_tmp_result(tag):
                genptr_dst_write_ext(f, tag, regtype, regid, "EXT_TMP")
            else:
                genptr_dst_write_ext(f, tag, regtype, regid)
        else:
            genptr_dst_write(f, tag, regtype, regid)
    elif hex_common.is_single(regid):
        if hex_common.is_hvx_reg(regtype):
            if hex_common.is_new_result(tag):
                genptr_dst_write_ext(f, tag, regtype, regid, "EXT_NEW")
            elif hex_common.is_tmp_result(tag):
                genptr_dst_write_ext(f, tag, regtype, regid, "EXT_TMP")
            else:
                genptr_dst_write_ext(f, tag, regtype, regid, "EXT_DFL")
        else:
            genptr_dst_write(f, tag, regtype, regid)
    else:
        hex_common.bad_register(regtype, regid)


##
## Generate the TCG code to call the helper
##     For A2_add: Rd32=add(Rs32,Rt32), { RdV=RsV+RtV;}
##     We produce:
##    static void generate_A2_add(DisasContext *ctx)
##    {
##        Insn *insn __attribute__((unused)) = ctx->insn;
##        const int RdN = insn->regno[0];
##        TCGv RdV = get_result_gpr(ctx, RdN);
##        TCGv RsV = hex_gpr[insn->regno[1]];
##        TCGv RtV = hex_gpr[insn->regno[2]];
##        <GEN>
##        gen_log_reg_write(ctx, RdN, RdV);
##    }
##
##       where <GEN> depends on hex_common.skip_qemu_helper(tag)
##       if hex_common.skip_qemu_helper(tag) is True
##       <GEN>  is fGEN_TCG_A2_add({ RdV=RsV+RtV;});
##       if hex_common.skip_qemu_helper(tag) is False
##       <GEN>  is gen_helper_A2_add(RdV, tcg_env, RsV, RtV);
##
def gen_tcg_func(f, tag, regs, imms):
    f.write(f"static void generate_{tag}(DisasContext *ctx)\n")
    f.write("{\n")

    f.write("    Insn *insn __attribute__((unused)) = ctx->insn;\n")

    if hex_common.need_ea(tag):
        gen_decl_ea_tcg(f, tag)
    i = 0
    ## Declare all the operands (regs and immediates)
    for regtype, regid in regs:
        genptr_decl_opn(f, tag, regtype, regid, i)
        i += 1
    for immlett, bits, immshift in imms:
        genptr_decl_imm(f, immlett)

    if "A_PRIV" in hex_common.attribdict[tag]:
        f.write("    fCHECKFORPRIV();\n")
    if "A_GUEST" in hex_common.attribdict[tag]:
        f.write("    fCHECKFORGUEST();\n")

    ## Read all the inputs
    for regtype, regid in regs:
        if hex_common.is_read(regid):
            genptr_src_read_opn(f, regtype, regid, tag)

    if hex_common.is_idef_parser_enabled(tag):
        declared = []
        ## Handle registers
        for regtype, regid in regs:
            if hex_common.is_pair(regid) or (
                hex_common.is_single(regid)
                and hex_common.is_old_val(regtype, regid, tag)
            ):
                declared.append(f"{regtype}{regid}V")
                if regtype == "M":
                    declared.append(f"{regtype}{regid}N")
            elif hex_common.is_new_val(regtype, regid, tag):
                declared.append(f"{regtype}{regid}N")
            else:
                hex_common.bad_register(regtype, regid)

        ## Handle immediates
        for immlett, bits, immshift in imms:
            declared.append(hex_common.imm_name(immlett))

        arguments = ", ".join(["ctx", "ctx->insn", "ctx->pkt"] + declared)
        f.write(f"    emit_{tag}({arguments});\n")

    elif hex_common.skip_qemu_helper(tag):
        f.write(f"    fGEN_TCG_{tag}({hex_common.semdict[tag]});\n")
    else:
        ## Generate the call to the helper
        for immlett, bits, immshift in imms:
            gen_helper_decl_imm(f, immlett)
        if hex_common.need_pkt_has_multi_cof(tag):
            f.write("    TCGv pkt_has_multi_cof = ")
            f.write("tcg_constant_tl(ctx->pkt->pkt_has_multi_cof);\n")
        if hex_common.need_pkt_need_commit(tag):
            f.write("    TCGv pkt_need_commit = ")
            f.write("tcg_constant_tl(ctx->need_commit);\n")
        if hex_common.need_part1(tag):
            f.write("    TCGv part1 = tcg_constant_tl(insn->part1);\n")
        if hex_common.need_slot(tag):
            f.write("    TCGv slotval = gen_slotval(ctx);\n")
        if hex_common.need_PC(tag):
            f.write("    TCGv PC = tcg_constant_tl(ctx->pkt->pc);\n")
        if hex_common.helper_needs_next_PC(tag):
            f.write("    TCGv next_PC = tcg_constant_tl(ctx->next_PC);\n")
        f.write(f"    gen_helper_{tag}(")
        i = 0
        ## If there is a scalar result, it is the return type
        for regtype, regid in regs:
            if hex_common.is_written(regid):
                if hex_common.is_hvx_reg(regtype):
                    continue
                gen_helper_call_opn(f, tag, regtype, regid, i)
                i += 1
        if i > 0:
            f.write(", ")
        f.write("tcg_env")
        i = 1
        ## For conditional instructions, we pass in the destination register
        if "A_CONDEXEC" in hex_common.attribdict[tag]:
            for regtype, regid in regs:
                if hex_common.is_writeonly(regid) and not hex_common.is_hvx_reg(
                    regtype
                ):
                    gen_helper_call_opn(f, tag, regtype, regid, i)
                    i += 1
        for regtype, regid in regs:
            if hex_common.is_written(regid):
                if not hex_common.is_hvx_reg(regtype):
                    continue
                gen_helper_call_opn(f, tag, regtype, regid, i)
                i += 1
        for regtype, regid in regs:
            if hex_common.is_read(regid):
                if hex_common.is_hvx_reg(regtype) and hex_common.is_readwrite(regid):
                    continue
                gen_helper_call_opn(f, tag, regtype, regid, i)
                i += 1
        for immlett, bits, immshift in imms:
            gen_helper_call_imm(f, immlett)

        if hex_common.need_pkt_has_multi_cof(tag):
            f.write(", pkt_has_multi_cof")
        if hex_common.need_pkt_need_commit(tag):
            f.write(", pkt_need_commit")
        if hex_common.need_PC(tag):
            f.write(", PC")
        if hex_common.helper_needs_next_PC(tag):
            f.write(", next_PC")
        if hex_common.need_slot(tag):
            f.write(", slotval")
        if hex_common.need_part1(tag):
            f.write(", part1")
        f.write(");\n")

    ## Write all the outputs
    for regtype, regid in regs:
        if hex_common.is_written(regid):
            genptr_dst_write_opn(f, regtype, regid, tag)

    f.write("}\n\n")


def gen_def_tcg_func(f, tag, tagregs, tagimms):
    regs = tagregs[tag]
    imms = tagimms[tag]

    gen_tcg_func(f, tag, regs, imms)


def main():
    hex_common.read_semantics_file(sys.argv[1])
    hex_common.read_attribs_file(sys.argv[2])
    hex_common.read_overrides_file(sys.argv[3])
    hex_common.read_overrides_file(sys.argv[4])
    hex_common.calculate_attribs()
    ## Whether or not idef-parser is enabled is
    ## determined by the number of arguments to
    ## this script:
    ##
    ##   5 args. -> not enabled,
    ##   6 args. -> idef-parser enabled.
    ##
    ## The 6:th arg. then holds a list of the successfully
    ## parsed instructions.
    is_idef_parser_enabled = len(sys.argv) > 6
    if is_idef_parser_enabled:
        hex_common.read_idef_parser_enabled_file(sys.argv[5])
    tagregs = hex_common.get_tagregs()
    tagimms = hex_common.get_tagimms()

    output_file = sys.argv[-1]
    with open(output_file, "w") as f:
        f.write("#ifndef HEXAGON_TCG_FUNCS_H\n")
        f.write("#define HEXAGON_TCG_FUNCS_H\n\n")
        if is_idef_parser_enabled:
            f.write('#include "idef-generated-emitter.h.inc"\n\n')

        for tag in hex_common.tags:
            ## Skip the priv instructions
            if "A_PRIV" in hex_common.attribdict[tag]:
                continue
            ## Skip the guest instructions
            if "A_GUEST" in hex_common.attribdict[tag]:
                continue
            ## Skip the diag instructions
            if tag == "Y6_diag":
                continue
            if tag == "Y6_diag0":
                continue
            if tag == "Y6_diag1":
                continue

            gen_def_tcg_func(f, tag, tagregs, tagimms)

        f.write("#endif    /* HEXAGON_TCG_FUNCS_H */\n")


if __name__ == "__main__":
    main()
