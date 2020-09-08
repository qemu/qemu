#! /usr/bin/env python3

# Wrapper for tests that hides the output if they succeed.
# Used by "make check"
#
# Copyright (C) 2020 Red Hat, Inc.
#
# Author: Paolo Bonzini <pbonzini@redhat.com>

import subprocess
import sys
import os
import argparse

parser = argparse.ArgumentParser(description='Test driver for QEMU')
parser.add_argument('-C', metavar='DIR', dest='dir', default='.',
                    help='change to DIR before doing anything else')
parser.add_argument('-v', '--verbose', dest='verbose', action='store_true',
                    help='be more verbose')
parser.add_argument('test_args', nargs=argparse.REMAINDER)

args = parser.parse_args()
os.chdir(args.dir)

test_args = args.test_args
if test_args[0] == '--':
    test_args = test_args[1:]

if args.verbose:
    result = subprocess.run(test_args, stdout=None, stderr=None)
else:
    result = subprocess.run(test_args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        sys.stdout.buffer.write(result.stdout)
sys.exit(result.returncode)
