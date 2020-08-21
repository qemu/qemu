#! /usr/bin/env python3

# Invoke sparse based on the contents of compile_commands.json

import json
import subprocess
import sys
import shlex

def extract_cflags(shcmd):
    cflags = shlex.split(shcmd)
    return [x for x in cflags
            if x.startswith('-D') or x.startswith('-I') or x.startswith('-W')
               or x.startswith('-std=')]

cflags = sys.argv[1:-1]
with open(sys.argv[-1], 'r') as fd:
    compile_commands = json.load(fd)

for cmd in compile_commands:
    cmd = ['sparse'] + cflags + extract_cflags(cmd['command']) + [cmd['file']]
    print(' '.join((shlex.quote(x) for x in cmd)))
    r = subprocess.run(cmd)
    if r.returncode != 0:
        sys.exit(r.returncode)
