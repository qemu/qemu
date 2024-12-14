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
import argparse


##
##     Generate data for printing each instruction (format string + operands)
##
def regprinter(m):
    str = m.group(1)
    str += ":".join(["%d"] * len(m.group(2)))
    str += m.group(3)
    if ("S" in m.group(1)) and (len(m.group(2)) == 1):
        str += "/%s"
    elif ("C" in m.group(1)) and (len(m.group(2)) == 1):
        str += "/%s"
    return str


def spacify(s):
    # Regular expression that matches any operator that contains '=' character:
    opswithequal_re = "[-+^&|!<>=]?="
    # Regular expression that matches any assignment operator.
    assignment_re = "[-+^&|]?="

    # Out of the operators that contain the = sign, if the operator is also an
    # assignment, spaces will be added around it, unless it's enclosed within
    # parentheses, or spaces are already present.

    equals = re.compile(opswithequal_re)
    assign = re.compile(assignment_re)

    slen = len(s)
    paren_count = {}
    i = 0
    pc = 0
    while i < slen:
        c = s[i]
        if c == "(":
            pc += 1
        elif c == ")":
            pc -= 1
        paren_count[i] = pc
        i += 1

    # Iterate over all operators that contain the equal sign. If any
    # match is also an assignment operator, add spaces around it if
    # the parenthesis count is 0.
    pos = 0
    out = []
    for m in equals.finditer(s):
        ms = m.start()
        me = m.end()
        # t is the string that matched opswithequal_re.
        t = m.string[ms:me]
        out += s[pos:ms]
        pos = me
        if paren_count[ms] == 0:
            # Check if the entire string t is an assignment.
            am = assign.match(t)
            if am and len(am.group(0)) == me - ms:
                # Don't add spaces if they are already there.
                if ms > 0 and s[ms - 1] != " ":
                    out.append(" ")
                out += t
                if me < slen and s[me] != " ":
                    out.append(" ")
                continue
        # If this is not an assignment, just append it to the output
        # string.
        out += t

    # Append the remaining part of the string.
    out += s[pos : len(s)]
    return "".join(out)


def main():
    parser = argparse.ArgumentParser(
        "Emit opaque macro calls with information for printing string representations of instrucions"
    )
    parser.add_argument("semantics", help="semantics file")
    parser.add_argument("out", help="output file")
    args = parser.parse_args()
    hex_common.read_semantics_file(args.semantics)

    immext_casere = re.compile(r"IMMEXT\(([A-Za-z])")

    with open(args.out, "w") as f:
        for tag in hex_common.tags:
            if not hex_common.behdict[tag]:
                continue
            extendable_upper_imm = False
            extendable_lower_imm = False
            m = immext_casere.search(hex_common.semdict[tag])
            if m:
                if m.group(1).isupper():
                    extendable_upper_imm = True
                else:
                    extendable_lower_imm = True
            beh = hex_common.behdict[tag]
            beh = hex_common.regre.sub(regprinter, beh)
            beh = hex_common.absimmre.sub(r"#%s0x%x", beh)
            beh = hex_common.relimmre.sub(r"PC+%s%d", beh)
            beh = spacify(beh)
            # Print out a literal "%s" at the end, used to match empty string
            # so C won't complain at us
            if "A_VECX" in hex_common.attribdict[tag]:
                macname = "DEF_VECX_PRINTINFO"
            else:
                macname = "DEF_PRINTINFO"
            f.write(f'{macname}({tag},"{beh}%s"')
            regs_or_imms = hex_common.reg_or_immre.findall(hex_common.behdict[tag])
            ri = 0
            seenregs = {}
            for allregs, a, b, c, d, allimm, immlett, bits, immshift in regs_or_imms:
                if a:
                    # register
                    if b in seenregs:
                        regno = seenregs[b]
                    else:
                        regno = ri
                    if len(b) == 1:
                        f.write(f", insn->regno[{regno}]")
                        if "S" in a:
                            f.write(f", sreg2str(insn->regno[{regno}])")
                        elif "C" in a:
                            f.write(f", creg2str(insn->regno[{regno}])")
                    elif len(b) == 2:
                        f.write(f", insn->regno[{regno}] + 1" f", insn->regno[{regno}]")
                    else:
                        print("Put some stuff to handle quads here")
                    if b not in seenregs:
                        seenregs[b] = ri
                        ri += 1
                else:
                    # immediate
                    if immlett.isupper():
                        if extendable_upper_imm:
                            if immlett in "rR":
                                f.write(',insn->extension_valid?"##":""')
                            else:
                                f.write(',insn->extension_valid?"#":""')
                        else:
                            f.write(',""')
                        ii = 1
                    else:
                        if extendable_lower_imm:
                            if immlett in "rR":
                                f.write(',insn->extension_valid?"##":""')
                            else:
                                f.write(',insn->extension_valid?"#":""')
                        else:
                            f.write(',""')
                        ii = 0
                    f.write(f", insn->immed[{ii}]")
            # append empty string so there is at least one more arg
            f.write(',"")\n')


if __name__ == "__main__":
    main()
