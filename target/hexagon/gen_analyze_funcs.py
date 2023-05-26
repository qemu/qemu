#!/usr/bin/env python3

##
##  Copyright(c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
## Helpers for gen_analyze_func
##
def is_predicated(tag):
    return "A_CONDEXEC" in hex_common.attribdict[tag]


def analyze_opn_old(f, tag, regtype, regid, regno):
    regN = f"{regtype}{regid}N"
    predicated = "true" if is_predicated(tag) else "false"
    if regtype == "R":
        if regid in {"ss", "tt"}:
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
            f.write(f"    ctx_log_reg_read_pair(ctx, {regN});\n")
        elif regid in {"dd", "ee", "xx", "yy"}:
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
            f.write(f"    ctx_log_reg_write_pair(ctx, {regN}, {predicated});\n")
        elif regid in {"s", "t", "u", "v"}:
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
            f.write(f"    ctx_log_reg_read(ctx, {regN});\n")
        elif regid in {"d", "e", "x", "y"}:
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
            f.write(f"    ctx_log_reg_write(ctx, {regN}, {predicated});\n")
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "P":
        if regid in {"s", "t", "u", "v"}:
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
            f.write(f"    ctx_log_pred_read(ctx, {regN});\n")
        elif regid in {"d", "e", "x"}:
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
            f.write(f"    ctx_log_pred_write(ctx, {regN});\n")
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "C":
        if regid == "ss":
            f.write(
                f"    const int {regN} = insn->regno[{regno}] "
                "+ HEX_REG_SA0;\n"
            )
            f.write(f"    ctx_log_reg_read_pair(ctx, {regN});\n")
        elif regid == "dd":
            f.write(f"    const int {regN} = insn->regno[{regno}] " "+ HEX_REG_SA0;\n")
            f.write(f"    ctx_log_reg_write_pair(ctx, {regN}, {predicated});\n")
        elif regid == "s":
            f.write(
                f"    const int {regN} = insn->regno[{regno}] "
                "+ HEX_REG_SA0;\n"
            )
            f.write(f"    ctx_log_reg_read(ctx, {regN});\n")
        elif regid == "d":
            f.write(f"    const int {regN} = insn->regno[{regno}] " "+ HEX_REG_SA0;\n")
            f.write(f"    ctx_log_reg_write(ctx, {regN}, {predicated});\n")
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "M":
        if regid == "u":
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
            f.write(f"    ctx_log_reg_read(ctx, {regN});\n")
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "V":
        newv = "EXT_DFL"
        if hex_common.is_new_result(tag):
            newv = "EXT_NEW"
        elif hex_common.is_tmp_result(tag):
            newv = "EXT_TMP"
        if regid in {"dd", "xx"}:
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
            f.write(
                f"    ctx_log_vreg_write_pair(ctx, {regN}, {newv}, " f"{predicated});\n"
            )
        elif regid in {"uu", "vv"}:
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
            f.write(f"    ctx_log_vreg_read_pair(ctx, {regN});\n")
        elif regid in {"s", "u", "v", "w"}:
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
            f.write(f"    ctx_log_vreg_read(ctx, {regN});\n")
        elif regid in {"d", "x", "y"}:
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
            f.write(f"    ctx_log_vreg_write(ctx, {regN}, {newv}, " f"{predicated});\n")
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "Q":
        if regid in {"d", "e", "x"}:
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
            f.write(f"    ctx_log_qreg_write(ctx, {regN});\n")
        elif regid in {"s", "t", "u", "v"}:
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
            f.write(f"    ctx_log_qreg_read(ctx, {regN});\n")
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "G":
        if regid in {"dd"}:
            f.write(f"//    const int {regN} = insn->regno[{regno}];\n")
        elif regid in {"d"}:
            f.write(f"//    const int {regN} = insn->regno[{regno}];\n")
        elif regid in {"ss"}:
            f.write(f"//    const int {regN} = insn->regno[{regno}];\n")
        elif regid in {"s"}:
            f.write(f"//    const int {regN} = insn->regno[{regno}];\n")
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "S":
        if regid in {"dd"}:
            f.write(f"//    const int {regN} = insn->regno[{regno}];\n")
        elif regid in {"d"}:
            f.write(f"//    const int {regN} = insn->regno[{regno}];\n")
        elif regid in {"ss"}:
            f.write(f"//    const int {regN} = insn->regno[{regno}];\n")
        elif regid in {"s"}:
            f.write(f"//    const int {regN} = insn->regno[{regno}];\n")
        else:
            hex_common.bad_register(regtype, regid)
    else:
        hex_common.bad_register(regtype, regid)


