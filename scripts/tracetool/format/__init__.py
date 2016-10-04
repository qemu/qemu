#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Format management.


Creating new formats
--------------------

A new format named 'foo-bar' corresponds to Python module
'tracetool/format/foo_bar.py'.

A format module should provide a docstring, whose first non-empty line will be
considered its short description.

All formats must generate their contents through the 'tracetool.out' routine.


Format functions
----------------

======== ==================================================================
Function Description
======== ==================================================================
generate Called to generate a format-specific file.
======== ==================================================================

"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2014, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@linux.vnet.ibm.com"


import os

import tracetool


def get_list():
    """Get a list of (name, description) pairs."""
    res = []
    modnames = []
    for filename in os.listdir(tracetool.format.__path__[0]):
        if filename.endswith('.py') and filename != '__init__.py':
            modnames.append(filename.rsplit('.', 1)[0])
    for modname in sorted(modnames):
        module = tracetool.try_import("tracetool.format." + modname)

        # just in case; should never fail unless non-module files are put there
        if not module[0]:
            continue
        module = module[1]

        doc = module.__doc__
        if doc is None:
            doc = ""
        doc = doc.strip().split("\n")[0]

        name = modname.replace("_", "-")
        res.append((name, doc))
    return res


def exists(name):
    """Return whether the given format exists."""
    if len(name) == 0:
        return False
    name = name.replace("-", "_")
    return tracetool.try_import("tracetool.format." + name)[1]


def generate(events, format, backend, group):
    if not exists(format):
        raise ValueError("unknown format: %s" % format)
    format = format.replace("-", "_")
    func = tracetool.try_import("tracetool.format." + format,
                                "generate")[1]
    if func is None:
        raise AttributeError("format has no 'generate': %s" % format)
    func(events, backend, group)
