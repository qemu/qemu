#!/usr/bin/env python3

##
##  Copyright(c) 2019-2022 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
    if ('A_CONDEXEC' in hex_common.attribdict[tag] or
        'A_LOAD' in hex_common.attribdict[tag]):
        f.write("    TCGv EA = tcg_temp_local_new();\n")
    else:
        f.write("    TCGv EA = tcg_temp_new();\n")

def gen_free_ea_tcg(f):
    f.write("    tcg_temp_free(EA);\n")

def genptr_decl_pair_writable(f, tag, regtype, regid, regno):
    regN="%s%sN" % (regtype,regid)
    f.write("    TCGv_i64 %s%sV = tcg_temp_local_new_i64();\n" % \
        (regtype, regid))
    if (regtype == "C"):
        f.write("    const int %s = insn->regno[%d] + HEX_REG_SA0;\n" % \
            (regN, regno))
    else:
        f.write("    const int %s = insn->regno[%d];\n" % (regN, regno))
    if ('A_CONDEXEC' in hex_common.attribdict[tag]):
        f.write("    if (!is_preloaded(ctx, %s)) {\n" % regN)
        f.write("        tcg_gen_mov_tl(hex_new_value[%s], hex_gpr[%s]);\n" % \
                             (regN, regN))
        f.write("    }\n")
        f.write("    if (!is_preloaded(ctx, %s + 1)) {\n" % regN)
        f.write("        tcg_gen_mov_tl(hex_new_value[%s + 1], hex_gpr[%s + 1]);\n" % \
                             (regN, regN))
        f.write("    }\n")

def genptr_decl_writable(f, tag, regtype, regid, regno):
    regN="%s%sN" % (regtype,regid)
    f.write("    TCGv %s%sV = tcg_temp_local_new();\n" % \
        (regtype, regid))
    if (regtype == "C"):
        f.write("    const int %s = insn->regno[%d] + HEX_REG_SA0;\n" % \
            (regN, regno))
    else:
        f.write("    const int %s = insn->regno[%d];\n" % (regN, regno))
    if ('A_CONDEXEC' in hex_common.attribdict[tag]):
        f.write("    if (!is_preloaded(ctx, %s)) {\n" % regN)
        f.write("        tcg_gen_mov_tl(hex_new_value[%s], hex_gpr[%s]);\n" % \
                             (regN, regN))
        f.write("    }\n")

