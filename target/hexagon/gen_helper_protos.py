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
## Generate the DEF_HELPER prototype for an instruction
##     For A2_add: Rd32=add(Rs32,Rt32)
##     We produce:
##         DEF_HELPER_3(A2_add, s32, env, s32, s32)
##
def gen_helper_prototype(f, tag, tagregs, tagimms):
    regs = tagregs[tag]
    imms = tagimms[tag]

    declared = []
    ret_type = hex_common.helper_ret_type(tag, regs).proto_arg
    declared.append(ret_type)

    for arg in hex_common.helper_args(tag, regs, imms):
        declared.append(arg.proto_arg)

    arguments = ", ".join(declared)

    ## Add the TCG_CALL_NO_RWG_SE flag to helpers that don't take the env
    ## argument and aren't HVX instructions.  Since HVX instructions take
    ## pointers to their arguments, they will have side effects.
    if hex_common.need_env(tag) or hex_common.is_hvx_insn(tag):
        f.write(f"DEF_HELPER_{len(declared) - 1}({tag}, {arguments})\n")
    else:
        f.write(f"DEF_HELPER_FLAGS_{len(declared) - 1}({tag}, "
                f"TCG_CALL_NO_RWG_SE, {arguments})\n")


def main():
    args = hex_common.parse_common_args(
        "Emit helper function prototypes for each instruction"
    )
    tagregs = hex_common.get_tagregs()
    tagimms = hex_common.get_tagimms()

    with open(args.out, "w") as f:
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

            gen_helper_prototype(f, tag, tagregs, tagimms)


if __name__ == "__main__":
    main()
