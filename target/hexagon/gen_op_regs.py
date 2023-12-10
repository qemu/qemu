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
##     Generate the register and immediate operands for each instruction
##
def calculate_regid_reg(tag):
    def letter_inc(x):
        return chr(ord(x) + 1)

    ordered_implregs = ["SP", "FP", "LR"]
    srcdst_lett = "X"
    src_lett = "S"
    dst_lett = "D"
    retstr = ""
    mapdict = {}
    for reg in ordered_implregs:
        reg_rd = 0
        reg_wr = 0
        if ("A_IMPLICIT_WRITES_" + reg) in hex_common.attribdict[tag]:
            reg_wr = 1
        if reg_rd and reg_wr:
            retstr += srcdst_lett
            mapdict[srcdst_lett] = reg
            srcdst_lett = letter_inc(srcdst_lett)
        elif reg_rd:
            retstr += src_lett
            mapdict[src_lett] = reg
            src_lett = letter_inc(src_lett)
        elif reg_wr:
            retstr += dst_lett
            mapdict[dst_lett] = reg
            dst_lett = letter_inc(dst_lett)
    return retstr, mapdict


def calculate_regid_letters(tag):
    retstr, mapdict = calculate_regid_reg(tag)
    return retstr


def strip_reg_prefix(x):
    y = x.replace("UREG.", "")
    y = y.replace("MREG.", "")
    return y.replace("GREG.", "")


def main():
    hex_common.read_semantics_file(sys.argv[1])
    hex_common.read_attribs_file(sys.argv[2])
    hex_common.init_registers()
    tagregs = hex_common.get_tagregs(full=True)
    tagimms = hex_common.get_tagimms()

    with open(sys.argv[3], "w") as f:
        for tag in hex_common.tags:
            regs = tagregs[tag]
            rregs = []
            wregs = []
            regids = ""
            for regtype, regid, _, numregs in regs:
                reg = hex_common.get_register(tag, regtype, regid)
                if reg.is_read():
                    if regid[0] not in regids:
                        regids += regid[0]
                    rregs.append(regtype + regid + numregs)
                if reg.is_written():
                    wregs.append(regtype + regid + numregs)
                    if regid[0] not in regids:
                        regids += regid[0]
            for attrib in hex_common.attribdict[tag]:
                if hex_common.attribinfo[attrib]["rreg"]:
                    rregs.append(strip_reg_prefix(attribinfo[attrib]["rreg"]))
                if hex_common.attribinfo[attrib]["wreg"]:
                    wregs.append(strip_reg_prefix(attribinfo[attrib]["wreg"]))
            regids += calculate_regid_letters(tag)
            f.write(
                f'REGINFO({tag},"{regids}",\t/*RD:*/\t"{",".join(rregs)}",'
                f'\t/*WR:*/\t"{",".join(wregs)}")\n'
            )

        for tag in hex_common.tags:
            imms = tagimms[tag]
            f.write(f"IMMINFO({tag}")
            if not imms:
                f.write(""",'u',0,0,'U',0,0""")
            for sign, size, shamt in imms:
                if sign == "r":
                    sign = "s"
                if not shamt:
                    shamt = "0"
                f.write(f""",'{sign}',{size},{shamt}""")
            if len(imms) == 1:
                if sign.isupper():
                    myu = "u"
                else:
                    myu = "U"
                f.write(f""",'{myu}',0,0""")
            f.write(")\n")


if __name__ == "__main__":
    main()