def genptr_decl(f, tag, regtype, regid, regno):
    regN="%s%sN" % (regtype,regid)
    if (regtype == "R"):
        if (regid in {"ss", "tt"}):
            f.write("    TCGv_i64 %s%sV = tcg_temp_local_new_i64();\n" % \
                (regtype, regid))
            f.write("    const int %s = insn->regno[%d];\n" % \
                (regN, regno))
        elif (regid in {"dd", "ee", "xx", "yy"}):
            genptr_decl_pair_writable(f, tag, regtype, regid, regno)
        elif (regid in {"s", "t", "u", "v"}):
            f.write("    TCGv %s%sV = hex_gpr[insn->regno[%d]];\n" % \
                (regtype, regid, regno))
        elif (regid in {"d", "e", "x", "y"}):
            genptr_decl_writable(f, tag, regtype, regid, regno)
        else:
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "P"):
        if (regid in {"s", "t", "u", "v"}):
            f.write("    TCGv %s%sV = hex_pred[insn->regno[%d]];\n" % \
                (regtype, regid, regno))
        elif (regid in {"d", "e", "x"}):
            genptr_decl_writable(f, tag, regtype, regid, regno)
        else:
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "C"):
        if (regid == "ss"):
            f.write("    TCGv_i64 %s%sV = tcg_temp_local_new_i64();\n" % \
                (regtype, regid))
            f.write("    const int %s = insn->regno[%d] + HEX_REG_SA0;\n" % \
                (regN, regno))
        elif (regid == "dd"):
            genptr_decl_pair_writable(f, tag, regtype, regid, regno)
        elif (regid == "s"):
            f.write("    TCGv %s%sV = tcg_temp_local_new();\n" % \
                (regtype, regid))
            f.write("    const int %s%sN = insn->regno[%d] + HEX_REG_SA0;\n" % \
                (regtype, regid, regno))
        elif (regid == "d"):
            genptr_decl_writable(f, tag, regtype, regid, regno)
        else:
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "M"):
        if (regid == "u"):
            f.write("    const int %s%sN = insn->regno[%d];\n"% \
                (regtype, regid, regno))
            f.write("    TCGv %s%sV = hex_gpr[%s%sN + HEX_REG_M0];\n" % \
                (regtype, regid, regtype, regid))
        else:
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "V"):
        if (regid in {"dd"}):
            f.write("    const int %s%sN = insn->regno[%d];\n" %\
                (regtype, regid, regno))
            f.write("    const intptr_t %s%sV_off =\n" %\
                 (regtype, regid))
            if (hex_common.is_tmp_result(tag)):
                f.write("        ctx_tmp_vreg_off(ctx, %s%sN, 2, true);\n" % \
                     (regtype, regid))
            else:
                f.write("        ctx_future_vreg_off(ctx, %s%sN," % \
                     (regtype, regid))
                f.write(" 2, true);\n")
            if (not hex_common.skip_qemu_helper(tag)):
                f.write("    TCGv_ptr %s%sV = tcg_temp_new_ptr();\n" % \
                    (regtype, regid))
                f.write("    tcg_gen_addi_ptr(%s%sV, cpu_env, %s%sV_off);\n" % \
                    (regtype, regid, regtype, regid))
        elif (regid in {"uu", "vv", "xx"}):
            f.write("    const int %s%sN = insn->regno[%d];\n" % \
                (regtype, regid, regno))
            f.write("    const intptr_t %s%sV_off =\n" % \
                 (regtype, regid))
            f.write("        offsetof(CPUHexagonState, %s%sV);\n" % \
                 (regtype, regid))
            if (not hex_common.skip_qemu_helper(tag)):
                f.write("    TCGv_ptr %s%sV = tcg_temp_new_ptr();\n" % \
                    (regtype, regid))
                f.write("    tcg_gen_addi_ptr(%s%sV, cpu_env, %s%sV_off);\n" % \
                    (regtype, regid, regtype, regid))
        elif (regid in {"s", "u", "v", "w"}):
            f.write("    const int %s%sN = insn->regno[%d];\n" % \
                (regtype, regid, regno))
            f.write("    const intptr_t %s%sV_off =\n" % \
                              (regtype, regid))
            f.write("        vreg_src_off(ctx, %s%sN);\n" % \
                              (regtype, regid))
            if (not hex_common.skip_qemu_helper(tag)):
                f.write("    TCGv_ptr %s%sV = tcg_temp_new_ptr();\n" % \
                    (regtype, regid))
        elif (regid in {"d", "x", "y"}):
            f.write("    const int %s%sN = insn->regno[%d];\n" % \
                (regtype, regid, regno))
            f.write("    const intptr_t %s%sV_off =\n" % \
                (regtype, regid))
            if (regid == "y"):
                f.write("        offsetof(CPUHexagonState, vtmp);\n")
            elif (hex_common.is_tmp_result(tag)):
                f.write("        ctx_tmp_vreg_off(ctx, %s%sN, 1, true);\n" % \
                    (regtype, regid))
            else:
                f.write("        ctx_future_vreg_off(ctx, %s%sN," % \
                    (regtype, regid))
                f.write(" 1, true);\n");
            if 'A_CONDEXEC' in hex_common.attribdict[tag]:
                f.write("    if (!is_vreg_preloaded(ctx, %s)) {\n" % (regN))
                f.write("        intptr_t src_off =")
                f.write(" offsetof(CPUHexagonState, VRegs[%s%sN]);\n"% \
                                     (regtype, regid))
                f.write("        tcg_gen_gvec_mov(MO_64, %s%sV_off,\n" % \
                                     (regtype, regid))
                f.write("                         src_off,\n")
                f.write("                         sizeof(MMVector),\n")
                f.write("                         sizeof(MMVector));\n")
                f.write("    }\n")

            if (not hex_common.skip_qemu_helper(tag)):
                f.write("    TCGv_ptr %s%sV = tcg_temp_new_ptr();\n" % \
                    (regtype, regid))
                f.write("    tcg_gen_addi_ptr(%s%sV, cpu_env, %s%sV_off);\n" % \
                    (regtype, regid, regtype, regid))
        else:
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "Q"):
        if (regid in {"d", "e", "x"}):
            f.write("    const int %s%sN = insn->regno[%d];\n" % \
                (regtype, regid, regno))
            f.write("    const intptr_t %s%sV_off =\n" % \
                (regtype, regid))
            f.write("        offsetof(CPUHexagonState,\n")
            f.write("                 future_QRegs[%s%sN]);\n" % \
                (regtype, regid))
            if (not hex_common.skip_qemu_helper(tag)):
                f.write("    TCGv_ptr %s%sV = tcg_temp_new_ptr();\n" % \
                    (regtype, regid))
                f.write("    tcg_gen_addi_ptr(%s%sV, cpu_env, %s%sV_off);\n" % \
                    (regtype, regid, regtype, regid))
        elif (regid in {"s", "t", "u", "v"}):
            f.write("    const int %s%sN = insn->regno[%d];\n" % \
                (regtype, regid, regno))
            f.write("    const intptr_t %s%sV_off =\n" %\
                (regtype, regid))
            f.write("        offsetof(CPUHexagonState, QRegs[%s%sN]);\n" % \
                (regtype, regid))
            if (not hex_common.skip_qemu_helper(tag)):
                f.write("    TCGv_ptr %s%sV = tcg_temp_new_ptr();\n" % \
                    (regtype, regid))
        else:
            print("Bad register parse: ", regtype, regid)
    else:
        print("Bad register parse: ", regtype, regid)

