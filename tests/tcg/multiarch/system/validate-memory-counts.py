#!/usr/bin/env python3
#
# validate-memory-counts.py: check we instrumented memory properly
#
# This program takes two inputs:
#   - the mem plugin output
#   - the memory binary output
#
# Copyright (C) 2024 Linaro Ltd
#
# SPDX-License-Identifier: GPL-2.0-or-later

import sys
from argparse import ArgumentParser

def extract_counts(path):
    """
    Load the output from path and extract the lines containing:

      Test data start: 0x40214000
      Test data end: 0x40218001
      Test data read: 2522280
      Test data write: 262111

    From the stream of data. Extract the values for use in the
    validation function.
    """
    start_address = None
    end_address = None
    read_count = 0
    write_count = 0
    with open(path, 'r') as f:
        for line in f:
            if line.startswith("Test data start:"):
                start_address = int(line.split(':')[1].strip(), 16)
            elif line.startswith("Test data end:"):
                end_address = int(line.split(':')[1].strip(), 16)
            elif line.startswith("Test data read:"):
                read_count = int(line.split(':')[1].strip())
            elif line.startswith("Test data write:"):
                write_count = int(line.split(':')[1].strip())
    return start_address, end_address, read_count, write_count


def parse_plugin_output(path, start, end):
    """
    Load the plugin output from path in the form of:

      Region Base, Reads, Writes, Seen all
      0x0000000040004000, 31093, 0, false
      0x0000000040214000, 2522280, 278579, true
      0x0000000040000000, 137398, 0, false
      0x0000000040210000, 54727397, 33721956, false

    And extract the ranges that match test data start and end and
    return the results.
    """
    total_reads = 0
    total_writes = 0
    seen_all = False

    with open(path, 'r') as f:
        next(f)  # Skip the header
        for line in f:

            if line.startswith("Region Base"):
                continue

            parts = line.strip().split(', ')
            if len(parts) != 4:
                continue

            region_base = int(parts[0], 16)
            reads = int(parts[1])
            writes = int(parts[2])

            if start <= region_base < end: # Checking if within range
                total_reads += reads
                total_writes += writes
                seen_all = parts[3] == "true"

    return total_reads, total_writes, seen_all

def main() -> None:
    """
    Process the arguments, injest the program and plugin out and
    verify they match up and report if they do not.
    """
    parser = ArgumentParser(description="Validate memory instrumentation")
    parser.add_argument('test_output',
                        help="The output from the test itself")
    parser.add_argument('plugin_output',
                        help="The output from memory plugin")
    parser.add_argument('--bss-cleared',
                        action='store_true',
                        help='Assume bss was cleared (and adjusts counts).')

    args = parser.parse_args()

    # Extract counts from memory binary
    start, end, exp_reads, exp_writes = extract_counts(args.test_output)

    # Some targets clear BSS before running but the test doesn't know
    # that so we adjust it by the size of the test region.
    if args.bss_cleared:
        exp_writes += 16384

    if start is None or end is None:
        print("Failed to test_data boundaries from output.")
        sys.exit(1)

    # Parse plugin output
    preads, pwrites, seen_all = parse_plugin_output(args.plugin_output,
                                                    start, end)

    if not seen_all:
        print("Fail: didn't instrument all accesses to test_data.")
        sys.exit(1)

    # Compare and report
    if preads == exp_reads and pwrites == exp_writes:
        sys.exit(0)
    else:
        print("Fail: The memory reads and writes count does not match.")
        print(f"Expected Reads: {exp_reads}, Actual Reads: {preads}")
        print(f"Expected Writes: {exp_writes}, Actual Writes: {pwrites}")
        sys.exit(1)

if __name__ == "__main__":
    main()
