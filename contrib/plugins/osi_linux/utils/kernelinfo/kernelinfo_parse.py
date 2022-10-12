#!/usr/bin/env python3

##  @file kernelinfo_parse.py
#   @brief Script for retrieving the last kernelinfo block from dmesg.
#   The printout format is suitable for appending to a kernelinfo.conf file.
#
#   @copyright  This work is licensed under the terms of the GNU GPL, version 2.
#               See the COPYING file in the top-level directory. 
#   @author Manolis Stamatogiannakis <manolis.stamatogiannakis@vu.nl>


import subprocess
import re
import sys
import os

start = 'KERNELINFO-BEGIN'
end = 'KERNELINFO-END'
inblock = False

# Choose input.
if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]):
    dmesg_in = open(sys.argv[1], 'r')
else:
    proc = subprocess.Popen(['dmesg',],stdout=subprocess.PIPE)
    dmesg_in = proc.stdout

# Retrieve the last block of kernel information lines.
for line in dmesg_in:
    if start in line:
        inblock = True
        lines = []
        continue
    elif end in line:
        inblock = False
        continue
    elif inblock:
        lines.append(line)

# Process lines.
trans = lambda s: re.sub(r'^\[[^]]*\]\s+', '', s)
lines = list(map(trans, lines))

if not lines:
    sys.exit(1)

# Get and parse the name line.
name_grep = lambda l: re.match(r'^\s*name\s*=', l)
kname = list(filter(name_grep, lines))[-1].split('=', 1)[1].strip().lower()
krelease, kversion, kmachine = kname.strip().split('|')

# Ignore distribution-specific version string. More trouble than benefits.
# kversion_m = re.search(r'(?<=\s)(?P<kversion_dist>[234]\.[0-9]+\.[0-9]+)', kversion)

# Don't do base version and variant parsing. More trouble than benefits.
# krelease_m = re.search(r'(?P<kversion_base>[234]\.[0-9]+\.[0-9]+-[0-9]+)-(?P<kversion_variant>[0-9a-z-]+)', krelease)

# Partial heuristics for dist name.
if 'debian' in kversion.lower():
    distname = 'debian'
elif 'ubuntu' in kversion.lower():
    distname = 'ubuntu'
else:
    distname = 'unknown'

# Print group line.
print('[%s:%s:%d]' % (
    distname,
    krelease,
    64 if kmachine == 'x86_64' else 32
))

# Print key-value pairs.
print(''.join(lines), end="")

