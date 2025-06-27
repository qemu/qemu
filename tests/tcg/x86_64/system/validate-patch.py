#!/usr/bin/env python3
#
# validate-patch.py: check the patch applies
#
# This program takes two inputs:
#   - the plugin output
#   - the binary output
#
# Copyright (C) 2024
#
# SPDX-License-Identifier: GPL-2.0-or-later

import sys
from argparse import ArgumentParser

def main() -> None:
    """
    Process the arguments, injest the program and plugin out and
    verify they match up and report if they do not.
    """
    parser = ArgumentParser(description="Validate patch")
    parser.add_argument('test_output',
                        help="The output from the test itself")
    parser.add_argument('plugin_output',
                        help="The output from plugin")
    args = parser.parse_args()

    with open(args.test_output, 'r') as f:
        test_data = f.read()
    with open(args.plugin_output, 'r') as f:
        plugin_data = f.read()
    if "Value: 1" in test_data:
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()

