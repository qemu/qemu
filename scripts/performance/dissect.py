#!/usr/bin/env python3

#  Print the percentage of instructions spent in each phase of QEMU
#  execution.
#
#  Syntax:
#  dissect.py [-h] -- <qemu executable> [<qemu executable options>] \
#                   <target executable> [<target executable options>]
#
#  [-h] - Print the script arguments help message.
#
#  Example of usage:
#  dissect.py -- qemu-arm coulomb_double-arm
#
#  This file is a part of the project "TCG Continuous Benchmarking".
#
#  Copyright (C) 2020  Ahmed Karaman <ahmedkhaledkaraman@gmail.com>
#  Copyright (C) 2020  Aleksandar Markovic <aleksandar.qemu.devel@gmail.com>
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program. If not, see <https://www.gnu.org/licenses/>.

import argparse
import os
import subprocess
import sys
import tempfile


def get_JIT_line(callgrind_data):
    """
    Search for the first instance of the JIT call in
    the callgrind_annotate output when ran using --tree=caller
    This is equivalent to the self number of instructions of JIT.

    Parameters:
    callgrind_data (list): callgrind_annotate output

    Returns:
    (int): Line number
    """
    line = -1
    for i in range(len(callgrind_data)):
        if callgrind_data[i].strip('\n') and \
                callgrind_data[i].split()[-1] == "[???]":
            line = i
            break
    if line == -1:
        sys.exit("Couldn't locate the JIT call ... Exiting.")
    return line


def main():
    # Parse the command line arguments
    parser = argparse.ArgumentParser(
        usage='dissect.py [-h] -- '
        '<qemu executable> [<qemu executable options>] '
        '<target executable> [<target executable options>]')

    parser.add_argument('command', type=str, nargs='+', help=argparse.SUPPRESS)

    args = parser.parse_args()

    # Extract the needed variables from the args
    command = args.command

    # Insure that valgrind is installed
    check_valgrind = subprocess.run(
        ["which", "valgrind"], stdout=subprocess.DEVNULL)
    if check_valgrind.returncode:
        sys.exit("Please install valgrind before running the script.")

    # Save all intermediate files in a temporary directory
    with tempfile.TemporaryDirectory() as tmpdirname:
        # callgrind output file path
        data_path = os.path.join(tmpdirname, "callgrind.data")
        # callgrind_annotate output file path
        annotate_out_path = os.path.join(tmpdirname, "callgrind_annotate.out")

        # Run callgrind
        callgrind = subprocess.run((["valgrind",
                                     "--tool=callgrind",
                                     "--callgrind-out-file=" + data_path]
                                    + command),
                                   stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE)
        if callgrind.returncode:
            sys.exit(callgrind.stderr.decode("utf-8"))

        # Save callgrind_annotate output
        with open(annotate_out_path, "w") as output:
            callgrind_annotate = subprocess.run(
                ["callgrind_annotate", data_path, "--tree=caller"],
                stdout=output,
                stderr=subprocess.PIPE)
            if callgrind_annotate.returncode:
                sys.exit(callgrind_annotate.stderr.decode("utf-8"))

        # Read the callgrind_annotate output to callgrind_data[]
        callgrind_data = []
        with open(annotate_out_path, 'r') as data:
            callgrind_data = data.readlines()

        # Line number with the total number of instructions
        total_instructions_line_number = 20
        # Get the total number of instructions
        total_instructions_line_data = \
            callgrind_data[total_instructions_line_number]
        total_instructions = total_instructions_line_data.split()[0]
        total_instructions = int(total_instructions.replace(',', ''))

        # Line number with the JIT self number of instructions
        JIT_self_instructions_line_number = get_JIT_line(callgrind_data)
        # Get the JIT self number of instructions
        JIT_self_instructions_line_data = \
            callgrind_data[JIT_self_instructions_line_number]
        JIT_self_instructions = JIT_self_instructions_line_data.split()[0]
        JIT_self_instructions = int(JIT_self_instructions.replace(',', ''))

        # Line number with the JIT self + inclusive number of instructions
        # It's the line above the first JIT call when running with --tree=caller
        JIT_total_instructions_line_number = JIT_self_instructions_line_number-1
        # Get the JIT self + inclusive number of instructions
        JIT_total_instructions_line_data = \
            callgrind_data[JIT_total_instructions_line_number]
        JIT_total_instructions = JIT_total_instructions_line_data.split()[0]
        JIT_total_instructions = int(JIT_total_instructions.replace(',', ''))

        # Calculate number of instructions in helpers and code generation
        helpers_instructions = JIT_total_instructions-JIT_self_instructions
        code_generation_instructions = total_instructions-JIT_total_instructions

        # Print results (Insert commas in large numbers)
        # Print total number of instructions
        print('{:<20}{:>20}\n'.
              format("Total Instructions:",
                     format(total_instructions, ',')))
        # Print code generation instructions and percentage
        print('{:<20}{:>20}\t{:>6.3f}%'.
              format("Code Generation:",
                     format(code_generation_instructions, ","),
                     (code_generation_instructions / total_instructions) * 100))
        # Print JIT instructions and percentage
        print('{:<20}{:>20}\t{:>6.3f}%'.
              format("JIT Execution:",
                     format(JIT_self_instructions, ","),
                     (JIT_self_instructions / total_instructions) * 100))
        # Print helpers instructions and percentage
        print('{:<20}{:>20}\t{:>6.3f}%'.
              format("Helpers:",
                     format(helpers_instructions, ","),
                     (helpers_instructions/total_instructions)*100))


if __name__ == "__main__":
    main()
