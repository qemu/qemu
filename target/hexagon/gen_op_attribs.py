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


def main():
    parser = argparse.ArgumentParser(
        "Emit opaque macro calls containing instruction attributes"
    )
    parser.add_argument("semantics", help="semantics file")
    parser.add_argument("out", help="output file")
    args = parser.parse_args()
    hex_common.read_semantics_file(args.semantics)
    hex_common.calculate_attribs()

    ##
    ##     Generate all the attributes associated with each instruction
    ##
    with open(args.out, "w") as f:
        for tag in hex_common.tags:
            f.write(
                f"OP_ATTRIB({tag},ATTRIBS("
                f'{",".join(sorted(hex_common.attribdict[tag]))}))\n'
            )


if __name__ == "__main__":
    main()