def genptr_decl_new(f, tag, regtype, regid, regno):
    if (regtype == "N"):
        if (regid in {"s", "t"}):
            f.write("    TCGv %s%sN = hex_new_value[insn->regno[%d]];\n" % \
                (regtype, regid, regno))
        else:
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "P"):
        if (regid in {"t", "u", "v"}):
            f.write("    TCGv %s%sN = hex_new_pred_value[insn->regno[%d]];\n" % \
                (regtype, regid, regno))
        else:
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "O"):
        if (regid == "s"):
            f.write("    const intptr_t %s%sN_num = insn->regno[%d];\n" % \
                (regtype, regid, regno))
            if (hex_common.skip_qemu_helper(tag)):
                f.write("    const intptr_t %s%sN_off =\n" % \
                    (regtype, regid))
                f.write("         ctx_future_vreg_off(ctx, %s%sN_num," % \
                    (regtype, regid))
                f.write(" 1, true);\n")
            else:
                f.write("    TCGv %s%sN = tcg_constant_tl(%s%sN_num);\n" % \
                    (regtype, regid, regtype, regid))
        else:
            print("Bad register parse: ", regtype, regid)
    else:
        print("Bad register parse: ", regtype, regid)

def genptr_decl_opn(f, tag, regtype, regid, toss, numregs, i):
    if (hex_common.is_pair(regid)):
        genptr_decl(f, tag, regtype, regid, i)
    elif (hex_common.is_single(regid)):
        if hex_common.is_old_val(regtype, regid, tag):
            genptr_decl(f,tag, regtype, regid, i)
        elif hex_common.is_new_val(regtype, regid, tag):
            genptr_decl_new(f, tag, regtype, regid, i)
        else:
            print("Bad register parse: ",regtype,regid,toss,numregs)
    else:
        print("Bad register parse: ",regtype,regid,toss,numregs)

