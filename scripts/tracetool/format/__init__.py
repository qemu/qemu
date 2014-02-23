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

All the following functions are optional, and no output will be generated if
they do not exist.

======== =======================================================================
Function Description
======== =======================================================================
begin    Called to generate the format-specific file header.
end      Called to generate the format-specific file footer.
nop      Called to generate the per-event contents when the event is disabled or
         the selected backend is 'nop'.
======== =======================================================================
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


def _empty(events):
    pass

def generate_begin(name, events):
    """Generate the header of the format-specific file."""
    if not exists(name):
        raise ValueError("unknown format: %s" % name)

    name = name.replace("-", "_")
    func = tracetool.try_import("tracetool.format." + name,
                                "begin", _empty)[1]
    func(events)

def generate_end(name, events):
    """Generate the footer of the format-specific file."""
    if not exists(name):
        raise ValueError("unknown format: %s" % name)

    name = name.replace("-", "_")
    func = tracetool.try_import("tracetool.format." + name,
                                "end", _empty)[1]
    func(events)
