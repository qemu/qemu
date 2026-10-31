#!/usr/bin/env python3

##
##  Copyright(c) 2019-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
## Generate the TCG code to call the helper
##     For A2_add: Rd32=add(Rs32,Rt32), { RdV=RsV+RtV;}
##     We produce:
##       int32_t HELPER(A2_add)(CPUHexagonState *env, int32_t RsV, int32_t RtV)
##       {
##           int32_t RdV = 0;
##           { RdV=RsV+RtV;}
##           return RdV;
##       }
##
def gen_helper_function(f, tag, tagregs, tagimms):
    regs = tagregs[tag]
    imms = tagimms[tag]

    ret_type = hex_common.helper_ret_type(tag, regs).func_arg

    declared = []
    for arg in hex_common.helper_args(tag, regs, imms):
        declared.append(arg.func_arg)

    ## A helper with no arguments needs (void) to be a prototype
    arguments = ", ".join(declared) or "void"
    f.write(f"{ret_type} HELPER({tag})({arguments})\n")
    f.write("{\n")
    if hex_common.need_ea(tag):
        f.write(hex_common.code_fmt(f"""\
            uint32_t EA;
        """))
    ## Declare the return variable
    if not hex_common.is_predicated(tag):
        for regtype, regid in regs:
            reg = hex_common.get_register(tag, regtype, regid)
            is_vector = reg.is_hvx_reg() or reg.is_hlx_reg()
            if reg.is_writeonly() and not is_vector:
                f.write(hex_common.code_fmt(f"""\
                    {reg.helper_arg_type()} {reg.helper_arg_name()} = 0;
                """))

    ## Print useful information about HVX and HLX registers.
    for regtype, regid in regs:
        reg = hex_common.get_register(tag, regtype, regid)
        if reg.is_hvx_reg():
            reg.helper_hvx_desc(f)
        if reg.is_hlx_reg():
            reg.helper_hlx_desc(f)

    if hex_common.need_slot(tag):
        if "A_LOAD" in hex_common.attribdict[tag]:
            f.write(hex_common.code_fmt(f"""\
                bool pkt_has_scalar_store_s1 __attribute__((unused)) = \
                    slotval & 0x1;
            """))
        f.write(hex_common.code_fmt(f"""\
            uint32_t slot __attribute__((unused)) = slotval >> 1;
        """))

    if "A_FPOP" in hex_common.attribdict[tag]:
        f.write(hex_common.code_fmt(f"""\
            arch_fpop_start(env);
        """))

    ## Define HLX register variables.
    has_hlx_reg = False
    for regtype, regid in regs:
        reg = hex_common.get_register(tag, regtype, regid)
        if reg.is_hlx_reg():
            has_hlx_reg = True
            if reg.is_pair():
                tmp_name = f"_{reg.reg_tcg()}_tmp"
                f.write(hex_common.code_fmt(f"""\
                    HLXVectorPair {tmp_name};
                """))
                if reg.is_read():
                    f.write(hex_common.code_fmt(f"""\
                        hlx_interleave_pair(&{tmp_name},
                            (const HLXVector *)({reg.helper_arg_name()}),
                            (const HLXVector *)({reg.helper_arg_name()}) + 1);
                    """))
                else:
                    f.write(hex_common.code_fmt(f"""\
                        memset(&{tmp_name}, 0, sizeof({tmp_name}));
                    """))
                f.write(hex_common.code_fmt(f"""\
                    #define {reg.reg_tcg()} {tmp_name}
                """))
            else:
                reg_def = f"(*(HLXVector *)({reg.helper_arg_name()}))"
                f.write(hex_common.code_fmt(f"""\
                    #define {reg.reg_tcg()} {reg_def}
                """))

    ## Declare fLVFOREACH loop variables.
    semantics = hex_common.semdict[tag]
    if has_hlx_reg and "fLVFOREACH" in semantics:
        f.write(hex_common.code_fmt(f"""\
            int i;
        """))
        # Remove nested fHIDE() wrappers.
        while 'fHIDE(' in semantics:
            # Find the matching closing parenthesis.
            start = semantics.find('fHIDE(')
            paren_count = 0
            end = -1
            for pos in range(start + len('fHIDE('), len(semantics)):
                if semantics[pos] == '(':
                    paren_count += 1
                elif semantics[pos] == ')':
                    if paren_count == 0:
                        end = pos
                        break
                    paren_count -= 1
            if end < 0:
                raise Exception(f"unbalanced fHIDE( in {tag}")
            semantics = semantics[:start] + semantics[end + 1:]

    f.write(hex_common.code_fmt(f"""\
        {semantics};
    """))

    ## Undefine HLX registers and deinterleave written pairs.
    for regtype, regid in regs:
        reg = hex_common.get_register(tag, regtype, regid)
        if reg.is_hlx_reg():
            if reg.is_pair() and reg.is_written():
                tmp_name = f"_{reg.reg_tcg()}_tmp"
                f.write(hex_common.code_fmt(f"""\
                    hlx_deinterleave_pair(
                        (HLXVector *)({reg.helper_arg_name()}),
                        (HLXVector *)({reg.helper_arg_name()}) + 1,
                        &{tmp_name});
                """))
            f.write(hex_common.code_fmt(f"""\
                #undef {reg.reg_tcg()}
            """))

    ## Return the scalar result
    for regtype, regid in regs:
        reg = hex_common.get_register(tag, regtype, regid)
        if reg.is_written() and not reg.is_hvx_reg() and not reg.is_hlx_reg():
            f.write(hex_common.code_fmt(f"""\
                return {reg.helper_arg_name()};
            """))

    f.write("}\n\n")
    ## End of the helper definition


def main():
    args = hex_common.parse_common_args(
        "Emit helper function definitions for each instruction"
    )
    tagregs = hex_common.get_tagregs()
    tagimms = hex_common.get_tagimms()

    with open(args.out, "w") as f:
        for tag in hex_common.get_user_tags():
            if hex_common.tag_ignore(tag):
                continue
            if hex_common.skip_qemu_helper(tag):
                continue
            if hex_common.is_idef_parser_enabled(tag):
                continue
            gen_helper_function(f, tag, tagregs, tagimms)

        f.write("#if !defined(CONFIG_USER_ONLY)\n")
        for tag in hex_common.get_sys_tags():
            if hex_common.skip_qemu_helper(tag):
                continue
            if hex_common.is_idef_parser_enabled(tag):
                continue
            gen_helper_function(f, tag, tagregs, tagimms)
        f.write("#endif\n")


if __name__ == "__main__":
    main()
