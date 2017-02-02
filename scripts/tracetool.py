#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Command-line wrapper for the tracetool machinery.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2014, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@linux.vnet.ibm.com"


import sys
import getopt
import os.path
import re

from tracetool import error_write, out
import tracetool.backend
import tracetool.format


_SCRIPT = ""

def error_opt(msg = None):
    if msg is not None:
        error_write("Error: " + msg + "\n")

    backend_descr = "\n".join([ "    %-15s %s" % (n, d)
                                for n,d in tracetool.backend.get_list() ])
    format_descr = "\n".join([ "    %-15s %s" % (n, d)
                               for n,d in tracetool.format.get_list() ])
    error_write("""\
Usage: %(script)s --format=<format> --backends=<backends> [<options>]

Backends:
%(backends)s

Formats:
%(formats)s

Options:
    --help                   This help message.
    --list-backends          Print list of available backends.
    --check-backends         Check if the given backend is valid.
    --binary <path>          Full path to QEMU binary.
    --target-type <type>     QEMU emulator target type ('system' or 'user').
    --target-name <name>     QEMU emulator target name.
    --group <name>           Name of the event group
    --probe-prefix <prefix>  Prefix for dtrace probe names
                             (default: qemu-<target-type>-<target-name>).\
""" % {
            "script" : _SCRIPT,
            "backends" : backend_descr,
            "formats" : format_descr,
            })

    if msg is None:
        sys.exit(0)
    else:
        sys.exit(1)

def main(args):
    global _SCRIPT
    _SCRIPT = args[0]

    long_opts = ["backends=", "format=", "help", "list-backends",
                 "check-backends", "group="]
    long_opts += ["binary=", "target-type=", "target-name=", "probe-prefix="]

    try:
        opts, args = getopt.getopt(args[1:], "", long_opts)
    except getopt.GetoptError as err:
        error_opt(str(err))

    check_backends = False
    arg_backends = []
    arg_format = ""
    arg_group = None
    binary = None
    target_type = None
    target_name = None
    probe_prefix = None
    for opt, arg in opts:
        if opt == "--help":
            error_opt()

        elif opt == "--backends":
            arg_backends = arg.split(",")
        elif opt == "--group":
            arg_group = arg
        elif opt == "--format":
            arg_format = arg

        elif opt == "--list-backends":
            public_backends = tracetool.backend.get_list(only_public = True)
            out(", ".join([ b for b,_ in public_backends ]))
            sys.exit(0)
        elif opt == "--check-backends":
            check_backends = True

        elif opt == "--binary":
            binary = arg
        elif opt == '--target-type':
            target_type = arg
        elif opt == '--target-name':
            target_name = arg
        elif opt == '--probe-prefix':
            probe_prefix = arg

        else:
            error_opt("unhandled option: %s" % opt)

    if len(arg_backends) == 0:
        error_opt("no backends specified")

    if check_backends:
        for backend in arg_backends:
            if not tracetool.backend.exists(backend):
                sys.exit(1)
        sys.exit(0)

    if arg_group is None:
        error_opt("group name is required")

    if arg_format == "stap":
        if binary is None:
            error_opt("--binary is required for SystemTAP tapset generator")
        if probe_prefix is None and target_type is None:
            error_opt("--target-type is required for SystemTAP tapset generator")
        if probe_prefix is None and target_name is None:
            error_opt("--target-name is required for SystemTAP tapset generator")

        if probe_prefix is None:
            probe_prefix = ".".join(["qemu", target_type, target_name])

    if len(args) < 1:
        error_opt("missing trace-events filepath")
    events = []
    for arg in args:
        with open(arg, "r") as fh:
            events.extend(tracetool.read_events(fh))

    try:
        tracetool.generate(events, arg_group, arg_format, arg_backends,
                           binary=binary, probe_prefix=probe_prefix)
    except tracetool.TracetoolError as e:
        error_opt(str(e))

if __name__ == "__main__":
    main(sys.argv)
