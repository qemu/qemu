#!/usr/bin/env python3
#
# Copyright (C) 2023 Red Hat, Inc.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import sys
import os


def main(args):
    file_path = args[1]
    basename = os.path.basename(file_path)
    varname = basename.replace('-', '_').replace('.', '_')

    with os.fdopen(sys.stdout.fileno(), "wt", closefd=False, newline='\n') as stdout:
        with open(file_path, "r", encoding='utf-8') as file:
            print(f'static GLchar {varname}_src[] =', file=stdout)
            for line in file:
                line = line.rstrip()
                print(f'    "{line}\\n"', file=stdout)
            print('    "\\n";', file=stdout)


if __name__ == '__main__':
    sys.exit(main(sys.argv))
