#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import json
import shlex
import subprocess

def find_command(src, target, compile_commands):
    for command in compile_commands:
        if command['file'] != src:
            continue
        if target != '' and command['command'].find(target) == -1:
            continue
        return command['command']
    return 'false'

def process_command(src, command):
    skip = False
    arg = False
    out = []
    for item in shlex.split(command):
        if arg:
            out.append(x)
            arg = False
            continue
        if skip:
            skip = False
            continue
        if item == '-MF' or item == '-MQ' or item == '-o':
            skip = True
            continue
        if item == '-c':
            skip = True
            continue
        out.append(item)
    out.append('-DQEMU_MODINFO')
    out.append('-E')
    out.append(src)
    return out

def main(args):
    target = ''
    if args[0] == '--target':
        args.pop(0)
        target = args.pop(0)
        print("MODINFO_DEBUG target %s" % target)
        arch = target[:-8] # cut '-softmmu'
        print("MODINFO_START arch \"%s\" MODINFO_END" % arch)
    with open('compile_commands.json') as f:
        compile_commands = json.load(f)
    for src in args:
        print("MODINFO_DEBUG src %s" % src)
        command = find_command(src, target, compile_commands)
        cmdline = process_command(src, command)
        print("MODINFO_DEBUG cmd", cmdline)
        result = subprocess.run(cmdline, stdout = subprocess.PIPE,
                                universal_newlines = True)
        if result.returncode != 0:
            sys.exit(result.returncode)
        for line in result.stdout.split('\n'):
            if line.find('MODINFO') != -1:
                print(line)

if __name__ == "__main__":
    main(sys.argv[1:])
