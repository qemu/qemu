#!/usr/bin/env python3

import os
import sys
import json
import shlex
import subprocess

def process_command(src, command):
    skip = False
    out = []
    for item in shlex.split(command):
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
        compile_commands_json = json.load(f)
    compile_commands = { x['output']: x for x in compile_commands_json }

    for obj in args:
        entry = compile_commands.get(obj, None)
        if not entry:
            sys.stderr.write(f'modinfo: Could not find object file {obj}')
            sys.exit(1)
        src = entry['file']
        if not src.endswith('.c'):
            print("MODINFO_DEBUG skip %s" % src)
            continue
        command = entry['command']
        print("MODINFO_DEBUG src %s" % src)
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
