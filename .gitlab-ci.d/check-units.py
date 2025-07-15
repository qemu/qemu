#!/usr/bin/env python3
#
# check-units.py: check the number of compilation units and identify
#                 those that are rebuilt multiple times
#
# Copyright (C) 2025 Linaro Ltd.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from os import access, R_OK, path
from sys import exit
import json
import argparse
from pathlib import Path
from collections import Counter


def extract_build_units(cc_path):
    """
    Extract the build units and their counds from compile_commands.json file.

    Returns:
        Hash table of ["unit"] = count
    """

    j = json.load(open(cc_path, 'r'))
    files = [f['file'] for f in j]
    build_units = Counter(files)

    return build_units


def analyse_units(build_units, top_n):
    """
    Analyse the build units and report stats and the top 10 rebuilds
    """

    print(f"Total source files: {len(build_units.keys())}")
    print(f"Total build units: {sum(units.values())}")

    # Create a sorted list by number of rebuilds
    sorted_build_units = sorted(build_units.items(),
                                key=lambda item: item[1],
                                reverse=True)

    print("Most rebuilt units:")
    for unit, count in sorted_build_units[:top_n]:
        print(f"  {unit} built {count} times")

    print("Least rebuilt units:")
    for unit, count in sorted_build_units[-10:]:
        print(f"  {unit} built {count} times")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="analyse number of build units in compile_commands.json")
    parser.add_argument("cc_path", type=Path, default=None,
                        help="Path to compile_commands.json")
    parser.add_argument("-n", type=int, default=20,
                        help="Dump the top <n> entries")

    args = parser.parse_args()

    if path.isfile(args.cc_path) and access(args.cc_path, R_OK):
        units = extract_build_units(args.cc_path)
        analyse_units(units, args.n)
        exit(0)
    else:
        print(f"{args.cc_path} doesn't exist or isn't readable")
        exit(1)
