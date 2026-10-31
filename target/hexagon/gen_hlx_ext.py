#!/usr/bin/env python3

##
##  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
##  SPDX-License-Identifier: GPL-2.0-or-later
##

"""Process HLX extension instruction definitions."""

import sys

def main():
    """Copy preprocessed HLX instruction definitions."""
    input_file = sys.argv[1]
    output_file = sys.argv[2]

    with open(input_file, "rt") as infile:
        with open(output_file, "w") as outfile:
            for line in infile:
                # Skip preprocessor directives.
                if not line.startswith("#"):
                    outfile.write(line)

if __name__ == "__main__":
    main()