def genptr_decl_imm(f,immlett):
    if (immlett.isupper()):
        i = 1
    else:
        i = 0
    f.write("    int %s = insn->immed[%d];\n" % \
        (hex_common.imm_name(immlett), i))

def genptr_free(f, tag, regtype, regid, regno):
    if (regtype == "R"):
        if (regid in {"dd", "ss", "tt", "xx", "yy"}):
            f.write("    tcg_temp_free_i64(%s%sV);\n" % (regtype, regid))
        elif (regid in {"d", "e", "x", "y"}):
            f.write("    tcg_temp_free(%s%sV);\n" % (regtype, regid))
        elif (regid not in {"s", "t", "u", "v"}):
            print("Bad register parse: ",regtype,regid)
    elif (regtype == "P"):
        if (regid in {"d", "e", "x"}):
            f.write("    tcg_temp_free(%s%sV);\n" % (regtype, regid))
        elif (regid not in {"s", "t", "u", "v"}):
            print("Bad register parse: ",regtype,regid)
    elif (regtype == "C"):
        if (regid in {"dd", "ss"}):
            f.write("    tcg_temp_free_i64(%s%sV);\n" % (regtype, regid))
        elif (regid in {"d", "s"}):
            f.write("    tcg_temp_free(%s%sV);\n" % (regtype, regid))
        else:
            print("Bad register parse: ",regtype,regid)
    elif (regtype == "M"):
        if (regid != "u"):
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "V"):
        if (regid in {"dd", "uu", "vv", "xx", \
                      "d", "s", "u", "v", "w", "x", "y"}):
            if (not hex_common.skip_qemu_helper(tag)):
                f.write("    tcg_temp_free_ptr(%s%sV);\n" % \
                    (regtype, regid))
        else:
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "Q"):
        if (regid in {"d", "e", "s", "t", "u", "v", "x"}):
            if (not hex_common.skip_qemu_helper(tag)):
                f.write("    tcg_temp_free_ptr(%s%sV);\n" % \
                    (regtype, regid))
        else:
            print("Bad register parse: ", regtype, regid)
    else:
        print("Bad register parse: ", regtype, regid)

def genptr_free_new(f, tag, regtype, regid, regno):
    if (regtype == "N"):
        if (regid not in {"s", "t"}):
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "P"):
        if (regid not in {"t", "u", "v"}):
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "O"):
        if (regid != "s"):
            print("Bad register parse: ", regtype, regid)
    else:
        print("Bad register parse: ", regtype, regid)

def genptr_free_opn(f,regtype,regid,i,tag):
    if (hex_common.is_pair(regid)):
        genptr_free(f, tag, regtype, regid, i)
    elif (hex_common.is_single(regid)):
        if hex_common.is_old_val(regtype, regid, tag):
            genptr_free(f, tag, regtype, regid, i)
        elif hex_common.is_new_val(regtype, regid, tag):
            genptr_free_new(f, tag, regtype, regid, i)
        else:
            print("Bad register parse: ",regtype,regid,toss,numregs)
    else:
        print("Bad register parse: ",regtype,regid,toss,numregs)

