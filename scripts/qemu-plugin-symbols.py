#!/usr/bin/env python3
#
# Extract QEMU Plugin API symbols from a header file
#
# Copyright 2024 Linaro Ltd
#
# Author: Pierrick Bouvier <pierrick.bouvier@linaro.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import re

def extract_symbols(plugin_header):
    with open(plugin_header) as file:
        content = file.read()
    # Remove QEMU_PLUGIN_API macro definition.
    content = content.replace('#define QEMU_PLUGIN_API', '')
    expected = content.count('QEMU_PLUGIN_API')
    # Find last word between QEMU_PLUGIN_API and (, matching on several lines.
    # We use *? non-greedy quantifier.
    syms = re.findall(r'QEMU_PLUGIN_API.*?(\w+)\s*\(', content, re.DOTALL)
    syms.sort()
    # Ensure we found as many symbols as API markers.
    assert len(syms) == expected
    return syms

def main() -> None:
    parser = argparse.ArgumentParser(description='Extract QEMU plugin symbols')
    parser.add_argument('plugin_header', help='Path to QEMU plugin header.')
    args = parser.parse_args()

    syms = extract_symbols(args.plugin_header)

    print('{')
    for s in syms:
        print("  {};".format(s))
    print('};')

if __name__ == '__main__':
    main()
