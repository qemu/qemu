#!/usr/bin/env python3

# Before a shared module's DSO is produced, a static library is built for it
# and passed to this script.  The script generates -Wl,-u options to force
# the inclusion of symbol from libqemuutil.a if the shared modules need them,
# This is necessary because the modules may use functions not needed by the
# executable itself, which would cause the function to not be linked in.
# Then the DSO loading would fail because of the missing symbol.


import sys
import subprocess

def filter_lines_set(stdout, from_staticlib):
    linesSet = set()
    for line in stdout.splitlines():
        tokens = line.split(b' ')
        if len(tokens) >= 2:
            if from_staticlib and tokens[1] == b'U':
                continue
            if not from_staticlib and tokens[1] != b'U':
                continue
            new_line = b'-Wl,-u,' + tokens[0]
            if not new_line in linesSet:
                linesSet.add(new_line)
    return linesSet

def main(args):
    if len(args) <= 3:
        sys.exit(0)

    nm = args[1]
    staticlib = args[2]
    pc = subprocess.run([nm, "-P", "-g", staticlib], stdout=subprocess.PIPE)
    if pc.returncode != 0:
        sys.exit(1)
    staticlib_syms = filter_lines_set(pc.stdout, True)

    shared_modules = args[3:]
    pc = subprocess.run([nm, "-P", "-g"] + shared_modules, stdout=subprocess.PIPE)
    if pc.returncode != 0:
        sys.exit(1)
    modules_undef_syms = filter_lines_set(pc.stdout, False)
    lines = sorted(staticlib_syms.intersection(modules_undef_syms))
    sys.stdout.buffer.write(b'\n'.join(lines))

if __name__ == "__main__":
    main(sys.argv)