def genptr_src_read(f, tag, regtype, regid):
    if (regtype == "R"):
        if (regid in {"ss", "tt", "xx", "yy"}):
            f.write("    tcg_gen_concat_i32_i64(%s%sV, hex_gpr[%s%sN],\n" % \
                (regtype, regid, regtype, regid))
            f.write("                                 hex_gpr[%s%sN + 1]);\n" % \
                (regtype, regid))
        elif (regid in {"x", "y"}):
            f.write("    tcg_gen_mov_tl(%s%sV, hex_gpr[%s%sN]);\n" % \
                (regtype,regid,regtype,regid))
        elif (regid not in {"s", "t", "u", "v"}):
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "P"):
        if (regid == "x"):
            f.write("    tcg_gen_mov_tl(%s%sV, hex_pred[%s%sN]);\n" % \
                (regtype, regid, regtype, regid))
        elif (regid not in {"s", "t", "u", "v"}):
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "C"):
        if (regid == "ss"):
            f.write("    gen_read_ctrl_reg_pair(ctx, %s%sN, %s%sV);\n" % \
                             (regtype, regid, regtype, regid))
        elif (regid == "s"):
            f.write("    gen_read_ctrl_reg(ctx, %s%sN, %s%sV);\n" % \
                             (regtype, regid, regtype, regid))
        else:
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "M"):
        if (regid != "u"):
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "V"):
        if (regid in {"uu", "vv", "xx"}):
            f.write("    tcg_gen_gvec_mov(MO_64, %s%sV_off,\n" % \
                (regtype, regid))
            f.write("        vreg_src_off(ctx, %s%sN),\n" % \
                (regtype, regid))
            f.write("        sizeof(MMVector), sizeof(MMVector));\n")
            f.write("    tcg_gen_gvec_mov(MO_64,\n")
            f.write("        %s%sV_off + sizeof(MMVector),\n" % \
                (regtype, regid))
            f.write("        vreg_src_off(ctx, %s%sN ^ 1),\n" % \
                (regtype, regid))
            f.write("        sizeof(MMVector), sizeof(MMVector));\n")
        elif (regid in {"s", "u", "v", "w"}):
            if (not hex_common.skip_qemu_helper(tag)):
                f.write("    tcg_gen_addi_ptr(%s%sV, cpu_env, %s%sV_off);\n" % \
                                 (regtype, regid, regtype, regid))
        elif (regid in {"x", "y"}):
            f.write("    tcg_gen_gvec_mov(MO_64, %s%sV_off,\n" % \
                             (regtype, regid))
            f.write("        vreg_src_off(ctx, %s%sN),\n" % \
                             (regtype, regid))
            f.write("        sizeof(MMVector), sizeof(MMVector));\n")
        else:
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "Q"):
        if (regid in {"s", "t", "u", "v"}):
            if (not hex_common.skip_qemu_helper(tag)):
                f.write("    tcg_gen_addi_ptr(%s%sV, cpu_env, %s%sV_off);\n" % \
                    (regtype, regid, regtype, regid))
        elif (regid in {"x"}):
            f.write("    tcg_gen_gvec_mov(MO_64, %s%sV_off,\n" % \
                (regtype, regid))
            f.write("        offsetof(CPUHexagonState, QRegs[%s%sN]),\n" % \
                (regtype, regid))
            f.write("        sizeof(MMQReg), sizeof(MMQReg));\n")
        else:
            print("Bad register parse: ", regtype, regid)
    else:
        print("Bad register parse: ", regtype, regid)

def genptr_src_read_new(f,regtype,regid):
    if (regtype == "N"):
        if (regid not in {"s", "t"}):
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "P"):
        if (regid not in {"t", "u", "v"}):
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "O"):
        if (regid != "s"):
            print("Bad register parse: ", regtype, regid)
    else:
        print("Bad register parse: ", regtype, regid)

def genptr_src_read_opn(f,regtype,regid,tag):
    if (hex_common.is_pair(regid)):
        genptr_src_read(f, tag, regtype, regid)
    elif (hex_common.is_single(regid)):
        if hex_common.is_old_val(regtype, regid, tag):
            genptr_src_read(f, tag, regtype, regid)
        elif hex_common.is_new_val(regtype, regid, tag):
            genptr_src_read_new(f,regtype,regid)
        else:
            print("Bad register parse: ",regtype,regid,toss,numregs)
    else:
        print("Bad register parse: ",regtype,regid,toss,numregs)

