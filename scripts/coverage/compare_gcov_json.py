#!/usr/bin/env python3
#
# Compare output of two gcovr JSON reports and report differences. To
# generate the required output first:
#   - create two build dirs with --enable-gcov
#   - run set of tests in each
#   - run make coverage-html in each
#   - run gcovr --json --exclude-unreachable-branches \
#           --print-summary -o coverage.json --root ../../ . *.p
#
# Author: Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later
#

import argparse
import json
import sys
from pathlib import Path

def create_parser():
    parser = argparse.ArgumentParser(
        prog='compare_gcov_json',
        description='analyse the differences in coverage between two runs')

    parser.add_argument('-a', type=Path, default=None,
                        help=('First file to check'))

    parser.add_argument('-b', type=Path, default=None,
                        help=('Second file to check'))

    parser.add_argument('--verbose', action='store_true', default=False,
                        help=('A minimal verbosity level that prints the '
                              'overall result of the check/wait'))
    return parser


# See https://gcovr.com/en/stable/output/json.html#json-format-reference
def load_json(json_file_path: Path, verbose = False) -> dict[str, set[int]]:

    with open(json_file_path) as f:
        data = json.load(f)

    root_dir = json_file_path.absolute().parent
    covered_lines = dict()

    for filecov in data["files"]:
        file_path = Path(filecov["file"])

        # account for generated files - map into src tree
        resolved_path = Path(file_path).absolute()
        if resolved_path.is_relative_to(root_dir):
            file_path = resolved_path.relative_to(root_dir)
            # print(f"remapped {resolved_path} to {file_path}")

        lines = filecov["lines"]

        executed_lines = set(
            linecov["line_number"]
            for linecov in filecov["lines"]
            if linecov["count"] != 0 and not linecov["gcovr/noncode"]
        )

        # if this file has any coverage add it to the system
        if len(executed_lines) > 0:
            if verbose:
                print(f"file {file_path} {len(executed_lines)}/{len(lines)}")
            covered_lines[str(file_path)] = executed_lines

    return covered_lines

def find_missing_files(first, second):
    """
    Return a list of files not covered in the second set
    """
    missing_files = []
    for f in sorted(first):
        file_a = first[f]
        try:
            file_b = second[f]
        except KeyError:
            missing_files.append(f)

    return missing_files

def main():
    """
    Script entry point
    """
    parser = create_parser()
    args = parser.parse_args()

    if not args.a or not args.b:
        print("We need two files to compare")
        sys.exit(1)

    first_coverage = load_json(args.a, args.verbose)
    second_coverage = load_json(args.b, args.verbose)

    first_missing = find_missing_files(first_coverage,
                                       second_coverage)

    second_missing = find_missing_files(second_coverage,
                                        first_coverage)

    a_name = args.a.parent.name
    b_name = args.b.parent.name

    print(f"{b_name} missing coverage in {len(first_missing)} files")
    for f in first_missing:
        print(f"  {f}")

    print(f"{a_name} missing coverage in {len(second_missing)} files")
    for f in second_missing:
        print(f"  {f}")


if __name__ == '__main__':
    main()
