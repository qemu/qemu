#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Create symbols and mapping files for uftrace.
#
# Copyright 2025 Linaro Ltd
# Author: Pierrick Bouvier <pierrick.bouvier@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import elftools # pip install pyelftools
import os

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection

def elf_func_symbols(elf):
    symbol_tables = [(idx, s) for idx, s in enumerate(elf.iter_sections())
                  if isinstance(s, SymbolTableSection)]
    symbols = []
    for _, section in symbol_tables:
        for _, symbol in enumerate(section.iter_symbols()):
            if symbol_size(symbol) == 0:
                continue
            type = symbol['st_info']['type']
            if type == 'STT_FUNC' or type == 'STT_NOTYPE':
                symbols.append(symbol)
    symbols.sort(key = lambda x: symbol_addr(x))
    return symbols

def symbol_size(symbol):
    return symbol['st_size']

def symbol_addr(symbol):
    addr = symbol['st_value']
    # clamp addr to 48 bits, like uftrace entries
    return addr & 0xffffffffffff

def symbol_name(symbol):
    return symbol.name

class BinaryFile:
    def __init__(self, path, map_offset):
        self.fullpath = os.path.realpath(path)
        self.map_offset = map_offset
        with open(path, 'rb') as f:
            self.elf = ELFFile(f)
            self.symbols = elf_func_symbols(self.elf)

    def path(self):
        return self.fullpath

    def addr_start(self):
        return self.map_offset

    def addr_end(self):
        last_sym = self.symbols[-1]
        return symbol_addr(last_sym) + symbol_size(last_sym) + self.map_offset

    def generate_symbol_file(self, prefix_symbols):
        binary_name = os.path.basename(self.fullpath)
        sym_file_path = f'./uftrace.data/{binary_name}.sym'
        print(f'{sym_file_path} ({len(self.symbols)} symbols)')
        with open(sym_file_path, 'w') as sym_file:
            # print hexadecimal addresses on 48 bits
            addrx = "0>12x"
            for s in self.symbols:
                addr = symbol_addr(s)
                addr = f'{addr:{addrx}}'
                size = f'{symbol_size(s):{addrx}}'
                name = symbol_name(s)
                if prefix_symbols:
                    name = f'{binary_name}:{name}'
                print(addr, size, 'T', name, file=sym_file)

def parse_parameter(p):
    s = p.split(":")
    path = s[0]
    if len(s) == 1:
        return path, 0
    if len(s) > 2:
        raise ValueError('only one offset can be set')
    offset = s[1]
    if not offset.startswith('0x'):
        err = f'offset "{offset}" is not an hexadecimal constant. '
        err += 'It should starts with "0x".'
        raise ValueError(err)
    offset = int(offset, 16)
    return path, offset

def is_from_user_mode(map_file_path):
    if os.path.exists(map_file_path):
        with open(map_file_path, 'r') as map_file:
            if not map_file.readline().startswith('# map stack on'):
                return True
    return False

def generate_map(binaries):
    map_file_path = './uftrace.data/sid-0.map'

    if is_from_user_mode(map_file_path):
        print(f'do not overwrite {map_file_path} generated from qemu-user')
        return

    mappings = []

    # print hexadecimal addresses on 48 bits
    addrx = "0>12x"

    mappings += ['# map stack on highest address possible, to prevent uftrace']
    mappings += ['# from considering any kernel address']
    mappings += ['ffffffffffff-ffffffffffff rw-p 00000000 00:00 0 [stack]']

    for b in binaries:
        m = f'{b.addr_start():{addrx}}-{b.addr_end():{addrx}}'
        m += f' r--p 00000000 00:00 0 {b.path()}'
        mappings.append(m)

    with open(map_file_path, 'w') as map_file:
        print('\n'.join(mappings), file=map_file)
    print(f'{map_file_path}')
    print('\n'.join(mappings))

def main():
    parser = argparse.ArgumentParser(description=
                                     'generate symbol files for uftrace')
    parser.add_argument('elf_file', nargs='+',
                        help='path to an ELF file. '
                        'Use /path/to/file:0xdeadbeef to add a mapping offset.')
    parser.add_argument('--prefix-symbols',
                        help='prepend binary name to symbols',
                        action=argparse.BooleanOptionalAction)
    args = parser.parse_args()

    if not os.path.exists('./uftrace.data'):
        os.mkdir('./uftrace.data')

    binaries = []
    for file in args.elf_file:
        path, offset = parse_parameter(file)
        b = BinaryFile(path, offset)
        binaries.append(b)
    binaries.sort(key = lambda b: b.addr_end());

    for b in binaries:
        b.generate_symbol_file(args.prefix_symbols)

    generate_map(binaries)

if __name__ == '__main__':
    main()