def gen_helper_call_opn(f, tag, regtype, regid, toss, numregs, i):
    if (i > 0): f.write(", ")
    if (hex_common.is_pair(regid)):
        f.write("%s%sV" % (regtype,regid))
    elif (hex_common.is_single(regid)):
        if hex_common.is_old_val(regtype, regid, tag):
            f.write("%s%sV" % (regtype,regid))
        elif hex_common.is_new_val(regtype, regid, tag):
            f.write("%s%sN" % (regtype,regid))
        else:
            print("Bad register parse: ",regtype,regid,toss,numregs)
    else:
        print("Bad register parse: ",regtype,regid,toss,numregs)

def gen_helper_decl_imm(f,immlett):
    f.write("    TCGv tcgv_%s = tcg_constant_tl(%s);\n" % \
        (hex_common.imm_name(immlett), hex_common.imm_name(immlett)))

def gen_helper_call_imm(f,immlett):
    f.write(", tcgv_%s" % hex_common.imm_name(immlett))

def genptr_dst_write_pair(f, tag, regtype, regid):
    if ('A_CONDEXEC' in hex_common.attribdict[tag]):
        f.write("    gen_log_predicated_reg_write_pair(%s%sN, %s%sV, insn->slot);\n" % \
            (regtype, regid, regtype, regid))
    else:
        f.write("    gen_log_reg_write_pair(%s%sN, %s%sV);\n" % \
            (regtype, regid, regtype, regid))
    f.write("    ctx_log_reg_write_pair(ctx, %s%sN);\n" % \
        (regtype, regid))

def genptr_dst_write(f, tag, regtype, regid):
    if (regtype == "R"):
        if (regid in {"dd", "xx", "yy"}):
            genptr_dst_write_pair(f, tag, regtype, regid)
        elif (regid in {"d", "e", "x", "y"}):
            if ('A_CONDEXEC' in hex_common.attribdict[tag]):
                f.write("    gen_log_predicated_reg_write(%s%sN, %s%sV,\n" % \
                    (regtype, regid, regtype, regid))
                f.write("                                 insn->slot);\n")
            else:
                f.write("    gen_log_reg_write(%s%sN, %s%sV);\n" % \
                    (regtype, regid, regtype, regid))
            f.write("    ctx_log_reg_write(ctx, %s%sN);\n" % \
                (regtype, regid))
        else:
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "P"):
        if (regid in {"d", "e", "x"}):
            f.write("    gen_log_pred_write(ctx, %s%sN, %s%sV);\n" % \
                (regtype, regid, regtype, regid))
            f.write("    ctx_log_pred_write(ctx, %s%sN);\n" % \
                (regtype, regid))
        else:
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "C"):
        if (regid == "dd"):
            f.write("    gen_write_ctrl_reg_pair(ctx, %s%sN, %s%sV);\n" % \
                             (regtype, regid, regtype, regid))
        elif (regid == "d"):
            f.write("    gen_write_ctrl_reg(ctx, %s%sN, %s%sV);\n" % \
                             (regtype, regid, regtype, regid))
        else:
            print("Bad register parse: ", regtype, regid)
    else:
        print("Bad register parse: ", regtype, regid)