def analyze_opn_new(f, tag, regtype, regid, regno):
    regN = f"{regtype}{regid}N"
    if regtype == "N":
        if regid in {"s", "t"}:
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
            f.write(f"    ctx_log_reg_read(ctx, {regN});\n")
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "P":
        if regid in {"t", "u", "v"}:
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
            f.write(f"    ctx_log_pred_read(ctx, {regN});\n")
        else:
            hex_common.bad_register(regtype, regid)
    elif regtype == "O":
        if regid == "s":
            f.write(f"    const int {regN} = insn->regno[{regno}];\n")
            f.write(f"    ctx_log_vreg_read(ctx, {regN});\n")
        else:
            hex_common.bad_register(regtype, regid)
    else:
        hex_common.bad_register(regtype, regid)


def analyze_opn(f, tag, regtype, regid, i):
    if hex_common.is_pair(regid):
        analyze_opn_old(f, tag, regtype, regid, i)
    elif hex_common.is_single(regid):
        if hex_common.is_old_val(regtype, regid, tag):
            analyze_opn_old(f, tag, regtype, regid, i)
        elif hex_common.is_new_val(regtype, regid, tag):
            analyze_opn_new(f, tag, regtype, regid, i)
        else:
            hex_common.bad_register(regtype, regid)
    else:
        hex_common.bad_register(regtype, regid)


##
## Generate the code to analyze the instruction
##     For A2_add: Rd32=add(Rs32,Rt32), { RdV=RsV+RtV;}
##     We produce:
##     static void analyze_A2_add(DisasContext *ctx)
##     {
##         Insn *insn G_GNUC_UNUSED = ctx->insn;
##         const int RdN = insn->regno[0];
##         ctx_log_reg_write(ctx, RdN, false);
##         const int RsN = insn->regno[1];
##         ctx_log_reg_read(ctx, RsN);
##         const int RtN = insn->regno[2];
##         ctx_log_reg_read(ctx, RtN);
##     }
##
def gen_analyze_func(f, tag, regs, imms):
    f.write(f"static void analyze_{tag}(DisasContext *ctx)\n")
    f.write("{\n")

    f.write("    Insn *insn G_GNUC_UNUSED = ctx->insn;\n")

    i = 0
    ## Analyze all the registers
    for regtype, regid in regs:
        analyze_opn(f, tag, regtype, regid, i)
        i += 1

    has_generated_helper = not hex_common.skip_qemu_helper(
        tag
    ) and not hex_common.is_idef_parser_enabled(tag)

    ## Mark HVX instructions with generated helpers
    if (has_generated_helper and
        "A_CVI" in hex_common.attribdict[tag]):
        f.write("    ctx->has_hvx_helper = true;\n")

    f.write("}\n\n")


def main():
    hex_common.read_semantics_file(sys.argv[1])
    hex_common.read_attribs_file(sys.argv[2])
    hex_common.read_overrides_file(sys.argv[3])
    hex_common.read_overrides_file(sys.argv[4])
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
    hex_common.calculate_attribs()
    tagregs = hex_common.get_tagregs()
    tagimms = hex_common.get_tagimms()

    with open(sys.argv[-1], "w") as f:
        f.write("#ifndef HEXAGON_TCG_FUNCS_H\n")
        f.write("#define HEXAGON_TCG_FUNCS_H\n\n")

        for tag in hex_common.tags:
            gen_analyze_func(f, tag, tagregs[tag], tagimms[tag])

        f.write("#endif    /* HEXAGON_TCG_FUNCS_H */\n")


if __name__ == "__main__":
    main()
