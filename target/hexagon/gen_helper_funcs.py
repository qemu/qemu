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

    arguments = ", ".join(declared)
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
            if reg.is_writeonly() and not reg.is_hvx_reg():
                f.write(hex_common.code_fmt(f"""\
                    {reg.helper_arg_type()} {reg.helper_arg_name()} = 0;
                """))

    ## Print useful information about HVX registers
    for regtype, regid in regs:
        reg = hex_common.get_register(tag, regtype, regid)
        if reg.is_hvx_reg():
            reg.helper_hvx_desc(f)

    if hex_common.need_slot(tag):
        if "A_LOAD" in hex_common.attribdict[tag]:
            f.write(hex_common.code_fmt(f"""\
                bool pkt_has_store_s1 = slotval & 0x1;
            """))
        f.write(hex_common.code_fmt(f"""\
            uint32_t slot = slotval >> 1;
        """))

    if "A_FPOP" in hex_common.attribdict[tag]:
        f.write(hex_common.code_fmt(f"""\
            arch_fpop_start(env);
        """))

    f.write(hex_common.code_fmt(f"""\
        {hex_common.semdict[tag]}
    """))

    if "A_FPOP" in hex_common.attribdict[tag]:
        f.write(hex_common.code_fmt(f"""\
            arch_fpop_end(env);
        """))

    ## Return the scalar result
    for regtype, regid in regs:
        reg = hex_common.get_register(tag, regtype, regid)
        if reg.is_written() and not reg.is_hvx_reg():
            f.write(hex_common.code_fmt(f"""\
                return {reg.helper_arg_name()};
            """))

    f.write("}\n\n")
    ## End of the helper definition


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
    hex_common.init_registers()
    tagregs = hex_common.get_tagregs()
    tagimms = hex_common.get_tagimms()

    output_file = sys.argv[-1]
    with open(output_file, "w") as f:
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
            if hex_common.skip_qemu_helper(tag):
                continue
            if hex_common.is_idef_parser_enabled(tag):
                continue

            gen_helper_function(f, tag, tagregs, tagimms)


if __name__ == "__main__":
    main()
