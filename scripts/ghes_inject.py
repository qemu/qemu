#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Copyright (C) 2024-2025 Mauro Carvalho Chehab <mchehab+huawei@kernel.org>

"""
Handle ACPI GHESv2 error injection logic QEMU QMP interface.
"""

import argparse
import sys

from arm_processor_error import ArmProcessorEinj

EINJ_DESC = """
Handle ACPI GHESv2 error injection logic QEMU QMP interface.

It allows using UEFI BIOS EINJ features to generate GHES records.

It helps testing CPER and GHES drivers at the guest OS and how
userspace applications at the guest handle them.
"""

def main():
    """Main program"""

    # Main parser - handle generic args like QEMU QMP TCP socket options
    parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter,
                                     usage="%(prog)s [options]",
                                     description=EINJ_DESC)

    g_options = parser.add_argument_group("QEMU QMP socket options")
    g_options.add_argument("-H", "--host", default="localhost", type=str,
                           help="host name")
    g_options.add_argument("-P", "--port", default=4445, type=int,
                           help="TCP port number")
    g_options.add_argument('-d', '--debug', action='store_true')

    subparsers = parser.add_subparsers()

    ArmProcessorEinj(subparsers)

    args = parser.parse_args()
    if "func" in args:
        args.func(args)
    else:
        sys.exit(f"Please specify a valid command for {sys.argv[0]}")

if __name__ == "__main__":
    main()
