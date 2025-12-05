#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Create symbols, debug and mapping files for uftrace.
#
# Copyright 2025 Linaro Ltd
# Author: Pierrick Bouvier <pierrick.bouvier@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import os
import subprocess

class Symbol:
    def __init__(self, name, addr, size):
        self.name = name
        # clamp addr to 48 bits, like uftrace entries
        self.addr = addr & 0xffffffffffff
        self.full_addr = addr
        self.size = size

    def set_loc(self, file, line):
        self.file = file
        self.line = line

def get_symbols(elf_file):
    symbols=[]
    try:
        out = subprocess.check_output(['nm', '--print-size', elf_file],
                                      stderr=subprocess.STDOUT,
                                      text=True)
    except subprocess.CalledProcessError as e:
        print(e.output)
        raise
    out = out.strip().split('\n')
    for line in out:
        info = line.split(' ')
        if len(info) == 3:
            # missing size information
            continue
        addr, size, type, name = info
        # add only symbols from .text section
        if type.lower() != 't':
            continue
        addr = int(addr, 16)
        size = int(size, 16)
        symbols.append(Symbol(name, addr, size))
    symbols.sort(key = lambda x: x.addr)
    return symbols

def find_symbols_locations(elf_file, symbols):
    addresses = '\n'.join([hex(x.full_addr) for x in symbols])
    try:
        out = subprocess.check_output(['addr2line', '--exe', elf_file],
                                      stderr=subprocess.STDOUT,
                                      input=addresses, text=True)
    except subprocess.CalledProcessError as e:
        print(e.output)
        raise
    out = out.strip().split('\n')
    assert len(out) == len(symbols)
    for i in range(len(symbols)):
        s = symbols[i]
        file, line = out[i].split(':')
        # addr2line may return 'line (discriminator [0-9]+)' sometimes,
        # remove this to keep only line number.
        line = line.split(' ')[0]
        s.set_loc(file, line)

class BinaryFile:
    def __init__(self, path, map_offset):
        self.fullpath = os.path.realpath(path)
        self.map_offset = map_offset
        self.symbols = get_symbols(self.fullpath)
        find_symbols_locations(self.fullpath, self.symbols)

    def path(self):
        return self.fullpath

    def addr_start(self):
        return self.map_offset

    def addr_end(self):
        last_sym = self.symbols[-1]
        return last_sym.addr + last_sym.size + self.map_offset

    def generate_symbol_file(self, prefix_symbols):
        binary_name = os.path.basename(self.fullpath)
        sym_file_path = os.path.join('uftrace.data', f'{binary_name}.sym')
        print(f'{sym_file_path} ({len(self.symbols)} symbols)')
        with open(sym_file_path, 'w') as sym_file:
            # print hexadecimal addresses on 48 bits
            addrx = "0>12x"
            for s in self.symbols:
                addr = s.addr
                addr = f'{addr:{addrx}}'
                size = f'{s.size:{addrx}}'
                if prefix_symbols:
                    name = f'{binary_name}:{s.name}'
                else:
                    name = s.name
                print(addr, size, 'T', name, file=sym_file)

    def generate_debug_file(self):
        binary_name = os.path.basename(self.fullpath)
        dbg_file_path = os.path.join('uftrace.data', f'{binary_name}.dbg')
        with open(dbg_file_path, 'w') as dbg_file:
            for s in self.symbols:
                print(f'F: {hex(s.addr)} {s.name}', file=dbg_file)
                print(f'L: {s.line} {s.file}', file=dbg_file)

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
        err += 'It should start with "0x".'
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
    map_file_path = os.path.join('uftrace.data', 'sid-0.map')

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
                                     'generate symbol files for uftrace. '
                                     'Require binutils (nm and addr2line).')
    parser.add_argument('elf_file', nargs='+',
                        help='path to an ELF file. '
                        'Use /path/to/file:0xdeadbeef to add a mapping offset.')
    parser.add_argument('--prefix-symbols',
                        help='prepend binary name to symbols',
                        action=argparse.BooleanOptionalAction)
    args = parser.parse_args()

    if not os.path.exists('uftrace.data'):
        os.mkdir('uftrace.data')

    binaries = []
    for file in args.elf_file:
        path, offset = parse_parameter(file)
        b = BinaryFile(path, offset)
        binaries.append(b)
    binaries.sort(key = lambda b: b.addr_end());

    for b in binaries:
        b.generate_symbol_file(args.prefix_symbols)
        b.generate_debug_file()

    generate_map(binaries)

if __name__ == '__main__':
    main()
