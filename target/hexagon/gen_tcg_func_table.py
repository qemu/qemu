#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: GPL-2.0-or-later

import hex_common


def main():
    args = hex_common.parse_common_args(
        "Emit the table of instruction translation functions"
    )

    with open(args.out, "w") as f:
        f.write("#ifndef HEXAGON_FUNC_TABLE_H\n")
        f.write("#define HEXAGON_FUNC_TABLE_H\n\n")
        f.write("const SemanticInsn opcode_genptr[XX_LAST_OPCODE] = {\n")

        for tag in hex_common.tags:
            if hex_common.tag_ignore(tag):
                continue
            f.write(f"    [{tag}] = generate_{tag},\n")

        f.write("};\n\n")
        f.write("#endif    /* HEXAGON_FUNC_TABLE_H */\n")


if __name__ == "__main__":
    main()
