#!/usr/bin/env python3

##
##  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
## Helpers for gen_helper_function
##
def gen_decl_ea(f):
    f.write("    uint32_t EA;\n")

def gen_helper_return_type(f,regtype,regid,regno):
    if regno > 1 : f.write(", ")
    f.write("int32_t")

def gen_helper_return_type_pair(f,regtype,regid,regno):
    if regno > 1 : f.write(", ")
    f.write("int64_t")

def gen_helper_arg(f,regtype,regid,regno):
    if regno > 0 : f.write(", " )
    f.write("int32_t %s%sV" % (regtype,regid))

def gen_helper_arg_new(f,regtype,regid,regno):
    if regno >= 0 : f.write(", " )
    f.write("int32_t %s%sN" % (regtype,regid))

def gen_helper_arg_pair(f,regtype,regid,regno):
    if regno >= 0 : f.write(", ")
    f.write("int64_t %s%sV" % (regtype,regid))

def gen_helper_arg_opn(f,regtype,regid,i,tag):
    if (hex_common.is_pair(regid)):
        gen_helper_arg_pair(f,regtype,regid,i)
    elif (hex_common.is_single(regid)):
        if hex_common.is_old_val(regtype, regid, tag):
            gen_helper_arg(f,regtype,regid,i)
        elif hex_common.is_new_val(regtype, regid, tag):
            gen_helper_arg_new(f,regtype,regid,i)
        else:
            print("Bad register parse: ",regtype,regid,toss,numregs)
    else:
        print("Bad register parse: ",regtype,regid,toss,numregs)

def gen_helper_arg_imm(f,immlett):
    f.write(", int32_t %s" % (hex_common.imm_name(immlett)))

def gen_helper_dest_decl(f,regtype,regid,regno,subfield=""):
    f.write("    int32_t %s%sV%s = 0;\n" % \
        (regtype,regid,subfield))

def gen_helper_dest_decl_pair(f,regtype,regid,regno,subfield=""):
    f.write("    int64_t %s%sV%s = 0;\n" % \
        (regtype,regid,subfield))

def gen_helper_dest_decl_opn(f,regtype,regid,i):
    if (hex_common.is_pair(regid)):
        gen_helper_dest_decl_pair(f,regtype,regid,i)
    elif (hex_common.is_single(regid)):
        gen_helper_dest_decl(f,regtype,regid,i)
    else:
        print("Bad register parse: ",regtype,regid,toss,numregs)

def gen_helper_return(f,regtype,regid,regno):
    f.write("    return %s%sV;\n" % (regtype,regid))

def gen_helper_return_pair(f,regtype,regid,regno):
    f.write("    return %s%sV;\n" % (regtype,regid))

def gen_helper_return_opn(f, regtype, regid, i):
    if (hex_common.is_pair(regid)):
        gen_helper_return_pair(f,regtype,regid,i)
    elif (hex_common.is_single(regid)):
        gen_helper_return(f,regtype,regid,i)
    else:
        print("Bad register parse: ",regtype,regid,toss,numregs)

##
## Generate the TCG code to call the helper
##     For A2_add: Rd32=add(Rs32,Rt32), { RdV=RsV+RtV;}
##     We produce:
##       int32_t HELPER(A2_add)(CPUHexagonState *env, int32_t RsV, int32_t RtV)
##       {
##           uint32_t slot __attribute__(unused)) = 4;
##           int32_t RdV = 0;
##           { RdV=RsV+RtV;}
##           COUNT_HELPER(A2_add);
##           return RdV;
##       }
##
def gen_helper_function(f, tag, tagregs, tagimms):
    regs = tagregs[tag]
    imms = tagimms[tag]

    numresults = 0
    numscalarresults = 0
    numscalarreadwrite = 0
    for regtype,regid,toss,numregs in regs:
        if (hex_common.is_written(regid)):
            numresults += 1
            if (hex_common.is_scalar_reg(regtype)):
                numscalarresults += 1
        if (hex_common.is_readwrite(regid)):
            if (hex_common.is_scalar_reg(regtype)):
                numscalarreadwrite += 1

    if (numscalarresults > 1):
        ## The helper is bogus when there is more than one result
        f.write("void HELPER(%s)(CPUHexagonState *env) { BOGUS_HELPER(%s); }\n"
                % (tag, tag))
    else:
        ## The return type of the function is the type of the destination
        ## register
        i=0
        for regtype,regid,toss,numregs in regs:
            if (hex_common.is_written(regid)):
                if (hex_common.is_pair(regid)):
                    gen_helper_return_type_pair(f,regtype,regid,i)
                elif (hex_common.is_single(regid)):
                    gen_helper_return_type(f,regtype,regid,i)
                else:
                    print("Bad register parse: ",regtype,regid,toss,numregs)
            i += 1

        if (numscalarresults == 0):
            f.write("void")
        f.write(" HELPER(%s)(CPUHexagonState *env" % tag)

        i = 1

        ## Arguments to the helper function are the source regs and immediates
        for regtype,regid,toss,numregs in regs:
            if (hex_common.is_read(regid)):
                gen_helper_arg_opn(f,regtype,regid,i,tag)
                i += 1
        for immlett,bits,immshift in imms:
            gen_helper_arg_imm(f,immlett)
            i += 1
        if hex_common.need_slot(tag):
            if i > 0: f.write(", ")
            f.write("uint32_t slot")
            i += 1
        if hex_common.need_part1(tag):
            if i > 0: f.write(", ")
            f.write("uint32_t part1")
        f.write(")\n{\n")
        if (not hex_common.need_slot(tag)):
            f.write("    uint32_t slot __attribute__((unused)) = 4;\n" )
        if hex_common.need_ea(tag): gen_decl_ea(f)
        ## Declare the return variable
        i=0
        for regtype,regid,toss,numregs in regs:
            if (hex_common.is_writeonly(regid)):
                gen_helper_dest_decl_opn(f,regtype,regid,i)
            i += 1

        if 'A_FPOP' in hex_common.attribdict[tag]:
            f.write('    arch_fpop_start(env);\n');

        f.write("    %s\n" % hex_common.semdict[tag])

        if 'A_FPOP' in hex_common.attribdict[tag]:
            f.write('    arch_fpop_end(env);\n');

        ## Save/return the return variable
        for regtype,regid,toss,numregs in regs:
            if (hex_common.is_written(regid)):
                gen_helper_return_opn(f, regtype, regid, i)
        f.write("}\n\n")
        ## End of the helper definition

def main():
    hex_common.read_semantics_file(sys.argv[1])
    hex_common.read_attribs_file(sys.argv[2])
    hex_common.read_overrides_file(sys.argv[3])
    hex_common.calculate_attribs()
    tagregs = hex_common.get_tagregs()
    tagimms = hex_common.get_tagimms()

    with open(sys.argv[4], 'w') as f:
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
            if ( hex_common.skip_qemu_helper(tag) ):
                continue

            gen_helper_function(f, tag, tagregs, tagimms)

if __name__ == "__main__":
    main()
