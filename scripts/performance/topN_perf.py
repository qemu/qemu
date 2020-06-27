#!/usr/bin/env python3

#  Print the top N most executed functions in QEMU using perf.
#  Syntax:
#  topN_perf.py [-h] [-n] <number of displayed top functions>  -- \
#           <qemu executable> [<qemu executable options>] \
#           <target executable> [<target execurable options>]
#
#  [-h] - Print the script arguments help message.
#  [-n] - Specify the number of top functions to print.
#       - If this flag is not specified, the tool defaults to 25.
#
#  Example of usage:
#  topN_perf.py -n 20 -- qemu-arm coulomb_double-arm
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
    usage='topN_perf.py [-h] [-n] <number of displayed top functions >  -- '
          '<qemu executable> [<qemu executable options>] '
          '<target executable> [<target executable options>]')

parser.add_argument('-n', dest='top', type=int, default=25,
                    help='Specify the number of top functions to print.')

parser.add_argument('command', type=str, nargs='+', help=argparse.SUPPRESS)

args = parser.parse_args()

# Extract the needed variables from the args
command = args.command
top = args.top

# Insure that perf is installed
check_perf_presence = subprocess.run(["which", "perf"],
                                     stdout=subprocess.DEVNULL)
if check_perf_presence.returncode:
    sys.exit("Please install perf before running the script!")

# Insure user has previllage to run perf
check_perf_executability = subprocess.run(["perf", "stat", "ls", "/"],
                                          stdout=subprocess.DEVNULL,
                                          stderr=subprocess.DEVNULL)
if check_perf_executability.returncode:
    sys.exit(
"""
Error:
You may not have permission to collect stats.

Consider tweaking /proc/sys/kernel/perf_event_paranoid,
which controls use of the performance events system by
unprivileged users (without CAP_SYS_ADMIN).

  -1: Allow use of (almost) all events by all users
      Ignore mlock limit after perf_event_mlock_kb without CAP_IPC_LOCK
   0: Disallow ftrace function tracepoint by users without CAP_SYS_ADMIN
      Disallow raw tracepoint access by users without CAP_SYS_ADMIN
   1: Disallow CPU event access by users without CAP_SYS_ADMIN
   2: Disallow kernel profiling by users without CAP_SYS_ADMIN

To make this setting permanent, edit /etc/sysctl.conf too, e.g.:
   kernel.perf_event_paranoid = -1

* Alternatively, you can run this script under sudo privileges.
"""
)

# Run perf record
perf_record = subprocess.run((["perf", "record", "--output=/tmp/perf.data"] +
                              command),
                             stdout=subprocess.DEVNULL,
                             stderr=subprocess.PIPE)
if perf_record.returncode:
    os.unlink('/tmp/perf.data')
    sys.exit(perf_record.stderr.decode("utf-8"))

# Save perf report output to /tmp/perf_report.out
with open("/tmp/perf_report.out", "w") as output:
    perf_report = subprocess.run(
        ["perf", "report", "--input=/tmp/perf.data", "--stdio"],
        stdout=output,
        stderr=subprocess.PIPE)
    if perf_report.returncode:
        os.unlink('/tmp/perf.data')
        output.close()
        os.unlink('/tmp/perf_report.out')
        sys.exit(perf_report.stderr.decode("utf-8"))

# Read the reported data to functions[]
functions = []
with open("/tmp/perf_report.out", "r") as data:
    # Only read lines that are not comments (comments start with #)
    # Only read lines that are not empty
    functions = [line for line in data.readlines() if line and line[0]
                 != '#' and line[0] != "\n"]

# Limit the number of top functions to "top"
number_of_top_functions = top if len(functions) > top else len(functions)

# Store the data of the top functions in top_functions[]
top_functions = functions[:number_of_top_functions]

# Print table header
print('{:>4}  {:>10}  {:<30}  {}\n{}  {}  {}  {}'.format('No.',
                                                         'Percentage',
                                                         'Name',
                                                         'Invoked by',
                                                         '-' * 4,
                                                         '-' * 10,
                                                         '-' * 30,
                                                         '-' * 25))

# Print top N functions
for (index, function) in enumerate(top_functions, start=1):
    function_data = function.split()
    function_percentage = function_data[0]
    function_name = function_data[-1]
    function_invoker = ' '.join(function_data[2:-2])
    print('{:>4}  {:>10}  {:<30}  {}'.format(index,
                                             function_percentage,
                                             function_name,
                                             function_invoker))

# Remove intermediate files
os.unlink('/tmp/perf.data')
os.unlink('/tmp/perf_report.out')