def genptr_dst_write_ext(f, tag, regtype, regid, newv="EXT_DFL"):
    if (regtype == "V"):
        if (regid in {"dd", "xx", "yy"}):
            if ('A_CONDEXEC' in hex_common.attribdict[tag]):
                is_predicated = "true"
            else:
                is_predicated = "false"
            f.write("    gen_log_vreg_write_pair(ctx, %s%sV_off, %s%sN, " % \
                (regtype, regid, regtype, regid))
            f.write("%s, insn->slot, %s);\n" % \
                (newv, is_predicated))
            f.write("    ctx_log_vreg_write_pair(ctx, %s%sN, %s,\n" % \
                (regtype, regid, newv))
            f.write("        %s);\n" % (is_predicated))
        elif (regid in {"d", "x", "y"}):
            if ('A_CONDEXEC' in hex_common.attribdict[tag]):
                is_predicated = "true"
            else:
                is_predicated = "false"
            f.write("    gen_log_vreg_write(ctx, %s%sV_off, %s%sN, %s, " % \
                (regtype, regid, regtype, regid, newv))
            f.write("insn->slot, %s);\n" % \
                (is_predicated))
            f.write("    ctx_log_vreg_write(ctx, %s%sN, %s, %s);\n" % \
                (regtype, regid, newv, is_predicated))
        else:
            print("Bad register parse: ", regtype, regid)
    elif (regtype == "Q"):
        if (regid in {"d", "e", "x"}):
            if ('A_CONDEXEC' in hex_common.attribdict[tag]):
                is_predicated = "true"
            else:
                is_predicated = "false"
            f.write("    gen_log_qreg_write(%s%sV_off, %s%sN, %s, " % \
                (regtype, regid, regtype, regid, newv))
            f.write("insn->slot, %s);\n" % (is_predicated))
            f.write("    ctx_log_qreg_write(ctx, %s%sN, %s);\n" % \
                (regtype, regid, is_predicated))
        else:
            print("Bad register parse: ", regtype, regid)
    else:
        print("Bad register parse: ", regtype, regid)

def genptr_dst_write_opn(f,regtype, regid, tag):
    if (hex_common.is_pair(regid)):
        if (hex_common.is_hvx_reg(regtype)):
            if (hex_common.is_tmp_result(tag)):
                genptr_dst_write_ext(f, tag, regtype, regid, "EXT_TMP")
            else:
                genptr_dst_write_ext(f, tag, regtype, regid)
        else:
            genptr_dst_write(f, tag, regtype, regid)
    elif (hex_common.is_single(regid)):
        if (hex_common.is_hvx_reg(regtype)):
            if (hex_common.is_new_result(tag)):
                genptr_dst_write_ext(f, tag, regtype, regid, "EXT_NEW")
            elif (hex_common.is_tmp_result(tag)):
                genptr_dst_write_ext(f, tag, regtype, regid, "EXT_TMP")
            else:
                genptr_dst_write_ext(f, tag, regtype, regid, "EXT_DFL")
        else:
            genptr_dst_write(f, tag, regtype, regid)
    else:
        print("Bad register parse: ",regtype,regid,toss,numregs)

