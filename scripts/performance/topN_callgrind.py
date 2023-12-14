#!/usr/bin/env python3

#  Print the top N most executed functions in QEMU using callgrind.
#  Syntax:
#  topN_callgrind.py [-h] [-n] <number of displayed top functions>  -- \
#           <qemu executable> [<qemu executable options>] \
#           <target executable> [<target executable options>]
#
#  [-h] - Print the script arguments help message.
#  [-n] - Specify the number of top functions to print.
#       - If this flag is not specified, the tool defaults to 25.
#
#  Example of usage:
#  topN_callgrind.py -n 20 -- qemu-arm coulomb_double-arm
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


# Parse the command line arguments
parser = argparse.ArgumentParser(
    usage='topN_callgrind.py [-h] [-n] <number of displayed top functions>  -- '
          '<qemu executable> [<qemu executable options>] '
          '<target executable> [<target executable options>]')

parser.add_argument('-n', dest='top', type=int, default=25,
                    help='Specify the number of top functions to print.')

parser.add_argument('command', type=str, nargs='+', help=argparse.SUPPRESS)

args = parser.parse_args()

# Extract the needed variables from the args
command = args.command
top = args.top

# Insure that valgrind is installed
check_valgrind_presence = subprocess.run(["which", "valgrind"],
                                         stdout=subprocess.DEVNULL)
if check_valgrind_presence.returncode:
    sys.exit("Please install valgrind before running the script!")

# Run callgrind
callgrind = subprocess.run((
    ["valgrind", "--tool=callgrind", "--callgrind-out-file=/tmp/callgrind.data"]
    + command),
    stdout=subprocess.DEVNULL,
    stderr=subprocess.PIPE)
if callgrind.returncode:
    sys.exit(callgrind.stderr.decode("utf-8"))

# Save callgrind_annotate output to /tmp/callgrind_annotate.out
with open("/tmp/callgrind_annotate.out", "w") as output:
    callgrind_annotate = subprocess.run(["callgrind_annotate",
                                         "/tmp/callgrind.data"],
                                        stdout=output,
                                        stderr=subprocess.PIPE)
    if callgrind_annotate.returncode:
        os.unlink('/tmp/callgrind.data')
        output.close()
        os.unlink('/tmp/callgrind_annotate.out')
        sys.exit(callgrind_annotate.stderr.decode("utf-8"))

# Read the callgrind_annotate output to callgrind_data[]
callgrind_data = []
with open('/tmp/callgrind_annotate.out', 'r') as data:
    callgrind_data = data.readlines()

# Line number with the total number of instructions
total_instructions_line_number = 20

# Get the total number of instructions
total_instructions_line_data = callgrind_data[total_instructions_line_number]
total_number_of_instructions = total_instructions_line_data.split(' ')[0]
total_number_of_instructions = int(
    total_number_of_instructions.replace(',', ''))

# Line number with the top function
first_func_line = 25

# Number of functions recorded by callgrind, last two lines are always empty
number_of_functions = len(callgrind_data) - first_func_line - 2

# Limit the number of top functions to "top"
number_of_top_functions = (top if number_of_functions >
                           top else number_of_functions)

# Store the data of the top functions in top_functions[]
top_functions = callgrind_data[first_func_line:
                               first_func_line + number_of_top_functions]

# Print table header
print('{:>4}  {:>10}  {:<30}  {}\n{}  {}  {}  {}'.format('No.',
                                                         'Percentage',
                                                         'Function Name',
                                                         'Source File',
                                                         '-' * 4,
                                                         '-' * 10,
                                                         '-' * 30,
                                                         '-' * 30,
                                                         ))

# Print top N functions
for (index, function) in enumerate(top_functions, start=1):
    function_data = function.split()
    # Calculate function percentage
    function_instructions = float(function_data[0].replace(',', ''))
    function_percentage = (function_instructions /
                           total_number_of_instructions)*100
    # Get function name and source files path
    function_source_file, function_name = function_data[1].split(':')
    # Print extracted data
    print('{:>4}  {:>9.3f}%  {:<30}  {}'.format(index,
                                                round(function_percentage, 3),
                                                function_name,
                                                function_source_file))

# Remove intermediate files
os.unlink('/tmp/callgrind.data')
os.unlink('/tmp/callgrind_annotate.out')
