#! /usr/bin/env python3

# Invoke sparse based on the contents of compile_commands.json,
# also working around several deficiencies in cgcc's command line
# parsing

import json
import subprocess
import os
import sys
import shlex

def cmdline_for_sparse(sparse, cmdline):
    # Do not include the C compiler executable
    skip = True
    arg = False
    out = sparse + ['-no-compile']
    for x in cmdline:
        if arg:
            out.append(x)
            arg = False
            continue
        if skip:
            skip = False
            continue
        # prevent sparse from treating output files as inputs
        if x == '-MF' or x == '-MQ' or x == '-o':
            skip = True
            continue
        # cgcc ignores -no-compile if it sees -M or -MM?
        if x.startswith('-M'):
            continue
        # sparse does not understand these!
        if x == '-iquote' or x == '-isystem':
            x = '-I'
        if x == '-I':
            arg = True
        out.append(x)
    return out

root_path = os.getenv('MESON_BUILD_ROOT')
def build_path(s):
    return s if not root_path else os.path.join(root_path, s)

ccjson_path = build_path(sys.argv[1])
with open(ccjson_path, 'r') as fd:
    compile_commands = json.load(fd)

sparse = sys.argv[2:]
sparse_env = os.environ.copy()
for cmd in compile_commands:
    cmdline = shlex.split(cmd['command'])
    cmd = cmdline_for_sparse(sparse, cmdline)
    print('REAL_CC=%s' % shlex.quote(cmdline[0]),
          ' '.join((shlex.quote(x) for x in cmd)))
    sparse_env['REAL_CC'] = cmdline[0]
    r = subprocess.run(cmd, env=sparse_env, cwd=root_path)
    if r.returncode != 0:
        sys.exit(r.returncode)