##
## Generate the TCG code to call the helper
##     For A2_add: Rd32=add(Rs32,Rt32), { RdV=RsV+RtV;}
##     We produce:
##    static void generate_A2_add(DisasContext *ctx)
##       {
##           TCGv RdV = tcg_temp_local_new();
##           const int RdN = insn->regno[0];
##           TCGv RsV = hex_gpr[insn->regno[1]];
##           TCGv RtV = hex_gpr[insn->regno[2]];
##           <GEN>
##           gen_log_reg_write(RdN, RdV);
##           ctx_log_reg_write(ctx, RdN);
##           tcg_temp_free(RdV);
##       }
##
##       where <GEN> depends on hex_common.skip_qemu_helper(tag)
##       if hex_common.skip_qemu_helper(tag) is True
##       <GEN>  is fGEN_TCG_A2_add({ RdV=RsV+RtV;});
##       if hex_common.skip_qemu_helper(tag) is False
##       <GEN>  is gen_helper_A2_add(RdV, cpu_env, RsV, RtV);
##
def gen_tcg_func(f, tag, regs, imms):
    f.write("static void generate_%s(DisasContext *ctx)\n" %tag)
    f.write('{\n')

    f.write("    Insn *insn __attribute__((unused)) = ctx->insn;\n")

    if hex_common.need_ea(tag): gen_decl_ea_tcg(f, tag)
    i=0
    ## Declare all the operands (regs and immediates)
    for regtype,regid,toss,numregs in regs:
        genptr_decl_opn(f, tag, regtype, regid, toss, numregs, i)
        i += 1
    for immlett,bits,immshift in imms:
        genptr_decl_imm(f,immlett)

    if 'A_PRIV' in hex_common.attribdict[tag]:
        f.write('    fCHECKFORPRIV();\n')
    if 'A_GUEST' in hex_common.attribdict[tag]:
        f.write('    fCHECKFORGUEST();\n')

    ## Read all the inputs
    for regtype,regid,toss,numregs in regs:
        if (hex_common.is_read(regid)):
            genptr_src_read_opn(f,regtype,regid,tag)

    if ( hex_common.skip_qemu_helper(tag) ):
        f.write("    fGEN_TCG_%s(%s);\n" % (tag, hex_common.semdict[tag]))
    else:
        ## Generate the call to the helper
        for immlett,bits,immshift in imms:
            gen_helper_decl_imm(f,immlett)
        if hex_common.need_part1(tag):
            f.write("    TCGv part1 = tcg_constant_tl(insn->part1);\n")
        if hex_common.need_slot(tag):
            f.write("    TCGv slot = tcg_constant_tl(insn->slot);\n")
        f.write("    gen_helper_%s(" % (tag))
        i=0
        ## If there is a scalar result, it is the return type
        for regtype,regid,toss,numregs in regs:
            if (hex_common.is_written(regid)):
                if (hex_common.is_hvx_reg(regtype)):
                    continue
                gen_helper_call_opn(f, tag, regtype, regid, toss, numregs, i)
                i += 1
        if (i > 0): f.write(", ")
        f.write("cpu_env")
        i=1
        for regtype,regid,toss,numregs in regs:
            if (hex_common.is_written(regid)):
                if (not hex_common.is_hvx_reg(regtype)):
                    continue
                gen_helper_call_opn(f, tag, regtype, regid, toss, numregs, i)
                i += 1
        for regtype,regid,toss,numregs in regs:
            if (hex_common.is_read(regid)):
                if (hex_common.is_hvx_reg(regtype) and
                    hex_common.is_readwrite(regid)):
                    continue
                gen_helper_call_opn(f, tag, regtype, regid, toss, numregs, i)
                i += 1
        for immlett,bits,immshift in imms:
            gen_helper_call_imm(f,immlett)

        if hex_common.need_slot(tag): f.write(", slot")
        if hex_common.need_part1(tag): f.write(", part1" )
        f.write(");\n")

    ## Write all the outputs
    for regtype,regid,toss,numregs in regs:
        if (hex_common.is_written(regid)):
            genptr_dst_write_opn(f,regtype, regid, tag)

    ## Free all the operands (regs and immediates)
    if hex_common.need_ea(tag): gen_free_ea_tcg(f)
    for regtype,regid,toss,numregs in regs:
        genptr_free_opn(f,regtype,regid,i,tag)
        i += 1

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
    tagregs = hex_common.get_tagregs()
    tagimms = hex_common.get_tagimms()

    with open(sys.argv[5], 'w') as f:
        f.write("#ifndef HEXAGON_TCG_FUNCS_H\n")
        f.write("#define HEXAGON_TCG_FUNCS_H\n\n")

        for tag in hex_common.tags:
            ## Skip the priv instructions
            if ( "A_PRIV" in hex_common.attribdict[tag] ) :
                continue
            ## Skip the guest instructions
            if ( "A_GUEST" in hex_common.attribdict[tag] ) :
                continue
            ## Skip the diag instructions
            if ( tag == "Y6_diag" ) :
                continue
            if ( tag == "Y6_diag0" ) :
                continue
            if ( tag == "Y6_diag1" ) :
                continue

            gen_def_tcg_func(f, tag, tagregs, tagimms)

        f.write("#endif    /* HEXAGON_TCG_FUNCS_H */\n")

if __name__ == "__main__":
    main()
